#!/usr/bin/env bash

set -veufo pipefail
cd "$(dirname "$0")"

CHATID="-1001242020761"
BOTTOKEN="932499925:AAH1dPvHz2DQPretXqAra9kGb_9jjEOCj5g"

apt-get install -y build-essential libssl-dev bison python bc curl zip flex --no-install-recommends

function tg_upload()
{
  curl -s https://api.telegram.org/bot"${BOTTOKEN}"/sendDocument -F document=@"${1}" -F chat_id="${CHATID}"
}

git clone https://github.com/kdrag0n/proton-clang tc --depth=1
export PATH="$(pwd)/tc/bin:$PATH"

cd ..
mkdir out

git submodule init
git submodule update

git clone https://github.com/wloot/AnyKernel2 -b raphael FLASHER
rm -rf FLASHER/.git

export KBUILD_BUILD_USER=Julian
export KBUILD_BUILD_HOST=WLOOT
export ARCH=arm64

make O=out raphael_defconfig
make O=out CC=clang AR=llvm-ar LD=ld.lld NM=llvm-nm OBJCOPY=llvm-objcopy STRIP=llvm-strip OBJDUMP=llvm-objdump CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- -j$(nproc)

cp -f out/arch/arm64/boot/Image-dtb FLASHER/
cp -f out/arch/arm64/boot/dtbo.img FLASHER/

rel_date=$(date "+%Y%m%e-%H%M"|sed 's/[ ][ ]*/0/g')

pushd FLASHER
ZIPNAME=Candy-raphael-${rel_date}.zip
zip -r $ZIPNAME . -i *
tg_upload $ZIPNAME && rm -f $ZIPNAME
popd

rm -rf out/arch/arm64/boot

make O=out cepheus_defconfig
make O=out CC=clang AR=llvm-ar LD=ld.lld NM=llvm-nm OBJCOPY=llvm-objcopy STRIP=llvm-strip OBJDUMP=llvm-objdump CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- -j$(nproc)

cp -f out/arch/arm64/boot/Image-dtb FLASHER/
cp -f out/arch/arm64/boot/dtbo.img FLASHER/

pushd FLASHER
sed -i "/raphaelin/d" anykernel.sh
sed -i "s/raphael/cepheus/g" anykernel.sh
ZIPNAME=Candy-cepheus-${rel_date}.zip
zip -r $ZIPNAME . -i *
tg_upload $ZIPNAME && rm -f $ZIPNAME
popd

exit 0
