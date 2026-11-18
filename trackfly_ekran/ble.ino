// Bluetooth setup. While the WiFi setup portal is open, the display also offers a
// small BLE service, so Track Fly on a Mac can hand it the home network and its own
// address: nobody has to join the display's WiFi and type a password on a phone.
// The access point stays on next to it, as before.
//
// Service 7a1f0001-5c3e-4f6b-9a40-7472616b666c
//   7a1f0002  info    read          {"name","fw","mac","board"}
//   7a1f0003  nets    read          [{"s":ssid,"r":rssi,"l":locked}, ...] strongest first
//   7a1f0004  setup   write         {"ssid","pass","feed"} + "\n", in pieces of any size
//   7a1f0005  status  read, notify  {"state":"ready|scanning|connecting|connected|failed",
//                                    "msg","ip","name"}
// Once the display is on WiFi it restarts, so Bluetooth is off in normal use.
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

extern Settings cfg;

#define BLE_SVC     "7a1f0001-5c3e-4f6b-9a40-7472616b666c"
#define BLE_INFO    "7a1f0002-5c3e-4f6b-9a40-7472616b666c"
#define BLE_NETS    "7a1f0003-5c3e-4f6b-9a40-7472616b666c"
#define BLE_SETUP   "7a1f0004-5c3e-4f6b-9a40-7472616b666c"
#define BLE_STATUS  "7a1f0005-5c3e-4f6b-9a40-7472616b666c"

static BLECharacteristic *chNets = nullptr, *chStatus = nullptr;
static bool bleOn = false;
static bool bleJoining = false;          // the network being joined came over Bluetooth
static uint32_t bleRestartAt = 0;

// Filled by the Bluetooth task, picked up by loop().
static SemaphoreHandle_t bleMux;     // a mutex, not a spinlock: String allocates
static String rxBuf, rxDone;
static volatile bool wantScan = false;
static bool scanning = false;

static void bleStatus(const char *state, const String &msg = "") {
  if (!bleOn || !chStatus) return;
  JsonDocument doc;
  doc["state"] = state;
  if (msg.length()) doc["msg"] = msg;
  if (!strcmp(state, "connected")) {
    doc["ip"] = WiFi.localIP().toString();
    doc["name"] = cfg.name;
  }
  String out; serializeJson(doc, out);
  chStatus->setValue(out);
  chStatus->notify();
  Serial.println("BLE status " + out);
}

class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    xSemaphoreTake(bleMux, portMAX_DELAY); rxBuf = ""; xSemaphoreGive(bleMux);
    wantScan = true;                      // fresh network list for whoever is setting us up
    Serial.println("BLE: Mac connected");
  }
  void onDisconnect(BLEServer *) override { Serial.println("BLE: disconnected"); }
};

class SetupCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String piece = c->getValue();
    xSemaphoreTake(bleMux, portMAX_DELAY);
    rxBuf += piece;
    if (rxBuf.length() > 600) rxBuf = "";            // garbage: start over
    int nl = rxBuf.indexOf('\n');
    if (nl >= 0) { rxDone = rxBuf.substring(0, nl); rxBuf = ""; }
    xSemaphoreGive(bleMux);
  }
};

bool bleActive() { return bleOn; }

void bleStart() {
  if (bleOn) return;
  bleMux = xSemaphoreCreateMutex();
  String sfx = macSuffix(); sfx.toUpperCase();
  BLEDevice::init(("Track Fly " + sfx).c_str());
  BLEDevice::setMTU(247);
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCB());
  srv->advertiseOnDisconnect(true);
  BLEService *svc = srv->createService(BLE_SVC);

  JsonDocument info;
  info["name"] = cfg.name;
  info["fw"] = FW_VERSION;
  info["mac"] = WiFi.macAddress();
  info["board"] = "waveshare-3.49";
  String s; serializeJson(info, s);
  svc->createCharacteristic(BLE_INFO, BLECharacteristic::PROPERTY_READ)->setValue(s);

  chNets = svc->createCharacteristic(BLE_NETS, BLECharacteristic::PROPERTY_READ);
  chNets->setValue("[]");
  BLECharacteristic *chSetup = svc->createCharacteristic(BLE_SETUP, BLECharacteristic::PROPERTY_WRITE);
  chSetup->setCallbacks(new SetupCB());
  chStatus = svc->createCharacteristic(BLE_STATUS, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chStatus->setValue("{\"state\":\"ready\"}");
  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SVC);
  adv->setScanResponse(true);             // the name goes in the scan response
  BLEDevice::startAdvertising();
  bleOn = true;
  wantScan = true;
  Serial.printf("BLE setup on, free heap %u\n", ESP.getFreeHeap());
}

// The setup portal opened again (first start, BOOT held, or the network was not joined).
void bleSetupPortal(const char *why) {
  bleStart();
  if (bleJoining) { bleJoining = false; bleStatus("failed", why); }
}

// WiFi is up. If Bluetooth was on, tell the Mac where we are and restart without it.
void bleConnected() {
  if (!bleOn) return;
  bleStatus("connected");
  bleJoining = false;
  bleRestartAt = millis() + 3500;          // time for the Mac to read the answer
}

// "Can't join" message for the Mac, from what the WiFi driver last said.
String bleJoinProblem(const String &ssid) {
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL: return "Can't find " + ssid;
    case WL_CONNECT_FAILED: return "Wrong password for " + ssid + "?";
    default: return "Can't reach " + ssid;
  }
}

static void sendNets(int n) {
  int idx[40]; int k = 0;
  for (int i = 0; i < n && k < 40; i++) if (WiFi.SSID(i).length()) idx[k++] = i;
  for (int a = 0; a < k; a++)               // strongest first
    for (int b = a + 1; b < k; b++)
      if (WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[a])) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  String out = "[]";
  for (int a = 0; a < k; a++) {
    String ss = WiFi.SSID(idx[a]);
    bool dup = false;
    for (JsonObject o : arr) if (ss == (const char *)o["s"]) dup = true;
    if (dup) continue;
    JsonObject o = arr.add<JsonObject>();
    o["s"] = ss;
    o["r"] = WiFi.RSSI(idx[a]);
    o["l"] = WiFi.encryptionType(idx[a]) != WIFI_AUTH_OPEN ? 1 : 0;
    String t; serializeJson(doc, t);
    if (t.length() > 480) { arr.remove(arr.size() - 1); break; }   // one read, no long lists
    out = t;
  }
  chNets->setValue(out);
}

void bleLoop() {
  if (!bleOn) return;
  if (bleRestartAt && millis() > bleRestartAt) {
    Serial.println("BLE setup done, restarting without Bluetooth");
    delay(100);
    ESP.restart();
  }

  // Network list: an async scan, so the screen keeps running.
  if (wantScan && !scanning && !bleJoining) {
    wantScan = false;
    if (WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING) { scanning = true; bleStatus("scanning"); }
  }
  if (scanning) {
    int n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      scanning = false;
      if (n > 0) sendNets(n);
      WiFi.scanDelete();
      bleStatus("ready");
    }
  }

  String msg;
  xSemaphoreTake(bleMux, portMAX_DELAY);
  if (rxDone.length()) { msg = rxDone; rxDone = ""; }
  xSemaphoreGive(bleMux);
  if (!msg.length()) return;

  JsonDocument doc;
  if (deserializeJson(doc, msg)) { bleStatus("failed", "Unreadable setup data"); return; }
  String ssid = doc["ssid"] | "", pass = doc["pass"] | "", feed = doc["feed"] | "";
  ssid.trim(); feed.trim();
  if (!ssid.length()) { bleStatus("failed", "Network name is empty"); return; }
  if (feed.length()) { cfg.feed = feed; settingsSave(); }
  if (scanning) { WiFi.scanDelete(); scanning = false; }
  bleJoining = true;
  bleStatus("connecting", ssid);
  Serial.printf("BLE setup: network '%s', Track Fly at %s\n", ssid.c_str(), feed.c_str());
  netSaveWifi(ssid, pass);
}
