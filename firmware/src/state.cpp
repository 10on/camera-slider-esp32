// state.cpp — state machine, ported from slider_04_state.ino.
// ERR_ENDSTOP_UNEXPECTED (old: PCF8574-bus-down-while-moving safety) has no equivalent
// dispatch here -- there's no I2C-polled expander on this hardware to fail; the enum value
// is kept reserved for BLE Status-byte compatibility only.
#include "state.h"
#include "pins.h"
#include "motor.h"
#include "homing.h"
#include "config.h"
#include "power.h"

static bool directionBlockedByEndstop(bool forward) {
  return forward ? endstop2 : endstop1;
}

static bool motorIsDrivingIntoEndstop() {
  if (!motorRunning) return false;
  bool forward = !motorDirection;
  return directionBlockedByEndstop(forward);
}

static void startManualMove(bool forward) {
  // F/B are set-direction commands.  Repeating the current direction is idempotent;
  // requesting the opposite direction performs a controlled immediate direction change.
  // This makes a remote's left/right controls deterministic instead of silently ignoring
  // the second command while STATE_MANUAL_MOVING is active.
  if (sliderState == STATE_MANUAL_MOVING && motorRunning) {
    if ((!motorDirection) == forward) return;
    motorStopNow();
  }

  if (directionBlockedByEndstop(forward)) {
    sliderState = STATE_IDLE;
    displayDirty = true;
    return;
  }

  sliderState = STATE_MANUAL_MOVING;
  digitalWrite(MOTOR_EN, LOW);
  driver.rms_current(cfg.motorCurrent);
  motorStartRamp(forward, speedToInterval(cfg.speed));
  displayDirty = true;
}

void stateUpdate() {
  processBleCommands();

  // BLE disconnect during movement -> ERROR
  if (bleWasConnected && !bleConnected) {
    bleWasConnected = false;
    if (sliderState == STATE_MANUAL_MOVING ||
        sliderState == STATE_MOVING_TO_POS ||
        sliderState == STATE_HOMING ||
        sliderState == STATE_PING_PONG) {
      motorStopNow();
      sliderState = STATE_ERROR;
      errorCode = ERR_BLE_LOST;
      displayDirty = true;
      return;
    }
  }
  if (bleConnected) bleWasConnected = true;

  // Check the debounced level, not only the one-loop rising-edge flag.  That closes the
  // hole where a command or mode transition could begin after the edge had already been
  // consumed while the carriage was still holding the switch.
  if (motorIsDrivingIntoEndstop()) {
    handleEndstopHit();
  }

  if (sliderState == STATE_MOVING_TO_POS && !motorRunning) {
    sliderState = STATE_IDLE;
    displayDirty = true;
  }

  // Ping-Pong's first leg (heading to cfg.pingPongStart) uses motorMoveTo(), which stops the
  // motor itself once the target step count is reached -- no endstop involved when the start
  // is Center. Once arrived, kick off the actual bounce cycle. (If the start is an endstop
  // instead, handleEndstopHit() below gets there first and this never fires -- motorRunning
  // is already true again by the time we reach this check.)
  if (sliderState == STATE_PING_PONG && pingPongApproaching && !motorRunning) {
    pingPongApproaching = false;
    bool goForward = (cfg.pingPongStart != 2);  // start=End2 -> first leg is backward
    motorStartRamp(goForward, speedToInterval(cfg.speed));
    displayDirty = true;
  }

  if (sliderState == STATE_MANUAL_MOVING && !motorRunning) {
    sliderState = STATE_IDLE;
    displayDirty = true;
  }

  if (sliderState == STATE_PARKING && !motorRunning) {
    digitalWrite(MOTOR_EN, HIGH);  // disable driver
    sliderState = STATE_IDLE;
    displayDirty = true;
  }
}

void processBleCommands() {
  // High-level preset request. Selection/configuration is orthogonal to execution: a
  // remote can tune Ping-Pong while it is stopped or running, then issue START/STOP.
  if (cmdProgramPending) {
    uint8_t program = cmdProgramId;
    uint8_t action = cmdProgramAction;
    uint8_t speed = cmdProgramSpeed;
    uint8_t startPoint = cmdProgramStartPoint;
    uint8_t flags = cmdProgramFlags;
    cmdProgramPending = false;

    selectedProgram = program;
    if (speed >= 1 && speed <= 100) {
      cfg.speed = speed;
      targetInterval = speedToInterval(cfg.speed);
      if (motorRunning) rampStepsLeft = 50;
    }
    if (startPoint <= 2) cfg.pingPongStart = startPoint;
    if (flags & 0x01) configSave();
    if (action == PROGRAM_STOP) cmdStop = true;
    if (action == PROGRAM_START && program == PROGRAM_PING_PONG) cmdPingPongStart = true;
    displayDirty = true;
  }

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

  // Stop command -- highest priority
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

  // Forward command -- block if endstop2 (forward end) is triggered
  if (cmdForward) {
    cmdForward = false;
    if (sliderState == STATE_IDLE || sliderState == STATE_MANUAL_MOVING) {
      startManualMove(true);
    }
    return;
  }

  // Backward command -- block if endstop1 (backward end) is triggered
  if (cmdBackward) {
    cmdBackward = false;
    if (sliderState == STATE_IDLE || sliderState == STATE_MANUAL_MOVING) {
      startManualMove(false);
    }
    return;
  }

  // Go to position command -- block if moving toward a triggered endstop
  if (cmdGoToPos) {
    cmdGoToPos = false;
    if (sliderState == STATE_IDLE && isCalibrated) {
      int32_t target = constrain(cmdTargetPos, 0, travelDistance);
      bool wouldGoForward = target > currentPosition;
      bool blocked = (wouldGoForward && endstop2) || (!wouldGoForward && endstop1);
      if (!blocked) {
        sliderState = STATE_MOVING_TO_POS;
        digitalWrite(MOTOR_EN, LOW);
        driver.rms_current(cfg.motorCurrent);
        motorMoveTo(target, speedToInterval(cfg.speed));
        displayDirty = true;
      }
    }
    return;
  }

  // Start Ping-Pong -- head to cfg.pingPongStart first; stateUpdate() picks up the arrival
  // and kicks off the actual bounce cycle (see the pingPongApproaching check above).
  if (cmdPingPongStart) {
    cmdPingPongStart = false;
    if (sliderState == STATE_IDLE && isCalibrated) {
      int32_t target = (cfg.pingPongStart == 1) ? 0
                      : (cfg.pingPongStart == 2) ? travelDistance
                      : centerPosition;
      sliderState = STATE_PING_PONG;
      pingPongApproaching = true;
      digitalWrite(MOTOR_EN, LOW);
      driver.rms_current(cfg.motorCurrent);
      motorMoveTo(target, speedToInterval(cfg.speed));
      displayDirty = true;
    }
    return;
  }
}

void handleEndstopHit() {
  // During homing -- handled by homingUpdate()
  if (sliderState == STATE_HOMING) return;

  // During parking -- stop at endstop
  if (sliderState == STATE_PARKING) {
    motorStopNow();
    return;
  }

  // Ping-Pong: always reverse and keep going, regardless of cfg.endstopMode -- this is a
  // dedicated mode, not the general Endstop Mode setting. Also covers the case where
  // cfg.pingPongStart is an endstop itself: hitting it while still "approaching" is treated
  // the same as arriving, then immediately bounces off in the other direction.
  if (sliderState == STATE_PING_PONG) {
    motorStopNow();
    pingPongApproaching = false;
    bool newForward = motorDirection;  // motorDirection is inverted (true=backward)
    motorStartRamp(newForward, speedToInterval(cfg.speed));
    displayDirty = true;
    return;
  }

  if (sliderState == STATE_MANUAL_MOVING || sliderState == STATE_MOVING_TO_POS) {
    switch (cfg.endstopMode) {
      case ENDSTOP_STOP:
        motorStopNow();
        sliderState = STATE_IDLE;
        displayDirty = true;
        break;

      case ENDSTOP_BOUNCE:
        motorStopNow();
        powerRead();  // grab a fresh reading while motor is briefly stopped
        {
          bool newForward = motorDirection;  // motorDirection is inverted (true=backward)
          motorStartRamp(newForward, speedToInterval(cfg.speed));
        }
        displayDirty = true;
        break;

      case ENDSTOP_PARK:
        delay(50);  // brief push into the endstop
        motorStopNow();
        digitalWrite(MOTOR_EN, HIGH);  // disable driver
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
    case STATE_PING_PONG:     return "PING PONG";
    default:                  return "?";
  }
}

const char* errorToString(ErrorCode e) {
  switch (e) {
    case ERR_NONE:               return "None";
    case ERR_BLE_LOST:           return "BLE Lost";
    case ERR_HOMING_FAIL:        return "Homing Fail";
    case ERR_ENDSTOP_UNEXPECTED: return "Endstop Err";
    default:                     return "?";
  }
}
