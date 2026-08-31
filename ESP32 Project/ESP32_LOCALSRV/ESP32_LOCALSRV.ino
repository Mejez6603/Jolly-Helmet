#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "Shared_Common.h"
#include "index_html.h"
#include "style_css.h"
#include "script_js.h"

// Access Point Configurations
const char* AP_SSID     = "ESP32_LOCAL";
const char* AP_PASSWORD = "12345678";
const byte  DNS_PORT    = 53;

DNSServer      dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences    prefs;

uint16_t secondsPerCoin = 20;

MachineState currentMachineState = STATE_IDLE;
uint16_t     stepRemainingSec    = 0; // countdown used by STATE_FINISH only
uint32_t     activeTimer         = 0;
uint32_t     lastKnownPulsesA1   = 0;

uint8_t  conditionHoldTicks = 0; // generic ">2 second" debounce counter, reset on relevant state entry
uint16_t uvBlinkTicks       = 0; // 1Hz ticks since last UV toggle during STATE_CLEANING
bool     uvBlinkState       = false; // true = UV currently ON
bool     sensorsWasClosed   = false; // edge-detect for the "closed without helmet" retry nudge in STATE_SENSORS
bool     retrieveConfirmed  = false; // true once helmet has been taken out; now waiting for door to CLOSE

// A2's solenoid locks are enough to visually glitch A1's TFT (EMI/brownout, not a firmware
// bug - see PROJECT_HANDOFF.md gotcha #4). Since the machine itself keeps working fine, the
// pragmatic fix is cosmetic: force A1 to repaint its current screen shortly after every A2
// relay actuation. 0 = no repaint pending.
unsigned long a1RedrawAtMillis = 0;

TelemetryPacket nodeA1_Data;
TelemetryPacket nodeA2_Data;
ACSTelemetryPacket nodeACS_Data;
unsigned long lastSeenA1 = 0;
unsigned long lastSeenA2 = 0;
unsigned long lastSeenACS = 0;
unsigned long lastOneSecTick = 0;

// -------------------------------------------------------------
// Box 2 (Heater) - independent second unit (Nodes C1/C2) mirroring Box 1's cycle, sharing
// this same Master. Master-local only (Shared_Common.h doesn't need to know about this -
// C1/C2 just receive rendered screen strings and relay commands like A1/A2 do).
// -------------------------------------------------------------
enum Box2State : uint8_t {
    B2_STATE_IDLE           = 0, // Step 0: Welcome, waiting for coin
    B2_STATE_INSTRUCTIONS   = 1, // Step 1: waiting for Enclosure door OPEN
    B2_STATE_SENSORS        = 2, // Step 2: waiting for door CLOSED + helmet detected
    B2_STATE_HEATING        = 3, // Step 3: Heater + Fan active
    B2_STATE_ABORT_CONFIRM  = 4, // Step 3b: user-initiated abort confirmation dialog
    B2_STATE_RETRIEVE       = 5, // Step 4: waiting for door OPEN + helmet removed, then CLOSED again
    B2_STATE_FINISH         = 6, // Step 5: thank-you screen, then loops back to Step 0
    B2_STATE_PAUSED_SAFETY  = 7  // Mid-heating safety breach (unexpected door/helmet violation)
};

Box2State box2CurrentState     = B2_STATE_IDLE;
uint16_t  box2StepRemainingSec = 0;
uint32_t  box2ActiveTimer      = 0;
uint32_t  lastKnownPulsesC1    = 0;

uint8_t box2ConditionHoldTicks = 0;
bool    box2SensorsWasClosed   = false;
bool    box2RetrieveConfirmed  = false;

uint32_t box2SessionStartMillis  = 0;
uint32_t box2SessionCoinsAtStart = 0;

// Same cosmetic EMI-glitch fix as A1's a1RedrawAtMillis, applied to C1's TFT.
unsigned long c1RedrawAtMillis = 0;

TelemetryPacket nodeC1_Data;
TelemetryPacket nodeC2_Data;
unsigned long lastSeenC1 = 0;
unsigned long lastSeenC2 = 0;

// CommandPacket carries no "who sent this" field, only "who it's addressed to" - so a touch
// action from A1 and one from C1 look identical except for the sender's MAC. Master learns
// each terminal's MAC from its own periodic telemetry (which does carry a deviceID), then
// uses that to route CMD_TOUCH_ACTION to the correct box's state machine.
uint8_t macA1[6] = {0}; bool macA1Known = false;
uint8_t macC1[6] = {0}; bool macC1Known = false;

// -------------------------------------------------------------
// Alcohol Tank (Humidifier Container) Calibration - ultrasonic distance, sensor-to-liquid.
// Tank is 9cm(H) x 15cm(L) x 8cm(W). Measured against the real unit.
// -------------------------------------------------------------
const float ALC_DIST_EMPTY_CM = 9.0f; // sensor reading when tank is empty (0%) - tank's full height
const float ALC_DIST_FULL_CM  = 2.0f; // sensor reading when tank is at the safe fill limit (100%)
const float ALC_LOW_PCT       = 10.0f; // at/below this -> go refill
const float ALC_HIGH_PCT      = 90.0f; // at/above this -> refill complete
const float HUMID_LENGTH_CM   = 15.0f;
const float HUMID_WIDTH_CM    = 8.0f;
const float HUMID_HEIGHT_CM   = 9.0f;

bool humidifierRefillRequested = false; // edge-trigger latch, so the ACS request only fires once per low event

// -------------------------------------------------------------
// Statistics & Financial Analytics (persisted to NVS)
// -------------------------------------------------------------
const float COIN_VALUE_PESO = 1.0f; // 1 pulse = PHP 1
#define STAT_HISTORY_SIZE 10

struct SessionRecord {
    uint16_t durationSec;
    uint16_t coinsUsed;
};
SessionRecord statHistory[STAT_HISTORY_SIZE];
uint8_t  statHistoryCount = 0;
uint8_t  statHistoryHead  = 0;

uint32_t statTotalSessions    = 0;
uint32_t statTotalCoins       = 0;
uint32_t statTotalDurationSec = 0;

uint32_t sessionStartMillis  = 0;
uint32_t sessionCoinsAtStart = 0;

// -------------------------------------------------------------
// Hardcoded Step Screens (tokenized: BG|ST,x,y,col,text|TT,...|BTN,...)
// -------------------------------------------------------------
const char* SCREEN_WELCOME =
    "#000080|ST,10,15,#FFFFFF,WELCOME|TT,10,60,#FFFF00,Please insert a COIN to Proceed,500";

const char* SCREEN_INSTRUCTIONS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Proceed";

const char* SCREEN_CHECKING =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,80,#FFFFFF,Please Wait...|ST,10,105,#FFFF00,Checking Alcohol Status";

const char* SCREEN_REFILLING =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,80,#FFFFFF,Please Wait...|ST,10,105,#FFFF00,Refilling Alcohol...";

const char* SCREEN_SENSORS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Place the|ST,10,95,#FFFFFF,Headgear Inside the Chamber";

const char* SCREEN_SENSORS_RETRY =
    "#800000|ST,10,15,#FFFFFF,No Headgear Detected!|ST,10,50,#FFFFFF,Please Open the Door|ST,10,75,#FFFFFF,and Insert your Headgear";

const char* SCREEN_CLEANING =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Cleaning in Progress...|TT,10,95,#00FF00,Time: {TIMER}s,0|BTN,215,8,95,32,#ef4444,ABORT,ABORT";

const char* SCREEN_ABORT_CONFIRM =
    "#7f1d1d|ST,10,15,#FFFFFF,Abort Operation?|ST,10,45,#FFFFFF,Coins Won't Be Refunded|BTN,20,100,130,45,#dc2626,ABORT,ABORT_YES|BTN,170,100,130,45,#334155,RESUME,ABORT_NO";

const char* SCREEN_SAFETY_PAUSE =
    "#800000|ST,10,20,#FFFFFF,SAFETY PAUSE|TT,10,60,#FFFF00,CLOSE DOOR / REPLACE HELMET,300";

const char* SCREEN_RETRIEVE =
    "#006400|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Retrieve";

const char* SCREEN_RETRIEVE_CLOSE =
    "#006400|ST,10,20,#FFFFFF,Headgear Retrieved!|ST,10,50,#FFFFFF,Please Close the Door|ST,10,75,#FFFFFF,to Finish";

const char* SCREEN_FINISH =
    "#006400|ST,10,20,#FFFFFF,Thank you for your|ST,10,45,#FFFFFF,Patronage!|ST,10,80,#FFFF00,We Hope to See you Again! :D";

// -------------------------------------------------------------
// Box 2 (Heater) Screens - mirrors Box 1's, per the "HEATER PROGRAM FLOW" diagram (no
// alcohol check/refill steps; Heater+Fan instead of Mist+UV during the active phase).
// -------------------------------------------------------------
const char* SCREEN_C1_WELCOME =
    "#000080|ST,10,15,#FFFFFF,WELCOME|TT,10,60,#FFFF00,Please insert a COIN to Proceed,500";

const char* SCREEN_C1_INSTRUCTIONS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Proceed";

const char* SCREEN_C1_SENSORS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Place the|ST,10,95,#FFFFFF,Headgear Inside the Chamber";

const char* SCREEN_C1_SENSORS_RETRY =
    "#800000|ST,10,15,#FFFFFF,No Headgear Detected!|ST,10,50,#FFFFFF,Please Open the Door|ST,10,75,#FFFFFF,and Insert your Headgear";

const char* SCREEN_C1_HEATING =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Heating in Progress...|TT,10,95,#00FF00,Time: {TIMER}s,0|BTN,215,8,95,32,#ef4444,ABORT,ABORT";

const char* SCREEN_C1_ABORT_CONFIRM =
    "#7f1d1d|ST,10,15,#FFFFFF,Abort Operation?|ST,10,45,#FFFFFF,Coins Won't Be Refunded|BTN,20,100,130,45,#dc2626,ABORT,ABORT_YES|BTN,170,100,130,45,#334155,RESUME,ABORT_NO";

const char* SCREEN_C1_SAFETY_PAUSE =
    "#800000|ST,10,20,#FFFFFF,SAFETY PAUSE|TT,10,60,#FFFF00,CLOSE DOOR / REPLACE HELMET,300";

const char* SCREEN_C1_RETRIEVE =
    "#006400|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Retrieve";

const char* SCREEN_C1_RETRIEVE_CLOSE =
    "#006400|ST,10,20,#FFFFFF,Headgear Retrieved!|ST,10,50,#FFFFFF,Please Close the Door|ST,10,75,#FFFFFF,to Finish";

const char* SCREEN_C1_FINISH =
    "#006400|ST,10,20,#FFFFFF,Thank you for your|ST,10,45,#FFFFFF,Patronage!|ST,10,80,#FFFF00,We Hope to See you Again! :D";

void loadSettingsFromNVS() {
    prefs.begin("vending_nvs", false);
    secondsPerCoin = prefs.getUShort("sec_coin", 20);

    statTotalSessions    = prefs.getUInt("st_sess", 0);
    statTotalCoins       = prefs.getUInt("st_coins", 0);
    statTotalDurationSec = prefs.getUInt("st_dur", 0);

    Serial.println("[MASTER NVS] Settings & Statistics Loaded.");
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Direct Command Dispatcher
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
    Serial.printf("[MASTER ESP-NOW TX] Broadcast Status: %s\n", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS (ACK)" : "FAIL (NO-ACK)");
}

void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr) {
    CommandPacket cmd;
    memset(&cmd, 0, sizeof(CommandPacket));
    cmd.targetDeviceID = targetDev;
    cmd.commandID      = cmdId;
    cmd.subIndex       = subIdx;
    cmd.param16        = param;
    cmd.state          = state;
    if (str != nullptr) {
        strncpy(cmd.payloadStr, str, sizeof(cmd.payloadStr) - 1);
        cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
    }

    const char* devName = (targetDev == DEVICE_A1) ? "Node A1" : (targetDev == DEVICE_A2) ? "Node A2" :
                          (targetDev == DEVICE_ACS) ? "Node ACS" : (targetDev == DEVICE_C1) ? "Node C1" :
                          (targetDev == DEVICE_C2) ? "Node C2" : "Broadcast";
    Serial.printf("[MASTER ESP-NOW TX] >>> Sending Command Opcode %d to %s (Param: %d, State: %d)...\n", cmdId, devName, param, state);

    esp_err_t result = esp_now_send(BROADCAST_MAC, (uint8_t*)&cmd, sizeof(CommandPacket));
    if (result != ESP_OK) {
        Serial.printf("[MASTER ESP-NOW TX] ERROR: Send failed with code %d\n", result);
    }
}

// Mutes A1's coin ISR for the EMI burst window and schedules a TFT repaint shortly after,
// to paint over any visual glitch the solenoid/relay switching causes on A1's display.
void muteA1AndScheduleRedraw() {
    sendEspNowCmdDirect(DEVICE_A1, CMD_MUTE_COINS, 0, 250, 0); // 250ms EMI Lockout
    a1RedrawAtMillis = millis() + 300; // let the switching transient settle, then force a clean repaint
}

void applyRelayBitmask(uint8_t mask) {
    Serial.printf("[MASTER ACTUATION] Applying Relay Bitmask 0b%05b across Node A2.\n", mask);
    muteA1AndScheduleRedraw();
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t state = (mask >> i) & 0x01;
        sendEspNowCmdDirect(DEVICE_A2, CMD_SET_RELAY, i, 0, state);
    }
}

// Mist stays on for the whole cleaning step; UV follows uvBlinkState (60s on/off).
void applyCleaningRelays() {
    applyRelayBitmask(0b00001000 | (uvBlinkState ? 0b00010000 : 0b00000000));
}

// Same EMI-glitch cosmetic fix as muteA1AndScheduleRedraw(), for C1's TFT.
void muteC1AndScheduleRedraw() {
    sendEspNowCmdDirect(DEVICE_C1, CMD_MUTE_COINS, 0, 250, 0);
    c1RedrawAtMillis = millis() + 300;
}

void applyC2RelayBitmask(uint8_t mask) {
    Serial.printf("[MASTER ACTUATION] Applying Relay Bitmask 0b%06b across Node C2.\n", mask);
    muteC1AndScheduleRedraw();
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t state = (mask >> i) & 0x01;
        sendEspNowCmdDirect(DEVICE_C2, CMD_SET_RELAY, i, 0, state);
    }
}

// Heater + Fan both stay on for the whole heating step - no blink cycle needed (diagram
// doesn't call for one, unlike Box 1's UV toggle).
void applyC2HeatingRelays() {
    applyC2RelayBitmask(0b101000); // bit3=Heater ON, bit5=Fan ON
}

// -------------------------------------------------------------
// Sensor / Threshold Helpers
// -------------------------------------------------------------
bool helmetPresent() {
    return (nodeA2_Data.usHelmetDistance > 0.0f && nodeA2_Data.usHelmetDistance < 15.0f);
}

bool enclosureClosed() {
    return !nodeA2_Data.doorEnclosure;
}

bool box2HelmetPresent() {
    return (nodeC2_Data.usHelmetDistance > 0.0f && nodeC2_Data.usHelmetDistance < 30.0f);
}

bool box2EnclosureClosed() {
    return !nodeC2_Data.doorEnclosure;
}

const char* acsAutoStateName(uint8_t s) {
    switch (s) {
        case ACS_AUTO_IDLE:           return "Idle";
        case ACS_AUTO_PRECHECK:       return "Waiting for Ingredients";
        case ACS_AUTO_REFILL_WATER:   return "Pumping Water";
        case ACS_AUTO_REFILL_SCENTED: return "Pumping Scented Liquid";
        case ACS_AUTO_REFILL_ALCOHOL: return "Pumping Alcohol";
        case ACS_AUTO_MIXING:         return "Mixing";
        case ACS_AUTO_FINISHING:      return "Finishing";
        case ACS_AUTO_DELIVERING:     return "Delivering to Box 1";
        default:                      return "Unknown";
    }
}

float alcoholPercent(float distCm) {
    if (distCm <= 0.0f) return 0.0f; // sensor read failure - treat as empty for safety
    float pct = (ALC_DIST_EMPTY_CM - distCm) / (ALC_DIST_EMPTY_CM - ALC_DIST_FULL_CM) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

// How much mL the Humidifier Container needs to reach its safe fill line, same L*W*(H-dist)
// math ACS uses for its own tanks.
uint16_t humidifierVolumeNeededML(float distCm) {
    float liquidHeight = HUMID_HEIGHT_CM - distCm;
    if (liquidHeight < 0.0f) liquidHeight = 0.0f;
    float remainML = HUMID_LENGTH_CM * HUMID_WIDTH_CM * liquidHeight;

    float safeHeight = HUMID_HEIGHT_CM - ALC_DIST_FULL_CM;
    float capacityML = HUMID_LENGTH_CM * HUMID_WIDTH_CM * safeHeight;

    float needed = capacityML - remainML;
    if (needed < 0.0f) needed = 0.0f;
    if (needed > 65535.0f) needed = 65535.0f; // fits param16
    return (uint16_t)needed;
}

// Maps the machine's current state back to whichever SCREEN_* is currently showing on A1,
// for the post-solenoid repaint (a1RedrawAtMillis). Mirrors the two states with a
// sub-variant driven by an edge-detect flag rather than the state alone.
const char* screenForCurrentState() {
    switch (currentMachineState) {
        case STATE_IDLE:          return SCREEN_WELCOME;
        case STATE_INSTRUCTIONS:  return SCREEN_INSTRUCTIONS;
        case STATE_CHECKING:      return SCREEN_CHECKING;
        case STATE_REFILLING:     return SCREEN_REFILLING;
        case STATE_SENSORS:       return sensorsWasClosed ? SCREEN_SENSORS_RETRY : SCREEN_SENSORS;
        case STATE_CLEANING:      return SCREEN_CLEANING;
        case STATE_ABORT_CONFIRM: return SCREEN_ABORT_CONFIRM;
        case STATE_PAUSED_SAFETY: return SCREEN_SAFETY_PAUSE;
        case STATE_RETRIEVE:      return retrieveConfirmed ? SCREEN_RETRIEVE_CLOSE : SCREEN_RETRIEVE;
        case STATE_FINISH:        return SCREEN_FINISH;
        default:                  return SCREEN_WELCOME;
    }
}

const char* stateName(MachineState s) {
    switch (s) {
        case STATE_IDLE:          return "IDLE - Insert Coin";
        case STATE_INSTRUCTIONS:  return "Open Enclosure Door";
        case STATE_CHECKING:      return "Checking Alcohol Level";
        case STATE_REFILLING:     return "Refilling Alcohol";
        case STATE_SENSORS:       return "Place Headgear & Close Door";
        case STATE_CLEANING:      return "Cleaning In Progress";
        case STATE_ABORT_CONFIRM: return "Abort Confirmation";
        case STATE_RETRIEVE:      return "Retrieve Headgear";
        case STATE_FINISH:        return "Cycle Complete";
        case STATE_PAUSED_SAFETY: return "Paused - Safety";
        default:                  return "UNKNOWN";
    }
}

// Mirrors screenForCurrentState(), for Box 2 / C1's post-solenoid repaint.
const char* screenForCurrentBox2State() {
    switch (box2CurrentState) {
        case B2_STATE_IDLE:          return SCREEN_C1_WELCOME;
        case B2_STATE_INSTRUCTIONS:  return SCREEN_C1_INSTRUCTIONS;
        case B2_STATE_SENSORS:       return box2SensorsWasClosed ? SCREEN_C1_SENSORS_RETRY : SCREEN_C1_SENSORS;
        case B2_STATE_HEATING:       return SCREEN_C1_HEATING;
        case B2_STATE_ABORT_CONFIRM: return SCREEN_C1_ABORT_CONFIRM;
        case B2_STATE_PAUSED_SAFETY: return SCREEN_C1_SAFETY_PAUSE;
        case B2_STATE_RETRIEVE:      return box2RetrieveConfirmed ? SCREEN_C1_RETRIEVE_CLOSE : SCREEN_C1_RETRIEVE;
        case B2_STATE_FINISH:        return SCREEN_C1_FINISH;
        default:                     return SCREEN_C1_WELCOME;
    }
}

const char* box2StateName(Box2State s) {
    switch (s) {
        case B2_STATE_IDLE:          return "IDLE - Insert Coin";
        case B2_STATE_INSTRUCTIONS:  return "Open Enclosure Door";
        case B2_STATE_SENSORS:       return "Place Headgear & Close Door";
        case B2_STATE_HEATING:       return "Heating In Progress";
        case B2_STATE_ABORT_CONFIRM: return "Abort Confirmation";
        case B2_STATE_RETRIEVE:      return "Retrieve Headgear";
        case B2_STATE_FINISH:        return "Cycle Complete";
        case B2_STATE_PAUSED_SAFETY: return "Paused - Safety";
        default:                     return "UNKNOWN";
    }
}

void recordCompletedSession() {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesA1 >= sessionCoinsAtStart) ? (lastKnownPulsesA1 - sessionCoinsAtStart) : 0;
    uint16_t durSec16 = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    uint16_t coins16  = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;

    statTotalSessions++;
    statTotalDurationSec += durationSec;
    statHistory[statHistoryHead] = { durSec16, coins16 };
    statHistoryHead = (statHistoryHead + 1) % STAT_HISTORY_SIZE;
    if (statHistoryCount < STAT_HISTORY_SIZE) statHistoryCount++;

    prefs.putUInt("st_sess", statTotalSessions);
    prefs.putUInt("st_dur", statTotalDurationSec);
    Serial.printf("[MASTER STATS] Session #%d recorded: %ds, %d coins.\n", statTotalSessions, durSec16, coins16);
}

// Box 2 sessions feed the SAME shared stat counters/history as Box 1 (combined revenue
// dashboard) rather than a separate per-box breakdown - simplest option, easy to split later.
void recordCompletedBox2Session() {
    uint32_t durationSec = (millis() - box2SessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesC1 >= box2SessionCoinsAtStart) ? (lastKnownPulsesC1 - box2SessionCoinsAtStart) : 0;
    uint16_t durSec16 = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    uint16_t coins16  = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;

    statTotalSessions++;
    statTotalDurationSec += durationSec;
    statHistory[statHistoryHead] = { durSec16, coins16 };
    statHistoryHead = (statHistoryHead + 1) % STAT_HISTORY_SIZE;
    if (statHistoryCount < STAT_HISTORY_SIZE) statHistoryCount++;

    prefs.putUInt("st_sess", statTotalSessions);
    prefs.putUInt("st_dur", statTotalDurationSec);
    Serial.printf("[MASTER STATS] Box 2 Session #%d recorded: %ds, %d coins.\n", statTotalSessions, durSec16, coins16);
}

// -------------------------------------------------------------
// WebSocket Event Handler
// -------------------------------------------------------------
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    if (!arg || !data || len == 0) return;
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (!info) return;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        char* msg = (char*)data;

        if (msg[0] == '{') {
            StaticJsonDocument<768> doc;
            if (deserializeJson(doc, msg) == DeserializationError::Ok) {
                // "target" messages (buzzer/color/relay) also carry a numeric "cmd" field, so
                // "target" must be checked first - otherwise every one of them gets misrouted
                // into the string-cmd branch below and silently dropped.
                if (doc.containsKey("target")) {
                    uint8_t target = doc["target"] | 0;
                    uint8_t cmdId  = doc["cmd"] | 0;
                    if (target == DEVICE_A1) {
                        Serial.printf("[MASTER WS] Dispatching command %d to Node A1...\n", cmdId);
                        sendEspNowCmdDirect(DEVICE_A1, cmdId, 0, doc["param"] | 0, 0);
                    } else if (target == DEVICE_A2) {
                        Serial.printf("[MASTER WS] Dispatching relay %d (State: %d) to Node A2...\n", doc["relayIdx"] | 0, doc["state"] | 0);
                        muteA1AndScheduleRedraw();
                        sendEspNowCmdDirect(DEVICE_A2, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    } else if (target == DEVICE_ACS) {
                        Serial.printf("[MASTER WS] Dispatching relay %d (State: %d) to Node ACS...\n", doc["relayIdx"] | 0, doc["state"] | 0);
                        sendEspNowCmdDirect(DEVICE_ACS, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    } else if (target == DEVICE_C1) {
                        Serial.printf("[MASTER WS] Dispatching command %d to Node C1...\n", cmdId);
                        sendEspNowCmdDirect(DEVICE_C1, cmdId, 0, doc["param"] | 0, 0);
                    } else if (target == DEVICE_C2) {
                        Serial.printf("[MASTER WS] Dispatching relay %d (State: %d) to Node C2...\n", doc["relayIdx"] | 0, doc["state"] | 0);
                        muteC1AndScheduleRedraw();
                        sendEspNowCmdDirect(DEVICE_C2, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    }
                } else if (doc.containsKey("cmd")) {
                    const char* cmd = doc["cmd"];
                    if (cmd && strcmp(cmd, "save_spc") == 0) {
                        secondsPerCoin = doc["val"] | 20;
                        prefs.putUShort("sec_coin", secondsPerCoin);
                        Serial.printf("[MASTER WS] Updated secondsPerCoin to %d sec.\n", secondsPerCoin);
                    } else if (cmd && strcmp(cmd, "reset_stats") == 0) {
                        statTotalSessions = 0;
                        statTotalCoins = 0;
                        statTotalDurationSec = 0;
                        statHistoryCount = 0;
                        statHistoryHead = 0;
                        prefs.putUInt("st_sess", 0);
                        prefs.putUInt("st_coins", 0);
                        prefs.putUInt("st_dur", 0);
                        Serial.println("[MASTER WS] Statistics counters reset.");
                    } else if (cmd && strcmp(cmd, "acs_maintenance") == 0) {
                        uint8_t state = doc["state"] | 0;
                        Serial.printf("[MASTER WS] Setting ACS Maintenance Mode to %s...\n", state ? "ON" : "OFF");
                        sendEspNowCmdDirect(DEVICE_ACS, CMD_SET_MAINTENANCE, 0, 0, state);
                    }
                }
            }
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len);
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Receive Callback
// -------------------------------------------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0) return;

    const uint8_t* mac = info->src_addr;

    if (len == sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));
        Serial.printf("[MASTER ESP-NOW RX] Command from MAC %02X:%02X:%02X:%02X:%02X:%02X | Opcode: %d\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], cmd.commandID);

        if (cmd.commandID == CMD_TOUCH_ACTION) {
            Serial.printf("[MASTER TOUCH TRIGGER] Received action: '%s'\n", cmd.payloadStr);

            // CommandPacket carries no source device ID - route by which terminal's learned
            // MAC this came from. Defaults to Box 1 handling if C1's MAC isn't known yet.
            bool fromC1 = macC1Known && memcmp(mac, macC1, 6) == 0;

            if (fromC1) {
                if (strcmp(cmd.payloadStr, "RESET") == 0) {
                    box2ActiveTimer = 0;
                    box2CurrentState = B2_STATE_IDLE;
                    applyC2RelayBitmask(0b000000);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_WELCOME);
                } else if (strcmp(cmd.payloadStr, "ABORT") == 0 && box2CurrentState == B2_STATE_HEATING) {
                    box2CurrentState = B2_STATE_ABORT_CONFIRM;
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_ABORT_CONFIRM);
                } else if (strcmp(cmd.payloadStr, "ABORT_YES") == 0 && box2CurrentState == B2_STATE_ABORT_CONFIRM) {
                    Serial.println("[MASTER STEP ENGINE] Box 2 Cycle Cancelled by user (Abort Confirmed).");
                    box2ActiveTimer = 0;
                    box2CurrentState = B2_STATE_RETRIEVE;
                    box2ConditionHoldTicks = 0;
                    box2RetrieveConfirmed = false;
                    applyC2RelayBitmask(0b000001); // unlock enclosure only
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 500, 0);
                } else if (strcmp(cmd.payloadStr, "ABORT_NO") == 0 && box2CurrentState == B2_STATE_ABORT_CONFIRM) {
                    box2CurrentState = B2_STATE_HEATING;
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                }
            } else {
                if (strcmp(cmd.payloadStr, "RESET") == 0) {
                    activeTimer = 0;
                    currentMachineState = STATE_IDLE;
                    applyRelayBitmask(0b00000000);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_WELCOME);
                } else if (strcmp(cmd.payloadStr, "ABORT") == 0 && currentMachineState == STATE_CLEANING) {
                    currentMachineState = STATE_ABORT_CONFIRM;
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_ABORT_CONFIRM);
                } else if (strcmp(cmd.payloadStr, "ABORT_YES") == 0 && currentMachineState == STATE_ABORT_CONFIRM) {
                    Serial.println("[MASTER STEP ENGINE] Cycle Cancelled by user (Abort Confirmed).");
                    activeTimer = 0;
                    currentMachineState = STATE_RETRIEVE;
                    conditionHoldTicks = 0;
                    retrieveConfirmed = false;
                    applyRelayBitmask(0b00000001); // unlock enclosure only
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_RETRIEVE);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 500, 0);
                } else if (strcmp(cmd.payloadStr, "ABORT_NO") == 0 && currentMachineState == STATE_ABORT_CONFIRM) {
                    currentMachineState = STATE_CLEANING;
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_CLEANING);
                }
            }
        }
        return;
    }

    if (len == sizeof(ACSTelemetryPacket)) {
        memcpy(&nodeACS_Data, incomingData, sizeof(ACSTelemetryPacket));
        lastSeenACS = millis();
        return;
    }

    if (len != sizeof(TelemetryPacket)) return;

    TelemetryPacket packet;
    memcpy(&packet, incomingData, sizeof(TelemetryPacket));

    if (packet.deviceID == DEVICE_A1) {
        if (!macA1Known) { memcpy(macA1, mac, 6); macA1Known = true; }
        if (packet.pulseCount > lastKnownPulsesA1) {
            uint32_t newPulses = packet.pulseCount - lastKnownPulsesA1;
            uint32_t addedSeconds = newPulses * secondsPerCoin;
            activeTimer += addedSeconds;
            lastKnownPulsesA1 = packet.pulseCount;
            statTotalCoins += newPulses;
            prefs.putUInt("st_coins", statTotalCoins);
            Serial.printf("[MASTER COIN RECEIPT] Validated +%d seconds from A1. activeTimer = %d sec\n", addedSeconds, activeTimer);
        }
        nodeA1_Data = packet;
        lastSeenA1  = millis();
    } else if (packet.deviceID == DEVICE_A2) {
        nodeA2_Data = packet;
        lastSeenA2  = millis();
    } else if (packet.deviceID == DEVICE_C1) {
        if (!macC1Known) { memcpy(macC1, mac, 6); macC1Known = true; }
        if (packet.pulseCount > lastKnownPulsesC1) {
            uint32_t newPulses = packet.pulseCount - lastKnownPulsesC1;
            uint32_t addedSeconds = newPulses * secondsPerCoin;
            box2ActiveTimer += addedSeconds;
            lastKnownPulsesC1 = packet.pulseCount;
            statTotalCoins += newPulses;
            prefs.putUInt("st_coins", statTotalCoins);
            Serial.printf("[MASTER COIN RECEIPT] Validated +%d seconds from C1 (Box 2). box2ActiveTimer = %d sec\n", addedSeconds, box2ActiveTimer);
        }
        nodeC1_Data = packet;
        lastSeenC1  = millis();
    } else if (packet.deviceID == DEVICE_C2) {
        nodeC2_Data = packet;
        lastSeenC2  = millis();
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   MASTER SERVER: DIRECT TRANSMIT ENGINE (CORE 3.3.7)");
    Serial.println("=======================================================");

    memset(&nodeA1_Data, 0, sizeof(TelemetryPacket));
    memset(&nodeA2_Data, 0, sizeof(TelemetryPacket));
    memset(&nodeACS_Data, 0, sizeof(ACSTelemetryPacket));
    memset(&nodeC1_Data, 0, sizeof(TelemetryPacket));
    memset(&nodeC2_Data, 0, sizeof(TelemetryPacket));

    loadSettingsFromNVS();

    // 1. SoftAP Setup
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);

    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[MASTER WIFI] SoftAP Online -> SSID: '%s' | IP: %s\n", AP_SSID, apIP.toString().c_str());

    // 2. Captive Portal DNS
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("[MASTER DNS] Captive Portal active on Port 53.");

    // 3. ESP-NOW Initialization & Broadcast Peer Registration
    if (esp_now_init() != ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Initialization Failed!");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Broadcast Peer {FF:FF:FF:FF:FF:FF} Registered on Channel 1.");
    }

    // 4. WebServer Static Routes
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/css", STYLE_CSS);
    });

    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "application/javascript", SCRIPT_JS);
    });

    // -------------------------------------------------------------
    // EXPLICIT HTTP REST API ROUTES (Guaranteed to execute)
    // -------------------------------------------------------------
    server.on("/api/buzzer", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request) return;
        Serial.println("\n[MASTER HTTP API] >>> /api/buzzer HIT! Triggering Buzzer on Node A1...");
        sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
        request->send(200, "text/plain", "OK: Buzzer Triggered");
    });

    server.on("/api/trigger-buzzer", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request) return;
        Serial.println("\n[MASTER HTTP API] >>> /api/trigger-buzzer HIT! Triggering Buzzer on Node A1...");
        sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
        request->send(200, "text/plain", "OK: Buzzer Triggered");
    });

    server.on("/api/reset-coins", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request) return;
        Serial.println("\n[MASTER HTTP API] >>> /api/reset-coins HIT! Resetting coin pulses...");
        sendEspNowCmdDirect(DEVICE_A1, CMD_RESET_COINS, 0, 0, 0);
        request->send(200, "text/plain", "OK: Coins Reset");
    });

    server.on("/api/relay", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request) return;
        uint8_t rIdx = 0;
        uint8_t rState = 1;
        if (request->hasParam("idx")) rIdx = request->getParam("idx")->value().toInt();
        if (request->hasParam("state")) rState = request->getParam("state")->value().toInt();

        Serial.printf("\n[MASTER HTTP API] >>> /api/relay HIT! Setting Relay %d to State %d...\n", rIdx, rState);
        muteA1AndScheduleRedraw();
        sendEspNowCmdDirect(DEVICE_A2, CMD_SET_RELAY, rIdx, 0, rState);
        request->send(200, "text/plain", "OK: Relay Command Dispatched");
    });

    server.on("/api/color", HTTP_ANY, [](AsyncWebServerRequest *request) {
        if (!request) return;
        uint16_t color = 0x001F;
        if (request->hasParam("color")) color = (uint16_t)request->getParam("color")->value().toInt();
        Serial.printf("\n[MASTER HTTP API] >>> /api/color HIT! Setting Color 0x%04X on Node A1...\n", color);
        sendEspNowCmdDirect(DEVICE_A1, CMD_SET_COLOR, 0, color, 0);
        request->send(200, "text/plain", "OK: Color Dispatched");
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/html", INDEX_HTML);
    });

    server.begin();
    Serial.println("[MASTER HTTP] Web Server Online & Direct Routes Bound.");

    // Initial Broadcast of Step 0 (Welcome) Screen
    Serial.println("[MASTER BOOT] Broadcasting Welcome screen to Node A1...");
    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_WELCOME);

    Serial.println("[MASTER BOOT] Broadcasting Welcome screen to Node C1 (Box 2)...");
    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_WELCOME);
}

void loop() {
    dnsServer.processNextRequest();
    ws.cleanupClients();

    unsigned long currentMillis = millis();

    // Post-solenoid TFT repaint - checked every loop pass (not the 1s gate below) so the
    // 300ms window from muteA1AndScheduleRedraw() is honored promptly.
    if (a1RedrawAtMillis != 0 && currentMillis >= a1RedrawAtMillis) {
        a1RedrawAtMillis = 0;
        sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, screenForCurrentState());
    }
    if (c1RedrawAtMillis != 0 && currentMillis >= c1RedrawAtMillis) {
        c1RedrawAtMillis = 0;
        sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, screenForCurrentBox2State());
    }

    // 1-Second Master Execution Tick
    if (currentMillis - lastOneSecTick >= 1000) {
        lastOneSecTick = currentMillis;

        bool handshakeValid = enclosureClosed() && helmetPresent();

        // AUTO_CALL: A2's own humidifier level drives a delivery request to ACS, routed
        // through here (not A2 -> ACS directly) so it gets logged and stays visible on the
        // dashboard. Edge-triggered so it only fires once per low event, not every second.
        float humidPct = alcoholPercent(nodeA2_Data.usAlcoholDistance);
        if (humidPct <= ALC_LOW_PCT && !humidifierRefillRequested) {
            humidifierRefillRequested = true;
            uint16_t neededML = humidifierVolumeNeededML(nodeA2_Data.usAlcoholDistance);
            Serial.printf("[MASTER AUTO_CALL] Humidifier low (%.0f%%) -> requesting %umL delivery from ACS.\n", humidPct, neededML);
            sendEspNowCmdDirect(DEVICE_ACS, CMD_REQUEST_DELIVERY, 0, neededML, 0);
        } else if (humidPct > ALC_LOW_PCT) {
            humidifierRefillRequested = false; // re-arm for the next time it drops low
        }

        switch (currentMachineState) {
            case STATE_IDLE:
                // Step 0: waiting for a coin
                if (activeTimer > 0) {
                    Serial.println("[MASTER STEP ENGINE] Coin detected -> Step 1 (Instructions)");
                    currentMachineState = STATE_INSTRUCTIONS;
                    conditionHoldTicks = 0;
                    applyRelayBitmask(0b00000001); // unlock enclosure
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_INSTRUCTIONS);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case STATE_INSTRUCTIONS:
                // Step 1: wait for enclosure door OPEN for >2 seconds
                if (nodeA2_Data.doorEnclosure) {
                    conditionHoldTicks++;
                } else {
                    conditionHoldTicks = 0;
                }
                if (conditionHoldTicks >= 2) {
                    Serial.println("[MASTER STEP ENGINE] Door opened -> Step 2 (Checking Alcohol)");
                    currentMachineState = STATE_CHECKING;
                    applyRelayBitmask(0b00000000); // re-lock enclosure
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_CHECKING);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case STATE_CHECKING: {
                // Step 2: branch on alcohol level
                float pct = alcoholPercent(nodeA2_Data.usAlcoholDistance);
                if (pct <= ALC_LOW_PCT) {
                    Serial.printf("[MASTER STEP ENGINE] Alcohol low (%.0f%%) -> Step 3 (Refilling)\n", pct);
                    currentMachineState = STATE_REFILLING;
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_REFILLING);
                } else {
                    Serial.printf("[MASTER STEP ENGINE] Alcohol OK (%.0f%%) -> Step 4 (Sensors)\n", pct);
                    currentMachineState = STATE_SENSORS;
                    sensorsWasClosed = false;
                    applyRelayBitmask(0b00000001); // unlock enclosure so the helmet can be placed
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_SENSORS);
                }
                break;
            }

            case STATE_REFILLING: {
                // Step 3: wait for alcohol to reach HIGH threshold
                float pct = alcoholPercent(nodeA2_Data.usAlcoholDistance);
                if (pct >= ALC_HIGH_PCT) {
                    Serial.println("[MASTER STEP ENGINE] Refill complete -> Step 4 (Sensors)");
                    currentMachineState = STATE_SENSORS;
                    sensorsWasClosed = false;
                    applyRelayBitmask(0b00000001); // unlock enclosure so the helmet can be placed
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_SENSORS);
                }
                break;
            }

            case STATE_SENSORS: {
                // Step 4: enclosure is unlocked here so the user can open it and place the helmet.
                bool closedNow = enclosureClosed();
                if (closedNow && helmetPresent()) {
                    Serial.println("[MASTER STEP ENGINE] Handshake verified -> Step 5 (Cleaning)");
                    currentMachineState = STATE_CLEANING;
                    sessionStartMillis  = millis();
                    sessionCoinsAtStart = lastKnownPulsesA1;
                    uvBlinkTicks  = 0;
                    uvBlinkState  = true;
                    applyCleaningRelays(); // also re-locks the enclosure
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_CLEANING);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
                } else if (closedNow && !helmetPresent() && !sensorsWasClosed) {
                    // Door was just closed without a helmet inside - it's still unlocked, so
                    // nudge the user to open it again and try once instead of hanging forever.
                    Serial.println("[MASTER STEP ENGINE] Door closed without helmet - prompting retry (still unlocked).");
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_SENSORS_RETRY);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
                }
                sensorsWasClosed = closedNow;
                break;
            }

            case STATE_CLEANING:
                if (!handshakeValid) {
                    // Unexpected safety breach mid-cleaning (distinct from a deliberate Abort)
                    Serial.println("[MASTER STEP ENGINE] Safety Breach! Door opened or helmet removed.");
                    currentMachineState = STATE_PAUSED_SAFETY;
                    applyRelayBitmask(0b00000000);
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_SAFETY_PAUSE);
                } else if (activeTimer > 0) {
                    activeTimer--;

                    uvBlinkTicks++;
                    if (uvBlinkTicks >= 60) {
                        uvBlinkTicks = 0;
                        uvBlinkState = !uvBlinkState;
                        applyCleaningRelays();
                    }

                    if (activeTimer == 0) {
                        Serial.println("[MASTER STEP ENGINE] Cycle Complete! -> Step 6 (Retrieve)");
                        recordCompletedSession();
                        currentMachineState = STATE_RETRIEVE;
                        conditionHoldTicks = 0;
                        retrieveConfirmed = false;
                        applyRelayBitmask(0b00000001); // unlock enclosure, mist+UV off
                        sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_RETRIEVE);
                        sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 500, 0);
                    }
                }
                break;

            case STATE_PAUSED_SAFETY:
                if (handshakeValid) {
                    Serial.println("[MASTER STEP ENGINE] Safety Restored. Resuming Step 5 (Cleaning).");
                    currentMachineState = STATE_CLEANING;
                    applyCleaningRelays();
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_CLEANING);
                }
                break;

            case STATE_ABORT_CONFIRM:
                // Countdown/UV blink is frozen here; touch actions ABORT_YES/ABORT_NO drive the transition.
                break;

            case STATE_RETRIEVE:
                // Step 6a: wait for door OPEN + helmet removed, both for >2 seconds
                if (!retrieveConfirmed) {
                    if (nodeA2_Data.doorEnclosure && !helmetPresent()) {
                        conditionHoldTicks++;
                    } else {
                        conditionHoldTicks = 0;
                    }
                    if (conditionHoldTicks >= 2) {
                        Serial.println("[MASTER STEP ENGINE] Headgear retrieved -> waiting for door to close");
                        retrieveConfirmed = true;
                        conditionHoldTicks = 0;
                        sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_RETRIEVE_CLOSE);
                        sendEspNowCmdDirect(DEVICE_A1, CMD_BUZZER, 0, 250, 0);
                    }
                } else {
                    // Step 6b: wait for the door to be CLOSED again for >2 seconds before finishing
                    if (!nodeA2_Data.doorEnclosure) {
                        conditionHoldTicks++;
                    } else {
                        conditionHoldTicks = 0;
                    }
                    if (conditionHoldTicks >= 2) {
                        Serial.println("[MASTER STEP ENGINE] Door closed -> Step 7 (Finish)");
                        currentMachineState = STATE_FINISH;
                        retrieveConfirmed = false;
                        stepRemainingSec = 4;
                        applyRelayBitmask(0b00000000); // lock enclosure
                        sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_FINISH);
                    }
                }
                break;

            case STATE_FINISH:
                if (stepRemainingSec > 0) {
                    stepRemainingSec--;
                } else {
                    activeTimer = 0;
                    currentMachineState = STATE_IDLE;
                    sendEspNowCmdDirect(DEVICE_A1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_WELCOME);
                }
                break;
        }

        // -------------------------------------------------------------
        // Box 2 (Heater) Step Engine - independent parallel cycle, same 1s tick.
        // -------------------------------------------------------------
        switch (box2CurrentState) {
            case B2_STATE_IDLE:
                // Step 0: waiting for a coin
                if (box2ActiveTimer > 0) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Coin detected -> Step 1 (Instructions)");
                    box2CurrentState = B2_STATE_INSTRUCTIONS;
                    box2ConditionHoldTicks = 0;
                    applyC2RelayBitmask(0b000001); // unlock enclosure
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_INSTRUCTIONS);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case B2_STATE_INSTRUCTIONS:
                // Step 1: wait for enclosure door OPEN for >2 seconds
                if (nodeC2_Data.doorEnclosure) {
                    box2ConditionHoldTicks++;
                } else {
                    box2ConditionHoldTicks = 0;
                }
                if (box2ConditionHoldTicks >= 2) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Door opened -> Step 2 (Sensors)");
                    box2CurrentState = B2_STATE_SENSORS;
                    box2SensorsWasClosed = false;
                    applyC2RelayBitmask(0b000000); // re-lock enclosure
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SENSORS);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case B2_STATE_SENSORS: {
                // Step 2: enclosure was already opened during Step 1 - door doesn't need to
                // stay unlocked here, just watch for it to close again with a helmet inside.
                bool closedNow = box2EnclosureClosed();
                if (closedNow && box2HelmetPresent()) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Handshake verified -> Step 3 (Heating)");
                    box2CurrentState = B2_STATE_HEATING;
                    box2SessionStartMillis  = millis();
                    box2SessionCoinsAtStart = lastKnownPulsesC1;
                    applyC2HeatingRelays();
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                } else if (closedNow && !box2HelmetPresent() && !box2SensorsWasClosed) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Door closed without helmet - prompting retry.");
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SENSORS_RETRY);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                }
                box2SensorsWasClosed = closedNow;
                break;
            }

            case B2_STATE_HEATING: {
                bool handshakeValidB2 = box2EnclosureClosed() && box2HelmetPresent();
                if (!handshakeValidB2) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Safety Breach! Door opened or helmet removed.");
                    box2CurrentState = B2_STATE_PAUSED_SAFETY;
                    applyC2RelayBitmask(0b000000);
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SAFETY_PAUSE);
                } else if (box2ActiveTimer > 0) {
                    box2ActiveTimer--;
                    if (box2ActiveTimer == 0) {
                        Serial.println("[MASTER STEP ENGINE] Box 2: Cycle Complete! -> Step 4 (Retrieve)");
                        recordCompletedBox2Session();
                        box2CurrentState = B2_STATE_RETRIEVE;
                        box2ConditionHoldTicks = 0;
                        box2RetrieveConfirmed = false;
                        applyC2RelayBitmask(0b000001); // unlock enclosure, heater+fan off
                        sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE);
                        sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 500, 0);
                    }
                }
                break;
            }

            case B2_STATE_PAUSED_SAFETY:
                if (box2EnclosureClosed() && box2HelmetPresent()) {
                    Serial.println("[MASTER STEP ENGINE] Box 2: Safety Restored. Resuming Step 3 (Heating).");
                    box2CurrentState = B2_STATE_HEATING;
                    applyC2HeatingRelays();
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                }
                break;

            case B2_STATE_ABORT_CONFIRM:
                // Touch actions ABORT_YES/ABORT_NO (routed by MAC in onDataRecv) drive the transition.
                break;

            case B2_STATE_RETRIEVE:
                // Step 4a: wait for door OPEN + helmet removed, both for >2 seconds
                if (!box2RetrieveConfirmed) {
                    if (nodeC2_Data.doorEnclosure && !box2HelmetPresent()) {
                        box2ConditionHoldTicks++;
                    } else {
                        box2ConditionHoldTicks = 0;
                    }
                    if (box2ConditionHoldTicks >= 2) {
                        Serial.println("[MASTER STEP ENGINE] Box 2: Headgear retrieved -> waiting for door to close");
                        box2RetrieveConfirmed = true;
                        box2ConditionHoldTicks = 0;
                        sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE_CLOSE);
                        sendEspNowCmdDirect(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                    }
                } else {
                    // Step 4b: wait for the door to be CLOSED again for >2 seconds before finishing
                    if (!nodeC2_Data.doorEnclosure) {
                        box2ConditionHoldTicks++;
                    } else {
                        box2ConditionHoldTicks = 0;
                    }
                    if (box2ConditionHoldTicks >= 2) {
                        Serial.println("[MASTER STEP ENGINE] Box 2: Door closed -> Step 5 (Finish)");
                        box2CurrentState = B2_STATE_FINISH;
                        box2RetrieveConfirmed = false;
                        box2StepRemainingSec = 2; // per the Heater flow diagram's own spec
                        applyC2RelayBitmask(0b000000); // lock enclosure
                        sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_FINISH);
                    }
                }
                break;

            case B2_STATE_FINISH:
                if (box2StepRemainingSec > 0) {
                    box2StepRemainingSec--;
                } else {
                    box2ActiveTimer = 0;
                    box2CurrentState = B2_STATE_IDLE;
                    sendEspNowCmdDirect(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_WELCOME);
                }
                break;
        }

        // Push Real-Time Telemetry to Node A1
        sendEspNowCmdDirect(DEVICE_A1, CMD_SYNC_VARS, 0, (uint16_t)activeTimer, 0);
        sendEspNowCmdDirect(DEVICE_C1, CMD_SYNC_VARS, 0, (uint16_t)box2ActiveTimer, 0);

        // Broadcast Real-time Status over WebSockets
        if (ws.count() > 0) {
            StaticJsonDocument<2048> doc; // bumped from 1536 to fit Box 2's fields
            doc["heap"]         = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            doc["a1_online"]    = (lastSeenA1 > 0) && (currentMillis - lastSeenA1 <= 3000);
            doc["a2_online"]    = (lastSeenA2 > 0) && (currentMillis - lastSeenA2 <= 3000);
            doc["c1_online"]    = (lastSeenC1 > 0) && (currentMillis - lastSeenC1 <= 3000);
            doc["c2_online"]    = (lastSeenC2 > 0) && (currentMillis - lastSeenC2 <= 3000);

            doc["mach_state"]   = (uint8_t)currentMachineState;
            doc["step_name"]    = stateName(currentMachineState);
            doc["step_sec_rem"] = stepRemainingSec;
            doc["active_timer"] = activeTimer;
            doc["handshake_ok"] = handshakeValid;
            doc["sec_per_coin"] = secondsPerCoin;

            doc["a1_pulses"]    = nodeA1_Data.pulseCount;
            doc["a1_tx"]        = nodeA1_Data.touchX;
            doc["a1_ty"]        = nodeA1_Data.touchY;
            doc["a1_tp"]        = nodeA1_Data.touchPressed;

            doc["a2_us_alc"]    = nodeA2_Data.usAlcoholDistance;
            doc["a2_alc_pct"]   = alcoholPercent(nodeA2_Data.usAlcoholDistance);
            doc["a2_us_helm"]   = nodeA2_Data.usHelmetDistance;
            doc["a2_m_enc"]     = nodeA2_Data.doorEnclosure;
            doc["a2_m_pan"]     = nodeA2_Data.doorPanel;
            doc["a2_m_bak"]     = nodeA2_Data.doorBackdoor;

            doc["a2_r_enc"]     = nodeA2_Data.relayStates[RELAY_ENCLOSURE_LOCK];
            doc["a2_r_pan"]     = nodeA2_Data.relayStates[RELAY_PANEL_LOCK];
            doc["a2_r_bak"]     = nodeA2_Data.relayStates[RELAY_BACKDOOR_LOCK];
            doc["a2_r_hum"]     = nodeA2_Data.relayStates[RELAY_HUMIDIFIER];
            doc["a2_r_uv"]      = nodeA2_Data.relayStates[RELAY_UV_LIGHT];

            doc["box2_mach_state"]   = (uint8_t)box2CurrentState;
            doc["box2_step_name"]    = box2StateName(box2CurrentState);
            doc["box2_step_sec_rem"] = box2StepRemainingSec;
            doc["box2_active_timer"] = box2ActiveTimer;
            doc["box2_handshake_ok"] = box2EnclosureClosed() && box2HelmetPresent();

            doc["c1_pulses"]    = nodeC1_Data.pulseCount;
            doc["c1_tx"]        = nodeC1_Data.touchX;
            doc["c1_ty"]        = nodeC1_Data.touchY;
            doc["c1_tp"]        = nodeC1_Data.touchPressed;

            doc["c2_us_helm"]   = nodeC2_Data.usHelmetDistance;
            doc["c2_m_enc"]     = nodeC2_Data.doorEnclosure;
            doc["c2_m_pan"]     = nodeC2_Data.doorPanel;
            doc["c2_m_bak"]     = nodeC2_Data.doorBackdoor;

            doc["c2_r_enc"]     = nodeC2_Data.relayStates[C2_RELAY_ENCLOSURE_LOCK];
            doc["c2_r_pan"]     = nodeC2_Data.relayStates[C2_RELAY_PANEL_LOCK];
            doc["c2_r_bak"]     = nodeC2_Data.relayStates[C2_RELAY_BACKDOOR_LOCK];
            doc["c2_r_heat"]    = nodeC2_Data.relayStates[C2_RELAY_HEATER];
            doc["c2_r_uv"]      = nodeC2_Data.relayStates[C2_RELAY_UV_LIGHT];
            doc["c2_r_fan"]     = nodeC2_Data.relayStates[C2_RELAY_FAN];

            doc["stat_sessions"] = statTotalSessions;
            doc["stat_revenue"]  = statTotalCoins * COIN_VALUE_PESO;
            doc["stat_coins"]    = statTotalCoins;
            doc["stat_avg_dur"]  = (statTotalSessions > 0) ? (statTotalDurationSec / statTotalSessions) : 0;

            JsonArray hist = doc.createNestedArray("stat_history");
            for (uint8_t i = 0; i < statHistoryCount; i++) {
                uint8_t idx = (statHistoryHead + STAT_HISTORY_SIZE - statHistoryCount + i) % STAT_HISTORY_SIZE;
                JsonObject rec = hist.createNestedObject();
                rec["d"] = statHistory[idx].durationSec;
                rec["r"] = statHistory[idx].coinsUsed * COIN_VALUE_PESO;
            }

            doc["acs_online"]  = (lastSeenACS > 0) && (currentMillis - lastSeenACS <= 3000);
            doc["acs_busy"]    = nodeACS_Data.acsBusy;
            doc["acs_auto"]    = acsAutoStateName(nodeACS_Data.autoState);
            doc["acs_maint"]   = nodeACS_Data.maintenanceMode;
            doc["acs_water"]   = nodeACS_Data.usWaterDistance;
            doc["acs_scented"] = nodeACS_Data.usScentedDistance;
            doc["acs_alcohol"] = nodeACS_Data.usAlcoholDistance;
            doc["acs_mixer"]   = nodeACS_Data.usMixerDistance;
            doc["acs_water_low"]   = nodeACS_Data.waterLow;
            doc["acs_scented_low"] = nodeACS_Data.scentedLow;
            doc["acs_alcohol_low"] = nodeACS_Data.alcoholLow;
            doc["acs_mixer_low"]   = nodeACS_Data.mixerLow;

            JsonArray acsRelays = doc.createNestedArray("acs_relays");
            for (uint8_t i = 0; i < 6; i++) acsRelays.add(nodeACS_Data.relayStates[i]);

            String output;
            serializeJson(doc, output);
            ws.textAll(output);
        }
    }
}
