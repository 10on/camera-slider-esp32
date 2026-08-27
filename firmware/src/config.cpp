// config.cpp — Config struct, NVS load/save.
// Ported from slider_01_config.ino with schema changes: dropped wakeOnMotion (dead in old
// firmware, never wired to a wake trigger) and wifiEnabled/wifiSel (WiFi/OTA out of scope
// for this port); motorCurrent default fixed to 800mA (old default of 500mA contradicted
// both docs/07_ui_kit.md and the hardware-validated bring-up firmware); homingSpeed kept
// persisted as before, now also exposed in the Motion Settings menu (menu.cpp).
//
// bleEnabled/wifiMode/staSsid/staPass/apPass added later for the Wireless Settings menu.
// wifi_env.h's WIFI_CREDENTIALS[] (gitignored, pre-flash-time convenience) is only
// consulted here, once, to seed NVS on the very first boot (cfg.staSsid still empty) --
// after that NVS is the sole source of truth and the menu is how you change networks.
#include <string.h>
#include "config.h"
#include "globals.h"

#if __has_include("wifi_env.h")
#include "wifi_env.h"  // defines WIFI_CREDENTIALS[] / WIFI_CREDENTIALS_COUNT (gitignored)
#else
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_CREDENTIALS[] = {};
static const size_t WIFI_CREDENTIALS_COUNT = 0;
#endif

// Speed level (1-100) -> us/step for the timer. Level 1 = 5000us (slowest), level 100 =
// 100us (fastest).
//
// The map is geometric, not linear. Perceived speed is step *frequency* (1/interval); a
// linear interval ramp (the old `map(level,1,100,5000,100)`) barely moved the frequency
// until the last few percent, then shot up 25x between 96% and 100%. Here every +1%
// multiplies the frequency by a constant ratio (~4%), so the dial feels linear end to end:
//   level 1  -> 5000us (~200 steps/s)
//   level 25 -> ~2000us (~500 steps/s)
//   level 50 -> ~810us  (~1240 steps/s)
//   level 75 -> ~325us  (~3070 steps/s)
//   level 100 -> 100us  (10000 steps/s)
static const float kSpeedSlowUs = 5000.0f;
static const float kSpeedFastUs = 100.0f;

uint32_t speedToInterval(int32_t level) {
  float t = (constrain(level, 1, 100) - 1) / 99.0f;
  return (uint32_t)lroundf(kSpeedSlowUs * powf(kSpeedFastUs / kSpeedSlowUs, t));
}

uint16_t intervalToSpeed(uint32_t intervalUs) {
  float ratio = constrain(intervalUs, 100UL, 5000UL) / kSpeedSlowUs;
  float t = logf(ratio) / logf(kSpeedFastUs / kSpeedSlowUs);
  return (uint16_t)constrain((long)lroundf(1.0f + t * 99.0f), 1L, 100L);
}

static void configDefaults() {
  cfg.speed           = 50;    // 1-100%, 50 = ~2550us/step
  cfg.homingSpeed      = 400;   // us/step
  cfg.motorCurrent    = 800;
  cfg.microsteps      = 32;
  cfg.endstopMode     = ENDSTOP_STOP;
  cfg.rampSteps       = 200;
  cfg.sleepTimeout    = 5;     // minutes
  cfg.adxlSensitivity = 2;     // mid
  cfg.savedHome       = 0;
  cfg.savedTravel     = 0;
  cfg.savedCenter     = 0;
  cfg.savedCalibrated = false;

  cfg.bleEnabled = true;
  cfg.wifiMode   = WIFI_CFG_OFF;
  cfg.staSsid[0] = 0;
  cfg.staPass[0] = 0;
  strncpy(cfg.apPass, "slider1234", sizeof(cfg.apPass) - 1);
  cfg.apPass[sizeof(cfg.apPass) - 1] = 0;

  cfg.theme = THEME_DARK;
  cfg.brightness = 80;  // ~= the old hardcoded DEFAULT_BACKLIGHT (200/255)

  cfg.speakerEnabled = true;

  cfg.pingPongStart = 0;  // Center
}

void configLoad() {
  configDefaults();

  preferences.begin("slider", true);  // read-only

  cfg.speed           = preferences.getUShort("speed", cfg.speed);
  cfg.homingSpeed      = preferences.getUShort("homSpd", cfg.homingSpeed);
  cfg.motorCurrent    = preferences.getUShort("current", cfg.motorCurrent);
  cfg.microsteps      = preferences.getUChar("usteps", cfg.microsteps);
  cfg.endstopMode     = preferences.getUChar("endMode", cfg.endstopMode);
  cfg.rampSteps       = preferences.getUShort("ramp", cfg.rampSteps);
  cfg.sleepTimeout    = preferences.getUShort("sleepTo", cfg.sleepTimeout);
  cfg.adxlSensitivity = preferences.getUChar("adxlSens", cfg.adxlSensitivity);
  cfg.savedHome       = preferences.getLong("homePos", 0);
  cfg.savedTravel     = preferences.getLong("travel", 0);
  cfg.savedCenter     = preferences.getLong("center", 0);
  cfg.savedCalibrated = preferences.getBool("calib", false);

  cfg.bleEnabled = preferences.getBool("bleEn", cfg.bleEnabled);
  cfg.wifiMode   = preferences.getUChar("wifiMode", cfg.wifiMode);
  preferences.getString("staSsid", cfg.staSsid, sizeof(cfg.staSsid));
  preferences.getString("staPass", cfg.staPass, sizeof(cfg.staPass));
  preferences.getString("apPass", cfg.apPass, sizeof(cfg.apPass));
  if (cfg.apPass[0] == 0) strncpy(cfg.apPass, "slider1234", sizeof(cfg.apPass) - 1);
  cfg.theme = preferences.getUChar("theme", cfg.theme);
  cfg.brightness = preferences.getUChar("bright", cfg.brightness);
  cfg.speakerEnabled = preferences.getBool("spkEn", cfg.speakerEnabled);
  cfg.pingPongStart = preferences.getUChar("ppStart", cfg.pingPongStart);

  // One-time repair: an earlier build of this seeding logic (before wifiMode was left
  // off by default) could have already persisted wifiMode=STA against the literal
  // unfilled wifi_env.h.example placeholder "YourSSID" -- force it back off so the radio
  // stops retrying a network that was never real.
  if (strcmp(cfg.staSsid, "YourSSID") == 0 && cfg.wifiMode == WIFI_CFG_STA) {
    cfg.wifiMode = WIFI_CFG_OFF;
    cfg.staSsid[0] = 0;
    cfg.staPass[0] = 0;
  }

  preferences.end();

  if (cfg.savedCalibrated) {
    travelDistance = cfg.savedTravel;
    centerPosition = cfg.savedCenter;
    isCalibrated = true;
  }

  // First-ever boot (nothing saved yet): seed the SSID/password from wifi_env.h if
  // present, then persist so NVS becomes authoritative from here on -- wifi_env.h is
  // never read again. Deliberately does NOT flip wifiMode to STA -- WiFi stays off
  // (the configDefaults() default) until the user turns it on from the Wireless
  // Settings menu themselves. Radios should never self-activate at boot without an
  // explicit action; this also avoids the device endlessly retrying a bogus/stale
  // network if wifi_env.h was never filled in with real credentials.
  if (cfg.staSsid[0] == 0 && WIFI_CREDENTIALS_COUNT > 0) {
    strncpy(cfg.staSsid, WIFI_CREDENTIALS[0].ssid, sizeof(cfg.staSsid) - 1);
    strncpy(cfg.staPass, WIFI_CREDENTIALS[0].pass, sizeof(cfg.staPass) - 1);
    configSave();
  }

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
  preferences.putLong("homePos", cfg.savedHome);

  preferences.putBool("bleEn", cfg.bleEnabled);
  preferences.putUChar("wifiMode", cfg.wifiMode);
  preferences.putString("staSsid", cfg.staSsid);
  preferences.putString("staPass", cfg.staPass);
  preferences.putString("apPass", cfg.apPass);
  preferences.putUChar("theme", cfg.theme);
  preferences.putUChar("bright", cfg.brightness);
  preferences.putBool("spkEn", cfg.speakerEnabled);
  preferences.putUChar("ppStart", cfg.pingPongStart);

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
