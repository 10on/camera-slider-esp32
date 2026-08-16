// globals.h — shared enums, Config schema, and cross-module state.
//
// Plain externs/globals, not classes: several of these (currentPosition, motorRunning,
// stepInterval, rampStepsLeft, endstop flags) are written from onStepTimer() ISR context
// and read from loop()/BLE-callback context and must stay `volatile`. There is exactly
// one motor/one state machine/one config on this device, so a global-extern pattern is
// the direct translation of the old slider.ino globals section.
#pragma once

#include <Arduino.h>
#include <TMCStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <INA226.h>

// ── State machine ──
enum SliderState {
  STATE_IDLE,
  STATE_MANUAL_MOVING,
  STATE_MOVING_TO_POS,
  STATE_HOMING,
  STATE_PARKING,
  STATE_ERROR,
  STATE_SLEEP
};

enum ErrorCode {
  ERR_NONE = 0,
  ERR_BLE_LOST,
  ERR_HOMING_FAIL,
  ERR_ENDSTOP_UNEXPECTED   // reserved; no I2C bus to fail on this hardware, kept for BLE Status compatibility
};

enum HomingPhase {
  HOME_IDLE,
  HOME_SEEK_END1,
  HOME_BACKOFF1,
  HOME_SEEK_END2,
  HOME_BACKOFF2,
  HOME_GO_CENTER,
  HOME_DONE
};

enum EndstopMode {
  ENDSTOP_STOP   = 0,
  ENDSTOP_BOUNCE = 1,
  ENDSTOP_PARK   = 2
};

// Not named WIFI_MODE_* -- that collides with ESP-IDF's own wifi_mode_t.
enum WifiCfgMode {
  WIFI_CFG_OFF = 0,
  WIFI_CFG_STA = 1,
  WIFI_CFG_AP  = 2
};

enum UiTheme {
  THEME_DARK          = 0,
  THEME_LIGHT         = 1,
  THEME_HIGH_CONTRAST = 2
};

// ── LED patterns ──
enum LedPattern {
  LED_OFF,
  LED_ON,
  LED_SLOW_BLINK,   // BLE not connected
  LED_FAST_BLINK,   // ERROR
  LED_PULSE         // unused by ledUpdate() today, kept for parity with old enum
};

// ── Screens (graphical UI, 3-input model: encoder + BTN1 + BTN2) ──
enum MenuScreen {
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_MANUAL_MOVE,
  SCREEN_GO_TO_POS,
  SCREEN_CALIBRATION,       // also renders the active homing phases (old SCREEN_CALIBRATION behavior)
  SCREEN_SETTINGS,
  SCREEN_MOTION_SETTINGS,
  SCREEN_SLEEP_SETTINGS,
  SCREEN_SYSTEM_SETTINGS,
  SCREEN_VALUE_EDIT,
  SCREEN_ERROR,              // new: dedicated error screen (old drew error as an overlay)
  SCREEN_HOMING_CONFIRM,     // new: confirm dialog for BTN2-long homing shortcut
  SCREEN_WIRELESS_SETTINGS,  // new: Bluetooth on/off + WiFi status/entry
  SCREEN_WIFI_MODE,          // new: Off / Connect to Network / Create Hotspot
  SCREEN_WIFI_SCAN,          // new: scanned SSID list + Rescan
  SCREEN_TEXT_EDIT,          // new: generic character-wheel text entry (STA/AP password)
  SCREEN_WIFI_CONNECTING,    // new: connecting/failed status while joining a network
  SCREEN_DIAGNOSTICS,        // raw input/sensor readout, like the selftest sketch's screen
  SCREEN_MOTOR_TEST          // hold-to-jog motor bench test
};

// ── Config (persisted in NVS, namespace "slider") ──
// Schema changes vs old firmware: dropped wakeOnMotion (dead, never wired to a wake
// trigger) and wifiEnabled/wifiSel (WiFi/OTA out of scope for this port); motorCurrent
// default fixed to 800mA (old default of 500mA contradicted both the docs and the
// hardware-validated bring-up firmware); homingSpeed now exposed in Settings (was
// internal-only before).
struct Config {
  uint16_t speed;           // 1-100% (bigger = faster)
  uint16_t homingSpeed;     // us/step for homing
  uint16_t motorCurrent;    // mA (200..1500)
  uint8_t  microsteps;      // 1,2,4,8,16,32,64,128,256
  uint8_t  endstopMode;     // 0=STOP, 1=BOUNCE, 2=PARK
  uint16_t rampSteps;       // steps for accel/decel ramp
  uint16_t sleepTimeout;    // minutes, 0=disabled
  uint8_t  adxlSensitivity; // 0=off, 1=low, 2=mid, 3=high
  int32_t  savedHome;       // user-defined home position
  int32_t  savedTravel;
  int32_t  savedCenter;
  bool     savedCalibrated;

  bool     bleEnabled;      // takes effect on next boot (ESP.restart()), not live
  uint8_t  wifiMode;        // WifiCfgMode: OFF/STA/AP
  char     staSsid[33];
  char     staPass[65];
  char     apPass[65];      // hotspot password; AP SSID is derived from MAC, not stored

  uint8_t  theme;           // UiTheme: Dark/Light/High Contrast
  uint8_t  brightness;      // backlight, 10-100%
};

extern Config cfg;

// ── Hardware objects ──
extern HardwareSerial TMCSerial;
extern TMC2209Stepper driver;
extern Preferences preferences;

// ── Hardware presence flags ──
extern bool    adxlFound;
extern uint8_t adxlAddr;
extern bool    ina226Found;
extern INA226  ina226;

// ── BLE ──
extern BLEServer* pServer;
extern BLECharacteristic* pCommandChar;
extern BLECharacteristic* pStatusChar;
extern BLECharacteristic* pSpeedChar;
extern BLECharacteristic* pPositionChar;
extern BLECharacteristic* pCurrentChar;
extern BLECharacteristic* pConfigChar;

extern volatile bool bleConnected;
extern volatile bool bleWasConnected;

// ── BLE command flags (set from BLE callback task, read in loop) ──
extern volatile bool     cmdForward;
extern volatile bool     cmdBackward;
extern volatile bool     cmdStop;
extern volatile bool     cmdHome;
extern volatile bool     cmdGoToPos;
extern volatile int32_t  cmdTargetPos;
extern volatile bool     cmdSpeedChanged;
extern volatile uint16_t cmdNewSpeed;
extern volatile bool     cmdCurrentChanged;
extern volatile uint16_t cmdNewCurrent;

// ── Motor (volatile — shared with ISR) ──
extern volatile int32_t  currentPosition;
extern volatile bool     motorRunning;
extern volatile bool     motorDirection;  // false=forward(DIR LOW), true=backward(DIR HIGH)
extern volatile int32_t  motorTargetPos;
extern volatile bool     motorHasTarget;
extern volatile uint32_t stepInterval;    // current interval in us
extern volatile uint32_t targetInterval;  // desired interval in us
extern volatile uint32_t rampStepsLeft;
extern volatile bool     stopRequested;
extern volatile bool     stopAfterRamp;   // stop when decel ramp completes

extern hw_timer_t* stepTimer;

// ── State ──
extern SliderState sliderState;
extern ErrorCode   errorCode;
extern HomingPhase homingPhase;

// ── Calibration ──
extern int32_t travelDistance;
extern int32_t centerPosition;
extern bool    isCalibrated;

// ── Endstop state (direct GPIO, no PCF8574) ──
extern bool endstop1;  // true = triggered
extern bool endstop2;
extern bool endstop1Rising;  // edge flags, cleared each loop
extern bool endstop2Rising;

// ── Encoder + button state ──
// Encoder press has no long-press variant in the new 3-input model (only BTN2 does).
extern int32_t encoderDelta;      // accumulated rotation since last read
extern bool    encoderPressed;    // edge: encoder button short-press
extern bool    btn1Pressed;       // edge: BTN1 short-press
extern bool    btn2Pressed;       // edge: BTN2 short-press
extern bool    btn2LongPress;     // edge: BTN2 held >=800ms

// ── Power (INA226) ──
extern float busVoltage_V;
extern float current_mA;

// ── ADXL345 ──
extern float  adxlX, adxlY, adxlZ;
extern int8_t adxlMotionDir;  // -1 or +1, set by adxlCheckDrift()

// ── Display / Menu ──
extern MenuScreen    currentScreen;
extern int8_t         menuIndex;
extern int8_t         menuOffset;
extern bool            displayDirty;
extern unsigned long   lastDisplayUpdate;
extern MenuScreen      prevScreen;   // single-slot "return to" target (GoTo entry point, Homing-confirm cancel)

// Value editor state
extern const char*    editLabel;
extern int32_t         editValue;
extern int32_t         editMin;
extern int32_t         editMax;
extern int32_t         editStep;
extern void (*editCallback)(int32_t);
extern MenuScreen      editReturnScreen;
extern const char* const* editValueNames;  // optional text labels for values

// Text editor state (character-wheel entry, SCREEN_TEXT_EDIT -- STA/AP password entry)
extern char       editText[33];
extern int8_t      editTextLen;         // committed chars so far == cursor position
extern int8_t      editTextCharIdx;     // index into the charset for the pending (uncommitted) char
extern char        editTextLabel[24];
extern const char* editTextSubmitLabel; // "CONNECT" or "SAVE", shown in the hint bar
extern void (*editTextSubmit)(const char*);
extern MenuScreen  editTextCancelScreen;

// WiFi connect-attempt timing (SCREEN_WIFI_CONNECTING)
extern unsigned long wifiConnectStartMs;

// Character wheel for SCREEN_TEXT_EDIT -- shared between menu.cpp (cycles/commits) and
// display.cpp (renders the pending character), so it lives here rather than as a static
// local in either file.
extern const char TEXT_CHARSET[];
extern const int  TEXT_CHARSET_LEN;

// ── Timing ──
extern unsigned long lastBleNotify;
extern unsigned long lastActivityTime;
