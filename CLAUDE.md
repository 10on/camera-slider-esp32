# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based camera slider controller firmware. Controls a motorized camera slider using a TMC2209 stepper motor driver with:
- BLE (Bluetooth Low Energy) remote control
- OLED display (SSD1306 128x64) with rotary encoder for on-device UI
- Two endstop sensors via PCF8574 I/O expander
- ADXL345 accelerometer for drift detection and safe parking
- Battery voltage monitoring via ADC
- Persistent configuration in ESP32 NVS flash

## Build & Upload

Multi-file Arduino sketch targeting ESP32. All `slider*.ino` files in the root directory are compiled as a single sketch by the Arduino toolchain.

**Platform:** Arduino IDE (ESP32 Arduino core) or PlatformIO. Board: ESP32 Dev Module.

**Required libraries:**
- TMCStepper
- Adafruit GFX Library
- Adafruit SSD1306
- PCF8574
- ESP32 BLE Arduino (BLEDevice)
- Preferences (built into ESP32 core)
- Wire (built into ESP32 core)

**Serial monitor:** 115200 baud for debug output.

**Important:** All `.ino` files must be in the same directory. Arduino IDE concatenates them in alphabetical order (hence the numeric prefixes `slider_01_`, `slider_02_`, etc.).

No automated test suite, CI, or linting configured. Verification is done by compiling and testing on hardware.

## Code Structure

### Active firmware (root directory)

| File | Purpose |
|------|---------|
| `slider.ino` | Entry point: includes, pin defines, global state, enums, `setup()`, `loop()` |
| `slider_01_config.ino` | `Config` struct, NVS load/save, calibration persistence, `speedToInterval()` |
| `slider_02_hw.ino` | Hardware init: TMC2209 UART, I2C bus, PCF8574/OLED/ADXL345 autodetect, battery ADC |
| `slider_03_motor.ino` | Hardware timer ISR (`onStepTimer`), step generation, linear accel/decel ramp |
| `slider_04_state.ino` | State machine transitions, BLE command processing, endstop hit handling, error management |
| `slider_05_endstops.ino` | PCF8574 single-transaction polling, endstop edge detection, encoder reading |
| `slider_06_homing.ino` | Non-blocking homing sub-automaton (6 phases) |
| `slider_07_adxl.ino` | ADXL345 axis reading, blocking drift detection (`adxlCheckDrift`) |
| `slider_08_ble.ino` | BLE service setup, characteristic callbacks, 11-byte status notify |
| `slider_09_led.ino` | LED pattern engine via PCF8574, LED2 battery indicator |
| `slider_10_display.ino` | OLED screen rendering: main screen, menus, value editor, calibration progress |
| `slider_11_menu.ino` | Encoder input routing, menu navigation, value editor callbacks |
| `slider_12_sleep.ino` | Sleep/wake management with safe parking (ADXL drift check before sleep) |

### Documentation (`docs/`)

- `01_hardware.md` — Pin map, I2C addresses, PCF8574 layout, TMC2209/ADXL345 config
- `02_logic.md` — Logic description of the old monolithic implementation
- `03_ble_protocol.md` — BLE protocol spec (v1 format, 9-byte status)
- `04_system(2).md` — System architecture spec: state machine, safety, ramp, parking, sleep
- `05_ui_menu(1).md` — UI/menu specification
- `06_questions.md` — Design decisions and Q&A on implementation choices

**Note:** Some docs describe the old architecture or planned specs. The actual `slider*.ino` files are the source of truth.

### Legacy code (`old/`)

- `main_old.ino` — Original monolithic implementation (~490 lines, blocking homing, no display/encoder)
- `pcf_encoder/pcf_encoder.ino` — Hardware test: encoder + endstops via PCF8574
- `encoder_stepper_test/encoder_stepper_test.ino` — Encoder + stepper test
- `adxl345_test/adxl345_test.ino` — ADXL345 accelerometer test

## Architecture

### State Machine

Defined as `SliderState` enum in `slider.ino`:

```
IDLE → MANUAL_MOVING  (BLE F/B or encoder)
IDLE → MOVING_TO_POS  (BLE position write or encoder Go To)
IDLE → HOMING         (BLE H command or encoder Calibration)
IDLE → SLEEP          (inactivity timeout)
any moving state → ERROR   (BLE disconnect during movement)
IDLE/SLEEP → PARKING       (ADXL drift detected)
```

Priority (highest first): ERROR > HOMING > PARKING > MOVING_TO_POS > MANUAL_MOVING > IDLE/SLEEP

Error codes: `ERR_NONE`, `ERR_BLE_LOST`, `ERR_HOMING_FAIL`, `ERR_ENDSTOP_UNEXPECTED`

### Motor Control (Real-Time)

Step generation uses ESP32 hardware timer ISR (`onStepTimer` in `slider_03_motor.ino`). This is the **only** source of STEP pulses — UI, BLE, and `loop()` never directly toggle STEP/DIR.

- Linear acceleration/deceleration ramp (configurable `rampSteps`)
- Speed: 1–100% scale mapped via `speedToInterval()` (1% = 5000µs/step, 100% = 100µs/step)
- `motorStart()` — constant speed; `motorStartRamp()` — with accel ramp; `motorMoveTo()` — absolute position with ramp
- `motorStop()` — deceleration ramp then stop; `motorStopNow()` — immediate stop

### Main Loop Order

1. PCF8574 poll (single I2C write+read)
2. Endstop edge detection
3. Encoder rotation + button
4. Sleep wake check
5. State machine transitions (processes BLE commands)
6. Homing sub-automaton
7. Battery voltage read (every 5s, motor idle only)
8. BLE status notify (every 100ms)
9. LED pattern update
10. Display update (every 50ms if dirty)
11. Menu input handling
12. Sleep timeout check

### Endstop Modes

Global setting (`cfg.endstopMode`): STOP (halt), BOUNCE (reverse direction), PARK (push into endstop then disable driver). Movement commands are blocked if the destination endstop is already triggered.

### Homing Procedure (Non-Blocking)

Phase-based in `slider_06_homing.ino`:
1. `HOME_SEEK_END1` — move backward until endstop 1
2. `HOME_BACKOFF1` — back off 200 steps, reset position to 0
3. `HOME_SEEK_END2` — move forward until endstop 2, measure travel
4. `HOME_BACKOFF2` — back off 200 steps
5. `HOME_GO_CENTER` — move to travel/2
6. `HOME_DONE` — save calibration to NVS

### Sleep & Safe Parking

Before sleeping: disable motor → check ADXL for 3s → if drifting, park to nearest endstop → retry once → then disable OLED, LEDs, motor. Wake triggers: encoder activity, BLE connection.

### BLE Protocol

Device name: `Camera_Slider`. Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

| Characteristic | Type | Format |
|---|---|---|
| Command (`beb5483e-...`) | WRITE | 1 byte: `F`/`B`/`S`/`H`/`R` |
| Speed (`d8de624e-...`) | WRITE | uint16 LE, 1–100 (%) |
| Position (`a1b2c3d4-...`) | WRITE | int32 LE, steps (requires calibration) |
| Current (`f1e2d3c4-...`) | WRITE | uint16 LE, mA (200–1500) |
| Status (`1c95d5e3-...`) | READ/NOTIFY | 11 bytes v2 |

Status v2 (11 bytes, notified every 100ms):
- `[0–3]` currentPosition int32 LE
- `[4]` flags: bit0=E1, bit1=E2, bit2=HOMING, bit3=CAL, bit4=GO_TO, bit5=MOVING, bit6=PARK
- `[5–8]` travelDistance int32 LE
- `[9]` state enum (uint8)
- `[10]` errorCode (uint8)

### NVS Persistent Storage

Namespace: `"slider"`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `speed` | uint16 | 50 | Speed 1–100% |
| `homSpd` | uint16 | 400 | Homing speed µs/step |
| `current` | uint16 | 800 | Motor current mA |
| `usteps` | uint8 | 32 | Microsteps (1–256, powers of 2) |
| `endMode` | uint8 | 0 | Endstop mode: 0=STOP, 1=BOUNCE, 2=PARK |
| `ramp` | uint16 | 200 | Accel/decel ramp steps |
| `sleepTo` | uint16 | 5 | Sleep timeout minutes (0=disabled) |
| `adxlSens` | uint8 | 2 | ADXL sensitivity: 0=off, 1=low, 2=mid, 3=high |
| `wakeMotn` | bool | true | Wake on motion |
| `travel` | int32 | 0 | Calibrated travel distance |
| `center` | int32 | 0 | Calibrated center position |
| `calib` | bool | false | Calibration valid |

Changing microsteps resets calibration (requires new homing).

## Hardware Pin Map

### ESP32 GPIO

| GPIO | Function | Direction |
|------|----------|-----------|
| 0 | EN (TMC2209 enable) | OUTPUT |
| 1 | STEP | OUTPUT |
| 2 | DIR | OUTPUT |
| 3 | VBAT ADC (battery via divider, ratio 5.09) | INPUT |
| 4 | UART TX (TMC2209) | OUTPUT |
| 5 | UART RX (TMC2209) | INPUT |
| 8 | I2C SDA | Bidirectional |
| 9 | I2C SCL | Bidirectional |

### I2C Devices

| Address | Device | Notes |
|---------|--------|-------|
| 0x20/0x21 | PCF8574 | Autodetected |
| 0x3C | SSD1306 OLED | 128x64 |
| 0x53/0x1D | ADXL345 | Autodetected |

### PCF8574 Bit Assignments

| Bit | Function | Mode | Notes |
|-----|----------|------|-------|
| P0 | (unused) | — | Physical pin damaged |
| P1 | LED (status) | OUTPUT | |
| P2 | Encoder CLK | INPUT | |
| P3 | Encoder DT | INPUT | |
| P4 | Encoder SW | INPUT | Button |
| P5 | ENDSTOP_1 | INPUT | Active HIGH |
| P6 | ENDSTOP_2 | INPUT | Active HIGH |
| P7 | LED2 (battery) | OUTPUT | |

**Important:** Use `write8()` with full byte to avoid corrupting pull-ups on input pins. `pcfOutputState` must keep all input pin bits HIGH.

## Key Conventions

- All motor timing is in the ISR. Never generate steps from `loop()`.
- `volatile` variables are shared between ISR and main loop. Single-byte flags are effectively atomic; multi-byte reads (e.g. position) are opportunistic.
- Hardware peripherals are autodetected at startup. Check `pcfFound`/`oledFound`/`adxlFound` before access.
- Display uses dirty flag: set `displayDirty = true` on state changes; `loop()` renders at most every 50ms.
- Encoder: short press = select/confirm, long press (>500ms) = back/cancel.
- Value editor supports numeric values and named text labels (e.g. "Stop"/"Bounce"/"Park"). Microstep editor uses powers-of-2 stepping (`editStep=0` signals this).
- BLE callbacks set volatile flags (`cmdForward`, `cmdStop`, etc.) consumed in `stateUpdate()` next loop iteration — never do motor control from BLE callback context.
- Battery: 3S LiPo, 9.0V empty / 12.6V full. Precise ADC read (10x averaged, TMC silenced) when motor idle; quick read (single sample, smoothed) during bounce.
