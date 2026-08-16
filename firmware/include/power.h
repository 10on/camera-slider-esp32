// power.h — INA226 bus voltage/current (replaces the old resistor-divider ADC battery read).
#pragma once

void powerInit();
void powerRead();          // updates busVoltage_V / current_mA
int  batteryPercent();     // 0-100, same VBAT_EMPTY/VBAT_FULL formula as old vbatPercent()
