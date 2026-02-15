// slider_10_display.ino — OLED display rendering (u8g2)

static void drawHeader(const char* title) {
  oled->setFont(u8g2_font_6x10_tf);
  // Title left
  oled->setCursor(0, 8);
  if (title && title[0]) oled->print(title);
  // Battery right
  char bbuf[8];
  int bp = vbatPercent();
  snprintf(bbuf, sizeof(bbuf), "%d%%", bp);
  uint16_t bw = oled->getStrWidth(bbuf);
  oled->setCursor(128 - bw, 8);
  oled->print(bbuf);
  // Divider line
  oled->drawLine(0, 10, 127, 10);
}

void displayUpdate() {
  if (!oledFound) return;

  oled->clearBuffer();

  switch (currentScreen) {
    case SCREEN_MAIN:        drawMainScreen(); break;
    case SCREEN_MENU:        drawMenuScreen(); break;
    case SCREEN_MANUAL_MOVE: drawManualMoveScreen(); break;
    case SCREEN_GO_TO_POS:   drawGoToPosScreen(); break;
    case SCREEN_CALIBRATION: drawCalibrationScreen(); break;
    case SCREEN_SETTINGS:    drawSettingsMenu(); break;
    case SCREEN_MOTION_SETTINGS: drawMotionSettings(); break;
    case SCREEN_SLEEP_SETTINGS:  drawSleepSettings(); break;
    case SCREEN_SYSTEM_SETTINGS: drawSystemSettings(); break;
    case SCREEN_WIFI_NETWORKS: drawWifiNetworksScreen(); break;
    case SCREEN_WIFI_CONNECT:  drawWifiConnectScreen(); break;
    case SCREEN_WIFI_SCAN:     drawWifiScanScreen(); break;
    case SCREEN_WIFI_OTA:    drawWifiOtaScreen(); break;
    case SCREEN_VALUE_EDIT:  drawValueEditor(); break;
  }

  // Send only changed tiles to the display
  oled->updateDisplay();
}

// ── Main screen ──
void drawMainScreen() {
  // Line 1: State + BLE
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print(stateToString(sliderState));

  // Battery percentage (top right)
  {
    int bp = vbatPercent();
    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", bp);
    uint16_t w = oled->getStrWidth(buf);
    oled->setCursor(128 - w, 8);
    oled->print(buf);
  }

  // Line 2: Error (if any)
  if (sliderState == STATE_ERROR) {
    oled->setCursor(0, 18);
    oled->print("! ");
    oled->print(errorToString(errorCode));
  }

  // Line 3-4: Position (large)
  oled->setFont(u8g2_font_10x20_tf);
  oled->setCursor(0, 38);
  if (isCalibrated) {
    // Show percentage
    int pct = travelDistance > 0 ? (int)((int64_t)currentPosition * 100 / travelDistance) : 0;
    oled->print(pct);
    oled->print("%");
  } else {
    oled->print(currentPosition);
  }

  // Line 5: Speed
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 46);
  oled->print("Speed: ");
  oled->print(cfg.speed);
  oled->print("%");

  // Direction indicator
  oled->setCursor(80, 46);
  if (motorRunning) {
    oled->print(motorDirection ? "<< REV" : ">> FWD");
  }

  // Line 6: Endstops
  oled->setCursor(0, 58);
  oled->print("E1:");
  oled->print(endstop1 ? "ON " : "off");
  oled->print(" E2:");
  oled->print(endstop2 ? "ON" : "off");

  // Calibrated indicator
  if (isCalibrated) {
    oled->setCursor(100, 58);
    oled->print("CAL");
  }
}

// ── Generic list menu drawer ──
void drawListMenu(const char* title, const char* const* items, uint8_t count) {
  drawHeader(title);

  uint8_t maxVisible = 5;
  // Adjust scroll offset
  if (menuIndex < menuOffset) menuOffset = menuIndex;
  if (menuIndex >= menuOffset + maxVisible) menuOffset = menuIndex - maxVisible + 1;

  for (uint8_t i = 0; i < maxVisible && (menuOffset + i) < count; i++) {
    uint8_t idx = menuOffset + i;
    uint8_t y = 12 + i * 10;

    if (idx == menuIndex) {
      oled->drawBox(0, y, 128, 10);
      oled->setDrawColor(0);
    }

    oled->setCursor(2, y + 8);
    oled->print(items[idx]);

    if (idx == menuIndex) {
      oled->setDrawColor(1);
    }
  }
}

// ── Main menu ──
static const char* mainMenuItems[] = {
  "Manual Move",
  "Go to Position",
  "Calibration",
  "Settings",
  "Back"
};
#define MAIN_MENU_COUNT 5

void drawMenuScreen() {
  drawListMenu("MENU", mainMenuItems, MAIN_MENU_COUNT);
}

// ── Manual move screen ──
void drawManualMoveScreen() {
  drawHeader("MANUAL MOVE");

  oled->setCursor(0, 18);
  oled->print("Dir: ");
  oled->print(motorDirection ? "Backward" : "Forward");

  oled->setCursor(0, 30);
  oled->print("Speed: ");
  oled->print(cfg.speed);
  oled->print("%");

  oled->setFont(u8g2_font_10x20_tf);
  oled->setCursor(0, 48);
  if (sliderState == STATE_MANUAL_MOVING) {
    oled->print("RUNNING");
  } else {
    oled->print("STOPPED");
  }

  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 58);
  oled->print("Press=Start/Stop Long=Dir");
}

// ── Go to position screen ──
void drawGoToPosScreen() {
  drawHeader("GO TO POSITION");

  if (!isCalibrated) {
    oled->setCursor(10, 34);
    oled->print("Not calibrated!");
    oled->setCursor(10, 46);
    oled->print("Run homing first");
    return;
  }

  oled->setCursor(0, 18);
  oled->print("Current: ");
  oled->print(currentPosition);

  oled->setCursor(0, 30);
  oled->print("Target: ");
  oled->print(cmdTargetPos);

  oled->setCursor(0, 42);
  oled->print("Travel: ");
  oled->print(travelDistance);

  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 58);
  oled->print("Rotate=Target Press=Go");
}

// ── Calibration screen ──
void drawCalibrationScreen() {
  drawHeader("CALIBRATION");

  if (sliderState == STATE_HOMING) {
    oled->setCursor(0, 24);
    switch (homingPhase) {
      case HOME_SEEK_END1: oled->print("Seeking End 1..."); break;
      case HOME_BACKOFF1:  oled->print("Backing off 1..."); break;
      case HOME_SEEK_END2: oled->print("Seeking End 2..."); break;
      case HOME_BACKOFF2:  oled->print("Backing off 2..."); break;
      case HOME_GO_CENTER: oled->print("Going to center..."); break;
      default: oled->print("Homing..."); break;
    }

    // Progress bar
    oled->drawFrame(0, 42, 128, 10);
    uint8_t progress = 0;
    switch (homingPhase) {
      case HOME_SEEK_END1: progress = 20; break;
      case HOME_BACKOFF1:  progress = 40; break;
      case HOME_SEEK_END2: progress = 60; break;
      case HOME_BACKOFF2:  progress = 80; break;
      case HOME_GO_CENTER: progress = 100; break;
      default: break;
    }
    oled->drawBox(2, 44, (124 * progress) / 100, 6);
  } else {
    oled->setCursor(0, 18);
    oled->print("Status: ");
    oled->print(isCalibrated ? "Calibrated" : "Not calibrated");

    if (isCalibrated) {
      oled->setCursor(0, 30);
      oled->print("Travel: ");
      oled->print(travelDistance);

      oled->setCursor(0, 42);
      oled->print("Center: ");
      oled->print(centerPosition);
    }

    oled->setCursor(0, 58);
    oled->print("Press=Start homing");
  }
}

// ── Settings menus ──
#if FEATURE_WIFI
static const char* settingsItems[] = {
  "Motion",
  "Sleep",
  "System",
  "Back"
};
#define SETTINGS_COUNT 4
#else
static const char* settingsItems[] = {
  "Motion",
  "Sleep",
  "System",
  "Back"
};
#define SETTINGS_COUNT 4
#endif

void drawSettingsMenu() {
  drawListMenu("SETTINGS", settingsItems, SETTINGS_COUNT);
}

static const char* motionItems[] = {
  "Speed",
  "Accel Ramp",
  "Microsteps",
  "Endstop Mode",
  "Back"
};
#define MOTION_COUNT 5

void drawMotionSettings() {
  drawListMenu("MOTION", motionItems, MOTION_COUNT);
}

static const char* sleepItems[] = {
  "Sleep Timeout",
  "ADXL Sensitivity",
  "Wake on Motion",
  "Back"
};
#define SLEEP_COUNT 4

void drawSleepSettings() {
  drawListMenu("SLEEP", sleepItems, SLEEP_COUNT);
}

static const char* systemItems[] = {
  "Motor Current",
  "Reset Calibration",
  "Reset Error",
  "Status",
  "Back"
};
#define SYSTEM_COUNT 5

void drawSystemSettings() {
  drawListMenu("SYSTEM", systemItems, SYSTEM_COUNT);
}

// ── WiFi OTA info screen ──
void drawWifiOtaScreen() {
  drawHeader("WIFI OTA");

  oled->setCursor(0, 22);
  if (!wifiHasCredentials()) {
    oled->print("No WiFi creds");
    oled->setCursor(0, 34);
    oled->print("Add wifi_env.h & rebuild");
  } else {
    // If not running and not in progress — kick off OTA connect
    int st = wifiConnectState();
    if (st == 0 && strcmp(wifiGetIpStr(), "-") == 0) {
      wifiStartOta();
      st = wifiConnectState();
    }

    if (strcmp(wifiGetIpStr(), "-") != 0) {
      oled->print("IP: ");
      oled->print(wifiGetIpStr());
      oled->setCursor(0, 34);
      oled->print("Open /update");
    } else {
      // Show status while connecting
      if (st == 1) {
        oled->print("Scanning networks");
      } else if (st == 2) {
        oled->print("Connecting");
      } else {
        oled->print("Starting WiFi");
      }
      uint8_t dots = (millis() / 300) % 4;
      for (uint8_t i = 0; i < dots; i++) oled->print(".");
    }
  }

  // Popup overlay
  char pop[32];
  if (wifiGetIpPopup(pop, sizeof(pop))) {
    oled->setFont(u8g2_font_6x10_tf);
    uint16_t w = oled->getStrWidth(pop);
    uint8_t bx = (128 - (w + 8)) / 2;
    uint8_t by = 48;
    oled->drawBox(bx, by, w + 8, 14);
    oled->setDrawColor(0);
    oled->setCursor(bx + 4, by + 10);
    oled->print(pop);
    oled->setDrawColor(1);
  }

  oled->setCursor(0, 58);
  oled->print("Long=Back");
}

// ── WiFi Networks screen ──
static uint8_t rssiToBars(int rssi) {
  if (rssi <= -90) return 0;
  if (rssi <= -80) return 1;
  if (rssi <= -70) return 2;
  if (rssi <= -60) return 3;
  return 4;
}

static void drawBars(uint8_t x, uint8_t y, uint8_t bars) {
  // Draw 4 vertical bars (2px width, 2px gap), baseline at y+8
  uint8_t bw = 2, gap = 2, h[4] = {3, 5, 7, 9};
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t bx = x + i * (bw + gap);
    uint8_t bh = (i < bars) ? h[i] : 0;
    if (bh) oled->drawBox(bx, y + 10 - bh, bw, bh);
    else oled->drawFrame(bx, y + 10 - h[i], bw, h[i]);
  }
}

void drawWifiNetworksScreen() {
  drawHeader("WIFI NETS");

  // List: known SSIDs only (no Auto)
  int total = wifiKnownCount();
  // Adjust scroll offset
  uint8_t maxVisible = 5;
  if (menuIndex < menuOffset) menuOffset = menuIndex;
  if (menuIndex >= menuOffset + maxVisible) menuOffset = menuIndex - maxVisible + 1;

  for (uint8_t i = 0; i < maxVisible && (menuOffset + i) < total; i++) {
    uint8_t idx = menuOffset + i;
    uint8_t y = 12 + i * 10;

    if (idx == menuIndex) {
      oled->drawBox(0, y, 128, 10);
      oled->setDrawColor(0);
    }

    oled->setCursor(2, y + 8);
    const char* ssid = wifiKnownSsid(idx);
    oled->print(ssid);
    int rssi = wifiKnownRssi(idx);
    uint8_t bars = (rssi <= -120) ? 0 : rssiToBars(rssi);
    drawBars(128 - 4*(2+2), y, bars);

    if (idx == menuIndex) {
      oled->setDrawColor(1);
    }
  }

  // Mark selected
  int sel = wifiSelectedIndex();
  if (sel >= 0) {
    if (sel >= menuOffset && sel < menuOffset + 5) {
      uint8_t y = 12 + (sel - menuOffset) * 10;
      oled->drawTriangle(0, y + 5, 3, y + 2, 3, y + 8);
    }
  }

  // Popup IP/failed message overlay (centered) for a short time
  char pop[32];
  if (wifiGetIpPopup(pop, sizeof(pop))) {
    // simple centered box
    oled->setFont(u8g2_font_6x10_tf);
    uint16_t w = oled->getStrWidth(pop);
    uint8_t bx = (128 - (w + 8)) / 2;
    uint8_t by = 30;
    oled->drawBox(bx, by, w + 8, 14);
    oled->setDrawColor(0);
    oled->setCursor(bx + 4, by + 10);
    oled->print(pop);
    oled->setDrawColor(1);
  }
}

// ── WiFi Connect splash ──
void drawWifiConnectScreen() {
  drawHeader("WIFI");
  oled->setCursor(0, 30);
  int st = wifiConnectState();
  if (st == 1) {
    oled->print("Scanning networks");
  } else if (st == 2) {
    oled->print("Connecting");
  } else {
    oled->print("Idle");
  }
  // Dots animation
  uint8_t dots = (millis() / 300) % 4;
  for (uint8_t i = 0; i < dots; i++) oled->print(".");

  // Popup overlay
  char pop[32];
  if (wifiGetIpPopup(pop, sizeof(pop))) {
    oled->setFont(u8g2_font_6x10_tf);
    uint16_t w = oled->getStrWidth(pop);
    uint8_t bx = (128 - (w + 8)) / 2;
    uint8_t by = 44;
    oled->drawBox(bx, by, w + 8, 14);
    oled->setDrawColor(0);
    oled->setCursor(bx + 4, by + 10);
    oled->print(pop);
    oled->setDrawColor(1);
  }
}

// ── WiFi Scan screen (view-only) ──
void drawWifiScanScreen() {
  drawHeader("WIFI SCAN");
  int st = wifiScanState();
  if (st == 0) {
    oled->setCursor(0, 30);
    oled->print("Press=Scan");
  } else if (st == 1) {
    oled->setCursor(0, 30);
    oled->print("Scanning");
    uint8_t dots = (millis() / 300) % 4;
    for (uint8_t i = 0; i < dots; i++) oled->print(".");
  } else {
    // show list of found networks
    int count = wifiScanCount();
    int total = count;
    uint8_t maxVisible = 5;
    if (menuIndex < menuOffset) menuOffset = menuIndex;
    if (menuIndex >= menuOffset + maxVisible) menuOffset = menuIndex - maxVisible + 1;
    for (uint8_t i = 0; i < maxVisible && (menuOffset + i) < total; i++) {
      uint8_t idx = menuOffset + i;
      uint8_t y = 12 + i * 10;
      if (idx == menuIndex) { oled->drawBox(0, y, 128, 10); oled->setDrawColor(0); }
      oled->setCursor(2, y + 8);
      const char* s = wifiScanSsid(idx);
      oled->print(s);
      int rssi = wifiScanRssi(idx);
      uint8_t bars = (rssi <= -120) ? 0 : rssiToBars(rssi);
      drawBars(128 - 4*(2+2), y, bars);
      if (idx == menuIndex) oled->setDrawColor(1);
    }
    oled->setCursor(0, 58);
    oled->print("Press=Rescan Long=Back");
  }
}

// ── Value editor ──
void drawValueEditor() {
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "EDIT: %s", editLabel ? editLabel : "");
  drawHeader(hdr);

  // Show named label or numeric value
  if (editValueNames && editValue >= editMin && editValue <= editMax) {
    oled->setFont(u8g2_font_10x20_tf);
    const char* name = editValueNames[editValue - editMin];
    uint16_t w = oled->getStrWidth(name);
    oled->setCursor((128 - w) / 2, 40);
    oled->print(name);
  } else {
    oled->setFont(u8g2_font_10x20_tf);
    String valStr = String(editValue);
    uint16_t w = oled->getStrWidth(valStr.c_str());
    oled->setCursor((128 - w) / 2, 40);
    oled->print(valStr);
  }

  // Special hint for WiFi toggle when no credentials present
  if (editLabel && strcmp(editLabel, "WiFi") == 0 && !wifiHasCredentials()) {
    oled->setFont(u8g2_font_6x10_tf);
    oled->setCursor(0, 48);
    oled->print("No WiFi creds. Add wifi_env.h");
  }

  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 58);
  oled->print("Rotate=Adj Press=Save");
}
