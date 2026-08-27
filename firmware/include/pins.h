// pins.h — GPIO/UART/SPI/I2C pin map for the ESP32-WROOM-32 slider-v2 hardware.
// All values confirmed working on real hardware via firmware/src/selftest_main.cpp.
#pragma once

// ── TFT (VSPI) ──
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   13   // PWM -> PNP (A1015) base, backlight high-side switch, active-low
#define BL_PWM_CHANNEL 0

// ── Encoder (EC11), internal pull-ups ──
#define ENC_CLK  32
#define ENC_DT   33
#define ENC_SW   19

// ── Extra buttons + endstops, input-only pins, external 10k pull-up to 3V3 required ──
#define BTN1      35
#define BTN2      34
#define ENDSTOP_1 39
#define ENDSTOP_2 36

// ── Buzzer (bare piezo disc -- no driver transistor needed, drives straight off GPIO) ──
#define BUZZER_PIN         12
// NOT channel 1: ESP32 ledc shares one hardware timer per channel pair (timer = chan/2 %
// 4), so channel 1 would share BL_PWM_CHANNEL's (0) timer. ledcWriteTone() reconfigures
// that timer to 10-bit resolution for the tone frequency, which silently wrecked the
// backlight's 8-bit duty calibration the moment the buzzer ever played a click. Channel 2
// sits on timer 1, fully independent of the backlight's timer 0.
#define BUZZER_PWM_CHANNEL 2

// ── LEDs, direct GPIO drive through series resistor ──
// Physical colours on the assembled enclosure (there is also a green LED, which is wired
// straight to the power rail in hardware and is not driven by firmware at all):
//   GPIO14 = AMBER  -> status: BLE connected / waiting / error / asleep
//   GPIO15 = BLUE   -> battery level
// Swapped back from an earlier mapping (status on GPIO15/blue, battery on GPIO14/amber) --
// confirmed on hardware that GPIO14/GPIO15 drive the opposite physical colours from that.
#define LED_STATUS  14   // amber
#define LED_BATTERY 15   // blue
// PWM channels for the smooth WiFi-active crossfade (see led.cpp) -- both land on ledc
// timer 2 (chan/2%4), independent of the backlight's timer 0 and the buzzer's timer 1.
#define LED_STATUS_PWM_CHANNEL  4
#define LED_BATTERY_PWM_CHANNEL 5

// ── Motor (TMC2209) ──
#define MOTOR_EN   27
#define MOTOR_STEP 26
#define MOTOR_DIR  25
#define TMC_TX     17   // ESP32 -> TMC2209 RX
#define TMC_RX     16   // ESP32 <- TMC2209 TX
#define R_SENSE     0.11f
#define DRIVER_ADDR 0b00

// ── I2C bus (ADXL345 + INA226) ──
#define I2C_SDA 21
#define I2C_SCL 22

// ── ADXL345 registers ──
#define REG_DEVID       0x00
#define REG_BW_RATE     0x2C
#define REG_POWER_CTL   0x2D
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0      0x32

// ── Battery percentage formula (from INA226 bus voltage, 3S LiPo) ──
#define VBAT_FULL  12.6f
#define VBAT_EMPTY  9.0f

// A charger connected on top of the battery drives the same rail INA226 measures on, well
// above what a 3S pack can reach on its own (max 12.6V) -- confirmed on hardware at ~14.8V+
// while charging. Comfortably above VBAT_FULL, comfortably below the observed charging
// voltage, so this is a safe threshold either way.
#define VBAT_CHARGING_THRESHOLD 13.0f

// ── BLE UUIDs (kept byte/string-identical to the old firmware's protocol) ──
#define SERVICE_UUID   "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define COMMAND_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define SPEED_UUID     "d8de624e-140f-4a22-8594-e2216b84a5f2"
#define POSITION_UUID  "a1b2c3d4-e5f6-4789-a012-3456789abcde"
#define CURRENT_UUID   "f1e2d3c4-b5a6-4978-9012-3456789abcde"
#define CONFIG_UUID    "c0f1g000-0000-0000-0000-00000000c0de"
#define PROGRAM_UUID   "c0f1c001-0000-4000-8000-00000000c0de"

#define BACKOFF_STEPS 200
