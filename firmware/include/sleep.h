// sleep.h — sleep timeout, drift-based safe parking, wake.
#pragma once

void sleepCheck();          // call every loop(); enters sleep after timeout while IDLE
void sleepCheckWake();      // call every loop(); wakes on encoder/BTN1/BTN2/BLE activity
void sleepParkAndEnter();
void sleepEnter();
void sleepWake();
