// wifi.cpp — WiFi off/station/hotspot, driven by the Wireless Settings menu (menu.cpp) and
// persisted in cfg (config.cpp). Mode switches happen live via WiFi.mode()/begin()/softAP()
// -- unlike BLE, the ESP32 Arduino WiFi stack handles runtime mode changes fine, so no
// reboot is needed here (see the plan's BLE-vs-WiFi risk note). The old single-hardcoded-
// network/wifi_env.h logic moved to config.cpp, which now owns first-boot NVS seeding.
//
// Scan results are copied into a small static cache the moment WiFi.scanComplete() reports
// done, then WiFi.scanDelete() releases the internal buffers -- menu.cpp/display.cpp read
// only the cache (never call WiFi.SSID() etc. directly), since those would otherwise heap-
// allocate a String on every ~100ms render tick while the scan screen is open.
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <string.h>
#include <stdio.h>
#include "wifi_module.h"
#include "globals.h"
#include "pins.h"
#include "motor.h"
#include "display.h"
#include "ble.h"
#include "buzzer.h"

static const char* OTA_HOSTNAME = "camera-slider";
static bool otaStarted = false;

// ── Scan result cache ──
struct WifiScanResult { char ssid[33]; int8_t rssi; bool open; };
static WifiScanResult scanResults[12];
static int scanResultCount = 0;
static bool scanPending = false;  // true from wifiStartScan() until results are cached
static unsigned long scanStartMs = 0;

static void apSsid(char* buf, size_t n) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(buf, n, "CameraSlider-%02X%02X", mac[4], mac[5]);
}

void wifiApply() {
  // WiFi and BLE share one radio. Keeping WiFi.setSleep(true) up used to be enough to dodge
  // ESP-IDF's hard-abort ("Should enable WiFi modem sleep when both WiFi and Bluetooth are
  // enabled") at the moment WiFi came up, but running both radios together for a while still
  // hangs the device later (a deeper coexistence issue than the one setSleep() covers).
  // WiFi is only ever needed for occasional OTA pushes / BLE-free control, so it's simplest
  // and most robust to just not run both at once: BLE goes down for as long as WiFi is on,
  // and comes back the moment WiFi is turned off (if the user still wants it).
  if (cfg.wifiMode == WIFI_CFG_OFF) {
    if (cfg.bleEnabled) bleInit();
  } else {
    bleShutdown();
  }

  switch (cfg.wifiMode) {
    case WIFI_CFG_OFF:
      // wifioff=true, eraseap=true: also wipes whatever STA config esp_wifi has cached,
      // so a stale "last connected AP" from before WiFi.persistent(false) (see wifiInit())
      // can't survive as an auto-reconnect target across a later "Off" selection.
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      break;
    case WIFI_CFG_STA:
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      WiFi.begin(cfg.staSsid, cfg.staPass);
      break;
    case WIFI_CFG_AP: {
      WiFi.mode(WIFI_AP);
      char ssid[24];
      apSsid(ssid, sizeof(ssid));
      WiFi.softAP(ssid, cfg.apPass[0] ? cfg.apPass : NULL);
      break;
    }
  }

  otaStarted = false;
}

void wifiInit() {
  // Arduino-ESP32 defaults to persistent WiFi storage: esp_wifi keeps its own copy of the
  // last mode/SSID/password in flash (separate from our own "slider" NVS namespace) and
  // can carry it across a reboot regardless of what cfg.wifiMode says. Since we already
  // persist wifiMode/staSsid/staPass ourselves and re-apply them explicitly every boot,
  // that second copy only causes drift -- turn it off so cfg is the only source of truth.
  WiFi.persistent(false);

  // Policy: WiFi always starts OFF, regardless of whatever mode was last selected/saved.
  // staSsid/staPass are left untouched, so "Connect to Network" -> same SSID still
  // prefills the saved password -- this only forces a fresh opt-in every boot, it doesn't
  // forget anything.
  cfg.wifiMode = WIFI_CFG_OFF;
  wifiApply();
}

void wifiLoop() {
  // Cache scan results as soon as they're ready, regardless of which screen is showing.
  // displayDirty must be set on every terminal outcome here: the scan screen renders
  // "Scanning..." once and then has nothing to animate, so without an explicit dirty flag
  // displayUpdate() would skip every subsequent redraw and the screen would sit on
  // "Scanning..." forever even though the scan had actually finished.
  if (scanPending) {
    int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      scanResultCount = (n > 12) ? 12 : n;
      for (int i = 0; i < scanResultCount; i++) {
        strncpy(scanResults[i].ssid, WiFi.SSID(i).c_str(), sizeof(scanResults[i].ssid) - 1);
        scanResults[i].ssid[sizeof(scanResults[i].ssid) - 1] = 0;
        scanResults[i].rssi = (int8_t)WiFi.RSSI(i);
        scanResults[i].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();
      scanPending = false;
      displayDirty = true;
    } else if (n == WIFI_SCAN_FAILED) {
      scanResultCount = 0;
      scanPending = false;
      displayDirty = true;
    } else if (millis() - scanStartMs > 15000) {
      // Belt-and-braces: never leave the UI stuck on "Scanning..." if the driver never
      // reports a terminal state (observed on some cores when the radio was mid mode-switch).
      WiFi.scanDelete();
      scanResultCount = 0;
      scanPending = false;
      displayDirty = true;
    }
  }

  if (cfg.wifiMode != WIFI_CFG_STA || WiFi.status() != WL_CONNECTED) {
    otaStarted = false;
    return;
  }

  if (!otaStarted) {
    MDNS.begin(OTA_HOSTNAME);
    ArduinoOTA.setHostname(OTA_HOSTNAME);
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#endif

    ArduinoOTA.onStart([]() {
      // Safety first: the transfer blocks loop() completely, so nothing would service the
      // state machine or endstops while the motor kept stepping -- and the reboot at the
      // end would leave it energised mid-move. Stop and release it before accepting data.
      motorStopNow();
      digitalWrite(MOTOR_EN, HIGH);
      sliderState = STATE_IDLE;
      displayOtaBegin();
      buzzerOtaStartChime();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      displayOtaProgress(total ? (progress * 100U) / total : 0U);
    });
    ArduinoOTA.onEnd([]() {
      displayOtaEnd("Done - rebooting", false);
      buzzerShutdownChime();  // ArduinoOTA calls ESP.restart() right after this callback returns
    });
    ArduinoOTA.onError([](ota_error_t err) {
      const char* m = "Update failed";
      switch (err) {
        case OTA_AUTH_ERROR:    m = "Auth failed";    break;
        case OTA_BEGIN_ERROR:   m = "Begin failed";   break;
        case OTA_CONNECT_ERROR: m = "Connect failed"; break;
        case OTA_RECEIVE_ERROR: m = "Receive failed"; break;
        case OTA_END_ERROR:     m = "End failed";     break;
      }
      displayOtaEnd(m, true);
      displayForceRepaint();  // the device keeps running, so restore the normal UI
    });

    ArduinoOTA.begin();
    otaStarted = true;
  }

  ArduinoOTA.handle();
}

void wifiStartScan() {
  WiFi.mode(WIFI_STA);  // scanning needs STA mode; drops any active AP, which is expected
  if (cfg.bleEnabled) WiFi.setSleep(true);  // see the coexistence note in wifiApply()
  WiFi.scanDelete();    // discard any previous result set before starting a fresh scan
  WiFi.scanNetworks(true);
  scanPending = true;
  scanResultCount = 0;
  scanStartMs = millis();
}

bool wifiScanInProgress() { return scanPending; }
int  wifiScanResultCount() { return scanResultCount; }
const char* wifiScanResultSsid(int i) { return (i >= 0 && i < scanResultCount) ? scanResults[i].ssid : ""; }
int8_t wifiScanResultRssi(int i) { return (i >= 0 && i < scanResultCount) ? scanResults[i].rssi : 0; }
bool wifiScanResultOpen(int i) { return (i >= 0 && i < scanResultCount) ? scanResults[i].open : false; }

void wifiStatusText(char* buf, size_t n) {
  switch (cfg.wifiMode) {
    case WIFI_CFG_OFF:
      snprintf(buf, n, "Off");
      break;
    case WIFI_CFG_STA:
      if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, n, "%s", WiFi.localIP().toString().c_str());
      } else {
        snprintf(buf, n, "Connecting...");
      }
      break;
    case WIFI_CFG_AP: {
      snprintf(buf, n, "AP %s", WiFi.softAPIP().toString().c_str());
      break;
    }
  }
}
