// Camera Slider Controller — ESP32 + TMC2209 + BLE
// Main sketch file: includes, pins, globals, setup(), loop()

#include <Wire.h>
#include <PCF8574.h>
#include <U8g2lib.h>
#include <TMCStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ── ESP32 GPIO pins ──
#define EN_PIN    0
#define STEP_PIN  1
#define DIR_PIN   2
#define VBAT_PIN  3        // ADC: motor supply voltage via resistor divider
#define VBAT_DIVIDER 5.09f // calibrated against multimeter
#define VBAT_FULL  12.6f   // 3S LiPo fully charged (4.2V × 3)
#define VBAT_EMPTY  9.0f   // 3S LiPo empty (3.0V × 3)
#define UART_TX   4
#define UART_RX   5

// ── I2C ──
#define SDA_PIN   8
#define SCL_PIN   9

// ── PCF8574 bit assignments ──
#define PCF_LED       1
#define PCF_ENC_CLK   2
#define PCF_ENC_DT    3
#define PCF_ENC_SW    4
#define PCF_LED2      7
#define PCF_ENDSTOP_1 5   // active LOW
#define PCF_ENDSTOP_2 6   // active LOW

// ── OLED ──
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// ── TMC2209 ──
#define R_SENSE     0.11f
#define DRIVER_ADDR 0b00

// ── ADXL345 registers ──
#define REG_DEVID       0x00
#define REG_BW_RATE     0x2C
#define REG_POWER_CTL   0x2D
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0      0x32

// ── BLE UUIDs ──
#define SERVICE_UUID   "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define COMMAND_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define SPEED_UUID     "d8de624e-140f-4a22-8594-e2216b84a5f2"
#define POSITION_UUID  "a1b2c3d4-e5f6-4789-a012-3456789abcde"
#define CURRENT_UUID   "f1e2d3c4-b5a6-4978-9012-3456789abcde"

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
  ERR_ENDSTOP_UNEXPECTED
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

// ── LED patterns ──
enum LedPattern {
  LED_OFF,
  LED_ON,
  LED_SLOW_BLINK,   // BLE not connected
  LED_FAST_BLINK,   // ERROR
  LED_PULSE          // moving
};

// ── Menu ──
enum MenuScreen {
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_MANUAL_MOVE,
  SCREEN_GO_TO_POS,
  SCREEN_CALIBRATION,
  SCREEN_SETTINGS,
  SCREEN_MOTION_SETTINGS,
  SCREEN_SLEEP_SETTINGS,
  SCREEN_SYSTEM_SETTINGS,
  SCREEN_WIFI_NETWORKS,
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_SCAN,
  SCREEN_WIFI_OTA,
  SCREEN_VALUE_EDIT
};

// ── Hardware objects ──
HardwareSerial TMCSerial(1);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDR);
PCF8574* pcf = NULL;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C* oled = NULL;
Preferences preferences;

// ── Hardware presence flags ──
bool pcfFound   = false;
bool oledFound  = false;
bool adxlFound  = false;
uint8_t pcfAddr  = 0;
uint8_t adxlAddr = 0;

// ── BLE ──
BLEServer* pServer = NULL;
BLECharacteristic* pCommandChar  = NULL;
BLECharacteristic* pStatusChar   = NULL;
BLECharacteristic* pSpeedChar    = NULL;
BLECharacteristic* pPositionChar = NULL;
BLECharacteristic* pCurrentChar  = NULL;

volatile bool bleConnected     = false;
volatile bool bleWasConnected  = false;

// ── BLE command flags (set from BLE callback task, read in loop) ──
volatile bool cmdForward       = false;
volatile bool cmdBackward      = false;
volatile bool cmdStop          = false;
volatile bool cmdHome          = false;
volatile bool cmdGoToPos       = false;
volatile int32_t cmdTargetPos  = 0;
volatile bool cmdSpeedChanged  = false;
volatile uint16_t cmdNewSpeed  = 0;
volatile bool cmdCurrentChanged = false;
volatile uint16_t cmdNewCurrent = 0;

// ── Motor (volatile — shared with ISR) ──
volatile int32_t currentPosition = 0;
volatile bool    motorRunning    = false;
volatile bool    motorDirection  = false;  // false=forward(DIR LOW), true=backward(DIR HIGH)
volatile int32_t motorTargetPos  = 0;
volatile bool    motorHasTarget  = false;
volatile uint32_t stepInterval   = 800;    // current interval in µs
volatile uint32_t targetInterval = 800;    // desired interval in µs
volatile uint32_t rampStepsLeft  = 0;
volatile bool    stopRequested   = false;
volatile bool    stopAfterRamp   = false;  // stop when decel ramp completes

hw_timer_t* stepTimer = NULL;

// ── State ──
SliderState  sliderState   = STATE_IDLE;
ErrorCode    errorCode     = ERR_NONE;
HomingPhase  homingPhase   = HOME_IDLE;

// ── Calibration ──
int32_t travelDistance = 0;
int32_t centerPosition = 0;
bool    isCalibrated   = false;

// ── Config (persisted in NVS) ──
struct Config {
  uint16_t speed;         // 1-100% (bigger = faster)
  uint16_t homingSpeed;   // µs/step for homing
  uint16_t motorCurrent;  // mA (200..1500)
  uint8_t  microsteps;    // 1,2,4,8,16,32,64,128,256
  uint8_t  endstopMode;   // 0=STOP, 1=BOUNCE, 2=PARK
  uint16_t rampSteps;     // steps for accel/decel ramp
  uint16_t sleepTimeout;  // minutes, 0=disabled
  uint8_t  adxlSensitivity; // 0=off, 1=low, 2=mid, 3=high
  bool     wakeOnMotion;
  bool     wifiEnabled;   // WiFi AP + Web API/OTA enabled
  int16_t  wifiSel;       // -1=Auto, otherwise index in WIFI_CREDENTIALS
  int32_t  savedHome;     // user-defined home position
  int32_t  savedTravel;
  int32_t  savedCenter;
  bool     savedCalibrated;
};

Config cfg;

// ── PCF8574 I/O ──
uint8_t pcfOutputState = 0xFD;  // all HIGH except LED bit (P1=0 → LED off)
uint8_t pcfInputState  = 0xFF;  // last read

// ── Endstop state ──
bool endstop1 = false;  // true = triggered
bool endstop2 = false;
bool endstop1Rising = false;  // edge flags, cleared each loop
bool endstop2Rising = false;

// ── Encoder state ──
uint8_t encClkPrev = HIGH;
int8_t  encoderDelta = 0;       // accumulated rotation since last read
bool    encoderPressed = false;  // edge: just pressed
bool    encoderLongPress = false;
bool    encSwState = HIGH;
bool    encSwPrev  = HIGH;
unsigned long encPressTime = 0;
bool    encLongFired = false;

// ── Battery voltage ──
float vbatVoltage = 0;
unsigned long lastVbatRead = 0;

// ── ADXL345 ──
float adxlX = 0, adxlY = 0, adxlZ = 0;
int8_t adxlMotionDir = 0;  // -1 or +1, set by adxlCheckDrift()

// ── Display / Menu ──
MenuScreen currentScreen = SCREEN_MANUAL_MOVE;
int8_t menuIndex = 0;
int8_t menuOffset = 0;
bool   displayDirty = true;
unsigned long lastDisplayUpdate = 0;

// Value editor state
const char* editLabel = NULL;
int32_t editValue = 0;
int32_t editMin = 0;
int32_t editMax = 0;
int32_t editStep = 1;
void (*editCallback)(int32_t) = NULL;
MenuScreen editReturnScreen = SCREEN_MAIN;
const char* const* editValueNames = NULL;  // optional text labels for values

// ── Timing ──
unsigned long lastBleNotify   = 0;
unsigned long lastActivityTime = 0;

// ── Backoff ──
#define BACKOFF_STEPS 200

// ── Forward declarations ──
// (Arduino IDE auto-generates prototypes, but listed for clarity)
void configLoad();
void configSave();
void hwInit();
void motorInit();
void motorStart(bool forward, uint32_t intervalUs);
void motorStop();
void motorStartRamp(bool forward, uint32_t intervalUs);
void motorMoveTo(int32_t position, uint32_t intervalUs);
void stateUpdate();
void pcfPoll();
void endstopsPoll();
void encoderPoll();
void homingUpdate();
void homingStart();
void adxlInit();
bool adxlCheckDrift(uint16_t durationMs);
void bleInit();
void bleStatusNotify();
void ledUpdate();
void ledSetPattern(LedPattern pat);
void displayUpdate();
void menuHandleEncoder();
void sleepCheck();
void sleepEnter();
void sleepWake();
// WiFi/Web API/OTA (disabled for now; keep declarations for future use)
void wifiInit();
void wifiStartIfEnabled();
void wifiStop();
void wifiLoop();
const char* wifiGetIpStr();
bool wifiHasCredentials();
// WiFi known networks (for UI)
int wifiKnownCount();
const char* wifiKnownSsid(int idx);
int wifiKnownRssi(int idx);
int wifiSelectedIndex();
void wifiRequestScan();
bool wifiGetIpPopup(char* buf, size_t len);
// Simple connect workflow for API/OTA
void wifiStartApi();
void wifiStartOta();
int wifiConnectState(); // 0=idle,1=scanning,2=connecting
// View-only scan helpers
void wifiScanStart();
int wifiScanState(); // 0=idle,1=scanning,2=done
int wifiScanCount();
const char* wifiScanSsid(int idx);
int wifiScanRssi(int idx);

void setup() {
  Serial.begin(115200);
  delay(200);

  configLoad();
  hwInit();
  motorInit();
  bleInit();
  // wifiInit(); // disabled
  // Do NOT auto-start WiFi on boot; only start from menu toggle

  lastActivityTime = millis();
}

void loop() {
  // 1. PCF8574: single I2C transaction
  pcfPoll();

  // 2. Endstop edge detection
  endstopsPoll();

  // 3. Encoder rotation + button
  encoderPoll();

  // 3b. Sleep wake check
  sleepCheckWake();

  // 4. State machine transitions
  stateUpdate();

  // 5. Homing sub-automaton
  if (sliderState == STATE_HOMING) {
    homingUpdate();
  }

  // 6. Battery voltage (every 5s, only when motor idle)
  if (!motorRunning && millis() - lastVbatRead > 5000) {
    vbatReadPrecise();
    lastVbatRead = millis();
  }

  // 7. BLE status notify (every 100ms)
  if (bleConnected && millis() - lastBleNotify > 100) {
    bleStatusNotify();
    lastBleNotify = millis();
  }

  // 8. LED pattern update
  ledUpdate();

  // 9. Display update (every 50ms if dirty)
  if (displayDirty && millis() - lastDisplayUpdate > 50) {
    displayUpdate();
    lastDisplayUpdate = millis();
    displayDirty = false;
  }

  // 10. Menu input handling
  menuHandleEncoder();

  // 11. Sleep check
  sleepCheck();

  // 12. WiFi server loop
  // wifiLoop(); // disabled
}
