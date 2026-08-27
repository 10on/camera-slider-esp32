// adxl.cpp — ADXL345 register-level access (reused verbatim from firmware/src/
// selftest_main.cpp, already hardware-validated) + adxlCheckDrift() ported from
// slider_07_adxl.ino, behavior preserved exactly (X-axis only, 4-level threshold table,
// blocking sample loop).
#include <Wire.h>
#include "adxl.h"
#include "pins.h"
#include "globals.h"

static void adxlWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void adxlInit() {
  for (uint8_t addr : {0x53, 0x1D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;

    Wire.beginTransmission(addr);
    Wire.write(REG_DEVID);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    uint8_t devid = Wire.read();

    if (devid == 0xE5) {
      adxlAddr = addr;
      adxlFound = true;
      adxlWriteReg(REG_DATA_FORMAT, 0x08); // full resolution, +-2g
      adxlWriteReg(REG_BW_RATE, 0x0A);     // 100 Hz
      adxlWriteReg(REG_POWER_CTL, 0x08);   // measurement mode
      delay(50);
      return;
    }
  }
}

void adxlReadAxes() {
  if (!adxlFound) return;
  Wire.beginTransmission(adxlAddr);
  Wire.write(REG_DATAX0);
  Wire.endTransmission(false);
  Wire.requestFrom(adxlAddr, (uint8_t)6);

  int16_t raw_x = Wire.read() | (Wire.read() << 8);
  int16_t raw_y = Wire.read() | (Wire.read() << 8);
  int16_t raw_z = Wire.read() | (Wire.read() << 8);

  adxlX = raw_x * 0.0039f; // 3.9 mg/LSB, full resolution +-2g
  adxlY = raw_y * 0.0039f;
  adxlZ = raw_z * 0.0039f;
}

// Blocking check: is the slider drifting after motor release?
// Reads ADXL for `durationMs`, returns true if the rail axis drifted beyond threshold.
// Sets adxlMotionDir to +1 (drifting toward E2 / increasing position) or -1 (toward E1).
//
// AXIS: Y, not Z. The board was physically remounted/rotated in the enclosure after the
// original bring-up (which measured Z as the along-rail axis, see git history) -- retested
// by hand-tilting in the new mounting orientation, and Y is now the one that responds to
// tilting along the rail, not Z.
//
// Sign is unconfirmed on the new mounting (kept the old Z convention as a best guess: (delta
// > 0) -> +1 -> toward E2/increasing position). If the parking direction after a detected
// drift ever drives toward the wrong endstop, flip this to `(dy > 0) ? -1 : 1` -- that is
// the one line to change, no other logic depends on the sign.
bool adxlCheckDrift(uint16_t durationMs) {
  if (!adxlFound || cfg.adxlSensitivity == 0) return false;

  static const float thresholds[] = {999.0f, 0.15f, 0.08f, 0.04f};
  float threshold = thresholds[cfg.adxlSensitivity];

  adxlReadAxes();
  float baseY = adxlY;

  unsigned long start = millis();
  while (millis() - start < durationMs) {
    delay(50);
    adxlReadAxes();

    float dy = adxlY - baseY;
    if (fabsf(dy) > threshold) {
      adxlMotionDir = (dy > 0) ? 1 : -1;
      return true;
    }
  }

  return false;
}
