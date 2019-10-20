// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#include <linux/dma-buf.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <asm/barrier.h>

struct msm_iommu_map {
	struct device *dev;
	struct msm_iommu_data *data;
	struct list_head data_node;
	struct list_head dev_node;
	struct scatterlist *sg;
	enum dma_data_direction dir;
	unsigned long attrs;
	int nents;
	int refcount;
};

static struct msm_iommu_map *msm_iommu_map_lookup(struct msm_iommu_data *data,
						  struct device *dev)
{
	struct msm_iommu_map *map;

	list_for_each_entry(map, &data->map_list, data_node) {
		if (map->dev == dev)
			return map;
	}

	return NULL;
}

static void msm_iommu_map_free(struct msm_iommu_map *map)
{
	struct sg_table table = {
		.orig_nents = map->nents,
		.nents = map->nents,
		.sgl = map->sg
	};

	list_del(&map->data_node);
	list_del(&map->dev_node);
	/* Skip an additional cache maintenance on the dma unmap path */
	map->attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	dma_unmap_sg_attrs(map->dev, map->sg, map->nents, map->dir,
			   map->attrs);
	sg_free_table(&table);
	kfree(map);
}

static struct scatterlist *clone_sgl(struct scatterlist *sg, int nents)
{
	struct scatterlist *next, *s;
	struct sg_table table;
	int i;

	sg_alloc_table(&table, nents, GFP_KERNEL | __GFP_NOFAIL);
	next = table.sgl;
	for_each_sg(sg, s, nents, i) {
		*next = *s;
		next = sg_next(next);
	}

	return table.sgl;
}

int msm_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
			 enum dma_data_direction dir, struct dma_buf *dmabuf,
			 unsigned long attrs)
{
	int not_lazy = attrs & DMA_ATTR_NO_DELAYED_UNMAP;
	struct msm_iommu_data *data = dmabuf->priv;
	struct msm_iommu_map *map;

	mutex_lock(&dev->iommu_map_lock);
	mutex_lock(&data->lock);
	map = msm_iommu_map_lookup(data, dev);
	if (map) {
		struct scatterlist *sg_tmp = sg;
		struct scatterlist *map_sg;
		int i;

		map->refcount++;
		for_each_sg(map->sg, map_sg, map->nents, i) {
			sg_dma_address(sg_tmp) = sg_dma_address(map_sg);
			sg_dma_len(sg_tmp) = sg_dma_len(map_sg);
			if (sg_dma_len(map_sg) == 0)
				break;

			sg_tmp = sg_next(sg_tmp);
			if (sg_tmp == NULL)
				break;
		}
		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_sg_for_device(dev, map->sg, map->nents,
					       map->dir);
		if (is_device_dma_coherent(dev))
			dmb(ish);
		nents = map->nents;
	} else {
		nents = dma_map_sg_attrs(dev, sg, nents, dir, attrs);
		if (nents) {
			map = kmalloc(sizeof(*map), GFP_KERNEL | __GFP_NOFAIL);
			map->data = data;
			map->dev = dev;
			map->dir = dir;
			map->nents = nents;
			map->attrs = attrs;
			map->refcount = 2 - not_lazy;
			map->sg = clone_sgl(sg, nents);
			list_add(&map->data_node, &data->map_list);
			list_add(&map->dev_node, &dev->iommu_map_list);
		}
	}

	mutex_unlock(&data->lock);
	mutex_unlock(&dev->iommu_map_lock);

	return nents;
}

void msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
			    int nents, enum dma_data_direction dir,
			    struct dma_buf *dmabuf, unsigned long attrs)
{
	struct msm_iommu_data *data = dmabuf->priv;
	struct msm_iommu_map *map;

	mutex_lock(&dev->iommu_map_lock);
	mutex_lock(&data->lock);
	map = msm_iommu_map_lookup(data, dev);
	if (map) {
		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_sg_for_cpu(dev, map->sg, map->nents,
					    dir);
		if (!--map->refcount)
			msm_iommu_map_free(map);
	}
	mutex_unlock(&data->lock);
	mutex_unlock(&dev->iommu_map_lock);
}

void msm_dma_unmap_all_for_dev(struct device *dev)
{
	struct msm_iommu_map *map, *tmp;

	mutex_lock(&dev->iommu_map_lock);
	list_for_each_entry_safe(map, tmp, &dev->iommu_map_list, dev_node) {
		struct msm_iommu_data *data = map->data;

		mutex_lock(&data->lock);
		msm_iommu_map_free(map);
		mutex_unlock(&data->lock);
	}
	mutex_unlock(&dev->iommu_map_lock);
}

void msm_dma_buf_freed(struct msm_iommu_data *data)
{
	struct msm_iommu_map *map, *tmp;
	int retry = 0;

	do {
		mutex_lock(&data->lock);
		list_for_each_entry_safe(map, tmp, &data->map_list, data_node) {
			struct device *dev = map->dev;

			if (!mutex_trylock(&dev->iommu_map_lock)) {
				retry = 1;
				break;
			}

			msm_iommu_map_free(map);
			mutex_unlock(&dev->iommu_map_lock);
		}
		mutex_unlock(&data->lock);
	} while (retry--);
}
