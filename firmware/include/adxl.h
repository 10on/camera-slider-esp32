// adxl.h — ADXL345 register-level access + drift-based safe-parking check.
#pragma once

#include <Arduino.h>

void adxlInit();
void adxlReadAxes();
bool adxlCheckDrift(uint16_t durationMs);  // blocking; used only by sleep.cpp before parking
