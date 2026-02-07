// slider_01_config.ino — Config struct, NVS load/save

void configDefaults() {
  cfg.speed           = 800;
  cfg.homingSpeed     = 400;
  cfg.motorCurrent    = 800;
  cfg.microsteps      = 32;
  cfg.endstopMode     = ENDSTOP_STOP;
  cfg.rampSteps       = 200;
  cfg.sleepTimeout    = 5;     // minutes
  cfg.adxlSensitivity = 2;    // mid
  cfg.wakeOnMotion    = true;
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
  cfg.savedTravel     = preferences.getLong("travel", 0);
  cfg.savedCenter     = preferences.getLong("center", 0);
  cfg.savedCalibrated = preferences.getBool("calib", false);

  preferences.end();

  // Apply calibration
  if (cfg.savedCalibrated) {
    travelDistance = cfg.savedTravel;
    centerPosition = cfg.savedCenter;
    isCalibrated = true;
    Serial.println("Calibration loaded from NVS");
    Serial.print("  Travel: "); Serial.println(travelDistance);
    Serial.print("  Center: "); Serial.println(centerPosition);
  }

  // Apply speed
  stepInterval = cfg.speed;
  targetInterval = cfg.speed;

  Serial.println("Config loaded");
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

  preferences.end();
  Serial.println("Config saved");
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

  Serial.println("Calibration saved");
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

  Serial.println("Calibration reset");
}
