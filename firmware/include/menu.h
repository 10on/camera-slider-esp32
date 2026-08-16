// menu.h — screen navigation + input dispatch (encoder + BTN1 + BTN2), full mapping
// table implemented in menu.cpp. Also exposes the list-screen item model so display.cpp
// can render Settings/Motion/Sleep/System lists generically.
#pragma once

#include "globals.h"

void menuInit();
void menuHandleInput();  // call every loop(); consumes encoderDelta/encoderPressed/
                          // btn1Pressed/btn2Pressed/btn2LongPress

// List-screen item model, used by SCREEN_MENU/SETTINGS/MOTION/SLEEP/SYSTEM_SETTINGS.
int         menuItemCount(MenuScreen screen);
const char* menuItemLabel(MenuScreen screen, int idx);
void        menuItemValueText(MenuScreen screen, int idx, char* buf, size_t bufsize);
