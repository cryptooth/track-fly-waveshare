// Hardware: power latch, display + LVGL, touch, backlight, battery, buttons.
// Pin map and the working display/touch setup come from the earlier
// waveshare-plesk project.

#define PIN_BL    8    // backlight, active low PWM
#define PIN_BAT   4    // battery ADC
#define PIN_BOOT  0    // BOOT button (active low)
#define PIN_PWR   16   // PWR button (active low)

#define SCR_W 640
#define SCR_H 172

extern Settings cfg;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(9 /*CS*/, 10 /*SCK*/, 11, 12, 13, 14 /*D0-D3*/);
Arduino_GFX *panel = new Arduino_AXS15231B(bus, 21 /*RST*/, 0, false, 172, 640, 0, 0, 0, 0,
                                           axs15231b_180640_init_operations,
                                           sizeof(axs15231b_180640_init_operations));
// Software framebuffer in PSRAM; rotation 1 = landscape, 3 = landscape flipped.
Arduino_Canvas *canvas = new Arduino_Canvas(172, 640, panel, 0, 0, 1);

int   batteryPct = -1;
float batteryVolts = 0;
bool  batteryCharging = false;
bool  usbPower = false;   // a computer is on the USB port
bool  screenOn = true;

static bool idleDark = false;      // nothing playing and the clock is turned off
static uint32_t peekUntil = 0;     // a touch lights an idle-dark screen for a while
static bool nightActive = false;
bool nightNow() { return nightActive; }
static const uint8_t BRIGHT_PCT[4] = {40, 60, 80, 100};   // PWM; measured: the panel stays black below ~40%
static const uint8_t BRIGHT_LABEL[4] = {25, 50, 75, 100}; // what the user sees: visible range as 25..100%

// ---------------- LVGL glue ----------------
static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  canvas->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)px, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
  if (lv_display_flush_is_last(d)) canvas->flush();   // push the frame once per refresh
  lv_display_flush_ready(d);
}

static void touchCb(lv_indev_t *, lv_indev_data_t *data) {
  static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
  static bool swallow = false;   // a touch that woke the screen is not passed on
  uint8_t buf[32] = {0};
  data->state = LV_INDEV_STATE_RELEASED;
  if (i2c_master_touch_write_read(disp_touch_dev_handle, (uint8_t *)cmd, 11, buf, 32) != ESP_OK) return;
  if (buf[1] == 0 || buf[1] >= 5) { swallow = false; return; }
  if (!screenOn) { backlightSet(true); swallow = true; }
  else if ((idleDark || (nightActive && cfg.nightMode == 1)) && millis() > peekUntil) {
    peekUntil = millis() + 15000; backlightApply(); swallow = true;
  }
  if (swallow) return;
  int px = (((uint16_t)buf[2] & 0x0f) << 8) | buf[3];
  int py = (((uint16_t)buf[4] & 0x0f) << 8) | buf[5];
  int x, y;
  if (!cfg.flip) { x = SCR_W - px; y = 170 - py; }
  else           { x = px;         y = py; }
  data->point.x = constrain(x, 0, SCR_W - 1);
  data->point.y = constrain(y, 0, SCR_H - 1);
  data->state = LV_INDEV_STATE_PRESSED;
}

void hwApplyRotation() {
  canvas->setRotation(cfg.flip ? 3 : 1);
  lv_obj_invalidate(lv_screen_active());
}


void idleSleep(bool on) {
  if (on == idleDark) return;
  idleDark = on;
  backlightApply();
}

// Backlight test: steps the PWM from 10% to 50% in 5% steps, 4 s each, and writes the
// value in the top line. The first number you can read is the panel's threshold.
// Started from the web API (/api/settings with bltest=1).
static int blTest = -1;           // current test percent, -1 = off
static uint32_t blTestAt = 0;
void backlightTest() { blTest = 10; blTestAt = 0; }

void backlightApply() {
  if (blTest >= 0) { ledcWrite(PIN_BL, map(blTest, 0, 100, 255, 0)); return; }
  bool peek = millis() < peekUntil;
  bool dark = !screenOn || (idleDark && !peek) || (nightActive && cfg.nightMode == 1 && !peek);
  int pct = dark ? 0 : BRIGHT_PCT[cfg.bright];
  if (nightActive && cfg.nightMode == 0) pct = min<int>(pct, BRIGHT_PCT[0]);
  ledcWrite(PIN_BL, map(pct, 0, 100, 255, 0));     // active low
}

void backlightSet(bool on) {
  screenOn = on;
  backlightApply();
}

// ---------------- Motion sensor (QMI8658, I2C 0x6b) ----------------
bool imuOk = false;
float accX = 0, accY = 0, accZ = 0;   // in g

static void imuInit() {
  uint8_t who = 0;
  if (i2c_read_buff(imu_dev_handle, 0x00, &who, 1) != ESP_OK || who != 0x05) {
    Serial.printf("IMU not found (who=0x%02X)\n", who);
    return;
  }
  uint8_t v;
  v = 0x40; i2c_write_buff(imu_dev_handle, 0x02, &v, 1);   // CTRL1: register auto-increment
  v = 0x16; i2c_write_buff(imu_dev_handle, 0x03, &v, 1);   // CTRL2: accel +-4 g, ~125 Hz
  v = 0x01; i2c_write_buff(imu_dev_handle, 0x08, &v, 1);   // CTRL7: accel on
  imuOk = true;
}

void imuRead() {
  if (!imuOk) return;
  uint8_t b[6];
  if (i2c_read_buff(imu_dev_handle, 0x35, b, 6) != ESP_OK) return;
  accX = (int16_t)(b[1] << 8 | b[0]) / 8192.0f;
  accY = (int16_t)(b[3] << 8 | b[2]) / 8192.0f;
  accZ = (int16_t)(b[5] << 8 | b[4]) / 8192.0f;
}

// Upright on the desk the sensor reads y = -0.9 g; turned over it reads +0.9 g.
// Lying flat (|y| small) keeps whatever it was. The new side must hold for 1 s.
static void autoRotate() {
  static uint32_t since = 0;
  imuRead();
  bool want = cfg.flip;
  if (accY > 0.5f) want = true;
  else if (accY < -0.5f) want = false;
  if (want == cfg.flip) { since = 0; return; }
  if (!since) { since = millis(); return; }
  if (millis() - since < 1000) return;
  since = 0;
  cfg.flip = want;
  hwApplyRotation();
}

static void nightUpdate() {
  bool on = false;
  struct tm t;
  if (cfg.night && localNow(t)) {
    int m = t.tm_hour * 60 + t.tm_min;
    on = cfg.nightFrom <= cfg.nightTo ? (m >= cfg.nightFrom && m < cfg.nightTo)
                                      : (m >= cfg.nightFrom || m < cfg.nightTo);   // across midnight
  }
  if (on != nightActive) { nightActive = on; backlightApply(); }
}

// Called from loop(): orientation 5x a second, night mode every 10 s.
void hwTick() {
  static uint32_t lastImu = 0, lastNight = 0;
  uint32_t now = millis();
  if (blTest >= 0 && now - blTestAt > 4000) {
    if (blTestAt) blTest += 5;
    blTestAt = now;
    if (blTest > 50) { blTest = -1; backlightApply(); uiToast("Backlight test done"); }
    else { backlightApply(); uiToast("Backlight " + String(blTest) + "%", 3800); }
  }
  if (cfg.rotate == 2 && now - lastImu > 200) { lastImu = now; autoRotate(); }
  if (now - lastNight > 10000 || !lastNight) { lastNight = now ? now : 1; nightUpdate(); }
}

// ---------------- Battery ----------------
// LiPo voltage -> percent (rough discharge curve). On this board a cell that has sat on the
// charger all night reads 4.10 V (charger cut-off and divider tolerance), so that is 100 %.
static int lipoPct(float v) {
  static const float V[] = {3.30, 3.45, 3.60, 3.68, 3.73, 3.78, 3.84, 3.90, 3.97, 4.04, 4.10};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 11; i++) {
    if (v < V[i]) return (int)((i - 1) * 10 + (v - V[i - 1]) / (V[i] - V[i - 1]) * 10);
  }
  return 100;
}

void batteryUpdate() {
  static uint32_t last = 0;
  static float filt = 0;
  if (millis() - last < 1000 && filt > 0) return;
  last = millis();
  // Waveshare's divider is 1/3 (their ADC demo: calibrated mV x 3). The old 3.25 factor
  // belonged with uncalibrated raw readings and made a resting cell look like 4.4 V.
  float v = analogReadMilliVolts(PIN_BAT) / 1000.0f * 3.0f;
  filt = (filt == 0) ? v : filt * 0.85f + v * 0.15f;
  batteryVolts = filt;
  // The board has no charge-status or VBUS pin, so "charging" = a USB host is talking to us,
  // or (wall charger) the voltage climbed over the last 3 minutes. A full cell on a wall
  // charger looks just like a full cell on its own; that is what the sleep checkbox is for.
  static float ref = 0; static uint32_t refAt = 0; static bool rising = false;
  if (!refAt) { ref = filt; refAt = millis(); }
  else if (millis() - refAt > 180000) { rising = filt - ref > 0.015f; ref = filt; refAt = millis(); }
  usbPower = HWCDC::isPlugged();
  batteryCharging = usbPower || rising;
  int p = lipoPct(filt);
  if (batteryPct < 0 || abs(p - batteryPct) >= 2) batteryPct = p;
}

// ---------------- Buttons ----------------
struct Btn {
  uint8_t pin; uint32_t downAt = 0; bool down = false; bool longFired = false;
  // returns 0 none, 1 click, 2 long
  int poll(uint32_t longMs) {
    bool p = digitalRead(pin) == LOW;
    uint32_t now = millis();
    if (p && !down) { down = true; downAt = now; longFired = false; }
    else if (p && down && !longFired && now - downAt > longMs) { longFired = true; return 2; }
    else if (!p && down) { down = false; if (!longFired && now - downAt > 40) return 1; }
    return 0;
  }
};
static Btn bBoot{PIN_BOOT}, bPwr{PIN_PWR};
static bool pwrArmed = false;   // PWR was released once since boot (the power-on press doesn't count)

void buttonsUpdate() {
  static bool peeking = false;     // turn the backlight off again when a peek ends
  bool p0 = millis() < peekUntil;
  if (peeking && !p0) backlightApply();
  peeking = p0;

  if (digitalRead(PIN_PWR) == HIGH) pwrArmed = true;

  // BOOT: one press = brightness (acted on after 350 ms, in case a second press follows),
  // two quick presses = play/pause, hold 3 s = setup.
  static uint32_t bootPending = 0;
  int b = bBoot.poll(3000);
  if (b == 1) {
    if (bootPending && millis() - bootPending < 350) {
      bootPending = 0;
      feedControl("playpause");
      uiToast("Play / pause");
    } else {
      bootPending = millis();
    }
  } else if (b == 2) {
    bootPending = 0;
    startSetupPortal("Setup mode");
  }

  if (bootPending && millis() - bootPending >= 350) {
    bootPending = 0;
    if (!screenOn) backlightSet(true);
    else { cfg.bright = (cfg.bright + 1) % 4; backlightApply(); settingsSave(); }
    uiToast(String("Brightness ") + BRIGHT_LABEL[cfg.bright] + "%");
  }

  int p = bPwr.poll(2000);
  if (p == 1) backlightSet(!screenOn);
  else if (p == 2 && pwrArmed) powerOff();
}

void powerOff() {
  uiShowMessage("Bye", "Powering off");
  for (int i = 0; i < 20; i++) { lv_timer_handler(); delay(25); }
  backlightSet(false);
  i2c_set_power_pin(false);   // cuts battery power; does nothing on USB
  delay(1500);
  // Still alive -> running from USB. Keep the screen dark until a button/touch.
  uiShowMain();
}

// ---------------- Init ----------------
// ---------------- Deep sleep ----------------
// The touch controller's interrupt line isn't wired, so only BOOT / PWR can wake the
// chip. A timer also wakes it every 2 minutes: it joins WiFi with the screen dark,
// asks Track Fly once, and lights up only if something is playing.
#include <driver/rtc_io.h>
bool quietWake = false;
bool sleepWake = false;

void deepSleepNow() {
  Serial.println("deep sleep");
  Serial.flush();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  panel->displayOff();
  ledcDetach(PIN_BL);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);                 // active low: HIGH = backlight off
  gpio_hold_en((gpio_num_t)PIN_BL);           // ...and keep it off while asleep
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)PIN_PWR);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_PWR);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BOOT);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BOOT);
  esp_sleep_enable_ext1_wakeup((1ULL << PIN_BOOT) | (1ULL << PIN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(120ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void hwInit() {
  gpio_hold_dis((gpio_num_t)PIN_BL);          // released after a deep sleep
  gpio_deep_sleep_hold_dis();
  if (quietWake) screenOn = false;            // stay dark until we know music is playing
  i2c_set_power_pin(true);          // latch battery power on
  // Same PWM as Waveshare's own driver: 50 kHz, 8 bit, active low. At analogWrite's
  // default 1 kHz the backlight stayed black at the lower steps.
  ledcAttach(PIN_BL, 50000, 8);
  ledcWrite(PIN_BL, 255);           // dark while the panel starts
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_PWR, INPUT_PULLUP);
  analogReadResolution(12);

  if (!canvas->begin()) Serial.println("display init failed");
  canvas->setRotation(cfg.flip ? 3 : 1);
  canvas->fillScreen(0x0000);
  canvas->flush();

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t *disp = lv_display_create(SCR_W, SCR_H);
  lv_display_set_flush_cb(disp, flushCb);
  size_t sz = SCR_W * SCR_H * 2;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!buf) { sz /= 8; buf = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM); }
  lv_display_set_buffers(disp, buf, NULL, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touchCb);
  lv_indev_set_long_press_time(touch, 2000);   // hold 2 s = info screen

  batteryUpdate();
  backlightApply();
  imuInit();
}
