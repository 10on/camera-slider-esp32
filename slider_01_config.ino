// slider_01_config.ino — Config struct, NVS load/save

// Speed level (1-100) → µs/step for timer
// Level 1 = 5000µs (slowest), Level 100 = 100µs (fastest)
uint32_t speedToInterval(int32_t level) {
  return map(constrain(level, 1, 100), 1, 100, 5000, 100);
}

void configDefaults() {
  cfg.speed           = 50;   // 1-100%, 50 = ~2550µs/step
  cfg.homingSpeed     = 400;  // µs/step (internal, not user-facing)
  cfg.motorCurrent    = 500;
  cfg.microsteps      = 32;
  cfg.endstopMode     = ENDSTOP_STOP;
  cfg.rampSteps       = 200;
  cfg.sleepTimeout    = 5;     // minutes
  cfg.adxlSensitivity = 2;    // mid
  cfg.wakeOnMotion    = true;
  // WiFi default OFF; enable manually in menu
  cfg.wifiEnabled     = false;
  cfg.wifiSel         = -1;   // Auto
  cfg.savedHome       = 0;
  cfg.savedTravel     = 0;
  cfg.savedCenter     = 0;
  cfg.savedCalibrated = false;
}

void configLoad() {
  configDefaults();

  preferences.begin("slider", true);  // read-only

  cfg.speed           = preferences.getUShort("speed", cfg.speed);
  cfg.homingSpeed     = preferences.getUShort("homSpd", cfg.homingSpeed);
  cfg.motorCurrent    = preferences.getUShort("current", cfg.motorCurrent);
  cfg.microsteps      = preferences.getUChar("usteps", cfg.microsteps);
  cfg.endstopMode     = preferences.getUChar("endMode", cfg.endstopMode);
  cfg.rampSteps       = preferences.getUShort("ramp", cfg.rampSteps);
  cfg.sleepTimeout    = preferences.getUShort("sleepTo", cfg.sleepTimeout);
  cfg.adxlSensitivity = preferences.getUChar("adxlSens", cfg.adxlSensitivity);
  cfg.wakeOnMotion    = preferences.getBool("wakeMotn", cfg.wakeOnMotion);
  cfg.wifiEnabled     = preferences.getBool("wifiEn", cfg.wifiEnabled);
  cfg.wifiSel         = preferences.getShort("wifiSel", -1);
  cfg.savedHome       = preferences.getLong("homePos", 0);
  cfg.savedTravel     = preferences.getLong("travel", 0);
  cfg.savedCenter     = preferences.getLong("center", 0);
  cfg.savedCalibrated = preferences.getBool("calib", false);

  preferences.end();

  // Apply calibration
  if (cfg.savedCalibrated) {
    travelDistance = cfg.savedTravel;
    centerPosition = cfg.savedCenter;
    isCalibrated = true;
    
  }

  // Apply speed
  stepInterval = speedToInterval(cfg.speed);
  targetInterval = speedToInterval(cfg.speed);

  
}

void configSave() {
  preferences.begin("slider", false);  // read-write

  preferences.putUShort("speed", cfg.speed);
  preferences.putUShort("homSpd", cfg.homingSpeed);
  preferences.putUShort("current", cfg.motorCurrent);
  preferences.putUChar("usteps", cfg.microsteps);
  preferences.putUChar("endMode", cfg.endstopMode);
  preferences.putUShort("ramp", cfg.rampSteps);
  preferences.putUShort("sleepTo", cfg.sleepTimeout);
  preferences.putUChar("adxlSens", cfg.adxlSensitivity);
  preferences.putBool("wakeMotn", cfg.wakeOnMotion);
  preferences.putBool("wifiEn", cfg.wifiEnabled);
  preferences.putShort("wifiSel", cfg.wifiSel);
  preferences.putLong("homePos", cfg.savedHome);

  preferences.end();
  
}

void configSaveCalibration() {
  preferences.begin("slider", false);

  preferences.putLong("travel", travelDistance);
  preferences.putLong("center", centerPosition);
  preferences.putBool("calib", true);

  preferences.end();

  cfg.savedTravel = travelDistance;
  cfg.savedCenter = centerPosition;
  cfg.savedCalibrated = true;

  
}

void configResetCalibration() {
  preferences.begin("slider", false);

  preferences.putLong("travel", 0);
  preferences.putLong("center", 0);
  preferences.putBool("calib", false);

  preferences.end();

  travelDistance = 0;
  centerPosition = 0;
  isCalibrated = false;
  cfg.savedCalibrated = false;

  
}
