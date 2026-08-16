// display.h — ST7735 graphical UI, per docs/07_ui_kit.md (Adafruit_GFX/Adafruit_ST7735,
// not the doc's stale LovyanGFX/PSRAM recommendation -- see plan).
#pragma once

#include <stdint.h>

void displayInit();
void displayUpdate();  // call every loop(); self-throttled (~50ms tick incl. position-bar lerp)
void displaySetTheme(uint8_t theme);  // UiTheme; re-applies the palette immediately
void displayForceRepaint();           // next displayUpdate() clears and redraws everything

// OTA progress screen. Drawn directly from the ArduinoOTA callbacks rather than through
// displayUpdate(), because loop() is blocked inside ArduinoOTA.handle() for the whole
// transfer -- without this the display just freezes on whatever screen was up.
void displayOtaBegin();
void displayOtaProgress(unsigned int pct);
void displayOtaEnd(const char* msg, bool failed);
