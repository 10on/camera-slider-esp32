#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

#define ADXL345_ADDR 0x53

// Регистры ADXL345
#define REG_DEVID       0x00
#define REG_BW_RATE     0x2C
#define REG_POWER_CTL   0x2D
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0      0x32

bool adxlFound = false;
float x, y, z;

void writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.read();
}

void readAxes(uint8_t addr, float *x, float *y, float *z) {
  Wire.beginTransmission(addr);
  Wire.write(REG_DATAX0);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)6);

  int16_t raw_x = Wire.read() | (Wire.read() << 8);
  int16_t raw_y = Wire.read() | (Wire.read() << 8);
  int16_t raw_z = Wire.read() | (Wire.read() << 8);

  // Full resolution, +-2g: 3.9 mg/LSB
  *x = raw_x * 0.0039;
  *y = raw_y * 0.0039;
  *z = raw_z * 0.0039;
}

void i2cScan() {
  Serial.println("I2C scan...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found: 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  Nothing found! Check SDA/SCL wiring");
  }
  Serial.print("Total devices: ");
  Serial.println(found);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================");
  Serial.println("  ADXL345 Test");
  Serial.println("================");
  Serial.print("SDA=");
  Serial.print(SDA_PIN);
  Serial.print(" SCL=");
  Serial.println(SCL_PIN);

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);

  // Сканируем шину
  i2cScan();

  // Пробуем оба адреса ADXL345
  uint8_t addr = ADXL345_ADDR;
  uint8_t devid = readReg(addr, REG_DEVID);

  if (devid != 0xE5) {
    Serial.print("0x53 DEVID=0x");
    Serial.print(devid, HEX);
    Serial.println(" - not ADXL345");

    addr = 0x1D;
    devid = readReg(addr, REG_DEVID);
  }

  if (devid == 0xE5) {
    Serial.print("ADXL345 OK at 0x");
    Serial.println(addr, HEX);
    adxlFound = true;

    writeReg(addr, REG_DATA_FORMAT, 0x08); // Full resolution, +-2g
    writeReg(addr, REG_BW_RATE, 0x0A);    // 100 Hz
    writeReg(addr, REG_POWER_CTL, 0x08);  // Measurement mode
    delay(100);
  } else {
    Serial.print("0x1D DEVID=0x");
    Serial.print(devid, HEX);
    Serial.println(" - not ADXL345 either");
    Serial.println("ADXL345 NOT FOUND");
  }
}

void loop() {
  static unsigned long cnt = 0;

  if (adxlFound) {
    readAxes(ADXL345_ADDR, &x, &y, &z);
    Serial.print("X:");
    Serial.print(x, 3);
    Serial.print("  Y:");
    Serial.print(y, 3);
    Serial.print("  Z:");
    Serial.print(z, 3);
    Serial.println("g");
  } else {
    // Переодический пульс чтобы было видно что ESP жив
    if (cnt % 10 == 0) {
      Serial.print("alive, no ADXL... uptime ");
      Serial.print(millis() / 1000);
      Serial.println("s");
    }
  }

  cnt++;
  delay(200);
}
