// slider_05_endstops.ino — PCF8574 polling (endstops + encoder), single I2C transaction

void pcfPoll() {
  if (!pcfFound) return;

  // Single write + read per loop iteration
  pcf->write8(pcfOutputState);
  pcfInputState = pcf->read8();
}

void endstopsPoll() {
  endstop1Rising = false;
  endstop2Rising = false;

  bool prevE1 = endstop1;
  bool prevE2 = endstop2;

  if (pcfFound) {
    // PCF8574 endstops: active HIGH
    endstop1 = (pcfInputState & (1 << PCF_ENDSTOP_1)) != 0;
    endstop2 = (pcfInputState & (1 << PCF_ENDSTOP_2)) != 0;
  } else {
    // Fallback: direct GPIO (active HIGH from main_old.ino)
    endstop1 = digitalRead(ENDSTOP_1_GPIO) == HIGH;
    endstop2 = digitalRead(ENDSTOP_2_GPIO) == HIGH;
  }

  // Rising edge (off → on)
  if (endstop1 && !prevE1) endstop1Rising = true;
  if (endstop2 && !prevE2) endstop2Rising = true;

  // Display dirty on ANY change (press or release)
  if (endstop1 != prevE1 || endstop2 != prevE2) {
    displayDirty = true;
  }
}

void encoderPoll() {
  if (!pcfFound) return;

  // Extract encoder bits from PCF8574 read
  uint8_t clk = (pcfInputState >> PCF_ENC_CLK) & 1;
  uint8_t dt  = (pcfInputState >> PCF_ENC_DT) & 1;
  uint8_t sw  = (pcfInputState >> PCF_ENC_SW) & 1;

  // ── Rotation ──
  if (clk != encClkPrev && clk == LOW) {
    if (dt == HIGH) {
      encoderDelta++;   // CW
    } else {
      encoderDelta--;   // CCW
    }
    lastActivityTime = millis();
  }
  encClkPrev = clk;

  // ── Button with debounce ──
  encSwState = sw;

  // Press edge (HIGH → LOW)
  if (encSwState == LOW && encSwPrev == HIGH) {
    encPressTime = millis();
    encLongFired = false;
    lastActivityTime = millis();
  }

  // Long press detection while held
  if (encSwState == LOW && !encLongFired) {
    if (millis() - encPressTime > 500) {
      encoderLongPress = true;
      encLongFired = true;
      lastActivityTime = millis();
    }
  }

  // Release edge (LOW → HIGH)
  encoderPressed = false;
  if (encSwState == HIGH && encSwPrev == LOW) {
    if (!encLongFired) {
      encoderPressed = true;  // short press
      lastActivityTime = millis();
    }
  }

  encSwPrev = encSwState;
}
