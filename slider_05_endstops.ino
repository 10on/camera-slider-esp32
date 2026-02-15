// slider_05_endstops.ino — PCF8574 polling (endstops + encoder), single I2C transaction

static unsigned long lastPcfPoll     = 0;
static unsigned long lastPcfOk       = 0;
static unsigned long lastPcfReinit   = 0;
static bool          pcfOkInit       = false;

// ── I2C debug helpers (local) ──
static bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Debug helpers removed for release build

static bool pcfTryRelocate() {
  // Try both typical addresses and (re)bind to the first responsive
  const uint8_t candidates[] = {0x20, 0x21};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    uint8_t addr = candidates[i];
    if (!i2cPing(addr)) continue;

    // Recreate object
    if (pcf) { delete pcf; pcf = NULL; }
    pcf = new PCF8574(addr);
    pcf->begin();
    // Restore outputs
    pcf->write8(pcfOutputState);

    pcfAddr = addr;
    pcfFound = true;
    lastPcfOk = millis();
    return true;
  }
  return false;
}

void pcfPoll() {
  if (!pcfFound) return;

  unsigned long now = millis();

  // Initialize lastPcfOk on first call
  if (!pcfOkInit) { lastPcfOk = now; pcfOkInit = true; }

  // Throttle: poll every 5ms (200 Hz) — достаточно для энкодера/концевиков
  if (now - lastPcfPoll < 5) return;
  lastPcfPoll = now;

  // Try up to 4 attempts
  for (uint8_t attempt = 0; attempt < 4; attempt++) {
    Wire.beginTransmission(pcfAddr);
    Wire.write(pcfOutputState);
    if (Wire.endTransmission() == 0) {
      // Небольшая пауза после записи перед чтением — некоторые экземпляры PCF любят settle
      delayMicroseconds(200);
      Wire.requestFrom(pcfAddr, (uint8_t)1);
      if (Wire.available()) {
        pcfInputState = Wire.read();
        lastPcfOk = now;
        return;  // success
      }
    }
    delayMicroseconds(100);  // brief pause before retry
  }

  // Both attempts failed — bus may be stuck
  unsigned long downMs = now - lastPcfOk;

  // Re-init bus at most once every 3 seconds
  if (downMs > 500 && now - lastPcfReinit > 3000) {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    Wire.setTimeOut(10);
    delay(2);  // дать шине стабилизироваться
    lastPcfReinit = now;

    // Try to relocate/rebind PCF if alternative address is alive
    bool currentAlive = (pcfAddr && i2cPing(pcfAddr));
    if (!currentAlive) {
      pcfTryRelocate();
    }
  }

  // Emergency stop if PCF lost >1000ms while motor is running
  if (motorRunning && downMs > 1000) {
    motorStopNow();
    sliderState = STATE_ERROR;
    errorCode = ERR_ENDSTOP_UNEXPECTED;
    displayDirty = true;
    
  }
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
