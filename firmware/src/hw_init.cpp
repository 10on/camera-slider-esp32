// hw_init.cpp — shared bus bring-up, copied from the validated selftest_main.cpp setup().
#include <Wire.h>
#include <SPI.h>
#include "hw_init.h"
#include "pins.h"
#include "globals.h"

void hwInit() {
  pinMode(LED_STATUS,  OUTPUT);
  pinMode(LED_BATTERY, OUTPUT);

  Wire.begin(I2C_SDA, I2C_SCL);

  // Claim VSPI bus manually, MISO redirected to an unused pin (GPIO12): passing -1 here
  // does NOT disable MISO -- esp32-hal-spi.c falls back to the VSPI default (GPIO19) when
  // miso<0, which would silently strip ENC_SW's pull-up and attach it to the SPI matrix.
  // GPIO12 is otherwise unused; its strapping function only matters during boot sampling,
  // not for a runtime pinMode() change, so parking MISO there is safe.
  SPI.begin(18 /*SCK*/, 12 /*MISO -> unused pin, NOT GPIO19*/, 23 /*MOSI*/, TFT_CS);

  // Backlight: PNP (A1015) high-side switch, active-low PWM. This core's ledc API is the
  // old-style ledcSetup/ledcAttachPin/ledcWrite(channel,...), not the 3.x ledcAttach().
  ledcSetup(BL_PWM_CHANNEL, 5000, 8);   // 5kHz PWM, 8-bit duty
  ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
  backlightSetBrightness(cfg.brightness);
}

void backlightOff() {
  ledcWrite(BL_PWM_CHANNEL, 255);  // active-low: 255 = 0% duty low-time = fully off
}

void backlightOn() {
  backlightSetBrightness(cfg.brightness);
}

void backlightSetBrightness(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  uint8_t duty = (uint16_t)pct * 255 / 100;
  ledcWrite(BL_PWM_CHANNEL, 255 - duty);  // active-low PNP high-side switch
}
