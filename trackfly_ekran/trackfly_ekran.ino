/*
 * Track Fly display — Waveshare ESP32-S3-Touch-LCD-3.49 (640x172, landscape)
 * ---------------------------------------------------------------------------
 * Shows the Track Fly now-playing JSON feed (http://<mac>:8765/now).
 *
 *  - First boot: the device opens its own WiFi "TRACKFLY-XXXX" (password
 *    trackfly) and shows a QR code. Join it, pick the home network, done.
 *  - Then open http://track-fly-xxxx.local (or the IP) for settings and
 *    firmware updates.
 *
 * Buttons
 *   BOOT  short: brightness step   long (3 s): WiFi setup mode
 *   PWR   short: screen on/off     long (2 s): power off (on battery)
 * Touch
 *   tap: device info     swipe left/right: next/previous source filter
 *
 * Build: ./build_firmware.sh  (Arduino core esp32 3.x, OPI PSRAM, 16 MB,
 *        partition app3M_fat9M_16MB which has two OTA slots)
 */

#define FW_VERSION "0.6.13"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Update.h>
#include "ArduinoJson-v7.4.3.h"   // vendored: the shared library copy is split by iCloud
#include <Arduino_GFX_Library.h>
#include "lvgl.h"
#include "app.h"
#include "i2c_bsp.h"
#include "user_config.h"

Settings cfg;

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// Last 4 hex digits of the WiFi MAC, e.g. 10:20:ba:3d:30:68 -> "3068"
String macSuffix() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char b[5];
  snprintf(b, sizeof b, "%02x%02x", m[4], m[5]);
  return b;
}

void settingsLoad() {
  prefs.begin("tfe", true);
  cfg.feed    = prefs.getString("feed", cfg.feed);
  cfg.zone    = prefs.getString("zone", cfg.zone);
  cfg.source  = prefs.getString("source", cfg.source);
  // Default name: track-fly-<last 4 hex digits of the MAC>, unique on a network with
  // several displays. Names that earlier versions picked automatically are replaced.
  String def = String("track-fly-") + macSuffix();
  cfg.name    = prefs.getString("name", def);
  bool oldAuto = cfg.name == "trackfly-ekran";
  if (cfg.name.startsWith("trackfly-") && cfg.name.length() == 13) {
    oldAuto = true;
    for (int i = 9; i < 13; i++) if (!isxdigit(cfg.name[i])) oldAuto = false;
  }
  if (oldAuto) cfg.name = def;
  cfg.webPass = prefs.getString("wpass", cfg.webPass);
  cfg.tz      = prefs.getInt("tz", cfg.tz);
  cfg.bright  = prefs.getUChar("bright", cfg.bright);
  bool oldFlip = prefs.getBool("flip", false);              // before 0.5.0 there was only "flip"
  cfg.rotate  = prefs.getUChar("rot", oldFlip ? 1 : cfg.rotate);
  cfg.flip    = cfg.rotate == 1;
  cfg.night     = prefs.getBool("night", cfg.night);
  cfg.nightFrom = prefs.getUShort("nfrom", cfg.nightFrom);
  cfg.nightTo   = prefs.getUShort("nto", cfg.nightTo);
  cfg.nightMode = prefs.getUChar("nmode", cfg.nightMode);
  cfg.sleep     = prefs.getBool("sleep", cfg.sleep);
  cfg.sleepMin  = prefs.getUChar("sleepm", cfg.sleepMin);
  cfg.speed   = prefs.getUChar("speed", cfg.speed);
  cfg.show    = prefs.getUShort("show", cfg.show);
  prefs.end();
  if (cfg.bright > 3) cfg.bright = 3;
}

void settingsSave() {
  prefs.begin("tfe", false);
  prefs.putString("feed", cfg.feed);
  prefs.putString("zone", cfg.zone);
  prefs.putString("source", cfg.source);
  prefs.putString("name", cfg.name);
  prefs.putString("wpass", cfg.webPass);
  prefs.putInt("tz", cfg.tz);
  prefs.putUChar("bright", cfg.bright);
  prefs.putUChar("rot", cfg.rotate);
  prefs.putBool("night", cfg.night);
  prefs.putUShort("nfrom", cfg.nightFrom);
  prefs.putUShort("nto", cfg.nightTo);
  prefs.putUChar("nmode", cfg.nightMode);
  prefs.putBool("sleep", cfg.sleep);
  prefs.putUChar("sleepm", cfg.sleepMin);
  prefs.putUChar("speed", cfg.speed);
  prefs.putUShort("show", cfg.show);
  prefs.end();
}

// Debug over USB serial:  "wifi <ssid> <password>"  saves a network like the setup page does
void serialCommands() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') { line += c; continue; }
    if (line.startsWith("wifi ")) {
      String rest = line.substring(5);
      int sp = rest.indexOf(' ');
      netSaveWifi(sp < 0 ? rest : rest.substring(0, sp), sp < 0 ? "" : rest.substring(sp + 1));
    }
    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  quietWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  Serial.printf("\nTrack Fly display v%s, reset reason %d%s\n", FW_VERSION, (int)esp_reset_reason(),
                quietWake ? ", timer wake" : "");
  WiFi.persistent(false);   // the WiFi driver must not write flash while the display runs
  WiFi.mode(WIFI_STA);   // bring the radio up before the display is busy
  i2c_master_Init();
  settingsLoad();
  hwInit();        // power latch, display, LVGL, touch, backlight, buttons, battery
  uiInit();
  uiShowMessage("Starting", "v" FW_VERSION);
  feedBegin();     // background task, waits for WiFi
  netBegin();      // connects or opens the setup portal
}

void loop() {
  static uint32_t beat = 0;
  if (millis() - beat > 10000) {   // heartbeat on the serial port, for debugging
    beat = millis();
    NowPlaying n; uint32_t rev;
    feedTake(n, rev);
    Serial.printf("[%lus] wifi=%d ip=%s heap=%u psram=%u bat=%.2fV feed=%s state=%s title=%s\n",
                  millis() / 1000, WiFi.status(), WiFi.localIP().toString().c_str(),
                  ESP.getFreeHeap(), ESP.getFreePsram(), batteryVolts,
                  n.reachable ? "ok" : "-", n.state.c_str(), n.title.c_str());
  }
  serialCommands();
  netLoop();
  buttonsUpdate();
  hwTick();
  batteryUpdate();
  uiLoop();
  lv_timer_handler();
  delay(5);
}
