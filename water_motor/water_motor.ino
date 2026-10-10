#include <Servo.h>
#include <EEPROM.h>

Servo myServo;

// --- EEPROM Allocation Map ---
const int EEPROM_VALID_FLAG_ADDR = 0;  // Magic byte identifier
const int EEPROM_MODE_ADDR       = 1;  // Stores currentMode
const int EEPROM_STATE_ADDR      = 2;  // Stores currentState
const int EEPROM_ON_DUR_ADDR     = 3;  // Stores activeOnDuration (4 bytes)
const int EEPROM_INT_DUR_ADDR    = 7;  // Stores activeIntervalDuration (4 bytes)
const int EEPROM_ELAPSED_ADDR    = 11; // Stores elapsed state time (4 bytes)
const byte EEPROM_MAGIC_BYTE     = 0xD3; // Updated magic byte to initialize new mode mapping

// EEPROM Auto-Save Interval (5 Seconds)
const unsigned long EEPROM_SAVE_INTERVAL = 5000UL;
unsigned long lastEepromSaveMillis = 0;

// --- Pin Definitions ---
const int SERVO_PIN = 9;
const int LED_INDICATOR_PIN = 52; 
const int POS_LED_NEG = 22;        

// --- Delay Constants ---
const unsigned long ONE_HOUR     = 1UL * 60UL * 60UL * 1000UL;
const unsigned long THREE_HOURS  = 3UL * 60UL * 60UL * 1000UL;
const unsigned long FIVE_HOURS   = 5UL * 60UL * 60UL * 1000UL;
const unsigned long SEVEN_HOURS  = 7UL * 60UL * 60UL * 1000UL;

// Off/Interval Duration Constants
const unsigned long TIME_1_HOUR  = 1UL * 60UL * 60UL * 1000UL;
const unsigned long TIME_2_HOURS = 2UL * 60UL * 60UL * 1000UL;
const unsigned long TIME_3_HOURS = 3UL * 60UL * 60UL * 1000UL;

// Timing durations for ON state
const unsigned long TIME_30_MINS = 30UL * 60UL * 1000UL;
const unsigned long TIME_45_MINS = 45UL * 60UL * 1000UL;
const unsigned long TIME_60_MINS = 60UL * 60UL * 1000UL;

// Operational Variables
unsigned long activeOnDuration = TIME_60_MINS;
unsigned long activeIntervalDuration = TIME_2_HOURS;
unsigned long elapsedTimeInState = 0; // Tracks accrued time in active state

// --- Servo Positions ---
const int POS_ON = 90;
const int POS_OFF = -15;

// --- Mode & State Definitions ---
enum Mode { 
  MODE_STOPPED, 
  MODE_CONTINUOUS, 
  MODE_ONE_HOUR_DELAY,
  MODE_TIMER_DELAY,       // 3-Hour Delay
  MODE_FIVE_HOUR_DELAY,   // 5-Hour Delay
  MODE_SEVEN_HOUR_DELAY
};

enum State { STATE_INITIAL_ON, STATE_OFF, STATE_ON, STATE_DELAY_WAIT, STATE_DELAY_ON };

Mode currentMode = MODE_STOPPED;
State currentState = STATE_INITIAL_ON;

unsigned long previousMillis = 0;

void saveSystemState() {
  EEPROM.update(EEPROM_VALID_FLAG_ADDR, EEPROM_MAGIC_BYTE);
  EEPROM.update(EEPROM_MODE_ADDR, (byte)currentMode);
  EEPROM.update(EEPROM_STATE_ADDR, (byte)currentState);
  EEPROM.put(EEPROM_ON_DUR_ADDR, activeOnDuration);
  EEPROM.put(EEPROM_INT_DUR_ADDR, activeIntervalDuration);
  EEPROM.put(EEPROM_ELAPSED_ADDR, elapsedTimeInState);
}

void loadSystemState() {
  if (EEPROM.read(EEPROM_VALID_FLAG_ADDR) == EEPROM_MAGIC_BYTE) {
    currentMode = (Mode)EEPROM.read(EEPROM_MODE_ADDR);
    currentState = (State)EEPROM.read(EEPROM_STATE_ADDR);
    EEPROM.get(EEPROM_ON_DUR_ADDR, activeOnDuration);
    EEPROM.get(EEPROM_INT_DUR_ADDR, activeIntervalDuration);
    EEPROM.get(EEPROM_ELAPSED_ADDR, elapsedTimeInState);
    Serial.println(F("Resumed system state and timer progression from EEPROM."));
  } else {
    Serial.println(F("No saved EEPROM found. Populating default state."));
    saveSystemState();
  }
}

void setServoPosition(int position) {
  myServo.write(position);

  if (position == POS_ON) {
    digitalWrite(POS_LED_NEG, LOW);
  } else {
    digitalWrite(POS_LED_NEG, HIGH);
  }
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  pinMode(POS_LED_NEG, OUTPUT);

  myServo.attach(SERVO_PIN);

  // Restore state parameters from EEPROM
  loadSystemState();

  previousMillis = millis();
  lastEepromSaveMillis = millis();

  // Restore physical hardware state to match loaded settings
  if (currentMode != MODE_STOPPED) {
    digitalWrite(LED_INDICATOR_PIN, HIGH);
    if (currentState == STATE_INITIAL_ON || currentState == STATE_ON || currentState == STATE_DELAY_ON) {
      setServoPosition(POS_ON);
    } else {
      setServoPosition(POS_OFF);
    }
  } else {
    digitalWrite(LED_INDICATOR_PIN, LOW);
    setServoPosition(POS_OFF);
  }
}

void loop() {
  checkBluetoothCommands();

  if (currentMode == MODE_STOPPED) {
    return;
  }

  unsigned long currentMillis = millis();
  
  // Calculate delta time since last iteration and update continuous total elapsed time
  unsigned long delta = currentMillis - previousMillis;
  previousMillis = currentMillis;
  elapsedTimeInState += delta;

  // Save current progress to EEPROM every 5 seconds to reduce wear
  if (currentMillis - lastEepromSaveMillis >= EEPROM_SAVE_INTERVAL) {
    EEPROM.put(EEPROM_ELAPSED_ADDR, elapsedTimeInState);
    lastEepromSaveMillis = currentMillis;
  }

  // --- Mode Handling ---
  if (currentMode == MODE_CONTINUOUS) {
    switch (currentState) {
      case STATE_INITIAL_ON:
      case STATE_ON:
        if (elapsedTimeInState >= activeOnDuration) {
          setServoPosition(POS_OFF);
          currentState = STATE_OFF;
          elapsedTimeInState = 0; // Reset state elapsed timer
          saveSystemState();
        }
        break;

      case STATE_OFF:
        if (elapsedTimeInState >= activeIntervalDuration) {
          setServoPosition(POS_ON);
          currentState = STATE_ON;
          elapsedTimeInState = 0; // Reset state elapsed timer
          saveSystemState();
        }
        break;
    }
  } 
  // Delay Modes (1H, 3H, 5H, 7H) -> Transition to CONTINUOUS cycling loop after initial ON duration completes
  else if (currentMode == MODE_ONE_HOUR_DELAY || currentMode == MODE_TIMER_DELAY || 
           currentMode == MODE_FIVE_HOUR_DELAY || currentMode == MODE_SEVEN_HOUR_DELAY) {
    
    // Determine target delay limit based on active mode
    unsigned long targetDelay = ONE_HOUR;
    if (currentMode == MODE_TIMER_DELAY) targetDelay = THREE_HOURS;
    else if (currentMode == MODE_FIVE_HOUR_DELAY) targetDelay = FIVE_HOURS;
    else if (currentMode == MODE_SEVEN_HOUR_DELAY) targetDelay = SEVEN_HOURS;

    switch (currentState) {
      case STATE_DELAY_WAIT:
        if (elapsedTimeInState >= targetDelay) {
          setServoPosition(POS_ON);
          currentState = STATE_DELAY_ON;
          elapsedTimeInState = 0;
          saveSystemState();
        }
        break;

      case STATE_DELAY_ON:
        if (elapsedTimeInState >= activeOnDuration) {
          // Instead of stopping, transition into regular continuous OFF state using configured interval
          setServoPosition(POS_OFF);
          currentMode = MODE_CONTINUOUS;  // Switch to continuous mode for regular looping
          currentState = STATE_OFF;
          elapsedTimeInState = 0;
          saveSystemState();
          
          Serial1.println(F("DELAY_COMPLETE_TRANSITIONING_TO_CONTINUOUS"));
          Serial.println(F("DELAY_COMPLETE_TRANSITIONING_TO_CONTINUOUS"));
        }
        break;
    }
  }
}

void checkBluetoothCommands() {
  if (Serial1.available() > 0) {
    char command = Serial1.read();

    if (command == '\r' || command == '\n') return;

    Serial.print(F("Received Bluetooth Command: "));
    Serial.println(command);

    // --- Mode Control Mapping ---
    if (command == 'a' || command == 'A') {
      startContinuousProcess();
    } 
    else if (command == '1') {
      startTimerDelayProcess(MODE_ONE_HOUR_DELAY); // '1' triggers 1-Hour Delay
    }
    else if (command == 'c' || command == 'C') {
      startTimerDelayProcess(MODE_TIMER_DELAY);    // 'c' triggers 3-Hour Delay
    }
    else if (command == 'd' || command == 'D') {
      startTimerDelayProcess(MODE_FIVE_HOUR_DELAY); // 'd' triggers 5-Hour Delay
    }
    else if (command == '0') {
      startTimerDelayProcess(MODE_SEVEN_HOUR_DELAY); // '0' triggers 7-Hour Delay
    }
    else if (command == 'b' || command == 'B') {
      stopProcess();
    }

    // --- ON Duration Configurations ---
    else if (command == '3') {
      activeOnDuration = TIME_30_MINS;
      saveSystemState();
      Serial1.println(F("ON_DURATION_SET_30_MINS"));
    }
    else if (command == '4') {
      activeOnDuration = TIME_45_MINS;
      saveSystemState();
      Serial1.println(F("ON_DURATION_SET_45_MINS"));
    }
    else if (command == '6') {
      activeOnDuration = TIME_60_MINS;
      saveSystemState();
      Serial1.println(F("ON_DURATION_SET_60_MINS"));
    }

    // --- Interval (OFF) Configurations ---
    else if (command == '7') {
      activeIntervalDuration = TIME_1_HOUR;
      saveSystemState();
      Serial1.println(F("INTERVAL_SET_1_HOUR"));
    }
    else if (command == '8') {
      activeIntervalDuration = TIME_2_HOURS;
      saveSystemState();
      Serial1.println(F("INTERVAL_SET_2_HOURS"));
    }
    else if (command == '9') {
      activeIntervalDuration = TIME_3_HOURS;
      saveSystemState();
      Serial1.println(F("INTERVAL_SET_3_HOURS"));
    }
  }
}

void startContinuousProcess() {
  currentMode = MODE_CONTINUOUS;
  currentState = STATE_INITIAL_ON;
  elapsedTimeInState = 0; // Explicit reset on user command
  previousMillis = millis();
  
  saveSystemState();

  digitalWrite(LED_INDICATOR_PIN, HIGH);
  setServoPosition(POS_ON);
  
  Serial1.println(F("CONTINUOUS_MODE_STARTED"));
  Serial.println(F("CONTINUOUS_MODE_STARTED"));
}

void startTimerDelayProcess(Mode selectedMode) {
  currentMode = selectedMode;
  currentState = STATE_DELAY_WAIT;
  elapsedTimeInState = 0; // Explicit reset on user command
  previousMillis = millis();
  
  saveSystemState();

  digitalWrite(LED_INDICATOR_PIN, HIGH);
  setServoPosition(POS_OFF);

  if (selectedMode == MODE_ONE_HOUR_DELAY) {
    Serial1.println(F("1H_DELAY_MODE_STARTED"));
    Serial.println(F("1H_DELAY_MODE_STARTED"));
  } else if (selectedMode == MODE_TIMER_DELAY) {
    Serial1.println(F("3H_DELAY_MODE_STARTED"));
    Serial.println(F("3H_DELAY_MODE_STARTED"));
  } else if (selectedMode == MODE_FIVE_HOUR_DELAY) {
    Serial1.println(F("5H_DELAY_MODE_STARTED"));
    Serial.println(F("5H_DELAY_MODE_STARTED"));
  } else if (selectedMode == MODE_SEVEN_HOUR_DELAY) {
    Serial1.println(F("7H_DELAY_MODE_STARTED"));
    Serial.println(F("7H_DELAY_MODE_STARTED"));
  }
}

void stopProcess() {
  currentMode = MODE_STOPPED;
  elapsedTimeInState = 0; // Explicit reset on user command
  
  saveSystemState();

  digitalWrite(LED_INDICATOR_PIN, LOW);
  setServoPosition(POS_OFF);
  
  Serial1.println(F("PROCESS_STOPPED"));
  Serial.println(F("PROCESS_STOPPED"));
}