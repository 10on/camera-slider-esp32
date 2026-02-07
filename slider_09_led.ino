// slider_09_led.ino — LED patterns via PCF8574

static LedPattern currentLedPattern = LED_OFF;
static unsigned long ledLastToggle = 0;
static bool ledState = false;

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
    // Ensure all other pins stay HIGH (pull-up for inputs)
    pcfOutputState |= 0xFC;  // P2-P7 HIGH
    pcfOutputState |= 0x01;  // P0 HIGH
    // pcfPoll() will write this on next loop
  }
}
