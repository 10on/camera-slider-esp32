// slider_13_wifi.ino — WiFi STA + Web API + Web OTA (no UI)

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

#if __has_include("wifi_env.h")
#include "wifi_env.h"  // defines WIFI_CREDENTIALS[] and WIFI_CREDENTIALS_COUNT
#else
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_CREDENTIALS[] = {};
static const size_t WIFI_CREDENTIALS_COUNT = 0;
#endif

static WebServer* http = NULL;
static bool wifiRunning = false;
static String ipStr;
static bool wifiConnecting = false;
static unsigned long wifiLastAttempt = 0;
static int* knownRssi = NULL; // size WIFI_CREDENTIALS_COUNT, -128 if not seen
static unsigned long lastScan = 0;
static bool scanPending = false;
static unsigned long ipPopupUntil = 0;
static String ipPopupMsg;
static int wifiState = 0; // 0=idle,1=scanning,2=connecting
static int wifiMode = 0;  // 0=API, 1=OTA

// View-only scan cache
static int scanViewState = 0; // 0=idle,1=scanning,2=done
static int scanCount = 0;
static char scanSsids[16][33];
static int scanRssi[16];
static unsigned long scanViewStartAt = 0;

static void httpHandleRoot();
static void httpHandleStatus();
static void httpHandleNotFound();
static void httpHandleApi();
static void httpHandleUpdateGet();
static void httpHandleUpdatePost();

void wifiInit() {}

void wifiStartIfEnabled() {
  if (wifiRunning) return;
  if (!cfg.wifiEnabled) return;

  if (WIFI_CREDENTIALS_COUNT == 0) { return; }

  // Start async scanning workflow
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.scanNetworks(true); // async
  wifiState = 1; // scanning
  wifiLastAttempt = millis();
}

void wifiStop() {
  if (!wifiRunning) return;
  if (http) { http->stop(); delete http; http = NULL; }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiRunning = false;
  ipStr = String();
}

void wifiLoop() {
  if (wifiRunning && http) http->handleClient();
  // Async scan/connect workflow
  if (wifiState == 1) {
    int c = WiFi.scanComplete();
    if (c >= 0) {
      // choose first known SSID in env order
      int chosen = -1;
      for (size_t k = 0; k < WIFI_CREDENTIALS_COUNT; k++) {
        for (int i = 0; i < c; i++) {
          if (WiFi.SSID(i) == WIFI_CREDENTIALS[k].ssid) { chosen = (int)k; break; }
        }
        if (chosen >= 0) break;
      }
      if (chosen >= 0) {
        WiFi.begin(WIFI_CREDENTIALS[chosen].ssid, WIFI_CREDENTIALS[chosen].pass);
        wifiState = 2; // connecting
        wifiConnecting = true;
        wifiLastAttempt = millis();
      } else {
        // no known SSID found
        ipPopupMsg = String("WiFi failed");
        ipPopupUntil = millis() + 2000;
        displayDirty = true;
        WiFi.mode(WIFI_OFF);
        cfg.wifiEnabled = false;
        // Navigate away from connect screen
        if (wifiMode == 1) { currentScreen = SCREEN_WIFI_OTA; }
        else { currentScreen = SCREEN_SETTINGS; }
        displayDirty = true;
        wifiState = 0;
      }
      WiFi.scanDelete();
    }
    // Fail-safe timeout for scanning (connect flow)
    else if ((long)(millis() - wifiLastAttempt) > 10000) {
      // cancel scan
      WiFi.scanDelete();
      ipPopupMsg = String("WiFi failed");
      ipPopupUntil = millis() + 2000;
      displayDirty = true;
      WiFi.mode(WIFI_OFF);
      cfg.wifiEnabled = false;
      if (wifiMode == 1) { currentScreen = SCREEN_WIFI_OTA; }
      else { currentScreen = SCREEN_SETTINGS; }
      displayDirty = true;
      wifiState = 0;
    }
  } else if (wifiState == 2) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      // Start server
      IPAddress ip = WiFi.localIP();
      ipStr = ip.toString();
      http = new WebServer(80);
      http->on("/status", HTTP_GET, httpHandleStatus);
      http->on("/api", HTTP_ANY, httpHandleApi);
      if (wifiMode == 1) {
        // OTA mode: expose update route; root can be minimal text
        http->on("/", HTTP_GET, httpHandleRoot);
        http->on("/update", HTTP_GET, httpHandleUpdateGet);
        http->on("/update", HTTP_POST, httpHandleUpdatePost, httpHandleUpdatePost);
      } else {
        http->on("/", HTTP_GET, httpHandleRoot);
      }
      http->onNotFound(httpHandleNotFound);
      http->begin();
      wifiRunning = true;
      // popup IP
      ipPopupMsg = String("IP: ") + ipStr;
      ipPopupUntil = millis() + 2000;
      displayDirty = true;
      // jump to OTA screen if requested
      if (wifiMode == 1) { currentScreen = SCREEN_WIFI_OTA; displayDirty = true; }
      wifiState = 0; // done
    } else if (millis() - wifiLastAttempt > 12000) {
      wifiConnecting = false;
      ipPopupMsg = String("WiFi failed");
      ipPopupUntil = millis() + 2000;
      displayDirty = true;
      WiFi.mode(WIFI_OFF);
      cfg.wifiEnabled = false;
      // Navigate away from connect screen
      if (wifiMode == 1) { currentScreen = SCREEN_WIFI_OTA; }
      else { currentScreen = SCREEN_SETTINGS; }
      displayDirty = true;
      wifiState = 0;
    }
  }
  // View-only scan workflow
  if (scanViewState == 1) {
    int c = WiFi.scanComplete();
    if (c >= 0) {
      scanCount = (c > 16) ? 16 : c;
      for (int i = 0; i < scanCount; i++) {
        String s = WiFi.SSID(i);
        s.toCharArray(scanSsids[i], sizeof(scanSsids[i]));
        scanRssi[i] = WiFi.RSSI(i);
      }
      WiFi.scanDelete();
      scanViewState = 2;
      displayDirty = true;
    }
    // Timeout guard for view-only scan
    else if ((long)(millis() - scanViewStartAt) > 8000) {
      WiFi.scanDelete();
      scanViewState = 0;
      ipPopupMsg = String("Scan timeout");
      ipPopupUntil = millis() + 2000;
      displayDirty = true;
    }
  }
}

const char* wifiGetIpStr() {
  return (wifiRunning && ipStr.length()) ? ipStr.c_str() : "-";
}

bool wifiHasCredentials() {
  return WIFI_CREDENTIALS_COUNT > 0;
}

// Known networks metadata for UI
int wifiKnownCount() { return (int)WIFI_CREDENTIALS_COUNT; }
const char* wifiKnownSsid(int idx) { return (idx >= 0 && idx < (int)WIFI_CREDENTIALS_COUNT) ? WIFI_CREDENTIALS[idx].ssid : ""; }
int wifiSelectedIndex() { return (int)cfg.wifiSel; }

void wifiRequestScan() { scanPending = true; }

static void wifiDoScanKnown() {
  if (WIFI_CREDENTIALS_COUNT == 0) return;
  if (!knownRssi) {
    knownRssi = new int[WIFI_CREDENTIALS_COUNT];
    for (size_t i = 0; i < WIFI_CREDENTIALS_COUNT; i++) knownRssi[i] = -128;
  }
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  for (size_t i = 0; i < WIFI_CREDENTIALS_COUNT; i++) knownRssi[i] = -128;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    for (size_t k = 0; k < WIFI_CREDENTIALS_COUNT; k++) {
      if (ssid == WIFI_CREDENTIALS[k].ssid) {
        knownRssi[k] = rssi;
      }
    }
  }
  lastScan = millis();
}

int wifiKnownRssi(int idx) {
  if (!knownRssi || idx < 0 || idx >= (int)WIFI_CREDENTIALS_COUNT) return -128;
  return knownRssi[idx];
}

bool wifiGetIpPopup(char* buf, size_t len) {
  if (ipPopupUntil == 0) return false;
  if ((long)(ipPopupUntil - millis()) <= 0) { ipPopupUntil = 0; ipPopupMsg = String(); return false; }
  if (buf && len) {
    strlcpy(buf, ipPopupMsg.c_str(), len);
  }
  return true;
}

// Start requests for API/OTA
void wifiStartApi() {
  if (!wifiHasCredentials()) { ipPopupMsg = String("No creds"); ipPopupUntil = millis() + 2000; displayDirty = true; return; }
  cfg.wifiEnabled = true;
  wifiMode = 0;
  wifiStartIfEnabled();
}

void wifiStartOta() {
  if (!wifiHasCredentials()) { ipPopupMsg = String("No creds"); ipPopupUntil = millis() + 2000; displayDirty = true; return; }
  cfg.wifiEnabled = true;
  wifiMode = 1;
  wifiStartIfEnabled();
}

int wifiConnectState() { return wifiState; }

// View-only scan API
void wifiScanStart() {
  // Start a fresh async scan
  scanCount = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.scanNetworks(true);
  scanViewState = 1;
  scanViewStartAt = millis();
  displayDirty = true;
}

int wifiScanState() { return scanViewState; }
int wifiScanCount() { return scanCount; }
const char* wifiScanSsid(int idx) {
  if (idx < 0 || idx >= scanCount) return "";
  return scanSsids[idx];
}
int wifiScanRssi(int idx) {
  if (idx < 0 || idx >= scanCount) return -128;
  return scanRssi[idx];
}

// ── HTTP Handlers ──
static void httpHandleRoot() {
  String txt = "Camera Slider WiFi API (STA)\n";
  txt += "IP: "; txt += ipStr; txt += "\n\n";
  txt += "Routes:\n";
  txt += "  GET /status   (JSON)\n";
  txt += "  ANY /api?cmd=forward|backward|stop|home\n";
  txt += "      /api?cmd=sethome\n";
  txt += "      /api?cmd=goto&pos=N\n";
  txt += "      /api?cmd=speed&val=1..100\n";
  txt += "      /api?cmd=current&val=200..1500\n";
  txt += "  GET/POST /update   (OTA upload .bin)\n";
  http->send(200, "text/plain", txt);
}

static void httpHandleStatus() {
  String s = "{";
  s += "\"state\":\"" + String(stateToString(sliderState)) + "\",";
  s += "\"pos\":" + String((long)currentPosition) + ",";
  s += "\"travel\":" + String((long)travelDistance) + ",";
  s += "\"center\":" + String((long)centerPosition) + ",";
  s += "\"home\":" + String((long)cfg.savedHome) + ",";
  s += "\"speed\":" + String((int)cfg.speed) + ",";
  s += "\"current\":" + String((int)cfg.motorCurrent) + ",";
  s += "\"end1\":" + String(endstop1 ? 1 : 0) + ",";
  s += "\"end2\":" + String(endstop2 ? 1 : 0) + ",";
  s += "\"calib\":" + String(isCalibrated ? 1 : 0) + ",";
  s += "\"battery\":" + String((int)vbatPercent()) + ",";
  s += "\"ble\":" + String(bleConnected ? 1 : 0);
  s += "}";
  http->send(200, "application/json", s);
}

static void apiSendOk()    { http->send(200, "application/json", "{\"ok\":true}"); }
static void apiSendError(const char* m) { http->send(400, "application/json", String("{\"error\":\"") + m + "\"}"); }

static void httpHandleApi() {
  if (!http->hasArg("cmd")) { apiSendError("no cmd"); return; }
  String cmd = http->arg("cmd");

  if (cmd == "forward") { cmdForward = true; apiSendOk(); return; }
  if (cmd == "backward") { cmdBackward = true; apiSendOk(); return; }
  if (cmd == "stop") { cmdStop = true; apiSendOk(); return; }
  if (cmd == "home") { cmdHome = true; apiSendOk(); return; }
  if (cmd == "sethome") {
    cfg.savedHome = currentPosition;
    preferences.begin("slider", false);
    preferences.putLong("homePos", cfg.savedHome);
    preferences.end();
    apiSendOk(); return;
  }
  if (cmd == "goto") {
    if (!http->hasArg("pos")) { apiSendError("no pos"); return; }
    cmdTargetPos = http->arg("pos").toInt();
    cmdGoToPos = true; apiSendOk(); return;
  }
  if (cmd == "speed") {
    if (!http->hasArg("val")) { apiSendError("no val"); return; }
    cmdNewSpeed = constrain(http->arg("val").toInt(), 1, 100);
    cmdSpeedChanged = true; apiSendOk(); return;
  }
  if (cmd == "current") {
    if (!http->hasArg("val")) { apiSendError("no val"); return; }
    cmdNewCurrent = constrain(http->arg("val").toInt(), 200, 1500);
    cmdCurrentChanged = true; apiSendOk(); return;
  }
  if (cmd == "reset_error") { stateResetError(); apiSendOk(); return; }

  apiSendError("unknown cmd");
}

// Simple web OTA page (upload .bin)
static void httpHandleUpdateGet() {
  String html = "<!doctype html><html><head><meta charset='utf-8'><title>OTA</title></head><body>";
  html += "<h3>Firmware Update</h3><form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='firmware'><input type='submit' value='Update'></form>";
  html += "</body></html>";
  http->send(200, "text/html", html);
}

static void httpHandleUpdatePost() {
  HTTPUpload& up = http->upload();
  if (http->method() == HTTP_POST) {
    if (up.status == UPLOAD_FILE_START) {
      Serial.printf("OTA: start %s\n", up.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        Update.printError(Serial);
      }
    } else if (up.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA: success, %u bytes\n", up.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  }
  if (Update.hasError()) {
    http->send(200, "text/plain", "Update Failed");
  } else {
    http->send(200, "text/plain", "Update OK, rebooting...");
    delay(500);
    ESP.restart();
  }
}

static void httpHandleNotFound() {
  http->send(404, "text/plain", "Not found");
}
