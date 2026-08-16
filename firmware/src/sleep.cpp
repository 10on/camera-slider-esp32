// sleep.cpp — sleep/wake + drift-based safe parking, ported from slider_12_sleep.ino.
// The old busy-wait park loop polled the PCF8574 (pcfPoll()/endstopsPoll()); this hardware
// reads endstops directly via input.cpp's inputPoll(), which also services the encoder and
// buttons during the same blocking wait. OLED setPowerSave() is replaced with the TFT
// backlight on/off helpers in hw_init.cpp. LED behavior during sleep no longer needs manual
// PCF writes -- led.cpp's ledUpdate() already turns LED_STATUS off automatically for
// STATE_SLEEP every loop, so sleepEnter() doesn't touch LEDs directly. Wake triggers are
// extended to BTN1/BTN2 (old only woke on encoder activity or BLE connect).
#include "sleep.h"
#include "pins.h"
#include "globals.h"
#include "motor.h"
#include "adxl.h"
#include "input.h"
#include "hw_init.h"

void sleepCheck() {
  if (cfg.sleepTimeout == 0) return;  // disabled
  if (sliderState != STATE_IDLE) return;

  unsigned long timeout = (unsigned long)cfg.sleepTimeout * 60000UL;
  if (millis() - lastActivityTime > timeout) {
    sleepParkAndEnter();
  }
}

// Try to safely park before sleeping.
// Blocking sequence: release motor -> check ADXL -> park if drifting -> retry once.
void sleepParkAndEnter() {
  motorStopNow();
  digitalWrite(MOTOR_EN, HIGH);  // disable holding
  delay(100);                    // let mechanics settle

  if (adxlCheckDrift(5000)) {
    digitalWrite(MOTOR_EN, LOW);

    bool forward = (adxlMotionDir > 0);
    motorStartRamp(forward, cfg.homingSpeed);
    sliderState = STATE_PARKING;
    displayDirty = true;

    while (motorRunning) {
      inputPoll();
      if (endstop1Rising || endstop2Rising) {
        motorStopNow();
      }
      delay(1);
    }

    sliderState = STATE_IDLE;

    // Attempt 2: release again, check the other direction
    digitalWrite(MOTOR_EN, HIGH);
    delay(100);

    if (adxlCheckDrift(5000)) {
      digitalWrite(MOTOR_EN, LOW);

      bool forward2 = (adxlMotionDir > 0);
      motorStartRamp(forward2, cfg.homingSpeed);
      sliderState = STATE_PARKING;

      while (motorRunning) {
        inputPoll();
        if (endstop1Rising || endstop2Rising) {
          motorStopNow();
        }
        delay(1);
      }

      sliderState = STATE_IDLE;
      digitalWrite(MOTOR_EN, HIGH);
    }
  }

  // Motor is off, safe (or best effort) -- enter sleep
  sleepEnter();
}

void sleepEnter() {
  sliderState = STATE_SLEEP;
  motorStopNow();
  digitalWrite(MOTOR_EN, HIGH);

  backlightOff();

  displayDirty = false;
}

void sleepWake() {
  if (sliderState != STATE_SLEEP) return;

  sliderState = STATE_IDLE;
  lastActivityTime = millis();

  backlightOn();

  // Engage motor holding after wake: enable driver at a reduced hold current.
  digitalWrite(MOTOR_EN, LOW);
  {
    uint16_t hold = (uint16_t)((cfg.motorCurrent * 30) / 100);  // ~30% hold
    if (hold < 200) hold = 200;  // safe floor
    driver.rms_current(hold);
  }

  currentScreen = SCREEN_MAIN;
  displayDirty = true;
}

// Check for wake triggers (called every loop). Extended vs old firmware: BTN1/BTN2 also
// wake now (old only woke on encoder activity or BLE connect).
void sleepCheckWake() {
  if (sliderState != STATE_SLEEP) return;

  if (encoderDelta != 0 || encoderPressed || btn1Pressed || btn2Pressed || btn2LongPress) {
    sleepWake();
    encoderDelta = 0;
    encoderPressed = false;
    btn1Pressed = false;
    btn2Pressed = false;
    btn2LongPress = false;
    return;
  }

  if (bleConnected) {
    sleepWake();
    return;
  }
}
