// menu.cpp — screen navigation + input dispatch, restructured for the new 3-input model
// (encoder + BTN1 + BTN2) per the plan's full button-mapping table. Ported logic (value
// ranges, callbacks, editor semantics) comes from slider_11_menu.ino; navigation itself is
// new since the old single-button short/long-press scheme no longer applies.
#include <string.h>
#include <stdio.h>
#include <WiFi.h>
#include "menu.h"
#include "globals.h"
#include "config.h"
#include "motor.h"
#include "state.h"
#include "homing.h"
#include "wifi_module.h"
#include "ble.h"
#include "hw_init.h"
#include "display.h"
#include "pins.h"
#include "adxl.h"
#include "power.h"

// ── Item tables ──
// Tile labels are hard-capped at 13 chars: the 2x2 grid gives each tile 80px and the
// font is 6px/char, so anything longer runs past the tile edge ("Go to Position" at 14
// chars did exactly that).
static const char* MENU_ITEMS[]    = { "Manual Move", "Position", "Ping Pong", "Calibration", "Settings" };
static const char* SETTINGS_ITEMS[] = { "Motion", "Sleep", "System", "Wireless" };
static const char* MOTION_ITEMS[]  = { "Speed", "Ramp", "Microsteps", "Endstop Mode", "Homing Speed", "PingPong Start" };
static const char* SLEEP_ITEMS[]   = { "Sleep Timeout", "ADXL Sensitivity" };
static const char* SYSTEM_ITEMS[]  = { "Motor Current", "Reset Calibration", "Reset Error", "Theme", "Brightness", "Diagnostics", "Motor Test", "Speaker" };
static const char* WIRELESS_ITEMS[] = { "Bluetooth", "WiFi" };
static const char* WIFI_MODE_ITEMS[] = { "Off", "Connect to Network", "Create Hotspot" };

static const char* ENDSTOP_MODE_NAMES[] = { "Stop", "Bounce", "Park" };
static const char* PINGPONG_START_NAMES[] = { "Center", "Endstop 1", "Endstop 2" };
static const char* ADXL_SENS_NAMES[]    = { "Off", "Low", "Mid", "High" };
static const char* BLE_NAMES[]          = { "Off", "On" };
static const char* THEME_NAMES[]        = { "Dark", "Light", "High Contrast" };

int menuItemCount(MenuScreen screen) {
  switch (screen) {
    case SCREEN_MENU:               return 5;
    case SCREEN_SETTINGS:           return 4;
    case SCREEN_MOTION_SETTINGS:    return 6;
    case SCREEN_SLEEP_SETTINGS:     return 2;
    case SCREEN_SYSTEM_SETTINGS:    return 8;
    case SCREEN_WIRELESS_SETTINGS:  return 2;
    case SCREEN_WIFI_MODE:          return 3;
    case SCREEN_WIFI_SCAN:          return wifiScanResultCount() + 1;  // +1 = "Rescan"
    default:                        return 0;
  }
}

const char* menuItemLabel(MenuScreen screen, int idx) {
  switch (screen) {
    case SCREEN_MENU:               return MENU_ITEMS[idx];
    case SCREEN_SETTINGS:           return SETTINGS_ITEMS[idx];
    case SCREEN_MOTION_SETTINGS:    return MOTION_ITEMS[idx];
    case SCREEN_SLEEP_SETTINGS:     return SLEEP_ITEMS[idx];
    case SCREEN_SYSTEM_SETTINGS:    return SYSTEM_ITEMS[idx];
    case SCREEN_WIRELESS_SETTINGS:  return WIRELESS_ITEMS[idx];
    case SCREEN_WIFI_MODE:          return WIFI_MODE_ITEMS[idx];
    case SCREEN_WIFI_SCAN:
      return (idx < wifiScanResultCount()) ? wifiScanResultSsid(idx) : "Rescan";
    default:                        return "";
  }
}

void menuItemValueText(MenuScreen screen, int idx, char* buf, size_t bufsize) {
  buf[0] = 0;
  switch (screen) {
    case SCREEN_MOTION_SETTINGS:
      switch (idx) {
        case 0: snprintf(buf, bufsize, "%u%%", cfg.speed); break;
        case 1: snprintf(buf, bufsize, "%u", cfg.rampSteps); break;
        case 2: snprintf(buf, bufsize, "%u", cfg.microsteps); break;
        case 3: snprintf(buf, bufsize, "%s", ENDSTOP_MODE_NAMES[cfg.endstopMode]); break;
        case 4: snprintf(buf, bufsize, "%uus", cfg.homingSpeed); break;
        case 5: snprintf(buf, bufsize, "%s", PINGPONG_START_NAMES[cfg.pingPongStart]); break;
      }
      break;
    case SCREEN_SLEEP_SETTINGS:
      switch (idx) {
        case 0: cfg.sleepTimeout == 0 ? snprintf(buf, bufsize, "Off") : snprintf(buf, bufsize, "%umin", cfg.sleepTimeout); break;
        case 1: snprintf(buf, bufsize, "%s", ADXL_SENS_NAMES[cfg.adxlSensitivity]); break;
      }
      break;
    case SCREEN_SYSTEM_SETTINGS:
      switch (idx) {
        case 0: snprintf(buf, bufsize, "%umA", cfg.motorCurrent); break;
        case 3: snprintf(buf, bufsize, "%s", THEME_NAMES[cfg.theme]); break;
        case 4: snprintf(buf, bufsize, "%u%%", cfg.brightness); break;
        case 7: snprintf(buf, bufsize, "%s", cfg.speakerEnabled ? "On" : "Off"); break;
      }
      break;
    case SCREEN_WIRELESS_SETTINGS:
      if (idx == 0) snprintf(buf, bufsize, "%s", cfg.bleEnabled ? "On" : "Off");
      else if (idx == 1) wifiStatusText(buf, bufsize);
      break;
    case SCREEN_WIFI_SCAN:
      if (idx < wifiScanResultCount()) {
        snprintf(buf, bufsize, "%s%ddBm", wifiScanResultOpen(idx) ? "open " : "", wifiScanResultRssi(idx));
      }
      break;
    default:
      break;
  }
}

// ── Value-editor callbacks ──
static void onSpeedChanged(int32_t v) {
  cfg.speed = v;
  targetInterval = speedToInterval(cfg.speed);
  if (motorRunning) rampStepsLeft = 50;
  configSave();
}
static void onRampChanged(int32_t v)     { cfg.rampSteps = v; configSave(); }
static void onMicrostepsChanged(int32_t v) {
  cfg.microsteps = v;
  driver.microsteps(v);
  configResetCalibration();
  configSave();
}
static void onEndstopModeChanged(int32_t v) { cfg.endstopMode = v; configSave(); }
static void onHomingSpeedChanged(int32_t v) { cfg.homingSpeed = v; configSave(); }
static void onPingPongStartChanged(int32_t v) { cfg.pingPongStart = v; configSave(); }
static void onSleepTOChanged(int32_t v)     { cfg.sleepTimeout = v; configSave(); }
static void onAdxlSensChanged(int32_t v)    { cfg.adxlSensitivity = v; configSave(); }
static void onCurrentChanged(int32_t v) {
  cfg.motorCurrent = v;
  driver.rms_current(v);
  configSave();
}
static void onThemeChanged(int32_t v) {
  cfg.theme = v;
  displaySetTheme(cfg.theme);
  configSave();
}
static void onBrightnessChanged(int32_t v) {
  cfg.brightness = v;
  backlightSetBrightness(cfg.brightness);
  configSave();
}
static void onSpeakerToggle(int32_t v) { cfg.speakerEnabled = (v != 0); configSave(); }
static void openEditor(const char* label, int32_t value, int32_t vmin, int32_t vmax,
                        int32_t step, void (*cb)(int32_t), MenuScreen returnScreen,
                        const char* const* names = NULL, const char* unit = NULL) {
  editLabel = label;
  editValue = value;
  editMin = vmin;
  editMax = vmax;
  editStep = step;
  editCallback = cb;
  editReturnScreen = returnScreen;
  editUnit = unit;
  editValueNames = names;
  currentScreen = SCREEN_VALUE_EDIT;
  displayDirty = true;
}

static void openTextEditor(const char* label, const char* prefill, const char* submitLabel,
                            void (*submitCb)(const char*), MenuScreen cancelScreen) {
  strncpy(editTextLabel, label, sizeof(editTextLabel) - 1);
  editTextLabel[sizeof(editTextLabel) - 1] = 0;
  strncpy(editText, prefill ? prefill : "", sizeof(editText) - 1);
  editText[sizeof(editText) - 1] = 0;
  editTextLen = strlen(editText);
  editTextCharIdx = 0;
  editTextSubmitLabel = submitLabel;
  editTextSubmit = submitCb;
  editTextCancelScreen = cancelScreen;
  currentScreen = SCREEN_TEXT_EDIT;
  displayDirty = true;
}

// Remembers which scanned SSID the user picked, between SCREEN_WIFI_SCAN and the
// SCREEN_TEXT_EDIT password prompt it opens.
static char pendingSsid[33] = "";

static int32_t wrapIndex(int32_t idx, int32_t count) {
  if (count <= 0) return 0;
  idx %= count;
  if (idx < 0) idx += count;
  return idx;
}

static void goScreen(MenuScreen s, int8_t idx = 0) {
  currentScreen = s;
  menuIndex = idx;
  menuOffset = 0;
  displayDirty = true;
}

static void maybeEnterHomingConfirm(MenuScreen from) {
  if (sliderState == STATE_IDLE) {
    prevScreen = from;
    currentScreen = SCREEN_HOMING_CONFIRM;
    displayDirty = true;
  }
}

static void onBleToggle(int32_t v) {
  // Applied live, not via reboot. Rebooting was the original (over-cautious) choice, but
  // currentPosition lives only in RAM -- a restart makes the slider believe the carriage
  // is at zero wherever it actually sits, so toggling a radio would silently invalidate
  // the position until the next homing run. Not worth it for a settings toggle.
  cfg.bleEnabled = (v != 0);
  configSave();
  if (cfg.bleEnabled) bleInit(); else bleShutdown();
  displayDirty = true;
}
static void onApPasswordSubmit(const char* pass) {
  strncpy(cfg.apPass, pass, sizeof(cfg.apPass) - 1);
  cfg.apPass[sizeof(cfg.apPass) - 1] = 0;
  cfg.wifiMode = WIFI_CFG_AP;
  configSave();
  wifiApply();
  goScreen(SCREEN_WIRELESS_SETTINGS, 1);
}
static void onStaPasswordSubmit(const char* pass) {
  strncpy(cfg.staSsid, pendingSsid, sizeof(cfg.staSsid) - 1);
  cfg.staSsid[sizeof(cfg.staSsid) - 1] = 0;
  strncpy(cfg.staPass, pass, sizeof(cfg.staPass) - 1);
  cfg.staPass[sizeof(cfg.staPass) - 1] = 0;
  cfg.wifiMode = WIFI_CFG_STA;
  configSave();
  wifiApply();
  wifiConnectStartMs = millis();
  goScreen(SCREEN_WIFI_CONNECTING);
}

// ── Per-screen handlers ──

static void handleMain(int32_t delta) {
  if (delta != 0) {
    cfg.speed = constrain((int32_t)cfg.speed + delta, 1, 100);
    targetInterval = speedToInterval(cfg.speed);
    if (motorRunning) rampStepsLeft = 50;
    displayDirty = true;
  }
  if (encoderPressed) {
    prevScreen = SCREEN_MAIN;
    cmdTargetPos = currentPosition;
    goScreen(SCREEN_GO_TO_POS);
  }
  if (btn1Pressed) {
    if (motorRunning) {
      cmdStop = true;
    } else {
      if (motorDirection) cmdForward = true; else cmdBackward = true;
    }
  }
  if (btn2Pressed) goScreen(SCREEN_MENU, 0);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_MAIN);
}

static void handleMenu(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 5); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: goScreen(SCREEN_MANUAL_MOVE); break;
      case 1: prevScreen = SCREEN_MENU; cmdTargetPos = currentPosition; goScreen(SCREEN_GO_TO_POS); break;
      case 2: goScreen(SCREEN_PING_PONG); break;
      case 3: goScreen(SCREEN_CALIBRATION); break;
      case 4: goScreen(SCREEN_SETTINGS); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_MAIN);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_MENU);
}

static void handleManualMove(int32_t delta) {
  if (delta != 0) {
    cfg.speed = constrain((int32_t)cfg.speed + delta, 1, 100);
    targetInterval = speedToInterval(cfg.speed);
    if (motorRunning) rampStepsLeft = 50;
    displayDirty = true;
  }
  if (encoderPressed) {
    if (motorRunning) {
      bool reverseForward = motorDirection;  // motorDirection true=backward -> reverse is forward
      bool blocked = reverseForward ? endstop2 : endstop1;
      if (!blocked) {
        motorStopNow();
        motorStartRamp(reverseForward, speedToInterval(cfg.speed));
      }
    } else {
      motorDirection = !motorDirection;  // no navigation side effect (fixes old bug)
    }
    displayDirty = true;
  }
  if (btn1Pressed) goScreen(SCREEN_MENU, 0);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress && sliderState == STATE_IDLE) maybeEnterHomingConfirm(SCREEN_MANUAL_MOVE);
}

static void handleGoToPos(int32_t delta) {
  if (delta != 0 && isCalibrated) {
    int32_t step = travelDistance / 100;
    if (step < 1) step = 1;
    cmdTargetPos = constrain(cmdTargetPos + delta * step, 0, travelDistance);
    displayDirty = true;
  }
  if (encoderPressed && sliderState == STATE_IDLE && isCalibrated) {
    cmdGoToPos = true;
  }
  if (btn1Pressed) { currentScreen = prevScreen; displayDirty = true; }
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_GO_TO_POS);
}

// BTN1 is the start/stop toggle here (matching the same convention as the Main screen and
// Motor Test), not "back" like most other sub-screens -- the Start Position itself lives in
// Motion Settings as an ordinary list item, deliberately not editable from here (see the
// plan note: no accidental encoder-bump changes to it).
static void handlePingPong(int32_t delta) {
  if (delta != 0) {
    cfg.speed = constrain((int32_t)cfg.speed + delta, 1, 100);
    targetInterval = speedToInterval(cfg.speed);
    if (motorRunning) rampStepsLeft = 50;
    displayDirty = true;
  }
  if (btn1Pressed) {
    if (motorRunning) {
      cmdStop = true;
    } else if (sliderState == STATE_IDLE) {
      cmdPingPongStart = true;
    }
  }
  if (btn2Pressed) goScreen(SCREEN_MENU, 2);
  if (btn2LongPress && sliderState == STATE_IDLE) maybeEnterHomingConfirm(SCREEN_PING_PONG);
}

static void handleCalibration(int32_t /*delta*/) {
  bool homing = (sliderState == STATE_HOMING);
  if (encoderPressed && !homing && sliderState == STATE_IDLE) {
    homingStart();
  }
  if (btn1Pressed && !homing) goScreen(SCREEN_MENU, 3);
  if (btn2Pressed && !homing) goScreen(SCREEN_MAIN);
  if (btn2LongPress && homing) homingAbort();
}

static void handleSettings(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 4); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: goScreen(SCREEN_MOTION_SETTINGS); break;
      case 1: goScreen(SCREEN_SLEEP_SETTINGS); break;
      case 2: goScreen(SCREEN_SYSTEM_SETTINGS); break;
      case 3: goScreen(SCREEN_WIRELESS_SETTINGS); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_MENU, 4);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_SETTINGS);
}

static void handleMotionSettings(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 6); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: openEditor("Speed", cfg.speed, 1, 100, 5, onSpeedChanged, SCREEN_MOTION_SETTINGS, NULL, "%"); break;
      case 1: openEditor("Ramp Steps", cfg.rampSteps, 10, 1000, 10, onRampChanged, SCREEN_MOTION_SETTINGS); break;
      case 2: openEditor("Microsteps", cfg.microsteps, 1, 256, 0, onMicrostepsChanged, SCREEN_MOTION_SETTINGS); break;
      case 3: openEditor("Endstop Mode", cfg.endstopMode, 0, 2, 1, onEndstopModeChanged, SCREEN_MOTION_SETTINGS, ENDSTOP_MODE_NAMES); break;
      case 4: openEditor("Homing Speed", cfg.homingSpeed, 100, 2000, 50, onHomingSpeedChanged, SCREEN_MOTION_SETTINGS, NULL, "us"); break;
      case 5: openEditor("PingPong Start", cfg.pingPongStart, 0, 2, 1, onPingPongStartChanged, SCREEN_MOTION_SETTINGS, PINGPONG_START_NAMES); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_SETTINGS, 0);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_MOTION_SETTINGS);
}

static void handleSleepSettings(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 2); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: openEditor("Sleep Timeout", cfg.sleepTimeout, 0, 60, 1, onSleepTOChanged, SCREEN_SLEEP_SETTINGS, NULL, "min"); break;
      case 1: openEditor("ADXL Sens", cfg.adxlSensitivity, 0, 3, 1, onAdxlSensChanged, SCREEN_SLEEP_SETTINGS, ADXL_SENS_NAMES); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_SETTINGS, 1);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_SLEEP_SETTINGS);
}

static void handleSystemSettings(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 8); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: openEditor("Motor Current", cfg.motorCurrent, 200, 1500, 50, onCurrentChanged, SCREEN_SYSTEM_SETTINGS, NULL, "mA"); break;
      case 1: configResetCalibration(); displayDirty = true; break;
      case 2: stateResetError(); displayDirty = true; break;
      case 3: openEditor("Theme", cfg.theme, 0, 2, 1, onThemeChanged, SCREEN_SYSTEM_SETTINGS, THEME_NAMES); break;
      case 4: openEditor("Brightness", cfg.brightness, 10, 100, 5, onBrightnessChanged, SCREEN_SYSTEM_SETTINGS, NULL, "%"); break;
      case 5: goScreen(SCREEN_DIAGNOSTICS); break;
      case 6: goScreen(SCREEN_MOTOR_TEST); break;
      case 7: openEditor("Speaker", cfg.speakerEnabled ? 1 : 0, 0, 1, 1, onSpeakerToggle, SCREEN_SYSTEM_SETTINGS, BLE_NAMES); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_SETTINGS, 2);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_SYSTEM_SETTINGS);
}

static void handleWirelessSettings(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 2); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0: openEditor("Bluetooth", cfg.bleEnabled ? 1 : 0, 0, 1, 1, onBleToggle, SCREEN_WIRELESS_SETTINGS, BLE_NAMES); break;
      case 1: goScreen(SCREEN_WIFI_MODE); break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_SETTINGS, 3);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_WIRELESS_SETTINGS);
}

static void handleWifiMode(int32_t delta) {
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, 3); displayDirty = true; }
  if (encoderPressed) {
    switch (menuIndex) {
      case 0:  // Off
        cfg.wifiMode = WIFI_CFG_OFF;
        wifiApply();
        configSave();
        goScreen(SCREEN_WIRELESS_SETTINGS, 1);
        break;
      case 1:  // Connect to Network
        wifiStartScan();
        goScreen(SCREEN_WIFI_SCAN);
        break;
      case 2:  // Create Hotspot
        openTextEditor("Hotspot Password", cfg.apPass, "SAVE", onApPasswordSubmit, SCREEN_WIFI_MODE);
        break;
    }
  }
  if (btn1Pressed) goScreen(SCREEN_WIRELESS_SETTINGS, 1);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_WIFI_MODE);
}

static void handleWifiScan(int32_t delta) {
  int count = wifiScanResultCount() + 1;  // +1 = "Rescan"
  if (delta != 0) { menuIndex = wrapIndex(menuIndex + delta, count); displayDirty = true; }
  if (encoderPressed) {
    if (menuIndex >= wifiScanResultCount()) {
      wifiStartScan();  // Rescan, stay on this screen
      displayDirty = true;
    } else {
      strncpy(pendingSsid, wifiScanResultSsid(menuIndex), sizeof(pendingSsid) - 1);
      pendingSsid[sizeof(pendingSsid) - 1] = 0;
      if (wifiScanResultOpen(menuIndex)) {
        onStaPasswordSubmit("");
      } else {
        const char* prefill = (strcmp(pendingSsid, cfg.staSsid) == 0) ? cfg.staPass : "";
        openTextEditor("Password", prefill, "CONNECT", onStaPasswordSubmit, SCREEN_WIFI_SCAN);
      }
    }
  }
  if (btn1Pressed) goScreen(SCREEN_WIFI_MODE, 1);
  if (btn2Pressed) goScreen(SCREEN_MAIN);
  if (btn2LongPress) maybeEnterHomingConfirm(SCREEN_WIFI_SCAN);
}

static void handleTextEdit(int32_t delta) {
  if (delta != 0) {
    editTextCharIdx = wrapIndex(editTextCharIdx + delta, TEXT_CHARSET_LEN);
    displayDirty = true;
  }
  if (encoderPressed) {
    if (editTextLen < (int)sizeof(editText) - 1) {
      editText[editTextLen++] = TEXT_CHARSET[editTextCharIdx];
      editText[editTextLen] = 0;
      editTextCharIdx = 0;
      displayDirty = true;
    }
  }
  if (btn1Pressed) {
    if (editTextLen > 0) {
      editTextLen--;
      editText[editTextLen] = 0;
      editTextCharIdx = 0;
      displayDirty = true;
    } else {
      currentScreen = editTextCancelScreen;
      displayDirty = true;
    }
  }
  if (btn2Pressed) {
    if ((editTextLen == 0 || editTextLen >= 8) && editTextSubmit) {
      editTextSubmit(editText);
    }
  }
  // BTN2 long suppressed here -- same precedent as the int value editor.
}

static void handleWifiConnecting(int32_t /*delta*/) {
  if (WiFi.status() == WL_CONNECTED) {
    goScreen(SCREEN_WIRELESS_SETTINGS, 1);
    return;
  }
  bool failed = (millis() - wifiConnectStartMs > 15000);
  if (failed && (btn1Pressed || btn2Pressed)) {
    WiFi.disconnect();
    goScreen(SCREEN_WIFI_MODE, 1);
  }
}

// Service screen: every normal binding is suppressed so nothing can be triggered by
// accident while poking at the hardware. The only way out is holding BTN1+BTN2 together
// for 3s, read straight off the GPIOs rather than from the edge flags -- the flags only
// describe transitions, and this needs a sustained "both are down right now".
static void handleDiagnostics(int32_t /*delta*/) {
  static unsigned long bothHeldSince = 0;

  bool both = (digitalRead(BTN1) == LOW) && (digitalRead(BTN2) == LOW);
  if (!both) {
    bothHeldSince = 0;
  } else {
    if (bothHeldSince == 0) bothHeldSince = millis();
    else if (millis() - bothHeldSince >= 3000) {
      bothHeldSince = 0;
      goScreen(SCREEN_SYSTEM_SETTINGS, 5);
    }
  }

  // Keep the sensor rows live -- but throttled to the display's own refresh rate. These are
  // blocking I2C transactions and menuHandleInput() runs on every loop() iteration, so
  // polling them unthrottled hammered the bus hundreds of times a second and starved the
  // WiFi task badly enough to make the device miss OTA handshakes.
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead >= 100) {
    lastSensorRead = millis();
    adxlReadAxes();
    if (!motorRunning) powerRead();
  }
}

// Motor bench test: latched start/stop, not hold-to-jog -- holding a button down would
// stop you turning the encoder to change speed at the same time. A short tap toggles:
// BTN1 = backward, BTN2 = forward, tapping the same button again stops, tapping the other
// direction reverses, and the encoder press is a dedicated stop.
//
// Runs the motor without entering STATE_MANUAL_MOVING, so the normal state machine stays
// out of the way; the flip side is that state.cpp's endstop handling doesn't apply here
// either, so this checks the endstops itself before and during a move.
static void handleMotorTest(int32_t delta) {
  static unsigned long bothHeldSince = 0;
  static int8_t running = 0;  // 0 = stopped, +1 = forward, -1 = backward

  auto stopJog = [&]() {
    motorStopNow();
    digitalWrite(MOTOR_EN, HIGH);  // release holding torque when idle
    running = 0;
    displayDirty = true;
  };
  auto startJog = [&](int8_t dir) {
    motorStopNow();
    digitalWrite(MOTOR_EN, LOW);
    driver.rms_current(cfg.motorCurrent);
    motorStartRamp(dir > 0, speedToInterval(cfg.speed));
    running = dir;
    displayDirty = true;
  };

  if (delta != 0) {
    cfg.speed = constrain((int32_t)cfg.speed + delta, 1, 100);
    targetInterval = speedToInterval(cfg.speed);
    if (motorRunning) rampStepsLeft = 50;  // ease into the new speed mid-run
    displayDirty = true;
  }

  // Exit combo: both buttons held together for 3s. Checked on the raw pins because this
  // needs "both are down right now", which the edge flags don't express.
  if (digitalRead(BTN1) == LOW && digitalRead(BTN2) == LOW) {
    if (running) stopJog();
    if (bothHeldSince == 0) bothHeldSince = millis();
    else if (millis() - bothHeldSince >= 3000) {
      bothHeldSince = 0;
      goScreen(SCREEN_SYSTEM_SETTINGS, 6);
    }
    return;
  }
  bothHeldSince = 0;

  int8_t want = running;
  if (btn1Pressed) want = (running == -1) ? 0 : -1;   // tap again to stop
  if (btn2Pressed) want = (running == +1) ? 0 : +1;
  if (encoderPressed) want = 0;                        // dedicated stop

  // Never drive into an endstop that is already triggered...
  if ((want > 0 && endstop2) || (want < 0 && endstop1)) want = 0;
  // ...and bail out the moment one is hit while running.
  if ((running > 0 && endstop2) || (running < 0 && endstop1)) { stopJog(); return; }

  if (want != running) {
    if (want == 0) stopJog(); else startJog(want);
  } else if (running != 0 && !motorRunning) {
    startJog(running);  // the accel ramp finished; keep it turning
  }
}

static void handleValueEdit(int32_t delta) {
  if (delta != 0) {
    if (editStep == 0) {
      // Microsteps: one power-of-2 step per detent, not a linear delta.
      int32_t steps = delta > 0 ? delta : -delta;
      bool up = delta > 0;
      for (int32_t i = 0; i < steps; i++) {
        if (up) editValue = (editValue < editMax) ? editValue * 2 : editMax;
        else    editValue = (editValue > editMin) ? editValue / 2 : editMin;
        if (editValue < 1) editValue = 1;
      }
    } else {
      editValue = constrain(editValue + delta * editStep, editMin, editMax);
    }
    displayDirty = true;
  }
  if (encoderPressed) {
    if (editCallback) editCallback(editValue);
    currentScreen = editReturnScreen;
    displayDirty = true;
  }
  if (btn1Pressed) {
    currentScreen = editReturnScreen;  // cancel, no save
    displayDirty = true;
  }
  // BTN2 suppressed here -- no silent discard mid-edit.
}

static void handleError() {
  if (btn1Pressed) { stateResetError(); goScreen(SCREEN_MAIN); }
  if (btn2Pressed) { stateResetError(); goScreen(SCREEN_MAIN); }
  if (btn2LongPress) {
    stateResetError();
    maybeEnterHomingConfirm(SCREEN_MAIN);
  }
}

static void handleHomingConfirm() {
  if (encoderPressed) {
    homingStart();
    currentScreen = SCREEN_CALIBRATION;
    displayDirty = true;
  }
  if (btn1Pressed || btn2Pressed) {
    currentScreen = prevScreen;
    displayDirty = true;
  }
  // btn2LongPress: no-op, already mid-confirm.
}

void menuInit() {
  currentScreen = SCREEN_MAIN;
  menuIndex = 0;
  menuOffset = 0;
  displayDirty = true;
}

void menuHandleInput() {
  if (sliderState == STATE_SLEEP) return;  // sleep.cpp owns wake; nothing to render (backlight off)

  // Auto-navigate to the error screen whenever an error occurs, regardless of prior screen.
  if (sliderState == STATE_ERROR && currentScreen != SCREEN_ERROR) {
    currentScreen = SCREEN_ERROR;
    displayDirty = true;
  }

  int32_t delta = encoderDelta;
  encoderDelta = 0;

  switch (currentScreen) {
    case SCREEN_MAIN:            handleMain(delta); break;
    case SCREEN_MENU:            handleMenu(delta); break;
    case SCREEN_MANUAL_MOVE:     handleManualMove(delta); break;
    case SCREEN_GO_TO_POS:       handleGoToPos(delta); break;
    case SCREEN_CALIBRATION:     handleCalibration(delta); break;
    case SCREEN_PING_PONG:       handlePingPong(delta); break;
    case SCREEN_SETTINGS:        handleSettings(delta); break;
    case SCREEN_MOTION_SETTINGS: handleMotionSettings(delta); break;
    case SCREEN_SLEEP_SETTINGS:  handleSleepSettings(delta); break;
    case SCREEN_SYSTEM_SETTINGS: handleSystemSettings(delta); break;
    case SCREEN_VALUE_EDIT:      handleValueEdit(delta); break;
    case SCREEN_ERROR:           handleError(); break;
    case SCREEN_HOMING_CONFIRM:  handleHomingConfirm(); break;
    case SCREEN_WIRELESS_SETTINGS: handleWirelessSettings(delta); break;
    case SCREEN_WIFI_MODE:       handleWifiMode(delta); break;
    case SCREEN_WIFI_SCAN:       handleWifiScan(delta); break;
    case SCREEN_TEXT_EDIT:       handleTextEdit(delta); break;
    case SCREEN_WIFI_CONNECTING: handleWifiConnecting(delta); break;
    case SCREEN_DIAGNOSTICS:     handleDiagnostics(delta); break;
    case SCREEN_MOTOR_TEST:      handleMotorTest(delta); break;
  }

  encoderPressed = false;
  btn1Pressed = false;
  btn2Pressed = false;
  btn2LongPress = false;
}
