// slider_11_menu.ino — Menu navigation, encoder handling, value editors

// Helper: open value editor (with optional text labels for values)
void openValueEditor(const char* label, int32_t value, int32_t mn, int32_t mx,
                     int32_t step, void (*cb)(int32_t), MenuScreen returnTo,
                     const char* const* names = NULL) {
  editLabel = label;
  editValue = value;
  editMin = mn;
  editMax = mx;
  editStep = step;
  editCallback = cb;
  editReturnScreen = returnTo;
  editValueNames = names;
  currentScreen = SCREEN_VALUE_EDIT;
  displayDirty = true;
}

// ── Callbacks for value editors ──
void onSpeedChanged(int32_t v)    { cfg.speed = v; targetInterval = v; configSave(); }
void onRampChanged(int32_t v)     { cfg.rampSteps = v; configSave(); }
void onCurrentChanged(int32_t v)  { cfg.motorCurrent = v; driver.rms_current(v); configSave(); }
void onSleepTOChanged(int32_t v)  { cfg.sleepTimeout = v; configSave(); }
void onAdxlSensChanged(int32_t v) { cfg.adxlSensitivity = v; configSave(); }

void onMicrostepsChanged(int32_t v) {
  cfg.microsteps = v;
  driver.microsteps(v);
  configResetCalibration();  // microstep change invalidates calibration
  configSave();
}

void onEndstopModeChanged(int32_t v) {
  cfg.endstopMode = v;
  configSave();
}

void onWakeOnMotionChanged(int32_t v) {
  cfg.wakeOnMotion = v;
  configSave();
}

// ── Named value labels ──
static const char* endstopModeNames[] = { "Stop", "Bounce", "Park" };
static const char* adxlSensNames[]    = { "Off", "Low", "Mid", "High" };
static const char* onOffNames[]       = { "Off", "On" };

// ── Menu input router ──
void menuHandleEncoder() {
  // Consume encoder delta and button events
  int8_t delta = encoderDelta;
  encoderDelta = 0;
  bool pressed = encoderPressed;
  encoderPressed = false;
  bool longPress = encoderLongPress;
  encoderLongPress = false;

  if (delta == 0 && !pressed && !longPress) return;

  displayDirty = true;

  switch (currentScreen) {
    case SCREEN_MAIN:        handleMainScreen(delta, pressed, longPress); break;
    case SCREEN_MENU:        handleMenuNav(delta, pressed, longPress); break;
    case SCREEN_MANUAL_MOVE: handleManualMove(delta, pressed, longPress); break;
    case SCREEN_GO_TO_POS:   handleGoToPos(delta, pressed, longPress); break;
    case SCREEN_CALIBRATION: handleCalibration(delta, pressed, longPress); break;
    case SCREEN_SETTINGS:    handleSettingsNav(delta, pressed, longPress); break;
    case SCREEN_MOTION_SETTINGS: handleMotionNav(delta, pressed, longPress); break;
    case SCREEN_SLEEP_SETTINGS:  handleSleepNav(delta, pressed, longPress); break;
    case SCREEN_SYSTEM_SETTINGS: handleSystemNav(delta, pressed, longPress); break;
    case SCREEN_VALUE_EDIT:  handleValueEdit(delta, pressed, longPress); break;
  }
}

// ── Main screen ──
void handleMainScreen(int8_t delta, bool pressed, bool longPress) {
  // Rotation adjusts speed
  if (delta != 0) {
    int32_t newSpeed = (int32_t)cfg.speed - delta * 50;
    cfg.speed = constrain(newSpeed, 100, 5000);
    targetInterval = cfg.speed;
    if (motorRunning) rampStepsLeft = 50;
  }
  // Press opens menu
  if (pressed) {
    currentScreen = SCREEN_MENU;
    menuIndex = 0;
    menuOffset = 0;
  }
}

// ── Main menu navigation ──
void handleMenuNav(int8_t delta, bool pressed, bool longPress) {
  menuIndex = constrain(menuIndex + delta, 0, MAIN_MENU_COUNT - 1);

  if (pressed) {
    switch (menuIndex) {
      case 0: currentScreen = SCREEN_MANUAL_MOVE; break;
      case 1: currentScreen = SCREEN_GO_TO_POS; cmdTargetPos = currentPosition; break;
      case 2: currentScreen = SCREEN_CALIBRATION; break;
      case 3: currentScreen = SCREEN_SETTINGS; menuIndex = 0; menuOffset = 0; break;
      case 4: currentScreen = SCREEN_MAIN; break;  // Back
    }
  }
  if (longPress) {
    currentScreen = SCREEN_MAIN;
  }
}

// ── Manual move ──
void handleManualMove(int8_t delta, bool pressed, bool longPress) {
  // Rotate = adjust speed
  if (delta != 0) {
    int32_t newSpeed = (int32_t)cfg.speed - delta * 50;
    cfg.speed = constrain(newSpeed, 100, 5000);
    if (motorRunning) {
      targetInterval = cfg.speed;
      rampStepsLeft = 50;
    }
  }

  // Press = start/stop toggle
  if (pressed) {
    if (sliderState == STATE_MANUAL_MOVING) {
      motorStop();
    } else if (sliderState == STATE_IDLE) {
      // Block if moving toward triggered endstop
      bool forward = !motorDirection;  // motorDirection: false=fwd, true=bwd
      bool blocked = (forward && endstop2) || (!forward && endstop1);
      if (!blocked) {
        sliderState = STATE_MANUAL_MOVING;
        digitalWrite(EN_PIN, LOW);
        motorStartRamp(forward, cfg.speed);
      }
    }
  }

  // Long press = change direction
  if (longPress) {
    if (sliderState == STATE_MANUAL_MOVING) {
      // Reverse — block if new direction hits a triggered endstop
      bool newForward = motorDirection;  // invert: current bwd→fwd, fwd→bwd
      bool blocked = (newForward && endstop2) || (!newForward && endstop1);
      if (!blocked) {
        motorStopNow();
        motorDirection = !motorDirection;
        motorStartRamp(newForward, cfg.speed);
      }
    } else {
      motorDirection = !motorDirection;
    }
  }

  // Back (double long press would need different approach, use menu for now)
  // Long press on stopped → go back
  if (longPress && sliderState != STATE_MANUAL_MOVING) {
    currentScreen = SCREEN_MENU;
    menuIndex = 0;
    menuOffset = 0;
  }
}

// ── Go to position ──
void handleGoToPos(int8_t delta, bool pressed, bool longPress) {
  if (!isCalibrated) {
    if (pressed || longPress) {
      currentScreen = SCREEN_MENU;
      menuIndex = 0;
    }
    return;
  }

  // Rotate = adjust target
  if (delta != 0) {
    int32_t step = travelDistance / 100;
    if (step < 1) step = 1;
    cmdTargetPos = constrain(cmdTargetPos + delta * step, (int32_t)0, travelDistance);
  }

  // Press = go
  if (pressed) {
    if (sliderState == STATE_IDLE) {
      cmdGoToPos = true;
    }
  }

  if (longPress) {
    currentScreen = SCREEN_MENU;
    menuIndex = 1;
    menuOffset = 0;
  }
}

// ── Calibration ──
void handleCalibration(int8_t delta, bool pressed, bool longPress) {
  if (pressed && sliderState == STATE_IDLE) {
    homingStart();
  }
  if (longPress && sliderState != STATE_HOMING) {
    currentScreen = SCREEN_MENU;
    menuIndex = 2;
    menuOffset = 0;
  }
}

// ── Settings navigation ──
void handleSettingsNav(int8_t delta, bool pressed, bool longPress) {
  menuIndex = constrain(menuIndex + delta, 0, SETTINGS_COUNT - 1);

  if (pressed) {
    switch (menuIndex) {
      case 0: currentScreen = SCREEN_MOTION_SETTINGS; menuIndex = 0; menuOffset = 0; break;
      case 1: currentScreen = SCREEN_SLEEP_SETTINGS; menuIndex = 0; menuOffset = 0; break;
      case 2: currentScreen = SCREEN_SYSTEM_SETTINGS; menuIndex = 0; menuOffset = 0; break;
      case 3: currentScreen = SCREEN_MENU; menuIndex = 3; menuOffset = 0; break;  // Back
    }
  }
  if (longPress) {
    currentScreen = SCREEN_MENU;
    menuIndex = 3;
    menuOffset = 0;
  }
}

// ── Motion settings ──
void handleMotionNav(int8_t delta, bool pressed, bool longPress) {
  menuIndex = constrain(menuIndex + delta, 0, MOTION_COUNT - 1);

  if (pressed) {
    switch (menuIndex) {
      case 0:
        openValueEditor("Speed", cfg.speed, 100, 5000, 50, onSpeedChanged, SCREEN_MOTION_SETTINGS);
        break;
      case 1:
        openValueEditor("Ramp Steps", cfg.rampSteps, 10, 1000, 10, onRampChanged, SCREEN_MOTION_SETTINGS);
        break;
      case 2:
        openValueEditor("Microsteps", cfg.microsteps, 1, 256, 0, onMicrostepsChanged, SCREEN_MOTION_SETTINGS);
        break;
      case 3:
        openValueEditor("Endstop Md", cfg.endstopMode, 0, 2, 1, onEndstopModeChanged, SCREEN_MOTION_SETTINGS, endstopModeNames);
        break;
      case 4:
        currentScreen = SCREEN_SETTINGS; menuIndex = 0; menuOffset = 0; break;
    }
  }
  if (longPress) {
    currentScreen = SCREEN_SETTINGS;
    menuIndex = 0;
    menuOffset = 0;
  }
}

// ── Sleep settings ──
void handleSleepNav(int8_t delta, bool pressed, bool longPress) {
  menuIndex = constrain(menuIndex + delta, 0, SLEEP_COUNT - 1);

  if (pressed) {
    switch (menuIndex) {
      case 0:
        openValueEditor("Sleep min", cfg.sleepTimeout, 0, 60, 1, onSleepTOChanged, SCREEN_SLEEP_SETTINGS);
        break;
      case 1:
        openValueEditor("ADXL Sens", cfg.adxlSensitivity, 0, 3, 1, onAdxlSensChanged, SCREEN_SLEEP_SETTINGS, adxlSensNames);
        break;
      case 2:
        openValueEditor("Wake Motn", cfg.wakeOnMotion ? 1 : 0, 0, 1, 1, onWakeOnMotionChanged, SCREEN_SLEEP_SETTINGS, onOffNames);
        break;
      case 3:
        currentScreen = SCREEN_SETTINGS; menuIndex = 1; menuOffset = 0; break;
    }
  }
  if (longPress) {
    currentScreen = SCREEN_SETTINGS;
    menuIndex = 1;
    menuOffset = 0;
  }
}

// ── System settings ──
void handleSystemNav(int8_t delta, bool pressed, bool longPress) {
  menuIndex = constrain(menuIndex + delta, 0, SYSTEM_COUNT - 1);

  if (pressed) {
    switch (menuIndex) {
      case 0:
        openValueEditor("Current mA", cfg.motorCurrent, 200, 1500, 50, onCurrentChanged, SCREEN_SYSTEM_SETTINGS);
        break;
      case 1:
        configResetCalibration();
        break;
      case 2:
        stateResetError();
        break;
      case 3:
        currentScreen = SCREEN_SETTINGS; menuIndex = 2; menuOffset = 0; break;
    }
  }
  if (longPress) {
    currentScreen = SCREEN_SETTINGS;
    menuIndex = 2;
    menuOffset = 0;
  }
}

// ── Value editor ──
void handleValueEdit(int8_t delta, bool pressed, bool longPress) {
  if (delta != 0) {
    if (editStep == 0) {
      // Special: microsteps — powers of 2
      if (delta > 0 && editValue < editMax) editValue *= 2;
      if (delta < 0 && editValue > editMin) editValue /= 2;
    } else {
      editValue = constrain(editValue + delta * editStep, editMin, editMax);
    }
  }

  // Press = save and return
  if (pressed) {
    if (editCallback) editCallback(editValue);
    currentScreen = editReturnScreen;
    menuIndex = 0;
    menuOffset = 0;
  }

  // Long press = cancel and return
  if (longPress) {
    currentScreen = editReturnScreen;
    menuIndex = 0;
    menuOffset = 0;
  }
}
