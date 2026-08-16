// motor.h — ISR-driven step generation. onStepTimer() is the ONLY source of STEP
// pulses; never toggle STEP/DIR from loop()/BLE/UI code.
#pragma once

#include <Arduino.h>

void motorInit();
void motorStart(bool forward, uint32_t intervalUs);       // constant speed, no ramp
void motorStartRamp(bool forward, uint32_t intervalUs);    // with accel ramp
void motorMoveTo(int32_t position, uint32_t intervalUs);   // absolute position, with ramp
void motorStop();      // graceful deceleration then halt
void motorStopNow();   // immediate hard stop
