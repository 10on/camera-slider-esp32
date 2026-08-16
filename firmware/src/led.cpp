// led.cpp — LED pattern engine, ported from slider_09_led.ino, PCF8574 bit-writes replaced
// with direct digitalWrite(LED_STATUS/LED_BATTERY, ...). Battery thresholds now driven by
// power.cpp's batteryPercent() (INA226) instead of the old ADC-based vbatPercent().
//
// LED_STATUS is the BLUE LED (GPIO15), LED_BATTERY the AMBER one (GPIO14) -- see pins.h.
// The green LED on the panel is hardwired to the power rail and never touched here.
#include "led.h"
#include "pins.h"
#include "power.h"

static LedPattern    currentLedPattern = LED_OFF;
static unsigned long ledLastToggle = 0;
static bool          ledState = false;

static bool          led2State = true;
static unsigned long led2BlinkTime = 0;

void ledSetPattern(LedPattern pat) {
  currentLedPattern = pat;
}

void ledUpdate() {
  // Diagnostics screen: drive both LEDs straight off the endstops, the way the selftest
  // sketch did, so they can be eyeballed by triggering the endstops. Handled here rather
  // than in the menu so these two GPIOs keep exactly one writer.
  if (currentScreen == SCREEN_DIAGNOSTICS) {
    digitalWrite(LED_STATUS,  endstop1 ? HIGH : LOW);  // blue  <- endstop 1
    digitalWrite(LED_BATTERY, endstop2 ? HIGH : LOW);  // amber <- endstop 2
    ledState = endstop1;   // keep the pattern engine's cached state in sync so it doesn't
    led2State = endstop2;  // skip the write that restores normal behaviour on exit
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
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);  // blue
  }

  // ── LED2 (amber): battery indicator ──
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
    digitalWrite(LED_BATTERY, led2State ? HIGH : LOW);  // amber
  }
}
