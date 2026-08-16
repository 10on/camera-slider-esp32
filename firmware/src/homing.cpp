// homing.cpp — 6-phase non-blocking homing sub-automaton.
// Ported from slider_06_homing.ino, behavior preserved exactly, plus a new homingAbort()
// (old firmware had no abort path once homingStart() ran -- see plan).
#include "homing.h"
#include "pins.h"
#include "globals.h"
#include "motor.h"
#include "config.h"

static int32_t homingBackoffTarget = 0;

void homingStart() {
  sliderState = STATE_HOMING;
  homingPhase = HOME_SEEK_END1;

  digitalWrite(MOTOR_EN, LOW);
  driver.rms_current(cfg.motorCurrent);

  // Phase 1: seek ENDSTOP_1 (move backward)
  motorStart(false, cfg.homingSpeed);
}

void homingUpdate() {
  switch (homingPhase) {

    case HOME_SEEK_END1:
      if (endstop1) {
        motorStopNow();
        homingBackoffTarget = currentPosition + BACKOFF_STEPS;
        homingPhase = HOME_BACKOFF1;
        motorStart(true, cfg.homingSpeed);  // forward
      }
      // Safety: wrong endstop hit while seeking endstop1
      if (endstop2) {
        motorStopNow();
        homingPhase = HOME_IDLE;
        sliderState = STATE_ERROR;
        errorCode = ERR_HOMING_FAIL;
      }
      break;

    case HOME_BACKOFF1:
      if (currentPosition >= homingBackoffTarget || !endstop1) {
        if (currentPosition >= homingBackoffTarget) {
          motorStopNow();
          currentPosition = 0;  // reset origin for this homing run
          homingPhase = HOME_SEEK_END2;
          motorStart(true, cfg.homingSpeed);  // forward
        }
      }
      break;

    case HOME_SEEK_END2:
      if (endstop2) {
        motorStopNow();
        travelDistance = currentPosition;
        homingBackoffTarget = currentPosition - BACKOFF_STEPS;
        homingPhase = HOME_BACKOFF2;
        motorStart(false, cfg.homingSpeed);  // backward
      }
      break;

    case HOME_BACKOFF2:
      if (currentPosition <= homingBackoffTarget) {
        motorStopNow();
        centerPosition = travelDistance / 2;
        homingPhase = HOME_GO_CENTER;
        motorMoveTo(centerPosition, cfg.homingSpeed);  // with ramp
      }
      break;

    case HOME_GO_CENTER:
      if (!motorRunning) {
        homingPhase = HOME_DONE;
        isCalibrated = true;
        configSaveCalibration();
        sliderState = STATE_IDLE;
        displayDirty = true;
      }
      break;

    case HOME_DONE:
    case HOME_IDLE:
      break;
  }
}

// New: abort an in-progress homing run. Leaves any prior (pre-this-run) calibration
// untouched -- since homingStart() never clears isCalibrated/travelDistance/centerPosition
// up front, an abort simply stops where it is without persisting anything new.
void homingAbort() {
  motorStopNow();
  homingPhase = HOME_IDLE;
  sliderState = STATE_IDLE;
  displayDirty = true;
}
