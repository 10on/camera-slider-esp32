// config.h — Config struct load/save (NVS, namespace "slider").
#pragma once

#include <Arduino.h>

uint32_t speedToInterval(int32_t level);  // 1-100 -> 5000..100 us/step, geometric
uint16_t intervalToSpeed(uint32_t intervalUs);  // 100..5000 us/step -> 1..100, geometric

void configLoad();
void configSave();
void configSaveCalibration();
void configResetCalibration();
