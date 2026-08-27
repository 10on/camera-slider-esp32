// led.cpp — LED pattern engine, ported from slider_09_led.ino, PCF8574 bit-writes replaced
// with direct digitalWrite(LED_STATUS/LED_BATTERY, ...). Battery thresholds now driven by
// power.cpp's batteryPercent() (INA226) instead of the old ADC-based vbatPercent().
//
// LED_STATUS is the AMBER LED (GPIO14), LED_BATTERY the BLUE one (GPIO15) -- see pins.h.
// The green LED on the panel is hardwired to the power rail and never touched here.
#include "led.h"
#include "pins.h"
#include "power.h"

static LedPattern    currentLedPattern = LED_OFF;
static unsigned long ledLastToggle = 0;
static bool          ledState = false;

static bool          led2State = true;
static unsigned long led2BlinkTime = 0;

// Both LEDs are plain on/off GPIOs everywhere else in this file (digitalWrite), but once a
// pin is attached to an ledc channel it has to stay on ledcWrite for the rest of its life --
// mixing digitalWrite() back in detaches the channel silently on some cores. duty 0/255
// reproduces the old LOW/HIGH exactly for every non-crossfade write below.
// The blue LED (LED_BATTERY) is noticeably brighter than the amber one at the same duty and
// was glary in a dark room, so cap its "on" level to ~70% instead of soldering in a bigger
// series resistor. LED brightness vs. duty isn't linear, so this is a bit less than a 30%
// perceived drop, but it's the right ballpark and easy to retune here.
static const uint8_t LED_BATTERY_ON_DUTY = 178;

static void writeStatus(bool on)  { ledcWrite(LED_STATUS_PWM_CHANNEL,  on ? 255 : 0); }
static void writeBattery(bool on) { ledcWrite(LED_BATTERY_PWM_CHANNEL, on ? LED_BATTERY_ON_DUTY : 0); }

void ledInit() {
  ledcSetup(LED_STATUS_PWM_CHANNEL, 2000, 8);
  ledcAttachPin(LED_STATUS, LED_STATUS_PWM_CHANNEL);
  ledcSetup(LED_BATTERY_PWM_CHANNEL, 2000, 8);
  ledcAttachPin(LED_BATTERY, LED_BATTERY_PWM_CHANNEL);
}

void ledSetPattern(LedPattern pat) {
  currentLedPattern = pat;
}

// While WiFi is on (used for OTA pushes), the status LED's usual "waiting for BLE" slow
// blink turns on regardless -- BLE gets shut down for the coexistence fix in wifiApply()
// the moment WiFi comes up, which reads as bleConnected==false, cfg.bleEnabled==true, i.e.
// exactly the slow-blink condition. A hard on/off blink there read as an error/distress
// signal rather than "radio busy, this is normal" -- a smooth crossfade between the two
// LEDs reads as deliberate activity instead.
static void ledUpdateWifiActive() {
  const unsigned long periodMs = 1600;
  float phase = (millis() % periodMs) / (float)periodMs;      // 0..1
  float amberFrac = (cosf(phase * 2.0f * PI) + 1.0f) / 2.0f;   // 0..1, smooth triangle-ish
  ledcWrite(LED_STATUS_PWM_CHANNEL,  (uint32_t)(amberFrac * 255));
  ledcWrite(LED_BATTERY_PWM_CHANNEL, (uint32_t)((1.0f - amberFrac) * LED_BATTERY_ON_DUTY));
  ledState = amberFrac > 0.5f;
  led2State = !ledState;
}

void ledUpdate() {
  // Diagnostics screen: drive both LEDs straight off the endstops, the way the selftest
  // sketch did, so they can be eyeballed by triggering the endstops. Handled here rather
  // than in the menu so these two GPIOs keep exactly one writer.
  if (currentScreen == SCREEN_DIAGNOSTICS) {
    writeStatus(endstop1);   // amber <- endstop 1
    writeBattery(endstop2);  // blue  <- endstop 2
    ledState = endstop1;   // keep the pattern engine's cached state in sync so it doesn't
    led2State = endstop2;  // skip the write that restores normal behaviour on exit
    return;
  }

  if (cfg.wifiMode != WIFI_CFG_OFF) {
    ledUpdateWifiActive();
    return;
  }

  LedPattern pat;
  if (sliderState == STATE_ERROR) {
    pat = LED_FAST_BLINK;
  } else if (sliderState == STATE_SLEEP) {
    pat = LED_OFF;
  } else if (bleConnected) {
    pat = LED_ON;
  } else if (!cfg.bleEnabled) {
    // Slow blink means "waiting for a BLE connection". With the radio switched off in
    // Wireless Settings there is nothing to wait for, so blinking is just noise -- hold it
    // solid instead, where it reads as a plain "powered up and idle" indicator.
    pat = LED_ON;
  } else {
    pat = LED_SLOW_BLINK;
  }

  bool newLedState = ledState;
  unsigned long now = millis();

  switch (pat) {
    case LED_OFF:
      newLedState = false;
      break;
    case LED_ON:
      newLedState = true;
      break;
    case LED_SLOW_BLINK:
      if (now - ledLastToggle > 1000) {
        newLedState = !ledState;
        ledLastToggle = now;
      }
      break;
    case LED_FAST_BLINK:
      if (now - ledLastToggle > 200) {
        newLedState = !ledState;
        ledLastToggle = now;
      }
      break;
    case LED_PULSE:
      if (now - ledLastToggle > 500) {
        newLedState = !ledState;
        ledLastToggle = now;
      }
      break;
  }

  if (newLedState != ledState) {
    ledState = newLedState;
    writeStatus(ledState);  // amber
  }

  // ── LED2 (blue): battery indicator ──
  bool newLed2 = led2State;
  int bp = batteryPercent();

  if (bp > 50) {
    newLed2 = true;
  } else if (bp > 10) {
    if (now - led2BlinkTime > 60000) {
      newLed2 = false;
      led2BlinkTime = now;
    } else if (now - led2BlinkTime > 200) {
      newLed2 = true;
    }
  } else {
    if (now - led2BlinkTime > 10000) {
      newLed2 = false;
      led2BlinkTime = now;
    } else if (now - led2BlinkTime > 200) {
      newLed2 = true;
    }
  }

  if (newLed2 != led2State) {
    led2State = newLed2;
    writeBattery(led2State);  // blue
  }
}
