// slider_03_motor.ino — Hardware timer ISR, step generation, linear ramp

// ISR: generates STEP pulses at precise intervals
// Handles linear ramp (acceleration/deceleration)
// Updates volatile currentPosition

void IRAM_ATTR onStepTimer() {
  if (!motorRunning) return;

  // ── Generate STEP pulse ──
  digitalWrite(STEP_PIN, HIGH);
  // Brief pulse (~1µs with digitalWrite, sufficient for TMC2209 min 100ns)
  digitalWrite(STEP_PIN, LOW);

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
      return;
    }

    // Deceleration ramp: slow down when approaching target
    int32_t absRemaining = remaining < 0 ? -remaining : remaining;
    if (absRemaining <= (int32_t)rampStepsLeft) {
      // In deceleration zone — increase interval (slow down)
      if (stepInterval < 5000) {
        stepInterval += (targetInterval > 200) ? 2 : 4;
      }
    }
  }

  // ── Stop request from endstop or command ──
  if (stopRequested) {
    motorRunning = false;
    stopRequested = false;
    return;
  }

  // ── Linear ramp processing ──
  if (rampStepsLeft > 0) {
    rampStepsLeft--;

    if (stepInterval > targetInterval) {
      // Accelerating: decrease interval
      uint32_t delta = (stepInterval - targetInterval) / (rampStepsLeft + 1);
      if (delta < 1) delta = 1;
      stepInterval -= delta;
      if (stepInterval < targetInterval) stepInterval = targetInterval;
    } else if (stepInterval < targetInterval) {
      // Decelerating: increase interval
      uint32_t delta = (targetInterval - stepInterval) / (rampStepsLeft + 1);
      if (delta < 1) delta = 1;
      stepInterval += delta;
      if (stepInterval > targetInterval) stepInterval = targetInterval;
    }

    // Stop after deceleration ramp completes
    if (rampStepsLeft == 0 && stopAfterRamp) {
      motorRunning = false;
      stopAfterRamp = false;
      return;
    }
  }

  // ── Update timer period for next step ──
  timerAlarm(stepTimer, stepInterval, true, 0);
}

void motorInit() {
  // ESP32 hardware timer 0, 1 MHz tick (1µs resolution)
  stepTimer = timerBegin(1000000);  // 1 MHz
  timerAttachInterrupt(stepTimer, &onStepTimer);
  // Don't start alarm yet — motorStart() will do it
  Serial.println("Motor timer initialized");
}

// Start motor at constant speed (no ramp)
void motorStart(bool forward, uint32_t intervalUs) {
  motorDirection = forward ? false : true;
  digitalWrite(DIR_PIN, motorDirection ? HIGH : LOW);
  delay(1);  // DIR setup time

  stepInterval = intervalUs;
  targetInterval = intervalUs;
  rampStepsLeft = 0;
  motorHasTarget = false;
  stopRequested = false;
  stopAfterRamp = false;
  motorRunning = true;

  timerAlarm(stepTimer, intervalUs, true, 0);
}

// Start motor with linear acceleration ramp
void motorStartRamp(bool forward, uint32_t intervalUs) {
  motorDirection = forward ? false : true;
  digitalWrite(DIR_PIN, motorDirection ? HIGH : LOW);
  delay(1);

  // Start from slow speed, ramp to target
  uint32_t startInterval = 3000;  // slow start ~333 steps/sec
  if (startInterval < intervalUs) startInterval = intervalUs;  // don't start slower than target

  stepInterval = startInterval;
  targetInterval = intervalUs;
  rampStepsLeft = cfg.rampSteps;
  motorHasTarget = false;
  stopRequested = false;
  stopAfterRamp = false;
  motorRunning = true;

  timerAlarm(stepTimer, startInterval, true, 0);
}

// Move to absolute position with ramp
void motorMoveTo(int32_t position, uint32_t intervalUs) {
  int32_t delta = position - currentPosition;
  if (delta == 0) return;

  bool forward = delta > 0;
  motorDirection = forward ? false : true;
  digitalWrite(DIR_PIN, motorDirection ? HIGH : LOW);
  delay(1);

  motorTargetPos = position;
  motorHasTarget = true;

  int32_t absDelta = delta < 0 ? -delta : delta;

  // Start with ramp if distance is long enough
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

  timerAlarm(stepTimer, startInterval, true, 0);
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
}
