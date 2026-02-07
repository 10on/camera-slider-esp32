// slider_06_homing.ino — Non-blocking homing sub-automaton
// Phases: SEEK_END1 → BACKOFF1 → SEEK_END2 → BACKOFF2 → GO_CENTER → DONE

static int32_t homingStepCount = 0;
static int32_t homingBackoffTarget = 0;

void homingStart() {
  sliderState = STATE_HOMING;
  homingPhase = HOME_SEEK_END1;
  homingStepCount = 0;
  isCalibrated = false;

  digitalWrite(EN_PIN, LOW);

  // Phase 1: Seek ENDSTOP_1 (move backward = DIR HIGH)
  motorStart(false, cfg.homingSpeed);  // backward
  Serial.println("Homing: SEEK_END1");
}

void homingUpdate() {
  switch (homingPhase) {

    case HOME_SEEK_END1:
      // Moving backward toward endstop 1
      if (endstop1) {
        motorStopNow();
        // Prepare backoff
        homingBackoffTarget = currentPosition + BACKOFF_STEPS;
        homingPhase = HOME_BACKOFF1;
        // Move forward to back off
        motorStart(true, cfg.homingSpeed);  // forward
        Serial.println("Homing: BACKOFF1");
      }
      // Safety: if we hit endstop 2 while seeking endstop 1
      if (endstop2) {
        motorStopNow();
        homingPhase = HOME_IDLE;
        sliderState = STATE_ERROR;
        errorCode = ERR_HOMING_FAIL;
        Serial.println("Homing FAIL: hit END2 while seeking END1");
      }
      break;

    case HOME_BACKOFF1:
      // Moving forward, backing off from endstop 1
      if (currentPosition >= homingBackoffTarget || !endstop1) {
        // Wait until we're past backoff distance AND endstop is released
        if (currentPosition >= homingBackoffTarget) {
          motorStopNow();
          currentPosition = 0;  // Reset origin
          homingPhase = HOME_SEEK_END2;
          homingStepCount = 0;
          // Move forward toward endstop 2
          motorStart(true, cfg.homingSpeed);  // forward
          Serial.println("Homing: SEEK_END2, position reset to 0");
        }
      }
      break;

    case HOME_SEEK_END2:
      // Moving forward toward endstop 2, counting steps
      if (endstop2) {
        motorStopNow();
        travelDistance = currentPosition;
        homingBackoffTarget = currentPosition - BACKOFF_STEPS;
        homingPhase = HOME_BACKOFF2;
        // Move backward to back off
        motorStart(false, cfg.homingSpeed);  // backward
        Serial.print("Homing: BACKOFF2, travel=");
        Serial.println(travelDistance);
      }
      break;

    case HOME_BACKOFF2:
      // Moving backward, backing off from endstop 2
      if (currentPosition <= homingBackoffTarget) {
        motorStopNow();
        centerPosition = travelDistance / 2;
        homingPhase = HOME_GO_CENTER;
        // Move to center position
        motorMoveTo(centerPosition, cfg.homingSpeed);
        Serial.print("Homing: GO_CENTER → ");
        Serial.println(centerPosition);
      }
      break;

    case HOME_GO_CENTER:
      // Moving to center — motorMoveTo handles stopping
      if (!motorRunning) {
        homingPhase = HOME_DONE;
        isCalibrated = true;
        configSaveCalibration();
        sliderState = STATE_IDLE;
        displayDirty = true;
        Serial.println("Homing COMPLETE");
        Serial.print("  Position: "); Serial.println(currentPosition);
        Serial.print("  Center: "); Serial.println(centerPosition);
        Serial.print("  Travel: "); Serial.println(travelDistance);
      }
      break;

    case HOME_DONE:
    case HOME_IDLE:
      break;
  }
}
