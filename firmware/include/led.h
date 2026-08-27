// led.h — status LED pattern engine + battery LED thresholds, direct GPIO.
#pragma once

#include "globals.h"

void ledInit();
void ledSetPattern(LedPattern pat);
void ledUpdate();
