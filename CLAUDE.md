# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based camera slider controller firmware. Controls a motorized camera slider via BLE (Bluetooth Low Energy) using a TMC2209 stepper motor driver with two endstop sensors for position detection.

## Build & Upload

This is an Arduino sketch project targeting ESP32. Compile and upload using Arduino IDE or PlatformIO. No traditional build system (Makefile, npm, etc.).

**Required libraries:** TMCStepper, ESP32 BLE (BLEDevice/BLEServer/BLEUtils/BLE2902), Preferences (ESP32 built-in)

**For hardware test sketch (`old/pcf_encoder.ino`):** Wire, PCF8574, Adafruit_GFX, Adafruit_SSD1306

**Serial monitor:** 115200 baud for debug output.

## Code Structure

- `slider.ino` / `slider_functions.ino` — Active sketch files (currently template placeholders)
- `old/main_old.ino` — Complete working implementation (~490 lines)
- `old/pcf_encoder.ino` — Hardware test sketch for encoder + endstops via PCF8574

## Architecture (old/main_old.ino)

**Event-driven BLE firmware** with callback-based command handling:

1. **BLE layer** — 5 characteristics: Command (write), Status (read/notify), Speed (write), Position (write), Current (write). BLE device name: `Camera_Slider`. Commands are single chars: `F` (forward), `B` (backward), `S` (stop), `H` (home).

2. **Motor control** — TMC2209 via UART, 32 microsteps, StealthChop mode. Step/direction pulse generation with blocking `delayMicroseconds()` timing. Speed range: 100-5000 µs/step. Current range: 200-1500 mA.

3. **State machine** — Boolean flags (`isMoving`, `isHoming`, `isMovingToPosition`, `isCalibrated`) control behavior in `loop()`. Homing procedure: find ENDSTOP_1 → reset position → find ENDSTOP_2 → measure travel → move to center.

4. **Persistent storage** — ESP32 NVS (Preferences API) stores calibration data (travel distance, center position, calibrated flag) under namespace `"stepper"`.

5. **Status notifications** — 9-byte packed binary status sent every 100ms: 4 bytes position + 1 byte flags + 4 bytes travel distance. Flags encode endstop states, homing, calibration, and move-to-position status via bitmask.

## Hardware Pin Map

| Pin | Function    |
|-----|-------------|
| 0   | EN (enable) |
| 1   | STEP        |
| 2   | DIR         |
| 3   | ENDSTOP_1   |
| 4   | UART_TX     |
| 5   | UART_RX     |
| 8   | ENDSTOP_2   |

Endstops use INPUT_PULLUP; active HIGH means triggered.
