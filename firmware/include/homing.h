// homing.h — non-blocking 6-phase homing sub-automaton.
#pragma once

void homingStart();
void homingUpdate();
void homingAbort();  // new: no equivalent in the old firmware (see plan)
