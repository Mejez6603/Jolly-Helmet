#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

uint8_t serverMac[] = {0xB0, 0xCB, 0xD8, 0x03, 0xFF, 0xE0};

// ----------------------------------------------------
// HARDWARE PINS (ESP32 A1 Board)
// ----------------------------------------------------
const int PIN_UV          = 5;
const int PIN_HUMIDIFIER  = 18;
const int PIN_BUZZER      = 19;
const int PIN_LOCK_ENC    = 21;
const int PIN_LOCK_PANEL  = 16;
const int PIN_LOCK_BACK   = 17;

const int PIN_DOOR_ENC   = 22;
const int PIN_DOOR_PANEL = 35; // Ext 10k resistor attached
const int PIN_DOOR_BACK  = 34; // Ext 10k resistor attached
const int TRIG_HELM      = 26;
const int ECHO_HELM      = 25;
const int PIN_COIN_SIGNAL = 23; // D23 (Allan Coin Signal with 1k Pull-Up to 3.3V)

const int PIN_TFT_CS     = 15;
const int PIN_TOUCH_CS   = 27;

const int ALL_RELAYS[] = {PIN_UV, PIN_HUMIDIFIER, PIN_LOCK_ENC, PIN_LOCK_PANEL, PIN_LOCK_BACK};
const int NUM_RELAYS = 5;
const int HELMET_THRESHOLD_CM = 20;

// Dynamic Timeline Configurations
int targetPrice = 20;
int sanitizeDurationSec = 120;
char txtWelcome[35] = "Insert Coin (P%d) to Start";
char txtInstruction[35] = "Please Place Headgear Inside";

// Flowchart States
enum SystemState {
  STATE_1_1A_IDLE,
  STATE_1_1B_COIN_INSERTED,
  STATE_2_INSTRUCTION,
  STATE_3_1A_DOOR_CHECK,
  STATE_3_2A_NOTICE,
  STATE_5_1A_SANITIZING,
  STATE_5_1B_ABORT_CONFIRM,
  STATE_6_1_COMPLETED,
  STATE_6_2_CANCELLED,
  STATE_7_RETRIEVE
};

SystemState currentState = STATE_1_1A_IDLE;
SystemState previousState = (SystemState)-1;

// Coin & Interrupt Variables
volatile unsigned long pulseStartMicros = 0;
volatile unsigned long lastPulseTime = 0;
volatile bool pulseActive = false;
volatile int pulseCount = 0;
int totalInsertedCoins = 0;

unsigned long stateStartTime = 0;
unsigned long processStartTime = 0;
unsigned long uvTimer = 0;
bool uvState = false;

// Buzzer Controller
bool buzzerActive = false;
bool buzzerDelayPending = false;
unsigned long buzzerStartTime = 0;
int currentBeepDuration = 250;

typedef struct struct_command {
  char cmdType[15];
  int targetPrice;
  int sanitizeDurationSec;
  char msgWelcome[35];
  char msgInstruction[35];
  int targetPin;
  char hexColor[7];
} struct_command;

typedef struct struct_status {
  int totalCoins;
  int encDoor;
  int panelDoor;
  int backDoor;
  int helmDist;
  bool helmDetected;
} struct_status;

struct_command incomingCmd;
struct_status outgoingStatus;

// ----------------------------------------------------
// NOISE-FILTERED COIN INTERRUPT ISR (D23)
// ----------------------------------------------------
void IRAM_ATTR coinPulseISR() {
  unsigned long nowMicros = micros();
  
  if (digitalRead(PIN_COIN_SIGNAL) == LOW) {
    pulseStartMicros = nowMicros;
    pulseActive = true;
  } else if (pulseActive) {
    unsigned long pulseWidth = nowMicros - pulseStartMicros;
    pulseActive = false;

    // Discard microsecond noise spikes (< 10ms / 10,000us)
    if (pulseWidth > 10000) { 
      pulseCount++;
      lastPulseTime = millis();
    }
  }
}

// ----------------------------------------------------
// DIRECT GPIO BUZZER CONTROL
// ----------------------------------------------------
// Tells the buzzer to schedule a beep after a brief wait
void triggerBeep(int durationMs = 250) {
  buzzerDelayPending = true;
  buzzerStartTime = millis(); // Mark the exact time the coin dropped
  currentBeepDuration = durationMs;
}

// Automatically runs in your loop to handle the waiting and the beeping
void updateBuzzer() {
  unsigned long now = millis();

  // Step 1: Wait 500ms after the coin dropped before turning the buzzer ON
  if (buzzerDelayPending && (now - buzzerStartTime >= 500)) {
    digitalWrite(PIN_BUZZER, HIGH); // Turn ON [cite: 23]
    buzzerDelayPending = false;
    buzzerActive = true;
    buzzerStartTime = now;          // Reset timer to track the actual beep duration
  }

  // Step 2: Turn the buzzer OFF once its beep duration expires
  if (buzzerActive && (now - buzzerStartTime >= currentBeepDuration)) {
    digitalWrite(PIN_BUZZER, LOW);  // Turn OFF [cite: 24, 25]
    buzzerActive = false;
  }
}

int readUltrasonicCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

void turnAllRelaysOff() {
  for (int i = 0; i < NUM_RELAYS; i++) {
    digitalWrite(ALL_RELAYS[i], HIGH);
  }
}

void activateSingleActuator(int targetPin) {
  if (targetPin == PIN_BUZZER) {
    bool currentState = digitalRead(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, !currentState);
    return;
  }
  for (int i = 0; i < NUM_RELAYS; i++) {
    if (ALL_RELAYS[i] != targetPin) digitalWrite(ALL_RELAYS[i], HIGH);
  }
  bool state = digitalRead(targetPin);
  digitalWrite(targetPin, !state);
  triggerBeep(100);
}

void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingBytes, int len) {
  if (len == sizeof(struct_command)) {
    memcpy(&incomingCmd, incomingBytes, sizeof(incomingCmd));
    
    if (strcmp(incomingCmd.cmdType, "CONFIG") == 0) {
      targetPrice = incomingCmd.targetPrice;
      sanitizeDurationSec = incomingCmd.sanitizeDurationSec;
      strcpy(txtWelcome, incomingCmd.msgWelcome);
      strcpy(txtInstruction, incomingCmd.msgInstruction);
      previousState = (SystemState)-1;
    } 
    else if (strcmp(incomingCmd.cmdType, "TOGGLE") == 0) {
      activateSingleActuator(incomingCmd.targetPin);
    }
    else if (strcmp(incomingCmd.cmdType, "RESET_COINS") == 0) {
      totalInsertedCoins = 0;
      triggerBeep(100);
    }
    else if (strcmp(incomingCmd.cmdType, "READ_SENSORS") == 0) {
      outgoingStatus.totalCoins = totalInsertedCoins;
      outgoingStatus.encDoor   = digitalRead(PIN_DOOR_ENC);
      outgoingStatus.panelDoor = digitalRead(PIN_DOOR_PANEL);
      outgoingStatus.backDoor  = digitalRead(PIN_DOOR_BACK);

      int dist = readUltrasonicCM(TRIG_HELM, ECHO_HELM);
      outgoingStatus.helmDist = dist;
      outgoingStatus.helmDetected = (dist > 0 && dist <= HELMET_THRESHOLD_CM);

      esp_now_send(serverMac, (uint8_t *) &outgoingStatus, sizeof(outgoingStatus));
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Direct GPIO Silent Boot for Buzzer
  digitalWrite(PIN_BUZZER, LOW); 
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(ALL_RELAYS[i], OUTPUT);
  }
  turnAllRelaysOff();

  pinMode(PIN_DOOR_ENC, INPUT_PULLUP);
  pinMode(PIN_DOOR_PANEL, INPUT);
  pinMode(PIN_DOOR_BACK, INPUT);
  detachInterrupt(digitalPinToInterrupt(PIN_DOOR_BACK));
  
  pinMode(TRIG_HELM, OUTPUT);
  pinMode(ECHO_HELM, INPUT);

  // Coin Interrupt Setup (CHANGE mode measures pulse width)
  pinMode(PIN_COIN_SIGNAL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN_SIGNAL), coinPulseISR, CHANGE);

  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TOUCH_CS, OUTPUT);

  SPI.begin(14, 12, 13, 15);
  tft.init();
  tft.setRotation(1); // Or try 0 if 1 was upside down without mirroring
  tft.invertDisplay(false); 
  tft.fillScreen(TFT_BLACK);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setTxPower(WIFI_POWER_5dBm);

  if (esp_now_init() != ESP_OK) return;

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, serverMac, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  triggerBeep();
}

void loop() {
  updateBuzzer();

  // ----------------------------------------------------
  // VERIFIED COIN PROCESSING (350ms Quiet Gap)
  // ----------------------------------------------------
  if (pulseCount > 0 && (millis() - lastPulseTime > 400)) {
    // Atomic snapshot and clear
    noInterrupts();
    int detected = pulseCount;
    pulseCount = 0;
    interrupts();

    int coinValue = 0;

    // Pulse-to-Value Mapping for Allan Coin Slot
    if (detected == 1)                        { coinValue = 1;  } // 1 pulse  = P1
    else if (detected >= 3 && detected <= 6)  { coinValue = 5;  } // 3-6 pulses = P5
    else if (detected >= 9 && detected <= 12) { coinValue = 10; } // 9-12 pulses = P10
    else if (detected >= 18 && detected <= 22){ coinValue = 20; } // 18-22 pulses = P20

    if (coinValue > 0) {
      totalInsertedCoins += coinValue;
      Serial.printf("✅ ACCEPTED: P%d (%d pulses) | Total: P%d\n", coinValue, detected, totalInsertedCoins);

      // Advance State Machine if target reached
      if (currentState == STATE_1_1A_IDLE || currentState == STATE_1_1B_COIN_INSERTED) {
        if (totalInsertedCoins >= targetPrice) {
          currentState = STATE_2_INSTRUCTION;
        } else {
          currentState = STATE_1_1B_COIN_INSERTED;
          previousState = (SystemState)-1; // Force screen redraw on credit update
        }
      } else {
        previousState = (SystemState)-1; 
      }
      
      // Simply trigger the beep—the updated updateBuzzer() will wait 500ms automatically!
      triggerBeep(); 
    } else {
      Serial.printf("⚠️ REJECTED NOISE: Received %d pulses (Ignored)\n", detected);
    }
  }

  // ----------------------------------------------------
  // STATE MACHINE EXECUTION
  // ----------------------------------------------------
  bool isNewState = (currentState != previousState);
  if (isNewState) {
    previousState = currentState;
    stateStartTime = millis();
    tft.fillScreen(TFT_BLACK);
  }

  switch (currentState) {
    case STATE_1_1A_IDLE: {
      if (isNewState) {
        char buf[40];
        snprintf(buf, sizeof(buf), txtWelcome, targetPrice);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("HELMET SANITIZER", 160, 50, 4);
        tft.drawCentreString(buf, 160, 120, 2);
      }
      break;
    }

    case STATE_1_1B_COIN_INSERTED: {
      if (isNewState) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Inserted P%d out of P%d", totalInsertedCoins, targetPrice);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("DEPOSIT CREDITS", 160, 50, 4);
        tft.drawCentreString(buf, 160, 120, 2);
      }
      break;
    }

    case STATE_2_INSTRUCTION: {
      if (isNewState) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("INSTRUCTION", 160, 50, 4);
        tft.drawCentreString(txtInstruction, 160, 120, 2);
      }
      if (millis() - stateStartTime > 1500) {
        currentState = STATE_3_1A_DOOR_CHECK;
      }
      break;
    }

    case STATE_3_1A_DOOR_CHECK: {
      // 1. Read physical sensors
      bool doorClosed = (digitalRead(PIN_DOOR_ENC) == LOW);
      int dist = readUltrasonicCM(TRIG_HELM, ECHO_HELM);
      bool helmDetected = (dist > 0 && dist <= HELMET_THRESHOLD_CM);

      // 2. Solenoid Lock Control (LOW = Relay ON/Unlocked | HIGH = Relay OFF/Locked)
      if (!doorClosed) {
        // The user physically opened the door! Cut power instantly to prevent coil burnout
        digitalWrite(PIN_LOCK_ENC, HIGH); 
      } else {
        // Door is still closed/latched. Keep relay energized so they can pull it open
        digitalWrite(PIN_LOCK_ENC, LOW);  
      }

      // 3. Smart Routing Strategy
      if (!doorClosed && helmDetected) {
        // Safety Gate: Helmet is placed inside, but the door is still swinging open.
        if (isNewState || previousState != currentState) {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawCentreString("SAFETY NOTICE", 160, 50, 4);
          tft.drawCentreString("Close Door to Proceed", 160, 120, 2);
          previousState = currentState; 
        }
      }
      else if (doorClosed && helmDetected && (millis() - stateStartTime > 1000)) {
        // Success Condition: Door was opened, helmet inserted, and now the door is closed again!
        // (Added a 1-second buffer from state start so it doesn't accidentally trigger on initial boot)
        digitalWrite(PIN_LOCK_ENC, HIGH); // Lock secured / Power OFF
        triggerBeep(100);
        processStartTime = millis();
        uvTimer = millis();
        uvState = true;
        currentState = STATE_5_1A_SANITIZING;
      }
      else if (doorClosed && !helmDetected && (millis() - stateStartTime > 5000)) {
        // Timeout Notice: If 5 seconds pass and the door hasn't been opened or no helmet is detected,
        // send them to the notice helper state.
        digitalWrite(PIN_LOCK_ENC, HIGH); // Safe state power cut
        currentState = STATE_3_2A_NOTICE;
      }
      break;
    }

    case STATE_3_2A_NOTICE: {
      if (isNewState) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("NOTICE", 160, 40, 4);
        tft.drawCentreString("Please insert headgear", 160, 100, 2);
        tft.drawCentreString("and close the door!", 160, 130, 2);
      }
      if (millis() - stateStartTime > 2500) {
        currentState = STATE_3_1A_DOOR_CHECK;
      }
      break;
    }

    case STATE_5_1A_SANITIZING: {
      unsigned long elapsed = (millis() - processStartTime) / 1000;
      int progressPct = map(elapsed, 0, sanitizeDurationSec, 0, 100);
      if (progressPct > 100) progressPct = 100;

      // UV Light duty cycle
      if (millis() - uvTimer > 60000) {
        uvTimer = millis();
        uvState = !uvState;
      }
      digitalWrite(PIN_UV, uvState ? LOW : HIGH);
      digitalWrite(PIN_HUMIDIFIER, LOW); // Humidifier ON

      if (isNewState) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("SANITIZING...", 160, 30, 4);
      }

      // Monochrome Progress Bar (White Frame, White Fill)
      tft.drawRect(30, 130, 260, 30, TFT_WHITE);
      tft.fillRect(32, 132, (progressPct * 256) / 100, 26, TFT_WHITE);

      if (elapsed >= sanitizeDurationSec) {
        turnAllRelaysOff();
        triggerBeep(200);
        currentState = STATE_6_1_COMPLETED;
      }
      break;
    }

    case STATE_6_1_COMPLETED: {
      if (isNewState) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("OPERATION COMPLETE!", 160, 100, 4);
      }
      if (millis() - stateStartTime > 3000) {
        currentState = STATE_7_RETRIEVE;
      }
      break;
    }

    case STATE_7_RETRIEVE: {
      if (isNewState) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("PLEASE RETRIEVE", 160, 60, 4);
        tft.drawCentreString("YOUR HEADGEAR", 160, 120, 4);
      }

      if (digitalRead(PIN_DOOR_ENC) == HIGH) { // Door Opened
        delay(500);
        totalInsertedCoins = 0;
        currentState = STATE_1_1A_IDLE;
      }
      break;
    }
  }
}