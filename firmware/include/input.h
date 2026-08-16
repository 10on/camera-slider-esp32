// input.h — encoder + BTN1/BTN2 + endstops, direct GPIO (no PCF8574 on this hardware).
// Full-quadrature CHANGE-interrupt decode (better than the old single-edge poll) --
// ported from firmware/src/selftest_main.cpp, extended with BTN2 short/800ms-long press
// timing. Encoder press and BTN1 are short-press only (no long-press variant in the new
// 3-input model -- see plan's button-mapping table).
#pragma once

#include <stdint.h>

void inputInit();
void inputPoll();  // call every loop(); updates endstop1/2(+Rising), encoderDelta,
                    // encoderPressed, btn1Pressed, btn2Pressed, btn2LongPress

// Undivided quadrature counter (4 counts per detent), for the diagnostics screen only --
// everything else consumes the detent-normalised encoderDelta.
int32_t inputEncoderRawCount();
