// motor.cpp — hardware timer ISR, step generation, linear ramp.
// Ported from slider_03_motor.ino, behavior preserved exactly.
//
// TIMER API TRANSLATION: the old source was written against the Arduino-ESP32 3.x timer
// API (single-arg timerBegin(freq), 2-arg timerAttachInterrupt, combined timerAlarm(timer,
// value, autoreload, count)). This project's actual installed core exposes the older
// (2.0.x-style) signatures instead -- timerBegin(num,divider,countUp) / timerAttachInterrupt
// (timer,fn,edge) / separate timerAlarmWrite(timer,value,autoreload) + timerAlarmEnable(timer)
// -- confirmed by grepping the installed esp32-hal-timer.h; timerAlarm()/timerBegin(freq) do
// not exist in this toolchain. Every step still re-arms the alarm via timerAlarmWrite() from
// inside the ISR (the same "call it every step or the timer stops firing" pattern the old
// timerAlarm() call relied on), so the ramp/homing/speed timing behavior is unchanged.

#include "motor.h"
#include "pins.h"
#include "globals.h"

static bool directionBlockedByEndstop(bool forward) {
  return forward ? endstop2 : endstop1;
}

void IRAM_ATTR onStepTimer() {
  if (!motorRunning) return;

  // ── Generate STEP pulse ──
  digitalWrite(MOTOR_STEP, HIGH);
  digitalWrite(MOTOR_STEP, LOW);

  // ── Update position ──
  if (!motorDirection) {
    currentPosition++;
  } else {
    currentPosition--;
  }

  // ── Check target position ──
  if (motorHasTarget) {
    int32_t remaining = motorTargetPos - currentPosition;
    if (remaining == 0) {
      motorRunning = false;
      motorHasTarget = false;
      timerAlarmDisable(stepTimer);
      return;
    }

    // Deceleration ramp: slow down when approaching target
    int32_t absRemaining = remaining < 0 ? -remaining : remaining;
    if (absRemaining <= (int32_t)rampStepsLeft) {
      if (stepInterval < 5000) {
        stepInterval += (targetInterval > 200) ? 2 : 4;
      }
    }
  }

  // ── Stop request from endstop or command ──
  if (stopRequested) {
    motorRunning = false;
    stopRequested = false;
    timerAlarmDisable(stepTimer);
    return;
  }

  // ── Linear ramp processing ──
  if (rampStepsLeft > 0) {
    rampStepsLeft--;

    if (stepInterval > targetInterval) {
      uint32_t delta = (stepInterval - targetInterval) / (rampStepsLeft + 1);
      if (delta < 1) delta = 1;
      stepInterval -= delta;
      if (stepInterval < targetInterval) stepInterval = targetInterval;
    } else if (stepInterval < targetInterval) {
      uint32_t delta = (targetInterval - stepInterval) / (rampStepsLeft + 1);
      if (delta < 1) delta = 1;
      stepInterval += delta;
      if (stepInterval > targetInterval) stepInterval = targetInterval;
    }

    if (rampStepsLeft == 0 && stopAfterRamp) {
      motorRunning = false;
      stopAfterRamp = false;
      timerAlarmDisable(stepTimer);
      return;
    }
  }

  // ── Update timer period for next step ──
  timerAlarmWrite(stepTimer, stepInterval, true);
}

void motorInit() {
  pinMode(MOTOR_EN,   OUTPUT);
  pinMode(MOTOR_STEP, OUTPUT);
  pinMode(MOTOR_DIR,  OUTPUT);
  digitalWrite(MOTOR_EN, LOW);
  digitalWrite(MOTOR_DIR, LOW);

  TMCSerial.begin(115200, SERIAL_8N1, TMC_RX, TMC_TX);
  driver.begin();
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.microsteps(cfg.microsteps);
  driver.rms_current(cfg.motorCurrent);
  driver.en_spreadCycle(false);   // stealthChop

  // ESP32 hardware timer 0, 1 MHz tick (1us resolution): 80MHz APB / 80 divider.
  stepTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(stepTimer, &onStepTimer, true);
  // Don't enable the alarm yet -- motorStart()/motorStartRamp()/motorMoveTo() will.
}

// Start motor at constant speed (no ramp)
void motorStart(bool forward, uint32_t intervalUs) {
  // Last line of defence: no caller (BLE, UI, homing, parking, or a future mode) may
  // start the carriage deeper into an endstop that is already held.  Higher layers still
  // decide whether to stop, bounce, or park when a switch is reached while moving.
  if (directionBlockedByEndstop(forward)) {
    motorStopNow();
    return;
  }

  motorDirection = forward ? false : true;
  digitalWrite(MOTOR_DIR, motorDirection ? HIGH : LOW);
  delay(1);  // DIR setup time

  stepInterval = intervalUs;
  targetInterval = intervalUs;
  rampStepsLeft = 0;
  motorHasTarget = false;
  stopRequested = false;
  stopAfterRamp = false;
  motorRunning = true;

  timerAlarmWrite(stepTimer, intervalUs, true);
  timerAlarmEnable(stepTimer);
}

// Start motor with linear acceleration ramp
void motorStartRamp(bool forward, uint32_t intervalUs) {
  if (directionBlockedByEndstop(forward)) {
    motorStopNow();
    return;
  }

  motorDirection = forward ? false : true;
  digitalWrite(MOTOR_DIR, motorDirection ? HIGH : LOW);
  delay(1);

  uint32_t startInterval = 3000;  // slow start ~333 steps/sec
  if (startInterval < intervalUs) startInterval = intervalUs;  // don't start slower than target

  stepInterval = startInterval;
  targetInterval = intervalUs;
  rampStepsLeft = cfg.rampSteps;
  motorHasTarget = false;
  stopRequested = false;
  stopAfterRamp = false;
  motorRunning = true;

  timerAlarmWrite(stepTimer, startInterval, true);
  timerAlarmEnable(stepTimer);
}

// Move to absolute position with ramp
void motorMoveTo(int32_t position, uint32_t intervalUs) {
  int32_t delta = position - currentPosition;
  if (delta == 0) return;

  bool forward = delta > 0;
  if (directionBlockedByEndstop(forward)) {
    motorStopNow();
    return;
  }

  motorDirection = forward ? false : true;
  digitalWrite(MOTOR_DIR, motorDirection ? HIGH : LOW);
  delay(1);

  motorTargetPos = position;
  motorHasTarget = true;

  int32_t absDelta = delta < 0 ? -delta : delta;

  uint32_t startInterval = 3000;
  if (startInterval < intervalUs) startInterval = intervalUs;

  uint32_t rampLen = cfg.rampSteps;
  if ((uint32_t)absDelta < rampLen * 2) {
    rampLen = absDelta / 2;  // shorter ramp for short moves
  }

  stepInterval = startInterval;
  targetInterval = intervalUs;
  rampStepsLeft = rampLen;
  stopRequested = false;
  motorRunning = true;

  timerAlarmWrite(stepTimer, startInterval, true);
  timerAlarmEnable(stepTimer);
}

// Stop motor (with deceleration ramp, then stop)
void motorStop() {
  if (!motorRunning) return;

  targetInterval = 3000;
  rampStepsLeft = cfg.rampSteps / 2;
  stopAfterRamp = true;
  motorHasTarget = false;
}

// Immediate stop (no ramp)
void motorStopNow() {
  motorRunning = false;
  motorHasTarget = false;
  rampStepsLeft = 0;
  stopRequested = false;
  stopAfterRamp = false;
  timerAlarmDisable(stepTimer);
}
