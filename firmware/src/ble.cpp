// ble.cpp — BLE service/characteristics, ported verbatim from slider_08_ble.ino.
// Protocol (UUIDs, 1-byte Command opcodes, 11-byte Status, 24-byte Config snapshot)
// kept byte-identical -- see plan's "BLE protocol kept byte-identical" section.
//
// Config-snapshot byte 23 was the old wakeOnMotion flag; that Config field was dropped
// in this port (dead in old firmware, never wired to a wake trigger), so byte 23 is now
// reserved/always-0 to preserve the 24-byte layout and offsets of every other field.
// (Confirmed tools/ble-tester/index.html does not parse the Config characteristic at all,
// so nothing there depends on byte 23's old meaning.)
#include "ble.h"
#include "pins.h"
#include "globals.h"
#include "state.h"
#include "config.h"

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
  }
  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    BLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    uint8_t* data = pChar->getData();
    size_t len = pChar->getValue().length();
    if (len == 0) return;

    char cmd = data[0];
    switch (cmd) {
      case 'F': cmdForward = true; break;
      case 'B': cmdBackward = true; break;
      case 'S': cmdStop = true; break;
      case 'H': cmdHome = true; break;  // full homing (calibration)
      case 'G':  // Go to saved home (center or explicit savedHome if present)
        if (isCalibrated) {
          int32_t target = (cfg.savedHome != 0 || centerPosition == 0) ? cfg.savedHome : centerPosition;
          cmdTargetPos = target;
          cmdGoToPos = true;
        }
        break;
      case 'Z':  // Set saved home to current position
        cfg.savedHome = currentPosition;
        preferences.begin("slider", false);
        preferences.putLong("homePos", cfg.savedHome);
        preferences.end();
        break;
      case 'R':  // Reset error
        if (sliderState == STATE_ERROR) {
          stateResetError();
        }
        break;
    }
  }
};

class SpeedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    uint8_t* data = pChar->getData();
    size_t len = pChar->getValue().length();
    if (len >= 2) {
      uint16_t value = data[0] | (data[1] << 8);
      // The public BLE protocol defines this characteristic as an interval in
      // microseconds (100..5000, lower is faster).  cfg.speed was later changed to a
      // 1..100 percentage, but the wire format must remain stable for existing clients.
      // Also accept 1..99 as an explicit percentage for the short-lived development
      // firmware that exposed the internal representation by mistake; 100 means fastest
      // in both encodings.
      if (value >= 1 && value <= 100) {
        cmdNewSpeed = value;
        cmdSpeedChanged = true;
      } else if (value <= 5000) {
        cmdNewSpeed = intervalToSpeed(value);
        cmdSpeedChanged = true;
      }
    }
  }
};

class PositionCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    uint8_t* data = pChar->getData();
    size_t len = pChar->getValue().length();
    if (len >= 4) {
      cmdTargetPos = (int32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
      cmdGoToPos = true;
    }
  }
};

class CurrentCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    uint8_t* data = pChar->getData();
    size_t len = pChar->getValue().length();
    if (len >= 2) {
      cmdNewCurrent = data[0] | (data[1] << 8);
      cmdCurrentChanged = true;
    }
  }
};

// Tracks whether the stack is currently up, so the Wireless Settings toggle can call
// bleInit()/bleShutdown() at runtime without double-initialising or double-freeing.
static bool bleStarted = false;

void bleInit() {
  if (bleStarted) return;
  bleStarted = true;

  BLEDevice::init("Camera_Slider");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCommandChar = pService->createCharacteristic(
    COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCommandChar->setCallbacks(new CommandCallbacks());

  pStatusChar = pService->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
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

  pConfigChar = pService->createCharacteristic(
    CONFIG_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pConfigChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

void bleShutdown() {
  if (!bleStarted) return;

  BLEDevice::deinit(true);  // true = release the controller's memory too
  bleStarted = false;
  bleConnected = false;
  bleWasConnected = false;

  // deinit() frees everything the stack allocated, so drop our copies of those pointers --
  // bleStatusNotify() must never touch them again after this.
  pServer = NULL;
  pCommandChar = NULL;
  pStatusChar = NULL;
  pSpeedChar = NULL;
  pPositionChar = NULL;
  pCurrentChar = NULL;
  pConfigChar = NULL;
}

void bleStatusNotify() {
  if (!bleConnected || !pStatusChar) return;

  uint8_t status[11];

  int32_t pos = currentPosition;
  status[0] = pos & 0xFF;
  status[1] = (pos >> 8) & 0xFF;
  status[2] = (pos >> 16) & 0xFF;
  status[3] = (pos >> 24) & 0xFF;

  status[4] = 0;
  if (endstop1)                           status[4] |= 0x01;
  if (endstop2)                           status[4] |= 0x02;
  if (sliderState == STATE_HOMING)        status[4] |= 0x04;
  if (isCalibrated)                       status[4] |= 0x08;
  if (sliderState == STATE_MOVING_TO_POS) status[4] |= 0x10;
  if (motorRunning)                       status[4] |= 0x20;
  if (sliderState == STATE_PARKING)       status[4] |= 0x40;

  status[5] = travelDistance & 0xFF;
  status[6] = (travelDistance >> 8) & 0xFF;
  status[7] = (travelDistance >> 16) & 0xFF;
  status[8] = (travelDistance >> 24) & 0xFF;

  status[9]  = (uint8_t)sliderState;
  status[10] = (uint8_t)errorCode;

  pStatusChar->setValue(status, 11);
  pStatusChar->notify();

  uint8_t cfgbuf[24];
  *(int32_t*)&cfgbuf[0] = cfg.savedHome;
  *(int32_t*)&cfgbuf[4] = centerPosition;
  *(int32_t*)&cfgbuf[8] = travelDistance;
  cfgbuf[12] = cfg.speed & 0xFF; cfgbuf[13] = (cfg.speed >> 8) & 0xFF;
  cfgbuf[14] = cfg.motorCurrent & 0xFF; cfgbuf[15] = (cfg.motorCurrent >> 8) & 0xFF;
  cfgbuf[16] = cfg.microsteps;
  cfgbuf[17] = cfg.endstopMode;
  cfgbuf[18] = cfg.rampSteps & 0xFF; cfgbuf[19] = (cfg.rampSteps >> 8) & 0xFF;
  cfgbuf[20] = cfg.sleepTimeout & 0xFF; cfgbuf[21] = (cfg.sleepTimeout >> 8) & 0xFF;
  cfgbuf[22] = cfg.adxlSensitivity;
  cfgbuf[23] = 0;  // reserved (was wakeOnMotion, dropped -- see file header note)
  if (pConfigChar) {
    pConfigChar->setValue(cfgbuf, sizeof(cfgbuf));
    pConfigChar->notify();
  }
}
