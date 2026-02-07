// slider_07_adxl.ino — ADXL345 motion detection + direction + parking trigger

// Baseline readings (captured when idle)
static float adxlBaseX = 0, adxlBaseY = 0, adxlBaseZ = 0;
static bool  adxlBaselineSet = false;
static unsigned long adxlBaselineTime = 0;

// Thresholds indexed by sensitivity (0=off, 1=low, 2=mid, 3=high)
static const float adxlThresholds[] = {999.0f, 0.15f, 0.08f, 0.04f};

void adxlReadAxes() {
  if (!adxlFound) return;

  Wire.beginTransmission(adxlAddr);
  Wire.write(REG_DATAX0);
  Wire.endTransmission(false);
  Wire.requestFrom(adxlAddr, (uint8_t)6);

  int16_t raw_x = Wire.read() | (Wire.read() << 8);
  int16_t raw_y = Wire.read() | (Wire.read() << 8);
  int16_t raw_z = Wire.read() | (Wire.read() << 8);

  // Full resolution, ±2g: 3.9 mg/LSB
  adxlX = raw_x * 0.0039f;
  adxlY = raw_y * 0.0039f;
  adxlZ = raw_z * 0.0039f;
}

void adxlSetBaseline() {
  adxlReadAxes();
  adxlBaseX = adxlX;
  adxlBaseY = adxlY;
  adxlBaseZ = adxlZ;
  adxlBaselineSet = true;
  adxlBaselineTime = millis();
}

void adxlCheckMotion() {
  if (!adxlFound || cfg.adxlSensitivity == 0) return;

  adxlReadAxes();

  // Set baseline after entering idle/sleep (wait 500ms for vibration to settle)
  if (!adxlBaselineSet) {
    if (adxlBaselineTime == 0) {
      adxlBaselineTime = millis();
    }
    if (millis() - adxlBaselineTime > 500) {
      adxlSetBaseline();
    }
    return;
  }

  // Compare with baseline
  float dx = adxlX - adxlBaseX;
  float dy = adxlY - adxlBaseY;
  float dz = adxlZ - adxlBaseZ;

  float threshold = adxlThresholds[cfg.adxlSensitivity];

  // Check if slider axis (X) changed significantly
  // X axis is along the slider rail
  if (fabsf(dx) > threshold) {
    adxlMotionDetected = true;
    adxlMotionDir = (dx > 0) ? 1 : -1;

    Serial.print("ADXL motion detected! dx=");
    Serial.print(dx, 3);
    Serial.print(" dir=");
    Serial.println(adxlMotionDir);

    // Trigger parking
    parkingStart();
  }

  // Slowly drift baseline to compensate for temperature etc
  adxlBaseX = adxlBaseX * 0.99f + adxlX * 0.01f;
  adxlBaseY = adxlBaseY * 0.99f + adxlY * 0.01f;
  adxlBaseZ = adxlBaseZ * 0.99f + adxlZ * 0.01f;
}

void parkingStart() {
  if (sliderState != STATE_IDLE && sliderState != STATE_SLEEP) return;
  if (!isCalibrated) return;

  sliderState = STATE_PARKING;
  digitalWrite(EN_PIN, LOW);

  // Determine direction: park toward the nearest endstop
  // If motion is positive (forward), park at endstop 2 (forward end)
  // If motion is negative (backward), park at endstop 1 (backward end)
  bool forward;
  if (adxlMotionDir > 0) {
    forward = true;  // move forward to endstop 2
  } else {
    forward = false;  // move backward to endstop 1
  }

  motorStartRamp(forward, cfg.homingSpeed);
  displayDirty = true;
  Serial.print("Parking started, dir=");
  Serial.println(forward ? "FWD" : "REV");
}

// Reset ADXL baseline when entering idle/sleep
void adxlResetBaseline() {
  adxlBaselineSet = false;
  adxlBaselineTime = 0;
  adxlMotionDetected = false;
}
