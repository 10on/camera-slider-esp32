// slider_09_led.ino — LED patterns via PCF8574

static LedPattern currentLedPattern = LED_OFF;
static unsigned long ledLastToggle = 0;
static bool ledState = false;

// LED2 (battery indicator)
static bool led2State = true;
static unsigned long led2BlinkTime = 0;

void ledSetPattern(LedPattern pat) {
  currentLedPattern = pat;
}

void ledUpdate() {
  if (!pcfFound) return;

  // Auto-select pattern based on state
  LedPattern pat;
  if (sliderState == STATE_ERROR) {
    pat = LED_FAST_BLINK;
  } else if (sliderState == STATE_SLEEP) {
    pat = LED_OFF;
  } else if (bleConnected) {
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
    // Update PCF8574 output state: P1 is LED
    if (ledState) {
      pcfOutputState |= (1 << PCF_LED);   // LED on (HIGH)
    } else {
      pcfOutputState &= ~(1 << PCF_LED);  // LED off (LOW)
    }
    // Ensure input pins stay HIGH (pull-up)
    pcfOutputState |= 0x7C;  // P2-P6 HIGH (inputs pull-up)
    pcfOutputState |= 0x01;  // P0 HIGH
  }

  // ── LED2: battery indicator ──
  bool newLed2 = led2State;
  int bp = vbatPercent();

  if (bp > 50) {
    // High charge: always on
    newLed2 = true;
  } else if (bp > 10) {
    // Medium: on, blink off once per minute (200ms off)
    if (now - led2BlinkTime > 60000) {
      newLed2 = false;
      led2BlinkTime = now;
    } else if (now - led2BlinkTime > 200) {
      newLed2 = true;
    }
  } else {
    // Low: on, blink off once per 10s (200ms off)
    if (now - led2BlinkTime > 10000) {
      newLed2 = false;
      led2BlinkTime = now;
    } else if (now - led2BlinkTime > 200) {
      newLed2 = true;
    }
  }

  if (newLed2 != led2State) {
    led2State = newLed2;
    if (led2State) {
      pcfOutputState |= (1 << PCF_LED2);
    } else {
      pcfOutputState &= ~(1 << PCF_LED2);
    }
  }
}
