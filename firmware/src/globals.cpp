// globals.cpp — definitions for globals.h externs.
#include "globals.h"
#include "pins.h"

Config cfg;

HardwareSerial TMCSerial(1);
TMC2209Stepper driver(&TMCSerial, R_SENSE, DRIVER_ADDR);
Preferences preferences;

bool    adxlFound  = false;
uint8_t adxlAddr   = 0;
bool    ina226Found = false;
INA226  ina226(0x40);

BLEServer* pServer = NULL;
BLECharacteristic* pCommandChar  = NULL;
BLECharacteristic* pStatusChar   = NULL;
BLECharacteristic* pSpeedChar    = NULL;
BLECharacteristic* pPositionChar = NULL;
BLECharacteristic* pCurrentChar  = NULL;
BLECharacteristic* pConfigChar   = NULL;
BLECharacteristic* pProgramChar  = NULL;

volatile bool bleConnected    = false;
volatile bool bleWasConnected = false;

volatile bool     cmdForward        = false;
volatile bool     cmdBackward       = false;
volatile bool     cmdStop           = false;
volatile bool     cmdHome           = false;
volatile bool     cmdGoToPos        = false;
volatile int32_t  cmdTargetPos      = 0;
volatile bool     cmdPingPongStart  = false;
volatile bool     cmdSpeedChanged   = false;
volatile uint16_t cmdNewSpeed       = 0;
volatile bool     cmdCurrentChanged = false;
volatile uint16_t cmdNewCurrent     = 0;
volatile bool     cmdProgramPending = false;
volatile uint8_t  cmdProgramId = PROGRAM_PING_PONG;
volatile uint8_t  cmdProgramAction = PROGRAM_SELECT;
volatile uint8_t  cmdProgramSpeed = 0;
volatile uint8_t  cmdProgramStartPoint = 0xFF;
volatile uint8_t  cmdProgramFlags = 0;

volatile int32_t  currentPosition = 0;
volatile bool     motorRunning    = false;
volatile bool     motorDirection  = false;
volatile int32_t  motorTargetPos  = 0;
volatile bool     motorHasTarget  = false;
volatile uint32_t stepInterval    = 800;
volatile uint32_t targetInterval  = 800;
volatile uint32_t rampStepsLeft   = 0;
volatile bool     stopRequested   = false;
volatile bool     stopAfterRamp   = false;

hw_timer_t* stepTimer = NULL;

SliderState sliderState = STATE_IDLE;
ErrorCode   errorCode   = ERR_NONE;
HomingPhase homingPhase = HOME_IDLE;
bool        pingPongApproaching = false;
uint8_t     selectedProgram = PROGRAM_PING_PONG;

int32_t travelDistance = 0;
int32_t centerPosition = 0;
bool    isCalibrated   = false;

bool endstop1 = false;
bool endstop2 = false;
bool endstop1Rising = false;
bool endstop2Rising = false;

int32_t encoderDelta   = 0;
bool    encoderPressed = false;
bool    btn1Pressed    = false;
bool    btn2Pressed    = false;
bool    btn2LongPress  = false;

float busVoltage_V = 0;
float current_mA   = 0;
bool  isCharging    = false;

float  adxlX = 0, adxlY = 0, adxlZ = 0;
int8_t adxlMotionDir = 0;

MenuScreen    currentScreen     = SCREEN_MAIN;
int8_t        menuIndex         = 0;
int8_t        menuOffset        = 0;
bool          displayDirty      = true;
unsigned long lastDisplayUpdate = 0;
MenuScreen    prevScreen        = SCREEN_MAIN;

const char* editLabel = NULL;
int32_t     editValue = 0;
int32_t     editMin   = 0;
int32_t     editMax   = 0;
int32_t     editStep  = 1;
void (*editCallback)(int32_t) = NULL;
MenuScreen  editReturnScreen = SCREEN_MAIN;
const char* const* editValueNames = NULL;
const char*         editUnit = NULL;

char        editText[33]      = "";
int8_t      editTextLen       = 0;
int8_t      editTextCharIdx   = 0;
char        editTextLabel[24] = "";
const char* editTextSubmitLabel = "SAVE";
void (*editTextSubmit)(const char*) = NULL;
MenuScreen  editTextCancelScreen = SCREEN_MAIN;

unsigned long wifiConnectStartMs = 0;

const char TEXT_CHARSET[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*-_.";
const int TEXT_CHARSET_LEN = sizeof(TEXT_CHARSET) - 1;  // exclude trailing NUL

unsigned long lastBleNotify    = 0;
unsigned long lastActivityTime = 0;
