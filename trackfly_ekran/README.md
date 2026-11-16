# Track Fly display

Firmware for the Waveshare ESP32-S3-Touch-LCD-3.49 (640×172, landscape) that shows the Track Fly now-playing feed.

## First setup
1. Power on. The screen shows a QR code and `TRACK-FLY-XXXX` (password `trackfly`; XXXX = last 4 digits of the MAC).
2. Scan the QR code with your phone, or join that network yourself. The setup page opens (otherwise go to 192.168.4.1).
3. Pick the home network, type its password, check the Track Fly address, and press Save.
4. After it connects, the settings page is at `http://track-fly-xxxx.local` (same 4 digits).

## Controls
| | short press | long press |
|---|---|---|
| BOOT | brightness (25/50/75/100%); two quick presses: play/pause | 3 s: WiFi setup mode |
| PWR | screen on/off | 2 s: power off (on battery) |

Touch: one tap opens the control layer (touches are ignored for its first 0.5 s, it closes 6 s after the last touch). In it: top corners < > switch what to follow (Auto, Roon zones, sources); left / middle / right thirds = previous / play-pause / next; drag along the bottom dot row = volume. Hold 2 s on the normal screen = device info.

Also: auto-rotate when turned over (motion sensor), night mode (dim or dark between two times), and the display finds Track Fly on the LAN by itself (`_trackfly._tcp`) when the saved address stops answering.

## Build
```
./build_firmware.sh          # firmware/trackfly_ekran-<ver>.bin (web update) + -full-usb.bin
./build_firmware.sh upload   # also flashes over USB
```
LVGL 9.5.0 and GFX Library 1.6.4 are cloned into `~/Library/Caches/trackfly_ekran-libs`, because the shared copies in `~/Documents/Arduino/libraries` were split by iCloud. ArduinoJson is in the sketch as a single header. `lv_conf.h` lives here. Fonts are Kode Mono, converted with lv_font_conv (Latin-1 + Latin Extended-A, including Turkish).

Bump `FW_VERSION` in `trackfly_ekran.ino` for every build that goes to the device.

`firmware/backup/` holds the first 3 MB of flash read from the device before this firmware (the old Plex firmware).
