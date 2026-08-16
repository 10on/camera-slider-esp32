// input.cpp — encoder + BTN1/BTN2 + endstops on direct GPIO.
// Quadrature ISR + debounce pattern reused verbatim from the validated
// firmware/src/selftest_main.cpp; extended here with BTN2 short-vs-800ms-long press
// timing (mirrors the old encoder-button 500ms long-press logic from
// slider_05_endstops.ino, just retargeted to BTN2 and at the new 800ms threshold).
//
// Endstops are read active-HIGH here (activeLow=false below), matching what was
// actually confirmed correct on this hardware's wiring during bring-up (not the
// pull-up-to-GND assumption originally sketched on paper) -- see selftest_main.cpp's
// es1In/es2In.
#include "input.h"
#include "pins.h"
#include "globals.h"

// ── Encoder quadrature (ISR) ──
// The ISR counts every quadrature transition, so a standard EC11 detent (one physical
// click) produces COUNTS_PER_DETENT raw counts. The old firmware decoded a single edge
// only (CLK falling, direction from DT), which happened to yield exactly 1 count per
// click; carrying the full-quadrature decode over unchanged made every click step the
// menu by 4 items and made the encoder twitch on sub-click movement. Keep the full
// decode (it rejects contact bounce/invalid transitions that single-edge decoding would
// happily count) and divide down to detents in inputPoll() instead -- sub-detent jitter
// then simply never reaches the menu.
static const int32_t COUNTS_PER_DETENT = 4;

static volatile int32_t encoderCount  = 0;
static volatile uint8_t lastEncoded   = 0;
static int32_t lastReadEncoderCount   = 0;

void IRAM_ATTR encoderISR() {
  uint8_t msb = digitalRead(ENC_CLK);
  uint8_t lsb = digitalRead(ENC_DT);
  uint8_t encoded = (msb << 1) | lsb;
  uint8_t sum = (lastEncoded << 2) | encoded;

  // Signs swapped vs the selftest sketch so that clockwise moves the menu selection down
  // / values up, matching how this encoder is physically mounted in the enclosure.
  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCount--;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderCount++;

  lastEncoded = encoded;
}

// ── Debounced short-press inputs (encoder SW, BTN1) ──
struct DebouncedInput {
  uint8_t  pin;
  bool     activeLow;
  bool     state      = false;
  bool     lastRaw    = false;
  unsigned long lastChange = 0;

  DebouncedInput(uint8_t p, bool al) : pin(p), activeLow(al) {}
};

static DebouncedInput encSwIn(ENC_SW, true);
static DebouncedInput btn1In(BTN1, true);
static DebouncedInput es1In(ENDSTOP_1, false);
static DebouncedInput es2In(ENDSTOP_2, false);

static const unsigned long DEBOUNCE_MS = 30;

static bool updateDebounced(DebouncedInput& in) {
  bool raw = digitalRead(in.pin) == (in.activeLow ? LOW : HIGH);
  if (raw != in.lastRaw) {
    in.lastChange = millis();
    in.lastRaw = raw;
  }
  bool changed = false;
  if ((millis() - in.lastChange) > DEBOUNCE_MS && in.state != in.lastRaw) {
    in.state = in.lastRaw;
    changed = true;
  }
  return changed;
}

// ── BTN2: short vs 800ms-long press ──
static const unsigned long BTN2_LONG_MS = 800;
static bool          btn2Raw       = false;
static bool          btn2LastRaw   = false;
static unsigned long btn2LastChange = 0;
static bool          btn2State     = false;
static unsigned long btn2PressTime = 0;
static bool          btn2LongFired = false;

static void updateBtn2() {
  btn2Raw = digitalRead(BTN2) == LOW;  // active-low
  if (btn2Raw != btn2LastRaw) {
    btn2LastChange = millis();
    btn2LastRaw = btn2Raw;
  }
  if ((millis() - btn2LastChange) > DEBOUNCE_MS && btn2State != btn2LastRaw) {
    bool newState = btn2LastRaw;
    if (newState && !btn2State) {
      // press edge
      btn2PressTime = millis();
      btn2LongFired = false;
    } else if (!newState && btn2State) {
      // release edge
      if (!btn2LongFired) btn2Pressed = true;  // short press only if long didn't already fire
    }
    btn2State = newState;
  }
  if (btn2State && !btn2LongFired && (millis() - btn2PressTime) > BTN2_LONG_MS) {
    btn2LongPress = true;
    btn2LongFired = true;
  }
}

void inputInit() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  pinMode(BTN1,      INPUT);   // external pull-up required (GPIO35 has none)
  pinMode(BTN2,      INPUT);   // external pull-up required (GPIO34 has none)
  pinMode(ENDSTOP_1, INPUT);   // external pull-up required (GPIO36 has none)
  pinMode(ENDSTOP_2, INPUT);   // external pull-up required (GPIO39 has none)

  lastEncoded = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderISR, CHANGE);
}

int32_t inputEncoderRawCount() { return encoderCount; }

void inputPoll() {
  // Encoder rotation: accumulate into the shared, consumer-cleared encoderDelta
  // (contract matches old slider_05_endstops.ino's encoderPoll(): producer accumulates,
  // menu.cpp's input handler clears it after consuming).
  // Integer division truncates toward zero, so a partial detent contributes nothing and
  // its raw counts stay banked in lastReadEncoderCount until the click completes.
  int32_t rawDelta = encoderCount - lastReadEncoderCount;
  int32_t detents  = rawDelta / COUNTS_PER_DETENT;
  if (detents != 0) {
    encoderDelta += detents;
    lastReadEncoderCount += detents * COUNTS_PER_DETENT;
    lastActivityTime = millis();
  }

  // Encoder button + BTN1: short-press only.
  if (updateDebounced(encSwIn) && encSwIn.state) {
    encoderPressed = true;
    lastActivityTime = millis();
  }
  if (updateDebounced(btn1In) && btn1In.state) {
    btn1Pressed = true;
    lastActivityTime = millis();
  }

  // BTN2: short vs 800ms-long.
  bool prevBtn2Pressed = btn2Pressed;
  bool prevBtn2Long    = btn2LongPress;
  updateBtn2();
  if ((btn2Pressed && !prevBtn2Pressed) || (btn2LongPress && !prevBtn2Long)) {
    lastActivityTime = millis();
  }

  // Endstops: edge-detected, flags cleared each poll (old contract).
  endstop1Rising = false;
  endstop2Rising = false;
  bool prevE1 = endstop1;
  bool prevE2 = endstop2;

  updateDebounced(es1In);
  updateDebounced(es2In);
  endstop1 = es1In.state;
  endstop2 = es2In.state;

  if (endstop1 && !prevE1) endstop1Rising = true;
  if (endstop2 && !prevE2) endstop2Rising = true;
  if (endstop1 != prevE1 || endstop2 != prevE2) displayDirty = true;
}
