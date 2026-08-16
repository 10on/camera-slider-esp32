// state.h — state machine transitions, BLE command dispatch, endstop-hit handling.
#pragma once

#include "globals.h"

void stateUpdate();
void processBleCommands();
void handleEndstopHit();
void stateEnterError(ErrorCode code);
void stateResetError();
const char* stateToString(SliderState s);
const char* errorToString(ErrorCode e);
