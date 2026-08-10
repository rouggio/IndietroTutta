#!/bin/bash

##set +x

echo "copying firmware.bin to backend/public/ota/firmware.bin"
cp .pio/build/esp32dev/firmware.bin ../backend/public/ota/firmware.bin
VERSION=$(grep '^[[:space:]]*#define[[:space:]]\+BUILD_VERSION' ./src/config.h | sed 's/.*BUILD_VERSION[[:space:]]*"\([^"]*\)".*/\1/')
echo "creating latest.txt with version $VERSION"
echo $VERSION > ../backend/public/ota/latest.txt
echo "adding files to git"
git -C ../backend add public/ota/latest.txt public/ota/firmware.bin
git -C ../backend commit -m "Update OTA firmware to version $VERSION"
git -C ../backend push
echo "firmware.bin and latest.txt pushed to backend repository"