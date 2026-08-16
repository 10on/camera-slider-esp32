// wifi_module.h — WiFi (off/station/hotspot, menu-driven) + ArduinoOTA. Named
// wifi_module.h (not wifi.h) to avoid a case-insensitive filename collision with the
// framework's own WiFi.h on macOS's default case-insensitive filesystem -- that collision
// previously made `#include <WiFi.h>` resolve to this file instead of the real one,
// breaking every WiFi symbol.
// Best-effort: the slider works fully offline over BLE; WiFi is optional (Wireless
// Settings menu) for OTA pushes (`pio run -e ota -t upload`) and BLE-free control.
#pragma once

#include <stdint.h>
#include <stddef.h>

void wifiInit();       // called once at boot; applies cfg.wifiMode using saved creds
void wifiLoop();        // call every loop(); non-blocking OTA handling when STA connected
void wifiApply();       // re-applies cfg.wifiMode/creds live (call after menu changes)

void  wifiStartScan();
bool  wifiScanInProgress();
int   wifiScanResultCount();
const char* wifiScanResultSsid(int i);
int8_t wifiScanResultRssi(int i);
bool  wifiScanResultOpen(int i);

// Fills buf with a short human-readable status for the Wireless Settings row:
// "Off" / "Connecting..." / an IP string / "AP <ip>".
void wifiStatusText(char* buf, size_t n);
