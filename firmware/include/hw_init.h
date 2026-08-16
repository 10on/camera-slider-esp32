// hw_init.h — shared bus bring-up (I2C, SPI, backlight PWM).
#pragma once

#include <stdint.h>

void hwInit();
void backlightOff();  // used by sleep.cpp
void backlightOn();   // restores cfg.brightness (called on wake, and after a live change)
void backlightSetBrightness(uint8_t pct);  // 10-100%, applies immediately
