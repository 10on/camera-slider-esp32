// Input test firmware — ESP32-WROOM-32 + ST7735 160x128
// Verifies wiring: rotary encoder (CLK/DT/SW), BTN1/BTN2, ENDSTOP_1/2. Shows live state on TFT.

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <TMCStepper.h>
#include <Wire.h>
#include <INA226.h>

// ── TFT (VSPI) ──
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   13   // PWM -> PNP (A1015) base, backlight high-side switch, active-low
#define BL_PWM_CHANNEL 0

uint8_t backlightBrightness = 200; // 0-255, adjusted live via BTN1 (dimmer) / BTN2 (brighter)

// ── Encoder (EC11), internal pull-ups ──
#define ENC_CLK  32
#define ENC_DT   33
#define ENC_SW   19

// ── Extra buttons + endstops, input-only pins, external 10k pull-up to 3V3 required ──
#define BTN1      35
#define BTN2      34
#define ENDSTOP_1 39
#define ENDSTOP_2 36

// ── LEDs, direct GPIO drive through series resistor ──
#define LED_STATUS  14
#define LED_BATTERY 15

// ── Motor (TMC2209) ──
#define MOTOR_EN   27
#define MOTOR_STEP 26
#define MOTOR_DIR  25
#define TMC_TX     17   // ESP32 -> TMC2209 RX
#define TMC_RX     16   // ESP32 <- TMC2209 TX
#define R_SENSE     0.11f
#define DRIVER_ADDR 0b00
#define MOTOR_CURRENT_MA 800

HardwareSerial TMCSerial(1);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDR);

// ── I2C bus (ADXL345 + INA226) ──
#define I2C_SDA 21
#define I2C_SCL 22

// ── ADXL345, register-level access (ported from slider_02_hw.ino / slider_07_adxl.ino) ──
#define REG_DEVID       0x00
#define REG_POWER_CTL   0x2D
#define REG_BW_RATE     0x2C
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0      0x32

bool    adxlFound = false;
uint8_t adxlAddr  = 0;
float   adxlX = 0, adxlY = 0, adxlZ = 0;

void adxlWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void adxlInit() {
  for (uint8_t addr : {0x53, 0x1D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;

    Wire.beginTransmission(addr);
    Wire.write(REG_DEVID);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    uint8_t devid = Wire.read();

    if (devid == 0xE5) {
      adxlAddr = addr;
      adxlFound = true;
      adxlWriteReg(REG_DATA_FORMAT, 0x08); // full resolution, +-2g
      adxlWriteReg(REG_BW_RATE, 0x0A);     // 100 Hz
      adxlWriteReg(REG_POWER_CTL, 0x08);   // measurement mode
      delay(50);
      return;
    }
  }
}

void adxlReadAxes() {
  if (!adxlFound) return;
  Wire.beginTransmission(adxlAddr);
  Wire.write(REG_DATAX0);
  Wire.endTransmission(false);
  Wire.requestFrom(adxlAddr, (uint8_t)6);

  int16_t raw_x = Wire.read() | (Wire.read() << 8);
  int16_t raw_y = Wire.read() | (Wire.read() << 8);
  int16_t raw_z = Wire.read() | (Wire.read() << 8);

  adxlX = raw_x * 0.0039f; // 3.9 mg/LSB, full resolution +-2g
  adxlY = raw_y * 0.0039f;
  adxlZ = raw_z * 0.0039f;
}

// ── INA226 (current/voltage) ──
INA226 ina226(0x40);
bool  ina226Found   = false;
float busVoltage_V  = 0;
float current_mA    = 0;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

#define COL_BG    0x0000
#define COL_TITLE 0xFFFF
#define COL_LABEL 0x7BEF
#define COL_VAL   0x07FF

// ── Encoder state (quadrature decode in ISR) ──
volatile int32_t encoderCount = 0;
volatile uint8_t lastEncoded  = 0;

void IRAM_ATTR encoderISR() {
  uint8_t msb = digitalRead(ENC_CLK);
  uint8_t lsb = digitalRead(ENC_DT);
  uint8_t encoded = (msb << 1) | lsb;
  uint8_t sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCount++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderCount--;

  lastEncoded = encoded;
}

// ── Debounced digital inputs ──
struct DebouncedInput {
  uint8_t  pin;
  bool     activeLow;
  bool     state      = false;
  bool     lastRaw    = false;
  unsigned long lastChange = 0;

  DebouncedInput(uint8_t p, bool al) : pin(p), activeLow(al) {}
};

DebouncedInput encSwIn(ENC_SW, true);
DebouncedInput btn1In(BTN1, true);
DebouncedInput btn2In(BTN2, true);
DebouncedInput es1In(ENDSTOP_1, false);
DebouncedInput es2In(ENDSTOP_2, false);

const unsigned long DEBOUNCE_MS = 30;

bool updateDebounced(DebouncedInput& in) {
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

// ── Display (redraw only on change) ──
int32_t lastDrawnCount = 0x7FFFFFFF;

// Status row: label + value on one line, redrawn together (no overlap risk)
struct StatusRow {
  int y;
  const char* label;
  int8_t lastState = -1;

  StatusRow(int y_, const char* label_) : y(y_), label(label_) {}
};

StatusRow rowEncSw(53, "ENC SW");
StatusRow rowBtn1(64, "BTN1");
StatusRow rowBtn2(75, "BTN2");
StatusRow rowEs1(86, "ENDSTOP1");
StatusRow rowEs2(97, "ENDSTOP2");

// Plain text rows (not boolean), redrawn every sensor read cycle
const int ROW_POWER_Y = 108;
const int ROW_ADXL_Y  = 119;

void drawTextRow(int y, const char* text) {
  tft.fillRect(0, y, 160, 10, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(4, y);
  tft.setTextColor(COL_VAL);
  tft.print(text);
}

void drawStatic() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TITLE);
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.print("Input test");
  tft.drawFastHLine(0, 12, 160, 0x4208);

  tft.setTextColor(COL_LABEL);
  tft.setCursor(4, 24);  tft.print("Encoder:");
}

void drawStatusRow(StatusRow& row, bool state) {
  tft.fillRect(0, row.y, 160, 10, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(4, row.y);
  tft.setTextColor(COL_LABEL);
  tft.print(row.label);
  tft.print(": ");
  tft.setTextColor(state ? ST77XX_GREEN : COL_LABEL);
  tft.print(state ? "PRESSED" : "-");
  row.lastState = state;
}

void render() {
  static bool firstRun = true;
  if (firstRun) { drawStatic(); firstRun = false; }

  if (encoderCount != lastDrawnCount) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", (long)encoderCount);
    tft.fillRect(4, 34, 156, 16, COL_BG);
    tft.setTextColor(COL_VAL);
    tft.setTextSize(2);
    tft.setCursor(4, 34);
    tft.print(buf);
    lastDrawnCount = encoderCount;
  }

  if ((int8_t)encSwIn.state != rowEncSw.lastState) drawStatusRow(rowEncSw, encSwIn.state);
  if ((int8_t)btn1In.state  != rowBtn1.lastState)  drawStatusRow(rowBtn1,  btn1In.state);
  if ((int8_t)btn2In.state  != rowBtn2.lastState)  drawStatusRow(rowBtn2,  btn2In.state);
  if ((int8_t)es1In.state   != rowEs1.lastState)   drawStatusRow(rowEs1,   es1In.state);
  if ((int8_t)es2In.state   != rowEs2.lastState)   drawStatusRow(rowEs2,   es2In.state);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("BOOT: start");

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  pinMode(BTN1,      INPUT);   // external pull-up required (GPIO35 has none)
  pinMode(BTN2,      INPUT);   // external pull-up required (GPIO34 has none)
  pinMode(ENDSTOP_1, INPUT);   // external pull-up required (GPIO39 has none)
  pinMode(ENDSTOP_2, INPUT);   // external pull-up required (GPIO36 has none)
  pinMode(LED_STATUS,  OUTPUT);
  pinMode(LED_BATTERY, OUTPUT);
  Serial.println("BOOT: pinMode done");

  pinMode(MOTOR_EN,   OUTPUT);
  pinMode(MOTOR_STEP, OUTPUT);
  pinMode(MOTOR_DIR,  OUTPUT);
  digitalWrite(MOTOR_EN, LOW);   // active-low: enable driver
  digitalWrite(MOTOR_DIR, LOW);
  Serial.println("BOOT: motor pins done");

  {
    // Bare-GPIO loopback, before UART2 claims these pins at all: toggle TMC_TX (17) as a
    // plain digital output and read TMC_RX (16) as a plain digital input. If this fails
    // too, it's not a UART-peripheral/pin-matrix issue -- the GPIOs themselves (or the
    // physical path between them) aren't passing a signal at all.
    pinMode(TMC_TX, OUTPUT);
    pinMode(TMC_RX, INPUT);
    digitalWrite(TMC_TX, LOW);
    delay(2);
    int lo = digitalRead(TMC_RX);
    digitalWrite(TMC_TX, HIGH);
    delay(2);
    int hi = digitalRead(TMC_RX);
    Serial.printf("BOOT: bare GPIO loopback -- TX=LOW -> RX read %d, TX=HIGH -> RX read %d (expect 0 then 1)\n", lo, hi);
  }

  TMCSerial.begin(115200, SERIAL_8N1, TMC_RX, TMC_TX);
  Serial.println("BOOT: TMCSerial.begin done");
  {
    // Raw loopback test, bypassing TMCStepper's protocol entirely: if the physical
    // TX->resistor->PDN_UART<-RX loop is intact, we should see our own byte echoed back
    // even if the TMC2209 itself is unpowered/not replying. If NOTHING comes back at all,
    // that's a real break in the wire/resistor, not a driver-side protocol issue.
    while (TMCSerial.available()) TMCSerial.read();  // flush
    TMCSerial.write(0x55);
    TMCSerial.flush();
    delay(5);
    int n = TMCSerial.available();
    Serial.printf("BOOT: raw UART loopback -- bytes received: %d", n);
    if (n > 0) {
      Serial.print(" (");
      while (TMCSerial.available()) Serial.printf("0x%02X ", TMCSerial.read());
      Serial.print(")");
    }
    Serial.println();
  }
  driver.begin();
  Serial.println("BOOT: driver.begin done");
  {
    uint8_t conn = driver.test_connection();
    uint8_t ver  = driver.version();
    Serial.printf("BOOT: TMC2209 test_connection=%u (0=OK), version=0x%02X (0x21 expected)\n", conn, ver);
  }
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.microsteps(32);
  driver.rms_current(MOTOR_CURRENT_MA);
  driver.en_spreadCycle(false);   // stealthChop
  Serial.println("BOOT: driver config done");

  lastEncoded = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderISR, CHANGE);
  Serial.println("BOOT: encoder interrupts attached");

  ledcSetup(BL_PWM_CHANNEL, 5000, 8);   // 5kHz PWM, 8-bit duty
  ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
  ledcWrite(BL_PWM_CHANNEL, 255 - backlightBrightness); // active-low (PNP high-side switch)
  Serial.println("BOOT: backlight ledc done");

  // Claim VSPI bus manually, MISO redirected to an unused pin (GPIO12): passing -1 here
  // does NOT disable MISO -- esp32-hal-spi.c falls back to the VSPI default (GPIO19) when
  // miso<0, which would silently strip ENC_SW's pull-up and attach it to the SPI matrix.
  // GPIO12 is otherwise unused; its strapping function only matters during boot sampling,
  // not for a runtime pinMode() change, so parking MISO there is safe.
  SPI.begin(18 /*SCK*/, 12 /*MISO -> unused pin, NOT GPIO19*/, 23 /*MOSI*/, TFT_CS);
  Serial.println("BOOT: SPI.begin done");

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("BOOT: Wire.begin done");

  {
    // Raw I2C bus scan -- independent of our own init logic, shows exactly what's
    // actually responding on the bus right now.
    Serial.println("BOOT: I2C scan...");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("BOOT: I2C device found at 0x%02X\n", addr);
        found++;
      }
    }
    Serial.printf("BOOT: I2C scan done, %d device(s) found\n", found);
  }

  adxlInit();
  Serial.printf("BOOT: adxlInit done, adxlFound=%d (addr=0x%02X)\n", adxlFound, adxlAddr);
  ina226Found = ina226.begin();
  if (ina226Found) {
    // Shunt is two 0.1ohm (R100) resistors in parallel: 1/R = 1/0.1 + 1/0.1 -> R = 0.05ohm
    // -> ceiling 81.92mV/0.05 = ~1.64A. Requesting 1.5A for a bit of safety margin under
    // that hard cap.
    int rtn = ina226.setMaxCurrentShunt(1.5, 0.05);
    Serial.printf("BOOT: ina226 setMaxCurrentShunt rtn=%d (0=OK)\n", rtn);
  }
  Serial.printf("BOOT: ina226.begin done, ina226Found=%d\n", ina226Found);

  Serial.println("BOOT: about to tft.initR...");
  tft.setSPISpeed(4000000);  // diagnostic: slow SPI way down (breadboard wiring test)
  tft.initR(INITR_BLACKTAB);
  Serial.println("BOOT: tft.initR done");
  tft.setRotation(1);
  Serial.println("BOOT: setRotation done");

  render();
  Serial.println("BOOT: render done, setup complete");
}

void loop() {
  bool encSwChanged = updateDebounced(encSwIn);
  bool btn1Changed  = updateDebounced(btn1In);
  bool btn2Changed  = updateDebounced(btn2In);
  bool es1Changed   = updateDebounced(es1In);
  bool es2Changed   = updateDebounced(es2In);

  // Encoder button: reset encoderCount to 0 (convenient stop control).
  if (encSwChanged && encSwIn.state) {
    encoderCount = 0;
    Serial.println("ENC SW: encoderCount reset to 0");
  }

  // Motor: continuous rotation while encoderCount != 0, direction follows its sign.
  // encoderCount == 0 -> stopped. No endstop gating -- bare wiring/motion test only.
  static bool lastRunning = false;
  static bool lastDir = false;
  static unsigned long lastStepMicros = 0;
  const unsigned long STEP_INTERVAL_US = 1000;  // matches the earlier-verified 1MHz-tick timing

  bool running = (encoderCount != 0);
  bool dirLevel = (encoderCount > 0);  // HIGH if positive, LOW if negative
  if (running != lastRunning || dirLevel != lastDir) {
    Serial.printf("MOTOR: encoderCount=%ld running=%d DIR=%s\n", (long)encoderCount, running, dirLevel ? "HIGH" : "LOW");
    digitalWrite(MOTOR_DIR, dirLevel ? HIGH : LOW);
    lastRunning = running;
    lastDir = dirLevel;
  }
  if (running && micros() - lastStepMicros >= STEP_INTERVAL_US) {
    lastStepMicros = micros();
    digitalWrite(MOTOR_STEP, HIGH);
    delayMicroseconds(3);
    digitalWrite(MOTOR_STEP, LOW);
  }

  if (encSwChanged || btn1Changed || btn2Changed || es1Changed || es2Changed || encoderCount != lastDrawnCount) {
    render();
  }

  // Brightness: BTN1 = dimmer, BTN2 = brighter, one step per press (rising edge only)
  const uint8_t BRIGHTNESS_STEP = 25;
  if (btn1Changed && btn1In.state) {
    backlightBrightness = (backlightBrightness > BRIGHTNESS_STEP) ? (backlightBrightness - BRIGHTNESS_STEP) : 0;
    ledcWrite(BL_PWM_CHANNEL, 255 - backlightBrightness);
  }
  if (btn2Changed && btn2In.state) {
    backlightBrightness = (backlightBrightness < 255 - BRIGHTNESS_STEP) ? (backlightBrightness + BRIGHTNESS_STEP) : 255;
    ledcWrite(BL_PWM_CHANNEL, 255 - backlightBrightness);
  }

  // LEDs mirror endstops directly: LED lights while its endstop is triggered
  digitalWrite(LED_STATUS,  es1In.state ? HIGH : LOW);
  digitalWrite(LED_BATTERY, es2In.state ? HIGH : LOW);

  // INA226 (power) + ADXL345 (tilt), throttled -- I2C reads are too slow for every loop() pass
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 200) {
    lastSensorRead = millis();
    char buf[32];

    if (ina226Found) {
      busVoltage_V = ina226.getBusVoltage();
      current_mA   = ina226.getCurrent_mA();
      snprintf(buf, sizeof(buf), "%.2fV  %.0fmA", busVoltage_V, current_mA);
    } else {
      snprintf(buf, sizeof(buf), "INA226 not found");
    }
    drawTextRow(ROW_POWER_Y, buf);

    if (adxlFound) {
      adxlReadAxes();
      snprintf(buf, sizeof(buf), "X%.2f Y%.2f Z%.2f", adxlX, adxlY, adxlZ);
    } else {
      snprintf(buf, sizeof(buf), "ADXL345 not found");
    }
    drawTextRow(ROW_ADXL_Y, buf);
  }
}
