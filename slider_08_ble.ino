// slider_08_ble.ino — BLE service, callbacks, status notifications

// ── BLE Callbacks ──

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
    
  }
  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    
    // Restart advertising
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
      case 'H': cmdHome = true; break;        // full homing (calibration)
      case 'G': // Go to saved home (center or explicit savedHome if present)
        if (isCalibrated) {
          int32_t target = (cfg.savedHome != 0 || centerPosition == 0) ? cfg.savedHome : centerPosition;
          cmdTargetPos = target;
          cmdGoToPos = true;
        }
        break;
      case 'Z': // Set saved home to current position
        cfg.savedHome = currentPosition;
        preferences.begin("slider", false);
        preferences.putLong("homePos", cfg.savedHome);
        preferences.end();
        break;
      case 'R': // Reset error
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
      cmdNewSpeed = data[0] | (data[1] << 8);
      cmdSpeedChanged = true;
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

// ── BLE Init ──

void bleInit() {
  BLEDevice::init("Camera_Slider");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Command characteristic (write)
  pCommandChar = pService->createCharacteristic(
    COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCommandChar->setCallbacks(new CommandCallbacks());

  // Status characteristic (read + notify)
  pStatusChar = pService->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusChar->addDescriptor(new BLE2902());

  // Speed characteristic (write)
  pSpeedChar = pService->createCharacteristic(
    SPEED_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pSpeedChar->setCallbacks(new SpeedCallbacks());

  // Position characteristic (write)
  pPositionChar = pService->createCharacteristic(
    POSITION_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pPositionChar->setCallbacks(new PositionCallbacks());

  // Current characteristic (write)
  pCurrentChar = pService->createCharacteristic(
    CURRENT_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCurrentChar->setCallbacks(new CurrentCallbacks());

  // Config snapshot (read/notify)
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

// ── Status notification (v2: 11 bytes) ──
// Bytes 0-3: currentPosition (int32)
// Byte 4: flags (endstop1, endstop2, homing, calibrated, movingToPos, moving, parking)
// Bytes 5-8: travelDistance (int32)
// Byte 9: state (SliderState enum)
// Byte 10: errorCode

void bleStatusNotify() {
  if (!bleConnected) return;

  uint8_t status[11];

  // Position (4 bytes, little-endian)
  int32_t pos = currentPosition;
  status[0] = pos & 0xFF;
  status[1] = (pos >> 8) & 0xFF;
  status[2] = (pos >> 16) & 0xFF;
  status[3] = (pos >> 24) & 0xFF;

  // Flags byte
  status[4] = 0;
  if (endstop1)                          status[4] |= 0x01;
  if (endstop2)                          status[4] |= 0x02;
  if (sliderState == STATE_HOMING)       status[4] |= 0x04;
  if (isCalibrated)                      status[4] |= 0x08;
  if (sliderState == STATE_MOVING_TO_POS) status[4] |= 0x10;
  if (motorRunning)                      status[4] |= 0x20;
  if (sliderState == STATE_PARKING)      status[4] |= 0x40;

  // Travel distance (4 bytes)
  status[5] = travelDistance & 0xFF;
  status[6] = (travelDistance >> 8) & 0xFF;
  status[7] = (travelDistance >> 16) & 0xFF;
  status[8] = (travelDistance >> 24) & 0xFF;

  // State + error (v2 extension)
  status[9] = (uint8_t)sliderState;
  status[10] = (uint8_t)errorCode;

  pStatusChar->setValue(status, 11);
  pStatusChar->notify();

  // Update config snapshot (24 bytes)
  uint8_t cfgbuf[24];
  // 0-3: savedHome
  *(int32_t*)&cfgbuf[0] = cfg.savedHome;
  // 4-7: centerPosition
  *(int32_t*)&cfgbuf[4] = centerPosition;
  // 8-11: travelDistance
  *(int32_t*)&cfgbuf[8] = travelDistance;
  // 12-13: speed
  cfgbuf[12] = cfg.speed & 0xFF; cfgbuf[13] = (cfg.speed >> 8) & 0xFF;
  // 14-15: motorCurrent
  cfgbuf[14] = cfg.motorCurrent & 0xFF; cfgbuf[15] = (cfg.motorCurrent >> 8) & 0xFF;
  // 16: microsteps
  cfgbuf[16] = cfg.microsteps;
  // 17: endstopMode
  cfgbuf[17] = cfg.endstopMode;
  // 18-19: rampSteps
  cfgbuf[18] = cfg.rampSteps & 0xFF; cfgbuf[19] = (cfg.rampSteps >> 8) & 0xFF;
  // 20-21: sleepTimeout
  cfgbuf[20] = cfg.sleepTimeout & 0xFF; cfgbuf[21] = (cfg.sleepTimeout >> 8) & 0xFF;
  // 22: adxlSensitivity
  cfgbuf[22] = cfg.adxlSensitivity;
  // 23: wakeOnMotion
  cfgbuf[23] = cfg.wakeOnMotion ? 1 : 0;
  if (pConfigChar) {
    pConfigChar->setValue(cfgbuf, sizeof(cfgbuf));
    pConfigChar->notify();
  }
}
