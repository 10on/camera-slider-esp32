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

  if (WIFI_CREDENTIALS_COUNT == 0) {
    Serial.println("WiFi: no credentials present");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  // Scan and select best known SSID
  int n = WiFi.scanNetworks();
  int bestIdx = -1;
  int bestRssi = -127;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      for (size_t k = 0; k < WIFI_CREDENTIALS_COUNT; k++) {
        if (ssid == WIFI_CREDENTIALS[k].ssid) {
          int r = WiFi.RSSI(i);
          if (r > bestRssi) { bestRssi = r; bestIdx = (int)k; }
        }
      }
    }
  }

  // Fallback: try sequentially if scan found none
  int tryOrderCount = bestIdx >= 0 ? 1 : (int)WIFI_CREDENTIALS_COUNT;
  for (int t = 0; t < tryOrderCount; t++) {
    int k = (bestIdx >= 0) ? bestIdx : t;
    
    WiFi.begin(WIFI_CREDENTIALS[k].ssid, WIFI_CREDENTIALS[k].pass);

    wifiConnecting = true;
    wifiLastAttempt = millis();
    unsigned long start = millis();
    while (millis() - start < 12000) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(100);
    }
    wifiConnecting = false;

    if (WiFi.status() == WL_CONNECTED) break;
  }

  if (WiFi.status() != WL_CONNECTED) {
    
    WiFi.mode(WIFI_OFF);
    return;
  }

  IPAddress ip = WiFi.localIP();
  ipStr = ip.toString();

  http = new WebServer(80);
  http->on("/", HTTP_GET, httpHandleRoot);
  http->on("/status", HTTP_GET, httpHandleStatus);
  http->on("/api", HTTP_ANY, httpHandleApi);
  // Web OTA
  http->on("/update", HTTP_GET, httpHandleUpdateGet);
  http->on("/update", HTTP_POST, httpHandleUpdatePost, httpHandleUpdatePost);
  http->onNotFound(httpHandleNotFound);
  http->begin();

  wifiRunning = true;
  
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
  // Auto-reconnect if disconnected
  if (cfg.wifiEnabled && wifiRunning && WiFi.status() != WL_CONNECTED && !wifiConnecting) {
    if (millis() - wifiLastAttempt > 10000) {
      wifiStop();
      wifiStartIfEnabled();
    }
  }
}

const char* wifiGetIpStr() {
  return (wifiRunning && ipStr.length()) ? ipStr.c_str() : "-";
}

// ── HTTP Handlers ──
static void httpHandleRoot() {
  String txt = "Camera Slider WiFi API (STA)\n";
  txt += "IP: "; txt += ipStr; txt += "\n\n";
  txt += "Routes:\n";
  txt += "  GET /status   (JSON)\n";
  txt += "  ANY /api?cmd=forward|backward|stop|home\n";
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
