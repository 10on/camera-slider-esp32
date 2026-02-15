// slider_10_display.ino — OLED display rendering (u8g2)

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
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print(title);
  oled->drawLine(0, 10, 127, 10);

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
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print("MANUAL MOVE");
  oled->drawLine(0, 10, 127, 10);

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
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print("GO TO POSITION");
  oled->drawLine(0, 10, 127, 10);

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
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print("CALIBRATION");
  oled->drawLine(0, 10, 127, 10);

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
static const char* settingsItems[] = {
  "Motion",
  "Sleep",
  "System",
  "Back"
};
#define SETTINGS_COUNT 4

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
  "WiFi",
  "WiFi OTA",
  "Back"
};
#define SYSTEM_COUNT 6

void drawSystemSettings() {
  drawListMenu("SYSTEM", systemItems, SYSTEM_COUNT);
}

// ── WiFi OTA info screen ──
void drawWifiOtaScreen() {
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print("WIFI OTA");
  oled->drawLine(0, 10, 127, 10);

  oled->setCursor(0, 22);
  oled->print("SSID: ");
  // SSID not stored here; instruct user to see web root
  oled->print("see Web");

  oled->setCursor(0, 34);
  oled->print("IP: ");
  oled->print(wifiGetIpStr());

  oled->setCursor(0, 48);
  oled->print("Open /update");

  oled->setCursor(0, 58);
  oled->print("Long=Back");
}

// ── Value editor ──
void drawValueEditor() {
  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 8);
  oled->print("EDIT: ");
  oled->print(editLabel);
  oled->drawLine(0, 10, 127, 10);

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

  oled->setFont(u8g2_font_6x10_tf);
  oled->setCursor(0, 58);
  oled->print("Rotate=Adj Press=Save");
}
