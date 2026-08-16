// config.h — Config struct load/save (NVS, namespace "slider").
#pragma once

#include <Arduino.h>

uint32_t speedToInterval(int32_t level);  // 1-100 -> 5000..100 us/step, linear

void configLoad();
void configSave();
void configSaveCalibration();
void configResetCalibration();
