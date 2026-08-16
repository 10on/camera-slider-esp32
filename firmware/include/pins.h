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
#define ENDSTOP_1 36
#define ENDSTOP_2 39

// ── LEDs, direct GPIO drive through series resistor ──
// Physical colours on the assembled enclosure (there is also a green LED, which is wired
// straight to the power rail in hardware and is not driven by firmware at all):
//   GPIO15 = BLUE   -> status: BLE connected / waiting / error / asleep
//   GPIO14 = AMBER  -> battery level
// The two were swapped relative to the original mapping: with status on GPIO14 it was the
// amber LED that blinked while waiting for a BLE connection, which reads as a warning.
#define LED_STATUS  15   // blue
#define LED_BATTERY 14   // amber

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

// ── BLE UUIDs (kept byte/string-identical to the old firmware's protocol) ──
#define SERVICE_UUID   "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define COMMAND_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define SPEED_UUID     "d8de624e-140f-4a22-8594-e2216b84a5f2"
#define POSITION_UUID  "a1b2c3d4-e5f6-4789-a012-3456789abcde"
#define CURRENT_UUID   "f1e2d3c4-b5a6-4978-9012-3456789abcde"
#define CONFIG_UUID    "c0f1g000-0000-0000-0000-00000000c0de"

#define BACKOFF_STEPS 200
