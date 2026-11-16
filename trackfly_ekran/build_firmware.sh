#!/bin/zsh
# Builds the firmware.
#   firmware/trackfly_ekran-<ver>.bin           -> web update (device page > Firmware)
#   firmware/trackfly_ekran-<ver>-full-usb.bin  -> first flash over USB at 0x0
#
#   ./build_firmware.sh            build only
#   ./build_firmware.sh upload     build and flash over USB (auto-detects the port)
set -e
cd "${0:A:h}"
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=1,EventsCore=1"
VER=$(grep -m1 '#define FW_VERSION' trackfly_ekran.ino | sed -E 's/.*"(.*)".*/\1/')
OUT=build
# LVGL + GFX are kept outside ~/Documents: iCloud split the shared copies in
# ~/Documents/Arduino/libraries into "x 2" folders and broke them.
LIBS="$HOME/Library/Caches/trackfly_ekran-libs"
mkdir -p "$LIBS"
[[ -d "$LIBS/lvgl" ]] || git clone -q --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git "$LIBS/lvgl"
[[ -d "$LIBS/GFX_Library_for_Arduino" ]] || git clone -q --depth 1 --branch v1.6.4 https://github.com/moononournation/Arduino_GFX.git "$LIBS/GFX_Library_for_Arduino"
cp lv_conf.h "$LIBS/lv_conf.h"     # LVGL looks for ../lv_conf.h next to its folder
arduino-cli compile --fqbn "$FQBN" --output-dir "$OUT" \
  --library "$LIBS/lvgl" --library "$LIBS/GFX_Library_for_Arduino" .
mkdir -p firmware
cp "$OUT/trackfly_ekran.ino.bin" "firmware/trackfly_ekran-$VER.bin"
# Full image for a first install over USB (bootloader + partitions + app), without the
# 16 MB padding of Arduino's merged.bin.
ESPTOOL=$(ls -d ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/ | tail -1)esptool
BOOTAPP=$(ls ~/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin | tail -1)
"$ESPTOOL" --chip esp32s3 merge_bin -o "firmware/trackfly_ekran-$VER-full-usb.bin" --flash_mode qio --flash_size 16MB \
  0x0 "$OUT/trackfly_ekran.ino.bootloader.bin" 0x8000 "$OUT/trackfly_ekran.ino.partitions.bin" \
  0xe000 "$BOOTAPP" 0x10000 "$OUT/trackfly_ekran.ino.bin" > /dev/null
echo "v$VER"
echo "web update : firmware/trackfly_ekran-$VER.bin"
echo "USB (0x0)  : firmware/trackfly_ekran-$VER-full-usb.bin"
if [[ "$1" == "upload" ]]; then
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
  [[ -z "$PORT" ]] && { echo "No USB device found (/dev/cu.usbmodem*)"; exit 1; }
  arduino-cli upload --fqbn "$FQBN" --input-dir "$OUT" -p "$PORT" .
fi
