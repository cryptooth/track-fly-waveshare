// Screens. Visual language follows the Track Fly icon: a row of LED dots on a
// dark ground, one orange dot lifted out of the line.
//
//   ┌───────────────────────────────────────────────────────────────┐
//   │ ROON · LIVING ROOM                                 ●●●●○  82% │
//   │ Title, large, scrolls when long                               │
//   │ Artist · Album                                                │
//   │ ●●●●●●●●●●●●●●●○ ○○○○○○○○○○○○○○○○○○○○              2:41 / 4:05 │  <- orange dot lifted
//   └───────────────────────────────────────────────────────────────┘

LV_FONT_DECLARE(tf_font_title);
LV_FONT_DECLARE(tf_font_meta);
LV_FONT_DECLARE(tf_font_small);
LV_FONT_DECLARE(tf_font_clock);

extern Settings cfg;

#define C_BG    lv_color_hex(0x0E0E0F)
#define C_TX    lv_color_hex(0xF2EFEA)
#define C_DIM   lv_color_hex(0x8E8B86)
#define C_SLOT  lv_color_hex(0x343437)
#define C_OR    lv_color_hex(0xFF5A1F)

#define DOTS      38
#define DOT_PITCH 12
#define DOT_D     6
#define DOT_X0    20
#define DOT_Y     150     // centre line of the progress row

// ---- objects ----
static lv_obj_t *scrMain, *scrMsg;
static lv_obj_t *lblTop, *batDots[5], *lblBat;
static lv_obj_t *boxPlay, *lblTitle, *lblSub, *dots[DOTS], *dotLift, *lblTime;
static lv_obj_t *boxIdle, *lblClock, *lblDate, *lblIdle, *idleDots[7];
static lv_obj_t *info, *lblInfo;
static lv_obj_t *msgTitle, *msgLine, *msgQr, *msgDots[11];
static lv_timer_t *msgTimer;

// ---- state ----
static bool mainActive = false;
static bool playMode = false;
static NowPlaying cur;
static uint32_t curRev = 0xFFFFFFFF;
static int liftIdx = -1;
static int shownSec = -1;
static uint32_t toastUntil = 0, infoUntil = 0;
static String topText;

// ---------- helpers ----------
static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, "");          // LVGL starts a label as "Text"
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

static lv_obj_t *mkBox(lv_obj_t *parent) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 640, 172);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

static lv_obj_t *mkDot(lv_obj_t *parent, int d, lv_color_t c) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static void setText(lv_obj_t *l, const String &s) {
  const char *old = lv_label_get_text(l);
  if (!old || strcmp(old, s.c_str()) != 0) lv_label_set_text(l, s.c_str());  // avoid restarting the scroll
}

static String fmtTime(int s) {
  if (s < 0) s = 0;
  char b[12];
  if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
  return b;
}

static String sourceName(const String &s) {
  if (s == "music") return "Apple Music";
  if (s == "spotify") return "Spotify";
  if (s == "roon") return "Roon";
  if (s == "plex") return "Plex";
  return s;
}

static void showObj(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static String upper(String s) { s.toUpperCase(); return s; }

// ---------- message screen (boot, connecting, setup, update) ----------
// "TRACK / FLY" set in 5x7 LED dots on the left of the message screen. After FLY a
// row of dots continues the baseline; one orange dot hops along it, lifted out of
// the line like in the app icon, while the device is busy.
#define LOGO_P   7          // dot pitch
#define LOGO_D   5          // dot size
#define LOGO_X   24
#define LOGO_Y   27
#define HOP_N    11         // dots after "FLY", up to the width of "TRACK"
static lv_obj_t *msgLogo;
static const uint8_t GLYPH[8][7] = {
  {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},   // T
  {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},   // R
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   // A
  {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},   // C
  {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},   // K
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},   // F
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},   // L
  {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},   // Y
};

static void logoDraw(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t box;
  lv_obj_get_coords((lv_obj_t *)lv_event_get_target(e), &box);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = C_TX;
  for (int g = 0; g < 8; g++) {
    int line = g < 5 ? 0 : 1, pos = g < 5 ? g : g - 5;
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++) {
        if (!(GLYPH[g][r] & (0x10 >> c))) continue;
        lv_area_t a;
        a.x1 = box.x1 + (pos * 6 + c) * LOGO_P;
        a.y1 = box.y1 + (line * 10 + r) * LOGO_P;
        a.x2 = a.x1 + LOGO_D - 1;
        a.y2 = a.y1 + LOGO_D - 1;
        // a soft left-to-right fade, like the dots in the icon
        d.bg_opa = 150 + (105 * (pos * 6 + c)) / 29;
        lv_draw_rect(layer, &d, &a);
      }
  }
}

static void msgTick(lv_timer_t *) {
  static int i = 0;
  i = (i + 1) % HOP_N;
  for (int k = 0; k < HOP_N; k++) {
    bool up = k == i;
    lv_obj_set_style_bg_color(msgDots[k], up ? C_OR : C_SLOT, 0);
    lv_obj_set_y(msgDots[k], LOGO_Y + 16 * LOGO_P - (up ? 10 : 0));
  }
}

static void buildMsg() {
  scrMsg = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrMsg, C_BG, 0);
  lv_obj_remove_flag(scrMsg, LV_OBJ_FLAG_SCROLLABLE);

  msgLogo = lv_obj_create(scrMsg);
  lv_obj_remove_style_all(msgLogo);
  lv_obj_set_pos(msgLogo, LOGO_X, LOGO_Y);
  lv_obj_set_size(msgLogo, 29 * LOGO_P, 17 * LOGO_P);
  lv_obj_remove_flag(msgLogo, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(msgLogo, logoDraw, LV_EVENT_DRAW_MAIN, NULL);

  msgQr = lv_qrcode_create(scrMsg);
  lv_qrcode_set_size(msgQr, 136);
  lv_qrcode_set_dark_color(msgQr, lv_color_hex(0x0E0E0F));
  lv_qrcode_set_light_color(msgQr, C_TX);
  lv_obj_set_pos(msgQr, 18, 18);
  lv_obj_set_style_border_color(msgQr, C_TX, 0);
  lv_obj_set_style_border_width(msgQr, 6, 0);
  lv_obj_add_flag(msgQr, LV_OBJ_FLAG_HIDDEN);

  msgTitle = mkLabel(scrMsg, &tf_font_title, C_TX);
  lv_label_set_long_mode(msgTitle, LV_LABEL_LONG_MODE_DOTS);
  msgLine = mkLabel(scrMsg, &tf_font_meta, C_DIM);
  lv_label_set_long_mode(msgLine, LV_LABEL_LONG_MODE_WRAP);

  for (int k = 0; k < HOP_N; k++) {
    msgDots[k] = mkDot(scrMsg, LOGO_D, C_SLOT);
    lv_obj_set_pos(msgDots[k], LOGO_X + (18 + k) * LOGO_P, LOGO_Y + 16 * LOGO_P);
  }
  msgTimer = lv_timer_create(msgTick, 140, NULL);
  lv_timer_pause(msgTimer);
}

// Logo on the left, status on the right. With a QR code (setup) the code takes the
// logo's place.
void uiShowMessage(const char *title, const char *line, const char *qr) {
  bool hasQr = qr && *qr;
  int x = hasQr ? 176 : 270;
  lv_label_set_text(msgTitle, title);
  lv_label_set_text(msgLine, line ? line : "");
  lv_obj_set_pos(msgTitle, x, hasQr ? 26 : 34);
  lv_obj_set_width(msgTitle, 640 - x - 20);
  lv_obj_set_pos(msgLine, x, hasQr ? 84 : 92);
  lv_obj_set_width(msgLine, 640 - x - 20);
  showObj(msgQr, hasQr);
  showObj(msgLogo, !hasQr);
  for (int k = 0; k < HOP_N; k++) showObj(msgDots[k], !hasQr);
  if (hasQr) lv_qrcode_update(msgQr, qr, strlen(qr));
  lv_timer_resume(msgTimer);
  mainActive = false;
  lv_screen_load(scrMsg);
}

// ---------- main screen ----------

// ---- control layer ----
// One tap on the normal screen opens it; nothing else on the normal screen reacts.
// For CTRL_LOCK_MS after opening, touches are ignored (a shaky double tap must not
// pause or skip). It closes CTRL_OPEN_MS after the last touch.
//   top strip     : <  what to follow  >
//   middle        : previous | play/pause | next   (thirds of the screen)
//   bottom strip  : volume, drag along the dots
#define CTRL_LOCK_MS  500
#define CTRL_OPEN_MS  6000
#define CTRL_MIN_MS   80      // shorter touches are noise
#define CTRL_TOP_Y    46
#define CTRL_VOL_Y    118
#define VOL_DOTS      38

static lv_obj_t *ctrl, *ctrlTarget, *ctrlL, *ctrlR, *icoPrev, *icoNext, *icoPlay, *icoPause, *volDots[VOL_DOTS], *lblVol;
static bool ctrlOpen = false;
static uint32_t ctrlOpenedAt = 0, ctrlUntil = 0;
static uint32_t pressAt = 0;
static lv_point_t pressPt, lastPt;
static bool longFired = false;
static int volShown = -2, volPending = -1;
static uint32_t volPendingUntil = 0;

// Icons drawn as LED dot patterns. Each string row is one row of dots, '#' = lit.
static lv_obj_t *mkDotIcon(lv_obj_t *parent, const char *const *rows, int nrows, lv_color_t c) {
  const int pitch = 9, d = 6;
  int cols = strlen(rows[0]);
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, cols * pitch, nrows * pitch);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  for (int r = 0; r < nrows; r++)
    for (int k = 0; k < cols; k++)
      if (rows[r][k] == '#') lv_obj_set_pos(mkDot(box, d, c), k * pitch, r * pitch);
  return box;
}
static const char *const ICON_PLAY[]  = {"#   ", "##  ", "### ", "####", "### ", "##  ", "#   "};
static const char *const ICON_PAUSE[] = {"## ##", "## ##", "## ##", "## ##", "## ##", "## ##", "## ##"};
static const char *const ICON_NEXT[]  = {"#   #", "##  #", "### #", "#####", "### #", "##  #", "#   #"};
static const char *const ICON_PREV[]  = {"#   #", "#  ##", "# ###", "#####", "# ###", "#  ##", "#   #"};

static String targetLabel() {
  if (cfg.zone.length()) return "Roon \xC2\xB7 " + cfg.zone;
  if (cfg.source.length()) return sourceName(cfg.source);
  return "Auto";
}

static void stepTarget(int dir) {
  Target list[13]; int n = 0;
  list[n++] = Target{"Auto", "", ""};
  Target got[12]; int g = feedTargets(got, 12);
  for (int i = 0; i < g; i++) if (got[i].source.length() || got[i].zone.length()) list[n++] = got[i];
  int at = 0;
  for (int i = 0; i < n; i++)
    if (list[i].zone == cfg.zone && (list[i].zone.length() || list[i].source == cfg.source)) at = i;
  at = (at + dir + n) % n;
  cfg.zone = list[at].zone;
  cfg.source = list[at].zone.length() ? String("") : list[at].source;
  settingsSave();
  curRev = 0xFFFFFFFF;
  feedKick();
  lv_label_set_text(ctrlTarget, upper(targetLabel()).c_str());
}

static void flash(lv_obj_t *o) {   // brief orange pulse on the touched control
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_values(&a, 255, 120);
  lv_anim_set_duration(&a, 250);
  lv_anim_set_playback_duration(&a, 250);
  lv_anim_set_exec_cb(&a, [](void *x, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)x, v, 0); });
  lv_anim_start(&a);
}

static int volFromX(int x) {
  int v = (x - DOT_X0) * 100 / ((VOL_DOTS - 1) * DOT_PITCH);
  return constrain(v, 0, 100);
}

static void drawVolume(int v) {
  if (v == volShown) return;
  volShown = v;
  bool has = v >= 0;
  int lit = has ? (v * VOL_DOTS + 50) / 100 : 0;
  for (int i = 0; i < VOL_DOTS; i++) {
    lv_obj_set_style_bg_color(volDots[i], i < lit ? C_OR : C_SLOT, 0);
    lv_obj_set_style_bg_opa(volDots[i], has ? 255 : 90, 0);
  }
  lv_label_set_text(lblVol, has ? ("VOL " + String(v)).c_str() : "NO VOLUME");
}

static void ctrlShow(bool on) {
  ctrlOpen = on;
  if (on) {
    ctrlOpenedAt = millis();
    ctrlUntil = ctrlOpenedAt + CTRL_OPEN_MS;
    lv_label_set_text(ctrlTarget, upper(targetLabel()).c_str());
    volShown = -2;
    lv_obj_remove_flag(ctrl, LV_OBJ_FLAG_HIDDEN);
    // Fade in over the lock time, so it looks inactive while it is.
    lv_obj_set_style_opa(ctrl, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ctrl);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, CTRL_LOCK_MS);
    lv_anim_set_exec_cb(&a, [](void *x, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)x, v, 0); });
    lv_anim_start(&a);
  } else {
    lv_obj_add_flag(ctrl, LV_OBJ_FLAG_HIDDEN);
  }
}

static void ctrlUpdate() {
  if (!ctrlOpen) return;
  if (millis() > ctrlUntil) { ctrlShow(false); return; }
  bool playing = cur.state == "playing";
  showObj(icoPlay, !playing);
  showObj(icoPause, playing);
  int v = (volPending >= 0 && millis() < volPendingUntil) ? volPending : cur.volume;
  drawVolume(v);
}

// Raw touch handling on the main screen (no LVGL gestures).
static void screenEvent(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_active();
  lv_point_t p;
  if (in) lv_indev_get_point(in, &p);
  uint32_t now = millis();

  if (code == LV_EVENT_PRESSED) {
    pressAt = now; pressPt = p; lastPt = p; longFired = false;
    return;
  }
  if (code == LV_EVENT_PRESSING) {
    lastPt = p;
    // live volume while dragging along the bottom row (sent on release)
    if (ctrlOpen && pressAt - ctrlOpenedAt >= CTRL_LOCK_MS && pressPt.y >= CTRL_VOL_Y && cur.volume >= 0 &&
        abs(p.x - pressPt.x) > 12) {
      volPending = volFromX(p.x); volPendingUntil = now + 3000;
      drawVolume(volPending);
      ctrlUntil = now + CTRL_OPEN_MS;
    }
    return;
  }
  if (code == LV_EVENT_LONG_PRESSED) {   // 2 s hold on the normal screen: info
    if (!ctrlOpen && !infoUntil) { longFired = true; uiShowInfo(); }
    return;
  }
  if (code != LV_EVENT_RELEASED) return;
  if (longFired) return;
  if (now - pressAt < CTRL_MIN_MS) return;                    // a brush, not a tap
  if (infoUntil) { infoUntil = 0; lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN); return; }
  if (!ctrlOpen) { ctrlShow(true); return; }
  if (pressAt - ctrlOpenedAt < CTRL_LOCK_MS) return;          // started during the lock: ignore
  ctrlUntil = now + CTRL_OPEN_MS;

  int x = pressPt.x, y = pressPt.y;
  if (y < CTRL_TOP_Y) {
    if (x < 160) { stepTarget(-1); flash(ctrlL); }
    else if (x > 480) { stepTarget(1); flash(ctrlR); }
  } else if (y >= CTRL_VOL_Y) {
    if (cur.volume < 0) return;
    int v = volFromX(lastPt.x);
    volPending = v; volPendingUntil = now + 3000;
    drawVolume(v);
    feedControl("volume", v);
  } else if (x < 213) {
    feedControl("prev"); flash(icoPrev);
  } else if (x > 427) {
    feedControl("next"); flash(icoNext);
  } else {
    feedControl("playpause");
    flash(cur.state == "playing" ? icoPause : icoPlay);
    cur.state = cur.state == "playing" ? "paused" : "playing";   // show the new icon right away
  }
}

static void buildCtrl() {
  ctrl = lv_obj_create(scrMain);
  lv_obj_remove_style_all(ctrl);
  lv_obj_set_size(ctrl, 640, 172);
  lv_obj_set_style_bg_color(ctrl, C_BG, 0);
  lv_obj_set_style_bg_opa(ctrl, 235, 0);
  lv_obj_remove_flag(ctrl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);

  ctrlL = mkLabel(ctrl, &tf_font_meta, C_OR);
  lv_label_set_text(ctrlL, "<");
  lv_obj_set_pos(ctrlL, 24, 8);
  ctrlR = mkLabel(ctrl, &tf_font_meta, C_OR);
  lv_label_set_text(ctrlR, ">");
  lv_obj_set_pos(ctrlR, 640 - 24 - 15, 8);
  ctrlTarget = mkLabel(ctrl, &tf_font_small, C_TX);
  lv_obj_set_width(ctrlTarget, 440);
  lv_obj_set_style_text_align(ctrlTarget, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(ctrlTarget, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_pos(ctrlTarget, 100, 13);

  icoPrev  = mkDotIcon(ctrl, ICON_PREV, 7, C_TX);
  icoPlay  = mkDotIcon(ctrl, ICON_PLAY, 7, C_OR);
  icoPause = mkDotIcon(ctrl, ICON_PAUSE, 7, C_OR);
  icoNext  = mkDotIcon(ctrl, ICON_NEXT, 7, C_TX);
  lv_obj_align(icoPrev, LV_ALIGN_TOP_MID, -213, 52);
  lv_obj_align(icoPlay, LV_ALIGN_TOP_MID, 4, 52);
  lv_obj_align(icoPause, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_align(icoNext, LV_ALIGN_TOP_MID, 213, 52);
  lv_obj_add_flag(icoPause, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < VOL_DOTS; i++) {
    volDots[i] = mkDot(ctrl, DOT_D, C_SLOT);
    lv_obj_set_pos(volDots[i], DOT_X0 + i * DOT_PITCH, DOT_Y - DOT_D / 2);
  }
  lblVol = mkLabel(ctrl, &tf_font_small, C_DIM);
  lv_obj_set_width(lblVol, 150);
  lv_obj_set_style_text_align(lblVol, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblVol, 640 - 20 - 150, DOT_Y - 10);
  lv_obj_add_flag(ctrl, LV_OBJ_FLAG_HIDDEN);
}

static void buildMain() {
  scrMain = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrMain, C_BG, 0);
  lv_obj_remove_flag(scrMain, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(scrMain, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(scrMain, screenEvent, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scrMain, screenEvent, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scrMain, screenEvent, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_add_event_cb(scrMain, screenEvent, LV_EVENT_RELEASED, NULL);

  // top bar
  lblTop = mkLabel(scrMain, &tf_font_small, C_DIM);
  lv_obj_set_pos(lblTop, 20, 10);
  lv_obj_set_width(lblTop, 460);
  lv_label_set_long_mode(lblTop, LV_LABEL_LONG_MODE_DOTS);
  lblBat = mkLabel(scrMain, &tf_font_small, C_DIM);
  lv_obj_set_width(lblBat, 48);
  lv_obj_set_style_text_align(lblBat, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblBat, 640 - 20 - 48, 10);
  for (int i = 0; i < 5; i++) {
    batDots[i] = mkDot(scrMain, 5, C_SLOT);
    lv_obj_set_pos(batDots[i], 640 - 20 - 48 - 8 - (5 - i) * 9, 17);
  }

  // playing
  boxPlay = mkBox(scrMain);
  lblTitle = mkLabel(boxPlay, &tf_font_title, C_TX);
  lv_obj_set_pos(lblTitle, 20, 34);
  lv_obj_set_width(lblTitle, 600);
  lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lblSub = mkLabel(boxPlay, &tf_font_meta, C_DIM);
  lv_obj_set_pos(lblSub, 20, 88);
  lv_obj_set_width(lblSub, 600);
  lv_label_set_long_mode(lblSub, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  for (int i = 0; i < DOTS; i++) {
    dots[i] = mkDot(boxPlay, DOT_D, C_SLOT);
    lv_obj_set_pos(dots[i], DOT_X0 + i * DOT_PITCH, DOT_Y - DOT_D / 2);
  }
  dotLift = mkDot(boxPlay, 10, C_OR);
  lv_obj_set_style_shadow_color(dotLift, C_OR, 0);
  lv_obj_set_style_shadow_width(dotLift, 14, 0);
  lv_obj_set_style_shadow_opa(dotLift, LV_OPA_60, 0);
  lv_obj_set_style_border_color(dotLift, C_OR, 0);
  lblTime = mkLabel(boxPlay, &tf_font_small, C_DIM);
  lv_obj_set_width(lblTime, 150);
  lv_obj_set_style_text_align(lblTime, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(lblTime, 640 - 20 - 150, DOT_Y - 10);

  // idle: clock + status
  boxIdle = mkBox(scrMain);
  lblClock = mkLabel(boxIdle, &tf_font_clock, C_TX);
  lv_obj_set_pos(lblClock, 14, 40);
  lblDate = mkLabel(boxIdle, &tf_font_meta, C_TX);
  lv_obj_set_pos(lblDate, 400, 50);
  lblIdle = mkLabel(boxIdle, &tf_font_small, C_DIM);
  lv_obj_set_pos(lblIdle, 400, 84);
  lv_obj_set_size(lblIdle, 222, 44);                 // two lines at most, clear of the dot row
  lv_label_set_long_mode(lblIdle, LV_LABEL_LONG_MODE_DOTS);
  for (int k = 0; k < 7; k++) {   // the icon's row, nobody lifted: nothing is playing
    idleDots[k] = mkDot(boxIdle, 8, C_TX);
    lv_obj_set_style_bg_opa(idleDots[k], k < 4 ? 90 + k * 40 : 255, 0);
    lv_obj_set_pos(idleDots[k], 402 + k * 16, 140);
  }
  lv_obj_add_flag(boxPlay, LV_OBJ_FLAG_HIDDEN);

  buildCtrl();

  // info overlay
  info = lv_obj_create(scrMain);
  lv_obj_remove_style_all(info);
  lv_obj_set_size(info, 640, 172);
  lv_obj_set_style_bg_color(info, C_BG, 0);
  lv_obj_set_style_bg_opa(info, LV_OPA_COVER, 0);
  lv_obj_remove_flag(info, LV_OBJ_FLAG_CLICKABLE);
  lblInfo = mkLabel(info, &tf_font_small, C_TX);
  lv_obj_set_pos(lblInfo, 20, 10);
  lv_obj_set_style_text_line_space(lblInfo, 2, 0);
  lv_obj_t *foot = mkLabel(info, &tf_font_small, C_DIM);   // bottom line: version + hint
  lv_label_set_text(foot, "v" FW_VERSION "   \xC2\xB7   tap to close");
  lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 20, -10);
  for (int k = 0; k < 7; k++) {   // the icon's dot row
    lv_obj_t *d = mkDot(info, 7, k == 4 ? C_OR : C_TX);
    if (k < 4) lv_obj_set_style_bg_opa(d, 110 + k * 40, 0);
    lv_obj_set_pos(d, 640 - 20 - (7 - k) * 13, k == 4 ? 140 : 150);   // bottom right, next to the footer
  }
  lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
}

void uiShowInfo() {
  String s;
  s += "web      http://" + cfg.name + ".local   " + WiFi.localIP().toString() + "\n";
  s += "wifi     " + WiFi.SSID() + "  " + String(WiFi.RSSI()) + " dBm\n";
  s += "feed     " + feedBase() + "\n";
  s += "follow   " + targetLabel() + "\n";
  s += "battery  " + String(batteryVolts, 2) + " V  " + String(batteryPct) + "%" + (batteryCharging ? "  charging" : "");
  lv_label_set_text(lblInfo, s.c_str());
  lv_obj_remove_flag(info, LV_OBJ_FLAG_HIDDEN);
  infoUntil = millis() + 10000;
}

void uiToast(const String &s, uint32_t ms) {
  if (!mainActive) return;
  lv_label_set_text(lblTop, upper(s).c_str());
  lv_obj_set_style_text_color(lblTop, C_OR, 0);
  showObj(lblTop, true);
  toastUntil = millis() + ms;
}

void uiShowMain() {
  lv_timer_pause(msgTimer);
  mainActive = true;
  curRev = 0xFFFFFFFF;   // force a redraw
  lv_screen_load(scrMain);
}

// ---------- updates ----------
static void setPlayMode(bool p) {
  if (p == playMode) return;
  playMode = p;
  lv_obj_t *show = p ? boxPlay : boxIdle, *hide = p ? boxIdle : boxPlay;
  lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(show, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, show);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 350);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, v, 0); });
  lv_anim_start(&a);
}

static void updateTop() {
  if (toastUntil) {
    if (millis() < toastUntil) return;
    toastUntil = 0;
    lv_obj_set_style_text_color(lblTop, C_DIM, 0);
    showObj(lblTop, cfg.show & SHOW_SOURCE);
    topText = "";
  }
  String t;
  if (!wifiReady()) t = "offline";
  else if (playMode) {
    t = sourceName(cur.source);
    if (cur.zone.length()) t += "  \xC2\xB7  " + cur.zone;
    if (cfg.show & SHOW_POS) {
      // Playlist place beats album place; Roon only knows what is left in the queue.
      String pos;
      if (cur.playlist.length()) {
        pos = cur.playlist;
        if (cur.queueIndex > 0) pos += "  " + String(cur.queueIndex) + (cur.queueCount > 0 ? "/" + String(cur.queueCount) : "");
      } else if (cur.trackNumber > 0) {
        pos = cur.trackCount > 0 ? String(cur.trackNumber) + "/" + String(cur.trackCount)
                                 : "#" + String(cur.trackNumber);   // no total known
      } else if (cur.queueRemaining > 0) {
        pos = String(cur.queueRemaining) + (cur.queueRemaining == 1 ? " track left" : " tracks left");
      }
      if (pos.length()) t += "  \xC2\xB7  " + pos;
    }
    if (cur.state == "paused") t += "  \xC2\xB7  paused";
  } else if (cfg.zone.length() || cfg.source.length()) t = targetLabel();   // the chosen source/zone
  t = upper(t);
  if (t != topText) { topText = t; lv_label_set_text(lblTop, t.c_str()); }
}

static void updateBattery() {
  static int shown = -2; static bool chg = false;
  if (batteryPct == shown && batteryCharging == chg) return;
  shown = batteryPct; chg = batteryCharging;
  int lit = batteryPct < 0 ? 0 : (batteryPct + 19) / 20;   // 1..5
  for (int i = 0; i < 5; i++) {
    lv_color_t c = i < lit ? (chg ? C_OR : C_TX) : C_SLOT;   // orange dots = charging
    lv_obj_set_style_bg_color(batDots[i], c, 0);
  }
  lv_label_set_text(lblBat, (String(max(batteryPct, 0)) + "%").c_str());
  lv_obj_set_style_text_color(lblBat, batteryPct <= 15 && !chg ? C_OR : C_DIM, 0);
}

static void updateProgress() {
  if (!playMode) return;
  float pos = cur.position;
  if (cur.state == "playing") pos += (millis() - cur.rxMillis) / 1000.0f;
  bool live = cur.duration <= 0;
  if (!live && pos > cur.duration) pos = cur.duration;
  int sec = (int)pos;
  int idx = live ? DOTS - 1 : (int)lroundf(pos / cur.duration * (DOTS - 1));
  idx = constrain(idx, 0, DOTS - 1);

  if (sec != shownSec) {
    shownSec = sec;
    String t = live ? "live  " + fmtTime(sec) : fmtTime(sec) + " / " + fmtTime((int)cur.duration);
    setText(lblTime, t);
  }
  if (idx != liftIdx) {
    liftIdx = idx;
    for (int i = 0; i < DOTS; i++) {
      // played part bright, rest dim; the slot under the lifted dot stays empty
      lv_color_t c = (!live && i < idx) ? C_TX : C_SLOT;
      lv_obj_set_style_bg_color(dots[i], c, 0);
      lv_obj_set_style_bg_opa(dots[i], (!live && i < idx) ? 150 + (105 * i) / max(idx, 1) : 255, 0);
    }
    lv_obj_set_pos(dotLift, DOT_X0 + idx * DOT_PITCH - 2, DOT_Y - 5 - 12);
  }
  // paused: the lifted dot becomes a ring
  bool paused = cur.state == "paused";
  lv_obj_set_style_bg_opa(dotLift, paused ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dotLift, paused ? 2 : 0, 0);
  lv_obj_set_style_shadow_opa(dotLift, paused ? LV_OPA_TRANSP : LV_OPA_60, 0);
}

static void updateClock() {
  if (playMode) return;
  static int lastMin = -1;
  struct tm t;
  if (!getLocalTime(&t, 0)) {
    setText(lblClock, "--:--");
    setText(lblDate, "");
  } else if (t.tm_min != lastMin) {
    lastMin = t.tm_min;
    char b[8], d[24];
    strftime(b, sizeof b, "%H:%M", &t);
    strftime(d, sizeof d, "%a %d %b", &t);
    setText(lblClock, b);
    setText(lblDate, d);
  }
  String s;
  if (!wifiReady()) s = "No WiFi";
  else if (!cur.reachable) {
    String host = feedBase();
    host.replace("http://", "");
    s = "Track Fly offline\n" + host;
  }
  else if (cur.error.length()) s = cur.error;
  else if (cur.state == "not_running") s = "Player closed";
  else s = "Nothing playing";
  setText(lblIdle, s);
}

// Scroll duration for one full loop of a label, in ms, from the chosen speed.
// (LVGL's own speed setting caps a loop at 10 s, so long titles all ended up
// scrolling at the same speed.) The fonts are monospaced: width = chars x advance.
static uint32_t scrollMs(lv_obj_t *l, const char *txt) {
  static const uint16_t PX[3] = {30, 60, 110};   // pixels per second
  const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
  uint32_t chars = 0;
  for (const char *p = txt; *p; p++) if ((*p & 0xC0) != 0x80) chars++;
  uint32_t px = (chars + 3) * lv_font_get_glyph_width(f, 'M', 0);   // + LVGL's 3-space gap
  return max<uint32_t>(px * 1000 / PX[min<int>(cfg.speed, 2)], 500);
}

static void setScroll(lv_obj_t *l, const String &s) {
  const char *old = lv_label_get_text(l);
  if (old && strcmp(old, s.c_str()) == 0) return;
  lv_obj_set_style_anim_duration(l, scrollMs(l, s.c_str()), 0);
  lv_label_set_text(l, s.c_str());
}

// Called after settings change (web page) and at start.
void uiApplySettings() {
  // re-setting the text restarts the scroll with the new speed
  String t = lv_label_get_text(lblTitle), s = lv_label_get_text(lblSub);
  lv_obj_set_style_anim_duration(lblTitle, scrollMs(lblTitle, t.c_str()), 0);
  lv_obj_set_style_anim_duration(lblSub, scrollMs(lblSub, s.c_str()), 0);
  lv_label_set_text(lblTitle, t.c_str());
  lv_label_set_text(lblSub, s.c_str());

  bool dotsOn = cfg.show & SHOW_DOTS, tm = cfg.show & SHOW_TIME;
  for (int i = 0; i < DOTS; i++) showObj(dots[i], dotsOn);
  showObj(dotLift, dotsOn);
  showObj(lblTime, tm);
  showObj(lblTop, cfg.show & SHOW_SOURCE);
  showObj(lblBat, cfg.show & SHOW_BATTERY);
  for (int i = 0; i < 5; i++) showObj(batDots[i], cfg.show & SHOW_BATTERY);
  // Without the bottom row the two text lines move down to sit centred.
  bool bottom = dotsOn || tm;
  lv_obj_set_y(lblTitle, bottom ? 34 : 48);
  lv_obj_set_y(lblSub, bottom ? 88 : 104);
  curRev = 0xFFFFFFFF;   // redraw text (album on/off)
}

static void applyNow() {
  bool active = cur.reachable && (cur.state == "playing" || cur.state == "paused") &&
                (cur.title.length() || cur.text.length());
  setPlayMode(active);
  if (active) {
    setScroll(lblTitle, cur.title.length() ? cur.title : cur.text);
    String sub = cur.artist;
    if (cur.album.length() && (cfg.show & SHOW_ALBUM)) sub += (sub.length() ? "  \xC2\xB7  " : "") + cur.album;
    if (!cur.title.length()) sub = "";
    setScroll(lblSub, sub);
    liftIdx = -1; shownSec = -1;
  }
}

// Deep sleep on battery after cfg.sleepMin idle minutes. After a timer wake: light the
// screen if music plays, otherwise go back to sleep as soon as we know (or after 25 s).
static uint32_t idleSince = 0;
static void sleepCheck() {
  uint32_t now = millis();
  if (quietWake) {
    if (mainActive && playMode) { quietWake = false; backlightSet(true); idleSince = now; return; }
    if (screenOn) { quietWake = false; idleSince = now; return; }   // a touch/button woke it fully
    if (usbPower) { quietWake = false; backlightSet(true); idleSince = now; return; }   // plugged in while asleep
    bool answered = mainActive && cur.rxMillis > 0;
    if ((answered && now > 12000) || now > 25000) deepSleepNow();
    return;
  }
  bool stay = !cfg.sleep || batteryCharging || !mainActive || playMode || ctrlOpen || infoUntil;
  if (stay || !idleSince) { idleSince = now; return; }
  if (now - idleSince > (uint32_t)cfg.sleepMin * 60000UL) deepSleepNow();
}

void uiLoop() {
  static uint32_t lastSleepCheck = 0;
  if (millis() - lastSleepCheck > 1000) { lastSleepCheck = millis(); sleepCheck(); }
  if (!mainActive) return;
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();

  NowPlaying n; uint32_t rev;
  if (feedTake(n, rev)) {
    bool changed = rev != curRev;
    cur = n;
    if (changed) { curRev = rev; applyNow(); }
  }
  idleSleep(!playMode && !(cfg.show & SHOW_CLOCK));
  if (infoUntil && millis() > infoUntil) { infoUntil = 0; lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN); }
  updateTop();
  updateBattery();
  updateProgress();
  updateClock();
  ctrlUpdate();
}

void uiInit() {
  lv_display_t *d = lv_display_get_default();
  lv_obj_set_style_bg_color(lv_screen_active(), C_BG, 0);
  (void)d;
  buildMsg();
  buildMain();
  uiApplySettings();
}
