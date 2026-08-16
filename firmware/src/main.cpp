// main.cpp — setup()/loop(), sequences every module's init()/update().
// Loop order mirrors old slider.ino's loop() (input poll, sleep-wake check, state update,
// homing update, throttled power read, throttled BLE notify, LED update, display update,
// menu input, sleep-timeout check) adapted for direct-GPIO input (inputPoll() replaces the
// old pcfPoll()+endstopsPoll()+encoderPoll() trio) and INA226 power (powerRead() replaces
// vbatReadPrecise()).
#include "globals.h"
#include "pins.h"
#include "hw_init.h"
#include "motor.h"
#include "config.h"
#include "state.h"
#include "homing.h"
#include "power.h"
#include "input.h"
#include "adxl.h"
#include "ble.h"
#include "led.h"
#include "sleep.h"
#include "display.h"
#include "menu.h"
#include "wifi_module.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  configLoad();
  hwInit();
  motorInit();
  inputInit();
  adxlInit();
  powerInit();
  if (cfg.bleEnabled) bleInit();
  wifiInit();
  displayInit();
  menuInit();

  lastActivityTime = millis();
}

void loop() {
  // 1-3. Input: endstops (edge-detected), encoder rotation + button, BTN1/BTN2.
  inputPoll();

  // 3b. Sleep wake check (must run before menuHandleInput() consumes the same press flags).
  sleepCheckWake();

  // 4. State machine transitions (also drains BLE command flags).
  stateUpdate();

  // 5. Homing sub-automaton.
  if (sliderState == STATE_HOMING) {
    homingUpdate();
  }

  // 6. Power reading (every 5s, only when motor idle -- matches old vbatReadPrecise() timing).
  static unsigned long lastPowerRead = 0;
  if (!motorRunning && millis() - lastPowerRead > 5000) {
    powerRead();
    lastPowerRead = millis();
  }

  // 7. BLE status notify (every 100ms).
  if (bleConnected && millis() - lastBleNotify > 100) {
    bleStatusNotify();
    lastBleNotify = millis();
  }

  // 8. LED pattern update.
  ledUpdate();

  // 9. Display update (self-throttled ~100ms internally, incl. position-bar lerp).
  displayUpdate();

  // 10. Menu input handling.
  menuHandleInput();

  // 11. Sleep timeout check.
  sleepCheck();

  // 12. WiFi/OTA (best-effort; no-op if no credentials configured).
  wifiLoop();
}
