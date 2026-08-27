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
#include "buzzer.h"
#include "sleep.h"
#include "display.h"
#include "menu.h"
#include "wifi_module.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  configLoad();
  hwInit();
  ledInit();
  buzzerInit();
  motorInit();
  inputInit();
  adxlInit();
  powerInit();
  powerRead();  // get a real busVoltage_V before the first render -- otherwise the battery
                // icon reads the default 0 (empty/red) for up to a minute, until the
                // throttled read in loop() below fires for the first time.
  if (cfg.bleEnabled) bleInit();
  wifiInit();
  displayInit();
  menuInit();
  buzzerBootChime();

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

  // 6. Power reading -- for the on-screen battery/charging indicator, which doesn't need
  // to track voltage tightly. Once a minute, only when motor idle.
  static unsigned long lastPowerRead = 0;
  if (!motorRunning && millis() - lastPowerRead > 60000) {
    powerRead();
    lastPowerRead = millis();
    // The battery/charge icon in the header only redraws on a screen change or a live
    // animation, neither of which "voltage changed in the background" is -- without this,
    // a stale reading (e.g. the empty/red icon before the very first read landed) could
    // sit on screen indefinitely even after busVoltage_V catches up. displayDirty alone
    // isn't enough: drawScreenHeader()/drawStatusBar() only draw when fullRepaint is set.
    displayForceRepaint();
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
