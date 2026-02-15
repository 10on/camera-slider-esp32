// slider_04_state.ino — State machine transitions, priorities, error handling

void stateUpdate() {
  // ── Process BLE command flags ──
  processBleCommands();

  // ── BLE disconnect during movement → ERROR ──
  if (bleWasConnected && !bleConnected) {
    bleWasConnected = false;
    if (sliderState == STATE_MANUAL_MOVING ||
        sliderState == STATE_MOVING_TO_POS ||
        sliderState == STATE_HOMING) {
      motorStopNow();
      sliderState = STATE_ERROR;
      errorCode = ERR_BLE_LOST;
      displayDirty = true;
      
      return;
    }
  }
  if (bleConnected) bleWasConnected = true;

  // ── Endstop handling during movement ──
  if (endstop1Rising || endstop2Rising) {
    handleEndstopHit();
  }

  // ── Target reached (motor stopped by ISR) ──
  if (sliderState == STATE_MOVING_TO_POS && !motorRunning) {
    sliderState = STATE_IDLE;
    displayDirty = true;
    
  }

  // ── Manual stop check (motor decelerated to stop) ──
  if (sliderState == STATE_MANUAL_MOVING && !motorRunning) {
    sliderState = STATE_IDLE;
    displayDirty = true;
  }

  // ── Parking complete ──
  if (sliderState == STATE_PARKING && !motorRunning) {
    digitalWrite(EN_PIN, HIGH);  // disable driver
    sliderState = STATE_IDLE;
    displayDirty = true;
    
  }
}

void processBleCommands() {
  // Speed change (always accepted, BLE sends 1-100)
  if (cmdSpeedChanged) {
    cmdSpeedChanged = false;
    cfg.speed = constrain(cmdNewSpeed, 1, 100);
    targetInterval = speedToInterval(cfg.speed);
    if (motorRunning) {
      rampStepsLeft = 50;  // smooth speed change
    }
    displayDirty = true;
  }

  // Current change (always accepted)
  if (cmdCurrentChanged) {
    cmdCurrentChanged = false;
    cfg.motorCurrent = constrain(cmdNewCurrent, 200, 1500);
    driver.rms_current(cfg.motorCurrent);
    displayDirty = true;
  }

  // Stop command — highest priority
  if (cmdStop) {
    cmdStop = false;
    if (sliderState != STATE_ERROR) {
      if (motorRunning) motorStopNow();
      if (sliderState == STATE_HOMING) {
        homingPhase = HOME_IDLE;
      }
      sliderState = STATE_IDLE;
      displayDirty = true;
      
    }
    return;
  }

  // Home command
  if (cmdHome) {
    cmdHome = false;
    if (sliderState == STATE_IDLE || sliderState == STATE_ERROR) {
      if (sliderState == STATE_ERROR) {
        errorCode = ERR_NONE;
      }
      homingStart();
      displayDirty = true;
      
    }
    return;
  }

  // Forward command — block if endstop2 (forward end) is triggered
  if (cmdForward) {
    cmdForward = false;
    if (sliderState == STATE_IDLE && !endstop2) {
      sliderState = STATE_MANUAL_MOVING;
      digitalWrite(EN_PIN, LOW);
      motorStartRamp(true, speedToInterval(cfg.speed));
      displayDirty = true;
      
    }
    return;
  }

  // Backward command — block if endstop1 (backward end) is triggered
  if (cmdBackward) {
    cmdBackward = false;
    if (sliderState == STATE_IDLE && !endstop1) {
      sliderState = STATE_MANUAL_MOVING;
      digitalWrite(EN_PIN, LOW);
      motorStartRamp(false, speedToInterval(cfg.speed));
      displayDirty = true;
      
    }
    return;
  }

  // Go to position command — block if moving toward a triggered endstop
  if (cmdGoToPos) {
    cmdGoToPos = false;
    if (sliderState == STATE_IDLE && isCalibrated) {
      int32_t target = constrain(cmdTargetPos, 0, travelDistance);
      bool wouldGoForward = target > currentPosition;
      bool blocked = (wouldGoForward && endstop2) || (!wouldGoForward && endstop1);
      if (!blocked) {
        sliderState = STATE_MOVING_TO_POS;
        digitalWrite(EN_PIN, LOW);
        motorMoveTo(target, speedToInterval(cfg.speed));
        displayDirty = true;
        
      }
    }
    return;
  }
}

void handleEndstopHit() {
  // During homing — handled by homingUpdate()
  if (sliderState == STATE_HOMING) return;

  // During parking — stop at endstop
  if (sliderState == STATE_PARKING) {
    motorStopNow();
    return;
  }

  // During manual moving or moving to position
  if (sliderState == STATE_MANUAL_MOVING || sliderState == STATE_MOVING_TO_POS) {
    switch (cfg.endstopMode) {
      case ENDSTOP_STOP:
        motorStopNow();
        sliderState = STATE_IDLE;
        displayDirty = true;
        
        break;

      case ENDSTOP_BOUNCE:
        motorStopNow();
        vbatReadQuick();  // grab voltage while motor is briefly stopped
        // Reverse direction and restart
        {
          bool newForward = motorDirection;  // motorDirection is inverted (true=backward)
          motorStartRamp(newForward, speedToInterval(cfg.speed));
        }
        displayDirty = true;
        
        break;

      case ENDSTOP_PARK:
        // Continue until firm stop, then disable motor
        // Motor will stall against endstop briefly, then we stop and disable
        delay(50);  // brief push
        motorStopNow();
        digitalWrite(EN_PIN, HIGH);  // disable driver
        sliderState = STATE_IDLE;
        displayDirty = true;
        
        break;
    }
  }
}

void stateEnterError(ErrorCode code) {
  motorStopNow();
  sliderState = STATE_ERROR;
  errorCode = code;
  displayDirty = true;
  
}

void stateResetError() {
  if (sliderState == STATE_ERROR) {
    sliderState = STATE_IDLE;
    errorCode = ERR_NONE;
    displayDirty = true;
    
  }
}

const char* stateToString(SliderState s) {
  switch (s) {
    case STATE_IDLE:          return "IDLE";
    case STATE_MANUAL_MOVING: return "MOVING";
    case STATE_MOVING_TO_POS: return "GO TO";
    case STATE_HOMING:        return "HOMING";
    case STATE_PARKING:       return "PARKING";
    case STATE_ERROR:         return "ERROR";
    case STATE_SLEEP:         return "SLEEP";
    default:                  return "?";
  }
}

const char* errorToString(ErrorCode e) {
  switch (e) {
    case ERR_NONE:              return "None";
    case ERR_BLE_LOST:          return "BLE Lost";
    case ERR_HOMING_FAIL:       return "Homing Fail";
    case ERR_ENDSTOP_UNEXPECTED: return "Endstop Err";
    default:                     return "?";
  }
}
