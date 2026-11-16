// Shared types and function prototypes for the Track Fly display firmware.
#pragma once
#include <Arduino.h>
#include "lvgl.h"

// ---------- Settings (stored in NVS namespace "tfe") ----------
struct Settings {
  String feed    = "http://192.168.86.251:8765";  // Track Fly address (host:port)
  String zone    = "";              // ?zone= (Roon zone), empty = none
  String source  = "";              // &source= filter, empty = auto
  String name    = "";              // mDNS name -> http://<name>.local (default track-fly-<mac4>)
  String webPass = "";              // web UI password (user "admin"), empty = no password
  int32_t tz     = 10800;           // UTC offset in seconds (Turkey = +3)
  uint8_t bright = 3;               // brightness step 0..3
  bool flip      = false;           // rotated 180 degrees right now (set by `rotate`)
  uint8_t rotate = 2;               // 0 normal, 1 upside down, 2 auto (motion sensor)
  bool night     = false;           // night mode on
  uint16_t nightFrom = 23 * 60;     // minutes after midnight
  uint16_t nightTo   = 7 * 60;
  uint8_t nightMode  = 0;           // 0 dim to 10%, 1 dark (a touch lights it)
  bool sleep     = true;            // deep sleep on battery when nothing plays
  uint8_t sleepMin = 5;             // ...after this many idle minutes
  uint8_t speed  = 1;               // text scroll: 0 slow, 1 normal, 2 fast
  uint16_t show  = 0xFFFF;          // SHOW_* bits: what the screen shows
};

#define SHOW_ALBUM   0x01   // album after the artist
#define SHOW_SOURCE  0x02   // source / zone line at the top
#define SHOW_DOTS    0x04   // dot progress row
#define SHOW_TIME    0x08   // elapsed / total time
#define SHOW_BATTERY 0x10   // battery dots + percent
#define SHOW_CLOCK   0x20   // clock when idle; off = screen goes dark when nothing plays
#define SHOW_POS     0x40   // place on the album / in the playlist, in the top line


// ---------- One reading of the /now feed ----------
struct NowPlaying {
  String state;          // playing | paused | stopped | not_running | "" (no data)
  String title, artist, album, text, source, zone, error;
  float position = 0;    // seconds, at rxMillis
  float duration = 0;    // seconds, 0 = unknown / stream
  int trackNumber = 0, trackCount = 0;   // place on the album
  String playlist;                       // playlist being played
  int queueIndex = 0, queueCount = 0;    // place in the playlist
  int queueRemaining = -1;               // tracks left in the queue (Roon), -1 unknown
  int volume = -1;                       // 0-100, -1 = no volume control
  uint32_t rxMillis = 0;
  bool reachable = false;  // last fetch got an answer from Track Fly
};

// ---------- hw.ino ----------
void hwInit();
void hwApplyRotation();
void backlightApply();
void backlightSet(bool on);
void batteryUpdate();
void buttonsUpdate();
void powerOff();
void idleSleep(bool on);
void hwTick();
void backlightTest();
void deepSleepNow();
extern bool quietWake;            // woke from deep sleep by the timer: screen stays dark unless music plays
bool nightNow();
extern int  batteryPct;
extern float batteryVolts;
extern bool batteryCharging;
extern bool usbPower;
extern bool screenOn;

// ---------- net.ino ----------
void netBegin();
void netLoop();
void startSetupPortal(const char *why);
bool wifiReady();
String feedBase();
void netSaveWifi(const String &ssid, const String &pass);

// ---------- feed.ino ----------
void feedBegin();
void feedKick();
bool feedTake(NowPlaying &out, uint32_t &rev);
// What the display can follow (from Track Fly /zones): label + the source/zone to ask for.
struct Target { String label, source, zone; };
struct CtrlMsg { char action[12]; int value; };   // one queued /control request
int  feedTargets(Target *out, int max);
void feedControl(const char *action, int value = -1);   // queued, sent by the feed task
bool feedTakeDiscovered(String &base);                   // new Track Fly address found on the LAN

// ---------- ui.ino ----------
void uiInit();
void uiLoop();
void uiShowMessage(const char *title, const char *line, const char *qr = nullptr);
void uiShowMain();
void uiToast(const String &s, uint32_t ms = 1800);
void uiShowInfo();
void uiApplySettings();

// ---------- settings (trackfly_ekran.ino) ----------
void settingsLoad();
void settingsSave();
String macSuffix();
