// slider_12_sleep.ino — Sleep/wake management with safe parking

void sleepCheck() {
  if (cfg.sleepTimeout == 0) return;  // disabled
  if (sliderState != STATE_IDLE) return;

  unsigned long timeout = (unsigned long)cfg.sleepTimeout * 60000UL;
  if (millis() - lastActivityTime > timeout) {
    sleepParkAndEnter();
  }
}

// Try to safely park before sleeping.
// Blocking sequence: release motor → check ADXL → park if drifting → retry.
void sleepParkAndEnter() {
  Serial.println("Sleep: starting safe park sequence");

  // Attempt 1: release motor, check for drift
  motorStopNow();
  digitalWrite(EN_PIN, HIGH);  // disable holding
  delay(100);                  // let mechanics settle

  if (adxlCheckDrift(3000)) {
    // Slider is drifting! Re-enable and park toward drift direction
    Serial.println("Sleep: drift on attempt 1, parking...");
    digitalWrite(EN_PIN, LOW);

    bool forward = (adxlMotionDir > 0);
    motorStartRamp(forward, cfg.homingSpeed);
    sliderState = STATE_PARKING;
    displayDirty = true;

    // Wait for parking to complete (endstop hit or motor stop)
    while (motorRunning) {
      pcfPoll();
      endstopsPoll();
      if (endstop1Rising || endstop2Rising) {
        motorStopNow();
      }
      delay(1);
    }

    sliderState = STATE_IDLE;

    // Attempt 2: release again, check other direction
    digitalWrite(EN_PIN, HIGH);
    delay(100);

    if (adxlCheckDrift(3000)) {
      // Still drifting — try opposite endstop
      Serial.println("Sleep: drift on attempt 2, parking opposite...");
      digitalWrite(EN_PIN, LOW);

      bool forward2 = (adxlMotionDir > 0);
      motorStartRamp(forward2, cfg.homingSpeed);
      sliderState = STATE_PARKING;

      while (motorRunning) {
        pcfPoll();
        endstopsPoll();
        if (endstop1Rising || endstop2Rising) {
          motorStopNow();
        }
        delay(1);
      }

      sliderState = STATE_IDLE;
      digitalWrite(EN_PIN, HIGH);
    }
  }

  // Motor is off, safe (or best effort) — enter sleep
  sleepEnter();
}

void sleepEnter() {
  Serial.println("Entering sleep mode");

  sliderState = STATE_SLEEP;
  motorStopNow();
  digitalWrite(EN_PIN, HIGH);

  // Turn off OLED (u8g2)
  if (oledFound) {
    oled->clearBuffer();
    oled->sendBuffer();
    oled->setPowerSave(1);
  }

  // Turn off LEDs
  if (pcfFound) {
    pcfOutputState = 0x7D;  // all inputs HIGH, LED1 off (P1=0), LED2 off (P7=0)
    pcf->write8(pcfOutputState);
  }

  displayDirty = false;
  Serial.println("Sleep mode active");
}

void sleepWake() {
  if (sliderState != STATE_SLEEP) return;

  Serial.println("Waking up");

  sliderState = STATE_IDLE;
  lastActivityTime = millis();

  // Re-enable OLED (u8g2)
  if (oledFound) {
    oled->setPowerSave(0);
  }

  currentScreen = SCREEN_MAIN;
  displayDirty = true;

  Serial.println("Awake");
}

// Check for wake triggers (called every loop)
void sleepCheckWake() {
  if (sliderState != STATE_SLEEP) return;

  // Wake on encoder activity
  if (encoderDelta != 0 || encoderPressed) {
    sleepWake();
    encoderDelta = 0;
    encoderPressed = false;
    return;
  }

  // Wake on BLE connect
  if (bleConnected) {
    sleepWake();
    return;
  }
}
