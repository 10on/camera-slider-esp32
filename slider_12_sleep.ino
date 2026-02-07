// slider_12_sleep.ino — Sleep/wake management

void sleepCheck() {
  if (cfg.sleepTimeout == 0) return;  // disabled
  if (sliderState != STATE_IDLE) return;

  unsigned long timeout = (unsigned long)cfg.sleepTimeout * 60000UL;
  if (millis() - lastActivityTime > timeout) {
    sleepEnter();
  }
}

void sleepEnter() {
  Serial.println("Entering sleep mode");

  sliderState = STATE_SLEEP;

  // Disable motor driver
  motorStopNow();
  digitalWrite(EN_PIN, HIGH);

  // Turn off OLED
  if (oledFound) {
    oled->clearDisplay();
    oled->display();
    oled->ssd1306_command(SSD1306_DISPLAYOFF);
  }

  // Turn off LED
  if (pcfFound) {
    pcfOutputState = 0xFD;  // LED off
    pcf->write8(pcfOutputState);
  }

  // Reset ADXL baseline for motion detection in sleep
  adxlResetBaseline();

  displayDirty = false;  // don't update display
  Serial.println("Sleep mode active");
}

void sleepWake() {
  if (sliderState != STATE_SLEEP) return;

  Serial.println("Waking up");

  sliderState = STATE_IDLE;
  lastActivityTime = millis();

  // Re-enable OLED
  if (oledFound) {
    oled->ssd1306_command(SSD1306_DISPLAYON);
  }

  // Restore display
  currentScreen = SCREEN_MAIN;
  displayDirty = true;

  Serial.println("Awake");
}

// Check for wake triggers (called from various poll functions)
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

  // Wake on ADXL motion (if enabled)
  if (cfg.wakeOnMotion && adxlMotionDetected) {
    // Note: parkingStart() is called from adxlCheckMotion() first,
    // then after parking completes we stay in IDLE (or go back to SLEEP)
    // The wake happens via parking completion → IDLE
  }
}
