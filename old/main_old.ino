#include <TMCStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// Пины
#define EN_PIN     0
#define STEP_PIN   1
#define DIR_PIN    2
#define UART_TX    4
#define UART_RX    5
#define ENDSTOP_1  3
#define ENDSTOP_2  8

// TMC2209
#define R_SENSE 0.11f
#define DRIVER_ADDR 0b00

HardwareSerial TMCSerial(1);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDR);
Preferences preferences;

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define COMMAND_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATUS_UUID         "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define SPEED_UUID          "d8de624e-140f-4a22-8594-e2216b84a5f2"
#define POSITION_UUID       "a1b2c3d4-e5f6-4789-a012-3456789abcde"
#define CURRENT_UUID        "f1e2d3c4-b5a6-4978-9012-3456789abcde"

BLEServer* pServer = NULL;
BLECharacteristic* pCommandChar = NULL;
BLECharacteristic* pStatusChar = NULL;
BLECharacteristic* pSpeedChar = NULL;
BLECharacteristic* pPositionChar = NULL;
BLECharacteristic* pCurrentChar = NULL;

bool deviceConnected = false;
bool isMoving = false;
bool isHoming = false;
bool isMovingToPosition = false;
int motorSpeed = 800;
int homingSpeed = 300;
int motorCurrent = 800;
long currentPosition = 0;
long targetPosition = 0;
long travelDistance = 0;
long centerPosition = 0;
bool isCalibrated = false;

#define BACKOFF_STEPS 200

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    isMoving = false;
    isHoming = false;
    isMovingToPosition = false;
    Serial.println("BLE disconnected");
  }
};

class CommandCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getValue().length();
    
    if (len > 0) {
      char cmd = data[0];
      
      switch(cmd) {
        case 'F':
          if (!isHoming && !isMovingToPosition) {
            isMoving = true;
            digitalWrite(DIR_PIN, LOW);
            Serial.println("CMD: Forward");
          }
          break;
          
        case 'B':
          if (!isHoming && !isMovingToPosition) {
            isMoving = true;
            digitalWrite(DIR_PIN, HIGH);
            Serial.println("CMD: Backward");
          }
          break;
          
        case 'S':
          isMoving = false;
          isHoming = false;
          isMovingToPosition = false;
          Serial.println("CMD: Stop");
          break;
          
        case 'H':
          Serial.println("CMD: Home - starting procedure");
          isHoming = true;
          isMoving = false;
          isMovingToPosition = false;
          break;
      }
    }
  }
};

class SpeedCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getValue().length();
    
    if (len >= 2) {
      uint16_t speed = data[0] | (data[1] << 8);
      motorSpeed = constrain(speed, 100, 5000);
      Serial.print("Speed set: ");
      Serial.println(motorSpeed);
    }
  }
};

class PositionCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getValue().length();
    
    if (len >= 4) {
      targetPosition = data[0] | 
                      (data[1] << 8) | 
                      (data[2] << 16) | 
                      (data[3] << 24);
      
      Serial.print("Target position set: ");
      Serial.println(targetPosition);
      
      if (isCalibrated) {
        if (targetPosition < 0) targetPosition = 0;
        if (targetPosition > travelDistance) targetPosition = travelDistance;
        
        isMovingToPosition = true;
        isMoving = false;
        Serial.println("Moving to target position...");
      } else {
        Serial.println("ERROR: Not calibrated! Run homing first.");
      }
    }
  }
};

class CurrentCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t* data = pCharacteristic->getData();
    size_t len = pCharacteristic->getValue().length();
    
    if (len >= 2) {
      uint16_t current = data[0] | (data[1] << 8);
      motorCurrent = constrain(current, 200, 1500);
      driver.rms_current(motorCurrent);
      
      Serial.print("Motor current set: ");
      Serial.print(motorCurrent);
      Serial.println(" mA");
    }
  }
};

void stepPulse() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PIN, LOW);
}

void reverseDirection() {
  digitalWrite(DIR_PIN, !digitalRead(DIR_PIN));
  delay(10);
  
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepPulse();
    
    if (digitalRead(DIR_PIN) == LOW) {
      currentPosition++;
    } else {
      currentPosition--;
    }
    
    delayMicroseconds(motorSpeed);
  }
  
  Serial.println("Reversed and backed off");
}

void saveCalibration() {
  preferences.begin("stepper", false);
  preferences.putLong("travel", travelDistance);
  preferences.putLong("center", centerPosition);
  preferences.putBool("calibrated", true);
  preferences.end();
  
  Serial.println("Calibration saved to NVS!");
}

void loadCalibration() {
  preferences.begin("stepper", true);
  
  if (preferences.getBool("calibrated", false)) {
    travelDistance = preferences.getLong("travel", 0);
    centerPosition = preferences.getLong("center", 0);
    isCalibrated = true;
    
    Serial.println("Calibration loaded from NVS:");
    Serial.print("  Travel: ");
    Serial.println(travelDistance);
    Serial.print("  Center: ");
    Serial.println(centerPosition);
  } else {
    Serial.println("No calibration found in NVS");
  }
  
  preferences.end();
}

void doHoming() {
  Serial.println("=== HOMING START ===");
  
  Serial.println("Step 1: Finding ENDSTOP_1...");
  digitalWrite(DIR_PIN, HIGH);
  
  while (digitalRead(ENDSTOP_1) == LOW) {
    stepPulse();
    delayMicroseconds(homingSpeed);
    
    if (digitalRead(ENDSTOP_2) == HIGH) {
      Serial.println("ERROR: Hit ENDSTOP_2 first!");
      isHoming = false;
      return;
    }
  }
  
  Serial.println("ENDSTOP_1 found!");
  
  digitalWrite(DIR_PIN, LOW);
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepPulse();
    delayMicroseconds(homingSpeed);
  }
  
  currentPosition = 0;
  Serial.println("Position reset to 0");
  
  delay(500);
  
  Serial.println("Step 2: Finding ENDSTOP_2...");
  digitalWrite(DIR_PIN, LOW);
  
  long stepCount = 0;
  while (digitalRead(ENDSTOP_2) == LOW) {
    stepPulse();
    stepCount++;
    currentPosition++;
    delayMicroseconds(homingSpeed);
  }
  
  travelDistance = stepCount;
  Serial.print("ENDSTOP_2 found! Travel: ");
  Serial.print(travelDistance);
  Serial.println(" steps");
  
  digitalWrite(DIR_PIN, HIGH);
  for (int i = 0; i < BACKOFF_STEPS; i++) {
    stepPulse();
    currentPosition--;
    delayMicroseconds(homingSpeed);
  }
  
  delay(500);
  
  centerPosition = travelDistance / 2;
  long stepsToCenter = currentPosition - centerPosition;
  
  Serial.print("Step 3: Moving to center (");
  Serial.print(centerPosition);
  Serial.println(")...");
  
  if (stepsToCenter > 0) {
    digitalWrite(DIR_PIN, HIGH);
    for (long i = 0; i < stepsToCenter; i++) {
      stepPulse();
      currentPosition--;
      delayMicroseconds(homingSpeed);
    }
  } else {
    digitalWrite(DIR_PIN, LOW);
    for (long i = 0; i < abs(stepsToCenter); i++) {
      stepPulse();
      currentPosition++;
      delayMicroseconds(homingSpeed);
    }
  }
  
  isCalibrated = true;
  saveCalibration();
  
  Serial.println("=== HOMING COMPLETE ===");
  Serial.print("Position: ");
  Serial.print(currentPosition);
  Serial.print(" | Center: ");
  Serial.print(centerPosition);
  Serial.print(" | Travel: ");
  Serial.println(travelDistance);
  
  isHoming = false;
}

void moveToPosition() {
  long stepsToGo = targetPosition - currentPosition;
  
  if (abs(stepsToGo) < 5) {
    Serial.println("Target position reached!");
    isMovingToPosition = false;
    return;
  }
  
  if (stepsToGo > 0) {
    digitalWrite(DIR_PIN, LOW);
    currentPosition++;
  } else {
    digitalWrite(DIR_PIN, HIGH);
    currentPosition--;
  }
  
  stepPulse();
  delayMicroseconds(motorSpeed);
}

void updateStatus() {
  uint8_t status[9];
  
  status[0] = currentPosition & 0xFF;
  status[1] = (currentPosition >> 8) & 0xFF;
  status[2] = (currentPosition >> 16) & 0xFF;
  status[3] = (currentPosition >> 24) & 0xFF;
  
  status[4] = 0;
  if (digitalRead(ENDSTOP_1) == HIGH) status[4] |= 0x01;
  if (digitalRead(ENDSTOP_2) == HIGH) status[4] |= 0x02;
  if (isHoming) status[4] |= 0x04;
  if (isCalibrated) status[4] |= 0x08;
  if (isMovingToPosition) status[4] |= 0x10;
  
  status[5] = travelDistance & 0xFF;
  status[6] = (travelDistance >> 8) & 0xFF;
  status[7] = (travelDistance >> 16) & 0xFF;
  status[8] = (travelDistance >> 24) & 0xFF;
  
  pStatusChar->setValue(status, 9);
  pStatusChar->notify();
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Camera Slider Controller ===");
  
  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENDSTOP_1, INPUT_PULLUP);
  pinMode(ENDSTOP_2, INPUT_PULLUP);
  
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  
  loadCalibration();
  
  TMCSerial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  driver.begin();
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.microsteps(32);
  driver.rms_current(motorCurrent);
  driver.en_spreadCycle(false);
  
  Serial.print("TMC2209 OK, current: ");
  Serial.print(motorCurrent);
  Serial.println("mA");
  
  BLEDevice::init("Camera_Slider");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCommandChar = pService->createCharacteristic(
    COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCommandChar->setCallbacks(new CommandCallbacks());
  
  pStatusChar = pService->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusChar->addDescriptor(new BLE2902());
  
  pSpeedChar = pService->createCharacteristic(
    SPEED_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pSpeedChar->setCallbacks(new SpeedCallbacks());
  
  pPositionChar = pService->createCharacteristic(
    POSITION_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pPositionChar->setCallbacks(new PositionCallbacks());
  
  pCurrentChar = pService->createCharacteristic(
    CURRENT_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCurrentChar->setCallbacks(new CurrentCallbacks());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE ready: Camera_Slider");
}

void loop() {
  if (isHoming) {
    doHoming();
    return;
  }
  
  if (isMovingToPosition) {
    moveToPosition();
  }
  
  static bool lastEnd1 = false;
  static bool lastEnd2 = false;
  
  bool end1 = digitalRead(ENDSTOP_1) == HIGH;
  bool end2 = digitalRead(ENDSTOP_2) == HIGH;
  
  if (end1 && !lastEnd1 && isMoving) {
    Serial.println("ENDSTOP_1 hit!");
    reverseDirection();
    isMoving = true;
  }
  
  if (end2 && !lastEnd2 && isMoving) {
    Serial.println("ENDSTOP_2 hit!");
    reverseDirection();
    isMoving = true;
  }
  
  lastEnd1 = end1;
  lastEnd2 = end2;
  
  if (isMoving) {
    stepPulse();
    
    if (digitalRead(DIR_PIN) == LOW) {
      currentPosition++;
    } else {
      currentPosition--;
    }
    
    delayMicroseconds(motorSpeed);
  }
  
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 100) {
    if (deviceConnected) {
      updateStatus();
    }
    lastUpdate = millis();
  }
}
