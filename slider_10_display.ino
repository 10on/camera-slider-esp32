// slider_10_display.ino — OLED display rendering

void displayUpdate() {
  if (!oledFound) return;

  oled->clearDisplay();
  oled->setTextColor(SSD1306_WHITE);

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
    case SCREEN_VALUE_EDIT:  drawValueEditor(); break;
  }

  oled->display();
}

// ── Main screen ──
void drawMainScreen() {
  // Line 1: State + BLE
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print(stateToString(sliderState));

  // BLE indicator (top right)
  oled->setCursor(100, 0);
  oled->print(bleConnected ? "BLE" : "---");

  // Line 2: Error (if any)
  if (sliderState == STATE_ERROR) {
    oled->setCursor(0, 10);
    oled->print("! ");
    oled->print(errorToString(errorCode));
  }

  // Line 3-4: Position (large)
  oled->setTextSize(2);
  oled->setCursor(0, 20);
  if (isCalibrated) {
    // Show percentage
    int pct = travelDistance > 0 ? (int)((int64_t)currentPosition * 100 / travelDistance) : 0;
    oled->print(pct);
    oled->print("%");
  } else {
    oled->print(currentPosition);
  }

  // Line 5: Speed
  oled->setTextSize(1);
  oled->setCursor(0, 44);
  oled->print("Spd:");
  oled->print(cfg.speed);
  oled->print("us");

  // Direction indicator
  oled->setCursor(80, 44);
  if (motorRunning) {
    oled->print(motorDirection ? "<< REV" : ">> FWD");
  }

  // Line 6: Endstops
  oled->setCursor(0, 56);
  oled->print("E1:");
  oled->print(endstop1 ? "ON " : "off");
  oled->print(" E2:");
  oled->print(endstop2 ? "ON" : "off");

  // Calibrated indicator
  if (isCalibrated) {
    oled->setCursor(100, 56);
    oled->print("CAL");
  }
}

// ── Generic list menu drawer ──
void drawListMenu(const char* title, const char* const* items, uint8_t count) {
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print(title);
  oled->drawLine(0, 9, 127, 9, SSD1306_WHITE);

  uint8_t maxVisible = 5;
  // Adjust scroll offset
  if (menuIndex < menuOffset) menuOffset = menuIndex;
  if (menuIndex >= menuOffset + maxVisible) menuOffset = menuIndex - maxVisible + 1;

  for (uint8_t i = 0; i < maxVisible && (menuOffset + i) < count; i++) {
    uint8_t idx = menuOffset + i;
    uint8_t y = 12 + i * 10;

    if (idx == menuIndex) {
      oled->fillRect(0, y, 128, 10, SSD1306_WHITE);
      oled->setTextColor(SSD1306_BLACK);
    }

    oled->setCursor(2, y + 1);
    oled->print(items[idx]);

    if (idx == menuIndex) {
      oled->setTextColor(SSD1306_WHITE);
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
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print("MANUAL MOVE");
  oled->drawLine(0, 9, 127, 9, SSD1306_WHITE);

  oled->setCursor(0, 14);
  oled->print("Dir: ");
  oled->print(motorDirection ? "Backward" : "Forward");

  oled->setCursor(0, 26);
  oled->print("Speed: ");
  oled->print(cfg.speed);
  oled->print(" us");

  oled->setTextSize(2);
  oled->setCursor(0, 40);
  if (sliderState == STATE_MANUAL_MOVING) {
    oled->print("RUNNING");
  } else {
    oled->print("STOPPED");
  }

  oled->setTextSize(1);
  oled->setCursor(0, 57);
  oled->print("Press=Start/Stop Long=Dir");
}

// ── Go to position screen ──
void drawGoToPosScreen() {
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print("GO TO POSITION");
  oled->drawLine(0, 9, 127, 9, SSD1306_WHITE);

  if (!isCalibrated) {
    oled->setCursor(10, 30);
    oled->print("Not calibrated!");
    oled->setCursor(10, 42);
    oled->print("Run homing first");
    return;
  }

  oled->setCursor(0, 14);
  oled->print("Current: ");
  oled->print(currentPosition);

  oled->setCursor(0, 26);
  oled->print("Target: ");
  oled->print(cmdTargetPos);

  oled->setCursor(0, 38);
  oled->print("Travel: ");
  oled->print(travelDistance);

  oled->setTextSize(1);
  oled->setCursor(0, 52);
  oled->print("Rotate=Target Press=Go");
}

// ── Calibration screen ──
void drawCalibrationScreen() {
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print("CALIBRATION");
  oled->drawLine(0, 9, 127, 9, SSD1306_WHITE);

  if (sliderState == STATE_HOMING) {
    oled->setCursor(0, 20);
    switch (homingPhase) {
      case HOME_SEEK_END1: oled->print("Seeking End 1..."); break;
      case HOME_BACKOFF1:  oled->print("Backing off 1..."); break;
      case HOME_SEEK_END2: oled->print("Seeking End 2..."); break;
      case HOME_BACKOFF2:  oled->print("Backing off 2..."); break;
      case HOME_GO_CENTER: oled->print("Going to center..."); break;
      default: oled->print("Homing..."); break;
    }

    // Progress bar
    oled->drawRect(0, 40, 128, 10, SSD1306_WHITE);
    uint8_t progress = 0;
    switch (homingPhase) {
      case HOME_SEEK_END1: progress = 20; break;
      case HOME_BACKOFF1:  progress = 40; break;
      case HOME_SEEK_END2: progress = 60; break;
      case HOME_BACKOFF2:  progress = 80; break;
      case HOME_GO_CENTER: progress = 100; break;
      default: break;
    }
    oled->fillRect(2, 42, (124 * progress) / 100, 6, SSD1306_WHITE);
  } else {
    oled->setCursor(0, 14);
    oled->print("Status: ");
    oled->print(isCalibrated ? "Calibrated" : "Not calibrated");

    if (isCalibrated) {
      oled->setCursor(0, 26);
      oled->print("Travel: ");
      oled->print(travelDistance);

      oled->setCursor(0, 38);
      oled->print("Center: ");
      oled->print(centerPosition);
    }

    oled->setCursor(0, 52);
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
  "Back"
};
#define SYSTEM_COUNT 4

void drawSystemSettings() {
  drawListMenu("SYSTEM", systemItems, SYSTEM_COUNT);
}

// ── Value editor ──
void drawValueEditor() {
  oled->setTextSize(1);
  oled->setCursor(0, 0);
  oled->print("EDIT: ");
  oled->print(editLabel);
  oled->drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Show named label or numeric value
  if (editValueNames && editValue >= editMin && editValue <= editMax) {
    oled->setTextSize(2);
    const char* name = editValueNames[editValue - editMin];
    int16_t w = strlen(name) * 12;
    oled->setCursor((128 - w) / 2, 26);
    oled->print(name);
  } else {
    oled->setTextSize(3);
    String valStr = String(editValue);
    int16_t w = valStr.length() * 18;
    oled->setCursor((128 - w) / 2, 22);
    oled->print(editValue);
  }

  oled->setTextSize(1);
  oled->setCursor(0, 57);
  oled->print("Rotate=Adj Press=Save");
}
