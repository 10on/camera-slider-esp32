// buzzer.cpp — passive piezo speaker, driven with ledcWriteTone() (square wave at a
// chosen frequency) rather than a fixed PWM duty, since a bare piezo disc only makes
// sound on an oscillating signal, not a static HIGH/LOW.
#include <Arduino.h>
#include "buzzer.h"
#include "pins.h"
#include "globals.h"

void buzzerInit() {
  ledcSetup(BUZZER_PWM_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CHANNEL);
  ledcWrite(BUZZER_PWM_CHANNEL, 0);
}

static void playTone(uint32_t freqHz, uint16_t ms) {
  if (!cfg.speakerEnabled) return;
  ledcWriteTone(BUZZER_PWM_CHANNEL, freqHz);
  delay(ms);
  ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
}

// A steady single-frequency tone reads as a "beep" once it runs long enough for the ear to
// place a pitch on it. A fast downward frequency sweep, cut short before it settles, reads
// instead as a dry mechanical "tick"/crack -- closer to what the bare piezo does on its own
// when just stepped with DC (see the earlier "clicks off a battery" behavior). Total length
// here is under 3ms, much shorter than the old flat 8ms/3kHz tone.
void buzzerClick() {
  if (!cfg.speakerEnabled) return;
  static const uint16_t sweep[] = {6000, 4500, 3400, 2600, 2000};
  for (uint16_t f : sweep) {
    ledcWriteTone(BUZZER_PWM_CHANNEL, f);
    delayMicroseconds(500);
  }
  ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
}

// Two short notes, low->high ("power up") / high->low ("power down") -- distinct enough
// from buzzerClick()'s single dry tick to read as a deliberate chime rather than an input
// tactile response.
void buzzerBootChime() {
  if (!cfg.speakerEnabled) return;
  playTone(1568, 60);   // G6
  delay(20);
  playTone(2093, 90);   // C7
}

// Two quick identical beeps -- "starting", distinct from both the boot chime (rising pitch)
// and the shutdown chime (three beeps below).
void buzzerOtaStartChime() {
  if (!cfg.speakerEnabled) return;
  for (int i = 0; i < 2; i++) {
    playTone(2600, 100);
    delay(90);
  }
}

// Three quick identical beeps -- "all good, done" confirmation, not a wind-down melody
// (which read as unfinished/uncertain when this played before an OTA reboot). Long enough
// per beep (120ms) for a small piezo disc to actually ring up to audible volume -- the
// first cut at 50ms/60ms-gap was too short to resolve as three distinct beeps by ear.
void buzzerShutdownChime() {
  if (!cfg.speakerEnabled) return;
  for (int i = 0; i < 3; i++) {
    playTone(2600, 120);
    delay(100);
  }
}
