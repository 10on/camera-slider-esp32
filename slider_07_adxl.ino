// slider_07_adxl.ino — ADXL345: read axes + blocking motion check for pre-sleep

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

// Blocking check: is the slider drifting after motor release?
// Reads ADXL for `durationMs`, returns true if X-axis drifted beyond threshold.
// Sets adxlMotionDir to +1 or -1 indicating drift direction.
bool adxlCheckDrift(uint16_t durationMs) {
  if (!adxlFound || cfg.adxlSensitivity == 0) return false;

  static const float thresholds[] = {999.0f, 0.15f, 0.08f, 0.04f};
  float threshold = thresholds[cfg.adxlSensitivity];

  // Capture baseline
  adxlReadAxes();
  float baseX = adxlX;

  unsigned long start = millis();
  while (millis() - start < durationMs) {
    delay(50);
    adxlReadAxes();

    float dx = adxlX - baseX;
    if (fabsf(dx) > threshold) {
      adxlMotionDir = (dx > 0) ? 1 : -1;
      Serial.print("ADXL drift detected! dx=");
      Serial.print(dx, 3);
      Serial.print(" dir=");
      Serial.println(adxlMotionDir);
      return true;
    }
  }

  Serial.println("ADXL: no drift");
  return false;
}
