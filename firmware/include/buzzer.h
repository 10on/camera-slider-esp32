// buzzer.h — passive piezo speaker on a spare ledc channel (same PWM peripheral used
// for the backlight). No driver transistor on the hardware -- a bare piezo disc is a
// capacitive load, not inductive, so GPIO drives it directly.
#pragma once

void buzzerInit();
void buzzerClick();     // short tactile tick, gated on cfg.speakerEnabled -- call on encoder detents
void buzzerBootChime(); // rising two-note chime -- call once at the end of setup()
void buzzerOtaStartChime(); // two quick beeps -- call when an OTA transfer begins
void buzzerShutdownChime(); // three quick beeps -- call right before a reboot (OTA end, etc.)
