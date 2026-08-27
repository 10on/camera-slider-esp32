// power.cpp — INA226-based battery voltage/current, replacing the old resistor-divider ADC
// (VBAT_PIN) method entirely. Reuses the old VBAT_EMPTY=9.0V/VBAT_FULL=12.6V percent
// formula against busVoltage_V instead of vbatVoltage. INA226 is a proper I2C sensor (no
// ADC/chopper noise concerns), so unlike the old vbatReadPrecise()/vbatReadQuick() split,
// a single read function is accurate enough for both the periodic poll and the brief
// mid-bounce grab.
//
// NOTE: the actual hardware is INA226, not INA219 -- they are different chips with
// different register formats (confirmed the hard way: Adafruit_INA219's bus-voltage read,
// which assumes INA219's 13-bit/4mV-LSB register layout, silently misreads an INA226's
// 16-bit/1.25mV-LSB register and produced a bogus 0.00V while current still looked live).
#include <Wire.h>
#include "power.h"
#include "pins.h"
#include "globals.h"

void powerInit() {
  ina226Found = ina226.begin();
  if (ina226Found) {
    // Calibration is mandatory for getCurrent()/getPower() (not for getBusVoltage()).
    // INA226's shunt ADC full-scale is +-81.92mV (fixed by the chip, not configurable), so
    // the calibration's max-current must stay within what the actual shunt resistance can
    // physically produce at that voltage, or the library's calibration silently comes out
    // wrong and getCurrent_mA() reads back a flat 0 regardless of real current -- confirmed
    // the hard way with the original bare 0.1ohm shunt (R100 alone -> ~0.82A hard ceiling,
    // too tight for this board's total draw of motor + ESP32 + display + backlight).
    //
    // Current hardware: two 0.1ohm (R100) resistors in parallel (the earlier 2xR110 fix
    // was replaced, not stacked on top). 1/R = 1/0.1 + 1/0.1 -> R = 0.05ohm -> ceiling
    // 81.92mV/0.05 = ~1.64A. Requesting 1.5A for a bit of margin under that cap.
    ina226.setMaxCurrentShunt(1.5, 0.05);
  }
}

void powerRead() {
  if (!ina226Found) return;
  busVoltage_V = ina226.getBusVoltage();
  current_mA   = ina226.getCurrent_mA();
  isCharging   = busVoltage_V > VBAT_CHARGING_THRESHOLD;
}

int batteryPercent() {
  int pct = (int)((busVoltage_V - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100);
  return constrain(pct, 0, 100);
}
