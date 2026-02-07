#include <Wire.h>
#include <PCF8574.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TMCStepper.h>

// I2C
#define SDA_PIN 8
#define SCL_PIN 9

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// PCF8574 пины
#define ENC_CLK    2
#define ENC_DT     3
#define ENC_SW     4
#define ENDSTOP_1  5
#define ENDSTOP_2  6
#define LED_PIN    1

// Мотор (ESP32 GPIO)
#define EN_PIN   0
#define STEP_PIN 1
#define DIR_PIN  2
#define UART_TX  4
#define UART_RX  5

// TMC2209
#define R_SENSE     0.11f
#define DRIVER_ADDR 0b00

HardwareSerial TMCSerial(1);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDR);
PCF8574* pcf = NULL;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool motorRunning = false;
int motorSpeed = 800;       // мкс между шагами
int motorCurrent = 800;     // mA
bool direction = false;     // false=вперёд, true=назад
uint8_t lastClk = HIGH;
long stepCount = 0;

void stepPulse() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PIN, LOW);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Статус
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(motorRunning ? ">> RUNNING" : "|| STOPPED");

  // Направление
  display.setCursor(90, 0);
  display.print(direction ? "REV" : "FWD");

  // Скорость - крупно
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("SPD:");
  display.print(motorSpeed);

  // Шаги
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("Steps: ");
  display.print(stepCount);

  // Концевики
  display.setCursor(0, 50);
  display.print("End1:");
  display.print(pcf ? (pcf->read(ENDSTOP_1) == LOW ? "ON " : "off") : "?");
  display.print("  End2:");
  display.print(pcf ? (pcf->read(ENDSTOP_2) == LOW ? "ON" : "off") : "?");

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Encoder + Stepper Test ===");

  // Мотор
  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);

  // TMC2209
  TMCSerial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  driver.begin();
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.microsteps(32);
  driver.rms_current(motorCurrent);
  driver.en_spreadCycle(false);
  Serial.println("TMC2209 OK");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // PCF8574 - пробуем 0x20 и 0x21
  uint8_t pcfAddr = 0;
  for (uint8_t addr : {0x20, 0x21}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      pcfAddr = addr;
      break;
    }
  }

  if (pcfAddr) {
    Serial.print("PCF8574 found at 0x");
    Serial.println(pcfAddr, HEX);
    pcf = new PCF8574(pcfAddr);
    pcf->begin();
    pcf->write(ENC_CLK, HIGH);
    pcf->write(ENC_DT, HIGH);
    pcf->write(ENC_SW, HIGH);
    pcf->write(LED_PIN, LOW);
    pcf->write(ENDSTOP_1, HIGH);
    pcf->write(ENDSTOP_2, HIGH);
  } else {
    Serial.println("PCF8574 NOT FOUND at 0x20 / 0x21!");
  }

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed!");
  } else {
    Serial.println("OLED OK");
  }

  if (pcf) lastClk = pcf->read(ENC_CLK);
  updateDisplay();
  Serial.println("Ready! Rotate=speed, Click=start/stop, Long=direction");
}

void loop() {
  if (!pcf) {
    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 2000) {
      Serial.println("No PCF8574, encoder disabled");
      lastMsg = millis();
    }
    return;
  }

  uint8_t clk = pcf->read(ENC_CLK);
  uint8_t dt = pcf->read(ENC_DT);
  uint8_t sw = pcf->read(ENC_SW);

  // Энкодер - крутим скорость
  if (clk != lastClk && clk == LOW) {
    if (dt == HIGH) {
      motorSpeed -= 50; // CW - быстрее
    } else {
      motorSpeed += 50; // CCW - медленнее
    }
    motorSpeed = constrain(motorSpeed, 100, 5000);
    Serial.print("Speed: ");
    Serial.println(motorSpeed);
    updateDisplay();
  }
  lastClk = clk;

  // Кнопка - короткое = старт/стоп, длинное = смена направления
  static bool lastSw = HIGH;
  static unsigned long pressTime = 0;

  if (sw == LOW && lastSw == HIGH) {
    pressTime = millis();
  }

  if (sw == HIGH && lastSw == LOW) {
    unsigned long held = millis() - pressTime;

    if (held > 500) {
      // Длинное нажатие - смена направления
      direction = !direction;
      digitalWrite(DIR_PIN, direction ? HIGH : LOW);
      Serial.print("Direction: ");
      Serial.println(direction ? "REV" : "FWD");
      pcf->write(LED_PIN, HIGH);
      delay(50);
      pcf->write(LED_PIN, LOW);
    } else {
      // Короткое нажатие - старт/стоп
      motorRunning = !motorRunning;
      Serial.println(motorRunning ? "Motor ON" : "Motor OFF");
      pcf->write(LED_PIN, motorRunning ? HIGH : LOW);
    }
    updateDisplay();
  }
  lastSw = sw;

  // Концевики
  static bool lastEnd1 = HIGH;
  static bool lastEnd2 = HIGH;
  uint8_t end1 = pcf->read(ENDSTOP_1);
  uint8_t end2 = pcf->read(ENDSTOP_2);

  // Пишем весь порт одним байтом: все входы HIGH (pull-up), LED по концевикам
  bool anyEndstop = (end1 == LOW) || (end2 == LOW);
  pcf->write8(anyEndstop ? 0xFF : (0xFF & ~(1 << LED_PIN)));

  if (end1 == LOW && lastEnd1 == HIGH) {
    Serial.println(">>> ENDSTOP_1 TRIGGERED <<<");
    if (motorRunning) {
      motorRunning = false;
      Serial.println("Motor stopped by endstop 1");
    }
    updateDisplay();
  }
  if (end2 == LOW && lastEnd2 == HIGH) {
    Serial.println(">>> ENDSTOP_2 TRIGGERED <<<");
    if (motorRunning) {
      motorRunning = false;
      Serial.println("Motor stopped by endstop 2");
    }
    updateDisplay();
  }
  if (end1 == HIGH && lastEnd1 == LOW) {
    Serial.println("ENDSTOP_1 released");
    updateDisplay();
  }
  if (end2 == HIGH && lastEnd2 == LOW) {
    Serial.println("ENDSTOP_2 released");
    updateDisplay();
  }
  lastEnd1 = end1;
  lastEnd2 = end2;

  // Шагаем
  if (motorRunning) {
    stepPulse();
    stepCount += direction ? -1 : 1;
    delayMicroseconds(motorSpeed);
  }

  // Обновляем экран каждые 200мс
  static unsigned long lastUpd = 0;
  if (millis() - lastUpd > 200) {
    updateDisplay();
    lastUpd = millis();
  }
}
