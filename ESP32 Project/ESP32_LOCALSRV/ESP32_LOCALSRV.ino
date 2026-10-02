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

// Box 1's entire state machine (STATE_IDLE...STATE_FINISH, relay control, coin processing,
// touch-action handling, AUTO_CALL) now runs on Node S3A, not here - see ESP32_S3A.ino. Local
// Server just listens to S3A's BoxStatusPacket broadcast (nodeS3AStatus below) for dashboard
// display, and no longer drives A1/A2 directly for Box 1's automated cycle. Manual relay/
// buzzer testing from the dashboard still talks to A1/A2 directly, unchanged - that's an
// independent bench-testing path, separate from the automated cycle.
BoxStatusPacket nodeS3AStatus;
uint32_t lastKnownB1SessionSeq = 0;
bool     b1SessionSeqInitialized = false; // avoids recording a phantom "new session" using
                                           // stale data on Local Server's very first packet
uint32_t lastKnownPulsesA1 = 0; // stats-only pulse counter - independent of S3A's own copy,
                                 // which drives the actual cycle now

TelemetryPacket nodeA1_Data;
TelemetryPacket nodeA2_Data;
ACSTelemetryPacket nodeACS_Data;
unsigned long lastSeenA1 = 0;
unsigned long lastSeenA2 = 0;
unsigned long lastSeenACS = 0;
unsigned long lastOneSecTick = 0;
bool acsWasOnline = false; // edge-detect so a (re)connecting ACS always gets a fresh config push

// -------------------------------------------------------------
// Box 2's entire state machine now runs on Node S3C, not here - see ESP32_S3C.ino, and the
// Box2State enum in Shared_Common.h (shared, unlike before, since S3C now needs it too).
// Local Server just listens to S3C's BoxStatusPacket broadcast (nodeS3CStatus below) for
// dashboard display, mirroring nodeS3AStatus above. Manual C2 relay testing from the
// dashboard still talks to C1/C2 directly - independent bench-testing path, unchanged.
// -------------------------------------------------------------
BoxStatusPacket nodeS3CStatus;
uint32_t lastKnownB2SessionSeq = 0;
bool     b2SessionSeqInitialized = false; // same first-packet-skip guard as b1SessionSeqInitialized

uint32_t lastKnownPulsesC1 = 0; // stats-only pulse counter - independent of S3C's own copy,
                                 // which drives the actual cycle now

TelemetryPacket nodeC1_Data;
TelemetryPacket nodeC2_Data;
unsigned long lastSeenC1 = 0;
unsigned long lastSeenC2 = 0;
unsigned long lastSeenS3A = 0; // Box 1's Internal Server Management
unsigned long lastSeenS3C = 0; // Box 2's Internal Server Management

// CommandPacket carries no "who sent this" field, only "who it's addressed to" - so a touch
// action from A1 and one from C1 look identical except for the sender's MAC. Master learns
// each node's MAC from its own periodic telemetry (which does carry a deviceID). Originally
// just A1/C1 (for touch-action routing); now also used to send outgoing commands as unicast
// instead of broadcast, once a node's MAC is known - see sendEspNowCmdDirect(). Indexed by
// DeviceID (index 0/DEVICE_SERVER unused).
uint8_t knownMac[8][6] = {0};
bool    macKnown[8]    = {false};

// -------------------------------------------------------------
// Alcohol Tank (Humidifier Container) Calibration - ultrasonic distance, sensor-to-liquid.
// Tank is 9cm(H) x 15cm(L) x 8cm(W). Measured against the real unit. The physical geometry
// (EMPTY/FULL cm, dimensions) stays fixed - it's tied to the actual tank measurements, not
// something a technician retunes day-of. alcLowPct/alcHighPct ARE dynamic (dashboard-
// editable) since those are the operational "when to refill" judgment calls - but they are
// now OWNED and NVS-persisted by Node S3A (Box 1's Internal Server Management), not here.
// These two vars are just Master's in-RAM cache of S3A's last broadcast; a dashboard save
// forwards the new value to S3A (pushS3AConfig()) rather than writing it to Local Server's
// own NVS, so a Local Server replacement/reflash doesn't lose Box 1's saved thresholds.
// -------------------------------------------------------------
const float ALC_DIST_EMPTY_CM = 9.0f; // sensor reading when tank is empty (0%) - tank's full height
const float ALC_DIST_FULL_CM  = 2.0f; // sensor reading when tank is at the safe fill limit (100%)
float alcLowPct  = 10.0f; // at/below this -> go refill (dynamic, owned by S3A)
float alcHighPct = 90.0f; // at/above this -> refill complete (dynamic, owned by S3A)

// AUTO_CALL (requesting an ACS delivery when Box 1's tank runs low), and the tank-geometry
// constants (HUMID_LENGTH/WIDTH/HEIGHT_CM) it needed, now live entirely on S3A along with
// the rest of Box 1's state machine - humidifierRefillRequested's edge-latch moved there too.

// Helmet-detection distance cutoffs - dynamic. A2 and C2 have different sensor mounting
// geometry (side-facing vs top-down), so they're tuned independently. Both are now
// owned/persisted by each box's own Internal Server Management node (S3A for A2, S3C for
// C2) - these two vars are just Master's in-RAM cache of the latest broadcast value.
float helmetDetectDistA2 = 15.0f;
float helmetDetectDistC2 = 30.0f;

// Box 2's own coin-economy settings, owned/persisted by S3C (Box2ConfigPacket) - independent
// of Box 1's secondsPerCoin/minCoinsRequired below, same reasoning as helmetDetectDistC2 above.
uint16_t secondsPerCoinC2   = 20;
uint32_t minCoinsRequiredC2 = 0;
uint32_t maxCoinsAllowedC2  = 0; // Box 2 only - coins beyond this stop adding cycle time (0 = no cap)
// Added 2026-09-14, Box 2 only - fraction of Heating's actual duration that Cool Down runs for.
// Dashboard constrains this to 1/0.75/0.5/0.25 (Full/Three Quarter/Half/Quarter).
float    coolDownRatioC2    = 1.0f;

// ACS's own thresholds/ratios, mirrored here so they can be edited from the dashboard and
// persisted in Master's NVS; pushed to ACS over ESP-NOW via CMD_SET_CONFIG since ACS's
// automatic refill sequence runs autonomously and needs its own local copy to act on.
float acsLowDistCm    = 11.0f;
float acsFullDistCm   = 3.3f;
float mixRatioAlcohol = 0.70f;
float mixRatioWater   = 0.28f;
float mixRatioScented = 0.02f;

// -------------------------------------------------------------
// Statistics & Financial Analytics (persisted to NVS)
// -------------------------------------------------------------
float coinValuePeso = 1.0f; // PHP per pulse (dynamic)
// Box 1's coin-economy settings, owned/persisted by S3A - these are just Master's in-RAM
// cache of S3A's last broadcast (Box1ConfigPacket). Box 2's own independent copies
// (secondsPerCoinC2/minCoinsRequiredC2, cached from S3C) live near helmetDetectDistC2 above.
uint16_t secondsPerCoin   = 20;
uint32_t minCoinsRequired = 0; // coins required before STATE_INSERT_COIN lets a session through (0 = any coin works) - cached from S3A
uint32_t maxCoinsAllowed  = 0; // Added 2026-09-13, mirrors Box 2's maxCoinsAllowedC2 - coins beyond
                                // this make STATE_TIME_ALLOT reject the selection (0 = no cap) - cached from S3A
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

// Box 1's SCREEN_* constants now live on S3A (ESP32_S3A.ino) - it owns A1's rendering now.
// Box 2's SCREEN_C1_* constants now live on S3C (ESP32_S3C.ino) - it owns C1's rendering now.

// Forward declaration - pushAcsConfig()/pushS3AConfig() below call this, but it's defined
// further down the file. Arduino's auto-prototype generator doesn't reliably handle this
// particular forward-reference (function with a default argument, called before its own
// definition), so it needs an explicit prototype here rather than relying on that.
void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr);

void loadSettingsFromNVS() {
    prefs.begin("vending_nvs", false);

    statTotalSessions    = prefs.getUInt("st_sess", 0);
    statTotalCoins       = prefs.getUInt("st_coins", 0);
    statTotalDurationSec = prefs.getUInt("st_dur", 0);

    coinValuePeso         = prefs.getFloat("coin_val", 1.0f);
    lastKnownPulsesA1     = prefs.getUInt("last_a1_pulses", 0); // survives Local Server reboot - see onDataRecv's A1 branch
    lastKnownPulsesC1     = prefs.getUInt("last_c1_pulses", 0); // same, for Box 2/C1
    // secondsPerCoin/minCoinsRequired/alcLowPct/alcHighPct/helmetDetectDistA2 (Box 1) and
    // secondsPerCoinC2/minCoinsRequiredC2/helmetDetectDistC2 (Box 2) are NOT loaded here -
    // Node S3A and Node S3C own and persist their own box's settings now (each box's own
    // Internal Server Management), so Local Server can be replaced/reflashed without losing
    // them. They stay at their compiled-in defaults until each S3 node's first broadcast
    // arrives (usually within ~1s of both being powered).
    acsLowDistCm          = prefs.getFloat("acs_low_cm", 11.0f);
    acsFullDistCm         = prefs.getFloat("acs_full_cm", 3.3f);
    mixRatioAlcohol       = prefs.getFloat("mix_alc", 0.70f);
    mixRatioWater         = prefs.getFloat("mix_wat", 0.28f);
    mixRatioScented       = prefs.getFloat("mix_sce", 0.02f);

    Serial.println("[MASTER NVS] Settings & Statistics Loaded.");
}

// Sends ACS's current dynamic config over ESP-NOW - called on every dashboard save that
// touches an ACS-relevant field, and once whenever ACS is (re)detected online, so a reboot
// on either side still converges on the latest values within a few seconds.
void pushAcsConfig() {
    char payload[64];
    snprintf(payload, sizeof(payload), "%.2f,%.2f,%.3f,%.3f,%.3f",
             acsLowDistCm, acsFullDistCm, mixRatioAlcohol, mixRatioWater, mixRatioScented);
    sendEspNowCmdDirect(DEVICE_ACS, CMD_SET_CONFIG, 0, 0, 0, payload);
    Serial.printf("[MASTER CONFIG] Pushed ACS config: %s\n", payload);
}

// Pushes a dashboard-driven edit to S3A (Box 1's Internal Server Management), which owns
// and persists these values now. S3A re-broadcasts immediately on receipt, so the in-RAM
// cache updated alongside this call in the "save_config" handler gets confirmed within
// a moment rather than waiting for S3A's next regular 1s broadcast.
void pushS3AConfig() {
    char payload[64];
    snprintf(payload, sizeof(payload), "%.1f,%.1f,%.1f,%u,%u,%u",
             helmetDetectDistA2, alcLowPct, alcHighPct, secondsPerCoin, minCoinsRequired, maxCoinsAllowed);
    sendEspNowCmdDirect(DEVICE_S3A, CMD_SET_CONFIG, 0, 0, 0, payload);
    Serial.printf("[MASTER CONFIG] Pushed S3A config: %s\n", payload);
}

// Pushes a dashboard-driven edit to S3C (Box 2's Internal Server Management), mirroring
// pushS3AConfig() above - S3C owns and persists these values, independent of Box 1's.
void pushS3CConfig() {
    char payload[64];
    snprintf(payload, sizeof(payload), "%.1f,%u,%u,%u,%.2f",
             helmetDetectDistC2, secondsPerCoinC2, minCoinsRequiredC2, maxCoinsAllowedC2, coolDownRatioC2);
    sendEspNowCmdDirect(DEVICE_S3C, CMD_SET_CONFIG, 0, 0, 0, payload);
    Serial.printf("[MASTER CONFIG] Pushed S3C config: %s\n", payload);
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Direct Command Dispatcher
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
    Serial.printf("[MASTER ESP-NOW TX] Broadcast Status: %s\n", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS (ACK)" : "FAIL (NO-ACK)");
}

// Registers a specific peer MAC for unicast sending, if it isn't already registered.
// Broadcast doesn't need this - BROADCAST_MAC is implicitly always sendable.
void ensureUnicastPeer(const uint8_t* peerMac) {
    if (esp_now_is_peer_exist(peerMac)) return;
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str) {
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
                          (targetDev == DEVICE_C2) ? "Node C2" : (targetDev == DEVICE_S3A) ? "Node S3A" :
                          (targetDev == DEVICE_S3C) ? "Node S3C" : "Broadcast";

    // Unicast once the target's MAC is known (learned from its own telemetry) - only that
    // node's radio has to receive/process it, instead of every node on the mesh. Falls back
    // to broadcast automatically until a MAC is learned (e.g. right after Master's own boot,
    // before any node has sent its first telemetry packet yet).
    bool haveUnicast = (targetDev < 8) && macKnown[targetDev];
    const uint8_t* destMac = haveUnicast ? knownMac[targetDev] : BROADCAST_MAC;
    if (haveUnicast) ensureUnicastPeer(destMac);

    Serial.printf("[MASTER ESP-NOW TX] >>> Sending Command Opcode %d to %s (%s) (Param: %d, State: %d)...\n",
                  cmdId, devName, haveUnicast ? "unicast" : "broadcast", param, state);

    esp_err_t result = esp_now_send(destMac, (uint8_t*)&cmd, sizeof(CommandPacket));
    if (result != ESP_OK) {
        Serial.printf("[MASTER ESP-NOW TX] ERROR: Send failed with code %d\n", result);
    }
}

// A2's manual relay testing from the dashboard still sends CMD_SET_RELAY directly (see the
// "target == DEVICE_A2" branch in handleWebSocketMessage) - that's an independent bench-
// testing path. The automated applyRelayBitmask()/applyCleaningRelays() that used to drive
// A2 during Box 1's actual cycle now live on S3A, since it owns that cycle.

// -------------------------------------------------------------
// Sensor / Threshold Helpers
// -------------------------------------------------------------
// helmetPresent()/enclosureClosed() (Box 1) and their Box 2 equivalents removed - S3A/S3C
// each compute handshakeValid themselves now and broadcast it directly (nodeS3AStatus.
// handshakeValid / nodeS3CStatus.handshakeValid).

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

// humidifierVolumeNeededML() and screenForCurrentState() (Box 1) removed - AUTO_CALL and
// A1's screen rendering both moved to S3A, which has its own copies of this math/logic.

const char* stateName(MachineState s) {
    switch (s) {
        case STATE_IDLE:           return "IDLE (superseded - see Taps)";
        case STATE_INSTRUCTIONS:   return "Open Enclosure Door";
        case STATE_CHECKING:       return "Checking Alcohol Level";
        case STATE_REFILLING:      return "Refilling Alcohol";
        case STATE_SENSORS:        return "Place Headgear & Close Door";
        case STATE_CLEANING:       return "Cleaning In Progress";
        case STATE_ABORT_CONFIRM:  return "Abort Confirmation";
        case STATE_RETRIEVE:       return "Retrieve Headgear";
        case STATE_FINISH:         return "Cycle Complete";
        case STATE_PAUSED_SAFETY:  return "Paused - Safety";
        // --- Added 2026-09-13 for the redesigned Steps 1-5, 11 customer flow ---
        case STATE_TAPS:           return "Tap to Start";
        case STATE_WELCOME:        return "Welcome";
        case STATE_RATE_A:         return "Rating Helmet (Before)";
        case STATE_TIME_ALLOT:     return "Picking Cleaning Time";
        case STATE_INSERT_COIN:    return "Insert Coin";
        case STATE_RATE_B:         return "Rating Helmet (After)";
        case STATE_CANCEL_CONFIRM: return "Cancel Confirmation";
        default:                   return "UNKNOWN";
    }
}

// screenForCurrentBox2State() (Box 2's post-solenoid repaint helper) removed - now lives on
// S3C, which owns C1's rendering (mirrors screenForCurrentState()'s removal for Box 1/S3A).

const char* box2StateName(Box2State s) {
    switch (s) {
        case B2_STATE_IDLE:          return "IDLE (superseded - see Taps)";
        case B2_STATE_INSTRUCTIONS:  return "Open Enclosure Door";
        case B2_STATE_SENSORS:       return "Place Headgear & Close Door";
        case B2_STATE_HEATING:       return "Heating In Progress";
        case B2_STATE_ABORT_CONFIRM: return "Abort Confirmation";
        case B2_STATE_RETRIEVE:      return "Retrieve Headgear";
        case B2_STATE_FINISH:        return "Cycle Complete";
        case B2_STATE_PAUSED_SAFETY: return "Paused - Safety";
        // --- Added 2026-09-14 for the redesigned Steps 1-5, 10 customer flow ---
        case B2_STATE_TAPS:           return "Tap to Start";
        case B2_STATE_WELCOME:        return "Welcome";
        case B2_STATE_RATE_A:         return "Rating Helmet (Before)";
        case B2_STATE_TIME_ALLOT:     return "Picking Heat Time";
        case B2_STATE_INSERT_COIN:    return "Insert Coin";
        case B2_STATE_COOL_DOWN:      return "Cooling Down";
        case B2_STATE_RATE_B:         return "Rating Helmet (After)";
        case B2_STATE_CANCEL_CONFIRM: return "Cancel Confirmation";
        default:                     return "UNKNOWN";
    }
}

// Both boxes' state machines (and the sessions they just finished) now run on S3A/S3C - this
// just records what either one reported (via BoxStatusPacket) into Local Server's shared
// stats (combined revenue dashboard, not split per-box - simplest option, easy to split later).
void recordCompletedSession(uint16_t durSec16, uint16_t coins16) {
    statTotalSessions++;
    statTotalDurationSec += durSec16;
    statHistory[statHistoryHead] = { durSec16, coins16 };
    statHistoryHead = (statHistoryHead + 1) % STAT_HISTORY_SIZE;
    if (statHistoryCount < STAT_HISTORY_SIZE) statHistoryCount++;

    prefs.putUInt("st_sess", statTotalSessions);
    prefs.putUInt("st_dur", statTotalDurationSec);
    Serial.printf("[MASTER STATS] Session #%d recorded: %ds, %d coins.\n", statTotalSessions, durSec16, coins16);
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
            StaticJsonDocument<1024> doc; // bumped from 768 to fit the "save_config" message's ~11 fields
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
                        // Manual bench-testing path, independent of S3A's automated cycle.
                        // Still mutes A1's coin ISR against the EMI burst; the TFT-repaint-
                        // after-relay cosmetic fix doesn't apply here anymore since S3A (not
                        // Local Server) owns A1's screen now.
                        sendEspNowCmdDirect(DEVICE_A1, CMD_MUTE_COINS, 0, 250, 0);
                        sendEspNowCmdDirect(DEVICE_A2, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    } else if (target == DEVICE_ACS) {
                        Serial.printf("[MASTER WS] Dispatching relay %d (State: %d) to Node ACS...\n", doc["relayIdx"] | 0, doc["state"] | 0);
                        sendEspNowCmdDirect(DEVICE_ACS, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    } else if (target == DEVICE_C1) {
                        Serial.printf("[MASTER WS] Dispatching command %d to Node C1...\n", cmdId);
                        sendEspNowCmdDirect(DEVICE_C1, cmdId, 0, doc["param"] | 0, 0);
                    } else if (target == DEVICE_C2) {
                        Serial.printf("[MASTER WS] Dispatching relay %d (State: %d) to Node C2...\n", doc["relayIdx"] | 0, doc["state"] | 0);
                        // Manual bench-testing path, independent of S3C's automated cycle.
                        // Still mutes C1's coin ISR against the EMI burst; the TFT-repaint-
                        // after-relay cosmetic fix doesn't apply here anymore since S3C (not
                        // Local Server) owns C1's screen now.
                        sendEspNowCmdDirect(DEVICE_C1, CMD_MUTE_COINS, 0, 250, 0);
                        sendEspNowCmdDirect(DEVICE_C2, CMD_SET_RELAY, doc["relayIdx"] | 0, 0, doc["state"] | 0);
                    }
                } else if (doc.containsKey("cmd")) {
                    const char* cmd = doc["cmd"];
                    if (cmd && strcmp(cmd, "save_spc") == 0) {
                        // secondsPerCoin is S3A-owned now (Box1ConfigPacket) - update the
                        // in-RAM cache for instant dashboard feedback, and push to S3A so it
                        // actually persists it and re-broadcasts, instead of saving locally.
                        secondsPerCoin = doc["val"] | 20;
                        pushS3AConfig();
                        Serial.printf("[MASTER WS] Pushed secondsPerCoin=%d to S3A.\n", secondsPerCoin);
                    } else if (cmd && strcmp(cmd, "save_spc_c2") == 0) {
                        // Mirrors save_spc above, for Box 2's own independent copy (S3C-owned,
                        // Box2ConfigPacket).
                        secondsPerCoinC2 = doc["val"] | 20;
                        pushS3CConfig();
                        Serial.printf("[MASTER WS] Pushed secondsPerCoin=%d to S3C.\n", secondsPerCoinC2);
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
                    } else if (cmd && strcmp(cmd, "save_config") == 0) {
                        coinValuePeso         = doc["coin_value"]         | coinValuePeso;
                        minCoinsRequired      = doc["min_coins_required"] | minCoinsRequired;
                        maxCoinsAllowed       = doc["max_coins_allowed"]  | maxCoinsAllowed;
                        minCoinsRequiredC2    = doc["min_coins_required_c2"] | minCoinsRequiredC2;
                        maxCoinsAllowedC2     = doc["max_coins_allowed_c2"] | maxCoinsAllowedC2;
                        coolDownRatioC2       = doc["cool_down_ratio_c2"]  | coolDownRatioC2;
                        alcLowPct             = doc["alc_low_pct"]     | alcLowPct;
                        alcHighPct            = doc["alc_high_pct"]    | alcHighPct;
                        helmetDetectDistA2    = doc["helmet_a2_cm"]    | helmetDetectDistA2;
                        helmetDetectDistC2    = doc["helmet_c2_cm"]    | helmetDetectDistC2;
                        acsLowDistCm          = doc["acs_low_cm"]      | acsLowDistCm;
                        acsFullDistCm         = doc["acs_full_cm"]     | acsFullDistCm;

                        // Mix ratios come in as 0-100 percentages and are normalized here so
                        // they always sum to exactly 1.0 regardless of rounding/typos on input.
                        float mAlc = doc["mix_alcohol_pct"] | (mixRatioAlcohol * 100.0f);
                        float mWat = doc["mix_water_pct"]   | (mixRatioWater   * 100.0f);
                        float mSce = doc["mix_scented_pct"] | (mixRatioScented * 100.0f);
                        float mSum = mAlc + mWat + mSce;
                        if (mSum > 0.0f) {
                            mixRatioAlcohol = mAlc / mSum;
                            mixRatioWater   = mWat / mSum;
                            mixRatioScented = mSce / mSum;
                        }

                        // alcLowPct/alcHighPct/helmetDetectDistA2/secondsPerCoin/minCoinsRequired
                        // (Box 1) and helmetDetectDistC2/secondsPerCoinC2/minCoinsRequiredC2
                        // (Box 2) are NOT saved to Local Server's NVS - Node S3A/S3C own and
                        // persist their own box's values. They're applied to the in-RAM cache
                        // above (for an instant-feeling dashboard save), and pushed to S3A/S3C
                        // here so they actually persist them and re-broadcast.
                        prefs.putFloat("coin_val", coinValuePeso);
                        prefs.putFloat("acs_low_cm", acsLowDistCm);
                        prefs.putFloat("acs_full_cm", acsFullDistCm);
                        prefs.putFloat("mix_alc", mixRatioAlcohol);
                        prefs.putFloat("mix_wat", mixRatioWater);
                        prefs.putFloat("mix_sce", mixRatioScented);

                        pushAcsConfig();
                        pushS3AConfig();
                        pushS3CConfig();
                        Serial.println("[MASTER WS] Dynamic config saved and pushed.");
                    }
                }
            }
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len);
}

void learnMac(uint8_t deviceId, const uint8_t* mac) {
    if (deviceId >= 8 || macKnown[deviceId]) return; // learn once - a node's MAC never changes
    memcpy(knownMac[deviceId], mac, 6);
    macKnown[deviceId] = true;
    Serial.printf("[MASTER] Learned MAC for device %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  deviceId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

        // Both boxes' touch actions (A1/C1 -> RESET/ABORT/etc.) now go straight to their own
        // Internal Server Management node (S3A/S3C), which owns that state machine - A1/C1
        // address them to DEVICE_S3A/DEVICE_S3C, not DEVICE_SERVER, so they never reach here
        // at all (this node only cares about commands addressed to it).
        return;
    }

    if (len == sizeof(ACSTelemetryPacket)) {
        learnMac(DEVICE_ACS, mac);
        memcpy(&nodeACS_Data, incomingData, sizeof(ACSTelemetryPacket));
        lastSeenACS = millis();
        return;
    }

    if (len == sizeof(Box1ConfigPacket)) {
        learnMac(DEVICE_S3A, mac);
        Box1ConfigPacket cfg;
        memcpy(&cfg, incomingData, sizeof(Box1ConfigPacket));
        helmetDetectDistA2    = cfg.helmetDetectCmA2;
        alcLowPct             = cfg.alcLowPct;
        alcHighPct            = cfg.alcHighPct;
        secondsPerCoin        = cfg.secondsPerCoin;
        minCoinsRequired      = cfg.minCoinsRequired;
        maxCoinsAllowed       = cfg.maxCoinsAllowed;
        lastSeenS3A = millis();
        return;
    }

    if (len == sizeof(Box2ConfigPacket)) {
        learnMac(DEVICE_S3C, mac);
        Box2ConfigPacket cfg;
        memcpy(&cfg, incomingData, sizeof(Box2ConfigPacket));
        helmetDetectDistC2 = cfg.helmetDetectCmC2;
        secondsPerCoinC2   = cfg.secondsPerCoin;
        minCoinsRequiredC2 = cfg.minCoinsRequired;
        maxCoinsAllowedC2  = cfg.maxCoinsAllowed;
        coolDownRatioC2    = cfg.coolDownRatio;
        lastSeenS3C = millis();
        return;
    }

    // Box 1's (from S3A) and Box 2's (from S3C) live status share the same BoxStatusPacket
    // shape (same disambiguate-by-deviceID pattern as TelemetryPacket, shared by A1/A2/C1/C2)
    // - deviceID says which one this is.
    if (len == sizeof(BoxStatusPacket)) {
        BoxStatusPacket status;
        memcpy(&status, incomingData, sizeof(BoxStatusPacket));

        if (status.deviceID == DEVICE_S3A) {
            learnMac(DEVICE_S3A, mac);
            nodeS3AStatus = status;
            lastSeenS3A = millis();

            // Session-completion tracking: completedSessionSeq only changes when S3A actually
            // finishes a session. Skip recording on the very first packet ever seen (that
            // would otherwise replay S3A's entire pre-existing history as one phantom
            // "session" using stale duration/coins data) - just learn the starting point.
            if (!b1SessionSeqInitialized) {
                lastKnownB1SessionSeq = nodeS3AStatus.completedSessionSeq;
                b1SessionSeqInitialized = true;
            } else if (nodeS3AStatus.completedSessionSeq != lastKnownB1SessionSeq) {
                lastKnownB1SessionSeq = nodeS3AStatus.completedSessionSeq;
                recordCompletedSession(nodeS3AStatus.lastSessionDurationSec, nodeS3AStatus.lastSessionCoins);
            }
        } else if (status.deviceID == DEVICE_S3C) {
            learnMac(DEVICE_S3C, mac);
            nodeS3CStatus = status;
            lastSeenS3C = millis();

            // Same first-packet-skip guard as Box 1/S3A above.
            if (!b2SessionSeqInitialized) {
                lastKnownB2SessionSeq = nodeS3CStatus.completedSessionSeq;
                b2SessionSeqInitialized = true;
            } else if (nodeS3CStatus.completedSessionSeq != lastKnownB2SessionSeq) {
                lastKnownB2SessionSeq = nodeS3CStatus.completedSessionSeq;
                recordCompletedSession(nodeS3CStatus.lastSessionDurationSec, nodeS3CStatus.lastSessionCoins);
            }
        }
        return;
    }

    if (len != sizeof(TelemetryPacket)) return;

    TelemetryPacket packet;
    memcpy(&packet, incomingData, sizeof(TelemetryPacket));

    if (packet.deviceID == DEVICE_A1) {
        learnMac(DEVICE_A1, mac);
        // S3A now owns activeTimer/coin-to-time conversion for Box 1 (see ESP32_S3A.ino) -
        // Local Server just independently watches the same raw pulse count for revenue stats.
        // lastKnownPulsesA1 is NVS-persisted (below) so a Local Server reboot doesn't forget
        // where it left off and double-count A1's already-tallied pulses on reconnect - this
        // exact bug happened once already (Local Server rebooted mid-session, reported double
        // the real revenue for that session even though Box 1's own display stayed accurate).
        if (packet.pulseCount > lastKnownPulsesA1) {
            uint32_t newPulses = packet.pulseCount - lastKnownPulsesA1;
            lastKnownPulsesA1 = packet.pulseCount;
            prefs.putUInt("last_a1_pulses", lastKnownPulsesA1);
            statTotalCoins += newPulses;
            prefs.putUInt("st_coins", statTotalCoins);
            Serial.printf("[MASTER STATS] +%d coin(s) from A1 (revenue tracking only - S3A drives the actual cycle).\n", newPulses);
        } else if (packet.pulseCount < lastKnownPulsesA1) {
            // A1 itself rebooted (its own counter reset, e.g. power-cycled) - resync the
            // baseline down rather than either double-counting or freezing stat updates
            // until A1's counter climbs back above the old (now stale) baseline.
            Serial.printf("[MASTER STATS] A1 pulse count dropped (%u -> %u) - A1 likely rebooted, resyncing baseline.\n", lastKnownPulsesA1, packet.pulseCount);
            lastKnownPulsesA1 = packet.pulseCount;
            prefs.putUInt("last_a1_pulses", lastKnownPulsesA1);
        }
        nodeA1_Data = packet;
        lastSeenA1  = millis();
    } else if (packet.deviceID == DEVICE_A2) {
        learnMac(DEVICE_A2, mac);
        nodeA2_Data = packet;
        lastSeenA2  = millis();
    } else if (packet.deviceID == DEVICE_C1) {
        learnMac(DEVICE_C1, mac);
        // S3C now owns activeTimer/coin-to-time conversion for Box 2 (see ESP32_S3C.ino) -
        // Local Server just independently watches the same raw pulse count for revenue stats,
        // same as A1's branch above. lastKnownPulsesC1 is NVS-persisted so a Local Server
        // reboot doesn't forget where it left off and double-count C1's already-tallied pulses.
        if (packet.pulseCount > lastKnownPulsesC1) {
            uint32_t newPulses = packet.pulseCount - lastKnownPulsesC1;
            lastKnownPulsesC1 = packet.pulseCount;
            prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
            statTotalCoins += newPulses;
            prefs.putUInt("st_coins", statTotalCoins);
            Serial.printf("[MASTER STATS] +%d coin(s) from C1 (revenue tracking only - S3C drives the actual cycle).\n", newPulses);
        } else if (packet.pulseCount < lastKnownPulsesC1) {
            // C1 itself rebooted - resync the baseline down, same as A1's branch above.
            Serial.printf("[MASTER STATS] C1 pulse count dropped (%u -> %u) - C1 likely rebooted, resyncing baseline.\n", lastKnownPulsesC1, packet.pulseCount);
            lastKnownPulsesC1 = packet.pulseCount;
            prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
        }
        nodeC1_Data = packet;
        lastSeenC1  = millis();
    } else if (packet.deviceID == DEVICE_C2) {
        learnMac(DEVICE_C2, mac);
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
    memset(&nodeS3AStatus, 0, sizeof(BoxStatusPacket));
    memset(&nodeS3CStatus, 0, sizeof(BoxStatusPacket));

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
        sendEspNowCmdDirect(DEVICE_A1, CMD_MUTE_COINS, 0, 250, 0);
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

    // A1's and C1's initial Welcome screens are now S3A's/S3C's job respectively (each owns
    // its own box's rendering) - Local Server no longer sends either on boot.

    // Best-effort initial push in case ACS is already up when Master boots; the online
    // edge-detect in loop() covers ACS booting after (or rebooting independently of) Master.
    pushAcsConfig();
}

void loop() {
    dnsServer.processNextRequest();
    ws.cleanupClients();

    unsigned long currentMillis = millis();

    // Post-solenoid TFT repaint for both boxes now lives on S3A/S3C respectively, since each
    // owns its own box's rendering.

    // 1-Second Master Execution Tick
    if (currentMillis - lastOneSecTick >= 1000) {
        lastOneSecTick = currentMillis;

        // Both boxes' entire step engines (coin -> ... -> finish), relay control, and AUTO_CALL
        // now run on S3A/S3C (ESP32_S3A.ino / ESP32_S3C.ino) - see nodeS3AStatus/nodeS3CStatus
        // for what Local Server still needs (dashboard display + session stats), populated in
        // onDataRecv().

        // Broadcast a heartbeat every second so A1/A2/C1/C2 can each independently tell whether
        // Master is reachable - S3A/S3C broadcast their own equivalent, so any node that hears
        // neither for a stretch knows nobody is coordinating it (see each node's OUT OF ORDER
        // detection). subIndex identifies the sender since CommandPacket has no sender field.
        {
            CommandPacket hb;
            memset(&hb, 0, sizeof(CommandPacket));
            hb.targetDeviceID = 0; // broadcast - every node listens for this opcode regardless
            hb.commandID      = CMD_HEARTBEAT;
            hb.subIndex       = DEVICE_SERVER;
            esp_now_send(BROADCAST_MAC, (uint8_t*)&hb, sizeof(CommandPacket));
        }

        // Push the current dynamic config to ACS whenever it (re)connects, so a reboot on
        // either side still converges on the latest dashboard-saved values within seconds.
        bool acsOnlineNow = (lastSeenACS > 0) && (currentMillis - lastSeenACS <= 3000);
        if (acsOnlineNow && !acsWasOnline) {
            Serial.println("[MASTER CONFIG] ACS (re)connected - pushing current config.");
            pushAcsConfig();
        }
        acsWasOnline = acsOnlineNow;

        // Broadcast Real-time Status over WebSockets
        if (ws.count() > 0) {
            StaticJsonDocument<2560> doc; // bumped from 2048 to also fit the dynamic-config fields
            doc["heap"]         = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            doc["free_psram"]   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // 0 if PSRAM isn't actually initialized
            doc["a1_online"]    = (lastSeenA1 > 0) && (currentMillis - lastSeenA1 <= 3000);
            doc["a2_online"]    = (lastSeenA2 > 0) && (currentMillis - lastSeenA2 <= 3000);
            doc["c1_online"]    = (lastSeenC1 > 0) && (currentMillis - lastSeenC1 <= 3000);
            doc["c2_online"]    = (lastSeenC2 > 0) && (currentMillis - lastSeenC2 <= 3000);
            doc["s3a_online"]   = (lastSeenS3A > 0) && (currentMillis - lastSeenS3A <= 3000);
            doc["s3c_online"]   = (lastSeenS3C > 0) && (currentMillis - lastSeenS3C <= 3000);

            // Box 1's live status now comes from S3A's broadcast (nodeS3AStatus), not local
            // computation - S3A owns the state machine now.
            doc["mach_state"]   = nodeS3AStatus.machineState;
            doc["step_name"]    = stateName((MachineState)nodeS3AStatus.machineState);
            doc["step_sec_rem"] = nodeS3AStatus.stepRemainingSec;
            doc["active_timer"] = nodeS3AStatus.activeTimer;
            doc["handshake_ok"] = nodeS3AStatus.handshakeValid;
            doc["sec_per_coin"] = secondsPerCoin;
            doc["sec_per_coin_c2"] = secondsPerCoinC2;

            // Current dynamic config, for the Settings tab to populate itself with live values.
            doc["cfg_coin_value"]      = coinValuePeso;
            doc["cfg_min_coins_required"] = minCoinsRequired;
            doc["cfg_max_coins_allowed"] = maxCoinsAllowed;
            doc["cfg_min_coins_required_c2"] = minCoinsRequiredC2;
            doc["cfg_max_coins_allowed_c2"] = maxCoinsAllowedC2;
            doc["cfg_cool_down_ratio_c2"] = coolDownRatioC2;
            doc["cfg_alc_low_pct"]     = alcLowPct;
            doc["cfg_alc_high_pct"]    = alcHighPct;
            doc["cfg_helmet_a2_cm"]    = helmetDetectDistA2;
            doc["cfg_helmet_c2_cm"]    = helmetDetectDistC2;
            doc["cfg_acs_low_cm"]      = acsLowDistCm;
            doc["cfg_acs_full_cm"]     = acsFullDistCm;
            doc["cfg_mix_alcohol_pct"] = mixRatioAlcohol * 100.0f;
            doc["cfg_mix_water_pct"]   = mixRatioWater * 100.0f;
            doc["cfg_mix_scented_pct"] = mixRatioScented * 100.0f;

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

            // Box 2's live status now comes from S3C's broadcast (nodeS3CStatus), not local
            // computation - S3C owns the state machine now (mirrors Box 1/S3A above).
            doc["box2_mach_state"]   = nodeS3CStatus.machineState;
            doc["box2_step_name"]    = box2StateName((Box2State)nodeS3CStatus.machineState);
            doc["box2_step_sec_rem"] = nodeS3CStatus.stepRemainingSec;
            doc["box2_active_timer"] = nodeS3CStatus.activeTimer;
            doc["box2_handshake_ok"] = nodeS3CStatus.handshakeValid;

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
            doc["stat_revenue"]  = statTotalCoins * coinValuePeso;
            doc["stat_coins"]    = statTotalCoins;
            doc["stat_avg_dur"]  = (statTotalSessions > 0) ? (statTotalDurationSec / statTotalSessions) : 0;

            JsonArray hist = doc.createNestedArray("stat_history");
            for (uint8_t i = 0; i < statHistoryCount; i++) {
                uint8_t idx = (statHistoryHead + STAT_HISTORY_SIZE - statHistoryCount + i) % STAT_HISTORY_SIZE;
                JsonObject rec = hist.createNestedObject();
                rec["d"] = statHistory[idx].durationSec;
                rec["r"] = statHistory[idx].coinsUsed * coinValuePeso;
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
