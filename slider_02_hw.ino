// slider_02_hw.ino — Hardware init: TMC2209, PCF8574, OLED, ADXL345

void hwInit() {
  // ── Motor GPIO ──
  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);   // enable driver
  digitalWrite(DIR_PIN, LOW);

  // ── Battery voltage ADC ──
  pinMode(VBAT_PIN, INPUT);
  analogSetAttenuation(ADC_11db);  // 0-3.3V range

  // ── TMC2209 via UART ──
  TMCSerial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  driver.begin();
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.microsteps(cfg.microsteps);
  driver.rms_current(cfg.motorCurrent);
  driver.en_spreadCycle(false);  // StealthChop
  Serial.print("TMC2209 OK, current=");
  Serial.print(cfg.motorCurrent);
  Serial.print("mA, usteps=");
  Serial.println(cfg.microsteps);

  // ── I2C bus ──
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(50);

  // ── PCF8574 autodetect (0x20 or 0x21) ──
  for (uint8_t addr : {0x20, 0x21}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      pcfAddr = addr;
      break;
    }
  }

  if (pcfAddr) {
    pcf = new PCF8574(pcfAddr);
    pcf->begin();
    // Set initial output state: all inputs HIGH (pull-up), LED off (P1=0)
    pcfOutputState = 0xFD;  // 0b11111101
    pcf->write8(pcfOutputState);
    pcfFound = true;
    Serial.print("PCF8574 at 0x");
    Serial.println(pcfAddr, HEX);
  } else {
    Serial.println("PCF8574 not found");
  }

  // ── OLED autodetect ──
  oled = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  if (oled->begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledFound = true;
    oled->clearDisplay();
    oled->setTextColor(SSD1306_WHITE);
    oled->setTextSize(1);
    oled->setCursor(20, 28);
    oled->print("Camera Slider");
    oled->display();
    Serial.println("OLED OK at 0x3C");
  } else {
    delete oled;
    oled = NULL;
    Serial.println("OLED not found");
  }

  // ── Battery voltage — first precise read ──
  vbatReadPrecise();

  // ── ADXL345 autodetect ──
  adxlInit();
}

// Precise ADC read: silence TMC chopping, 10x averaged
// Call only when motor is NOT running
void vbatReadPrecise() {
  driver.shaft(false);  // quiet TMC internal state
  delay(50);            // let noise settle

  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(VBAT_PIN);
    delayMicroseconds(100);
  }

  float v = (sum / 10.0f / 4095.0f) * 3.3f * VBAT_DIVIDER;
  vbatVoltage = v;
  Serial.print("Vbat=");
  Serial.print(vbatVoltage, 1);
  Serial.println("V");
}

// Quick ADC read: single sample, no delays
// Safe to call during movement (e.g. endstop bounce)
void vbatReadQuick() {
  int raw = analogRead(VBAT_PIN);
  float v = (raw / 4095.0f) * 3.3f * VBAT_DIVIDER;
  vbatVoltage = vbatVoltage * 0.7f + v * 0.3f;  // heavier smoothing
}

int vbatPercent() {
  int pct = (int)((vbatVoltage - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100);
  return constrain(pct, 0, 100);
}

void adxlInit() {
  // Try 0x53 first, then 0x1D
  for (uint8_t addr : {0x53, 0x1D}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      // Read DEVID register
      Wire.beginTransmission(addr);
      Wire.write(REG_DEVID);
      Wire.endTransmission(false);
      Wire.requestFrom(addr, (uint8_t)1);
      uint8_t devid = Wire.read();

      if (devid == 0xE5) {
        adxlAddr = addr;
        adxlFound = true;

        // Configure: full resolution, ±2g, 100 Hz, measurement mode
        adxlWriteReg(REG_DATA_FORMAT, 0x08);
        adxlWriteReg(REG_BW_RATE, 0x0A);
        adxlWriteReg(REG_POWER_CTL, 0x08);
        delay(50);

        Serial.print("ADXL345 at 0x");
        Serial.println(addr, HEX);
        return;
      }
    }
  }
  Serial.println("ADXL345 not found");
}

void adxlWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t adxlReadReg(uint8_t reg) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(adxlAddr, (uint8_t)1);
  return Wire.read();
}
