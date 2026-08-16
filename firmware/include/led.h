// led.h — status LED pattern engine + battery LED thresholds, direct GPIO.
#pragma once

#include "globals.h"

void ledSetPattern(LedPattern pat);
void ledUpdate();
