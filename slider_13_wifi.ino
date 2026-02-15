// slider_13_wifi.ino — WiFi AP + Web API + Web OTA (simple)

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

static WebServer* http = NULL;
static bool wifiRunning = false;
static String apSsid;
static const char* apPass = "slider123"; // 8+ chars
static String ipStr;

static void httpHandleRoot();
static void httpHandleStatus();
static void httpHandleNotFound();
static void httpHandleApi();
static void httpHandleUpdateGet();
static void httpHandleUpdatePost();

void wifiInit() {
  // Generate SSID with chip ID
  uint32_t chip = (uint32_t)ESP.getEfuseMac();
  char buf[20];
  snprintf(buf, sizeof(buf), "Slider-%04X", (uint16_t)(chip & 0xFFFF));
  apSsid = String(buf);
}

void wifiStartIfEnabled() {
  if (wifiRunning) return;
  if (!cfg.wifiEnabled) return;

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(apSsid.c_str(), apPass);
  if (!ok) {
    Serial.println("WiFi AP start failed");
    return;
  }

  IPAddress ip = WiFi.softAPIP();
  char ipb[24];
  snprintf(ipb, sizeof(ipb), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  ipStr = String(ipb);

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
  Serial.print("WiFi AP: "); Serial.print(apSsid); Serial.print(" "); Serial.println(ipStr);
}

void wifiStop() {
  if (!wifiRunning) return;
  if (http) { http->stop(); delete http; http = NULL; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiRunning = false;
  ipStr = String();
}

void wifiLoop() {
  if (wifiRunning && http) http->handleClient();
}

const char* wifiGetIpStr() {
  return (wifiRunning && ipStr.length()) ? ipStr.c_str() : "-";
}

// ── HTTP Handlers ──
static void httpHandleRoot() {
  String txt = "Camera Slider WiFi API\n";
  txt += "AP SSID: "; txt += apSsid; txt += "  PASS: "; txt += apPass; txt += "\n";
  txt += "IP: "; txt += ipStr; txt += "\n\n";
  txt += "Endpoints:\n";
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
