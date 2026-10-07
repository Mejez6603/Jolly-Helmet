#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "S3C_Screens.h"

// =====================================================================
// NODE S3C - BOX 2 (HEATER) INTERNAL SERVER MANAGEMENT (ESP32-S3)        (core 3.3.7)
// REWRITTEN 2026-10-03 for the new Box 2 procedure designed in the Box Cycle Designer ("BOX 2 STEP.txt", 24 steps,
// frozen as BOX2_STEPS_2026-10-03_v2.json). The previous 15-step flow is archived next to this file as
// ESP32_S3C_LEGACY_old_flow.ino.txt - that is what runs on the customer's Box 2 until this one is flashed.
//
// Same shape as Box 1's S3A (ESP32_S3A.ino), minus the container checks / refill / mist choice / cleaning phases, plus a
// HEATING step (Heater + UV + Fan) and a COOL DOWN step (Fan only):
//   TAPS -> WELCOME -> RATE A -> TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> HEATING ->
//   COOL DOWN -> OPEN B -> SENSORS B -> CLOSE DOOR B -> RATE B -> (FREE RETRY -> OPEN A ...) -> FINISH -> TAPS,
//   with ABORT (-> COOL DOWN, so a hot helmet gets the fan first) / CANCEL confirmations, MAINTENANCE and OUT OF ORDER.
// Talks to: C1 (screen / touch / coins), C2 (door + helmet sensors and every relay: ONE CMD_SET_RELAY_MASK, re-sent every 2 s).
//
// Not one state per Designer step: START HEATING is a routing decision, and HEATING / HEATING FREE are one B2_STATE_HEATING
// plus the heatFree flag (see the Box2State comments in Shared_Common.h).
//
// DYNAMIC SETTINGS (NVS, survive a LocalServer swap; LocalServer edits them with CMD_SET_CONFIG):
//   subIndex 0  the old 3-5 field line "helmCm,secPerCoin,minCoins[,maxCoins[,coolDownSec]]" (only helmCm, maxCoins and
//               coolDownSec still mean anything - a value under 5 in the last field is a leftover cool-down RATIO and is ignored)
//   subIndex 1  RULES : 4 rating bands (from,to,minutes) x4, minMinutes, coins, timeValue, unitMin(1/0), maxCoins, freeOn,
//                       freeMinRating, freeMinutes                                                (20 numbers, same as S3A)
//   subIndex 2  HEAT  : helmCm, coolDownSec                                                       (2 numbers)
// S3C re-broadcasts both as CMD_SET_CONFIG (target 0, state = 2) every 2 s so a dashboard always shows the live values.
// Until LocalServer has its page, everything can be set from the USB serial console: type "help".
// =====================================================================

Preferences prefs;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------
// Settings - RULES (defaults = the "rules" block of the Box 2 Designer export)
// ---------------------------------------------------------------------
uint8_t  bandFrom[4]    = {1, 4, 7, 9};
uint8_t  bandTo[4]      = {3, 6, 8, 10};
uint8_t  bandMinutes[4] = {3, 5, 8, 10};
uint8_t  minMinutes     = 3;        // the shortest session
uint16_t rateCoins      = 10;       // "N coins = X seconds/minutes": 10 coins = 3 minutes (18 s per coin)
float    rateValue      = 3.0f;
bool     rateUnitMin    = true;     // true = minutes, false = seconds
uint16_t maxCoins       = 100;      // most coins one session can take (100 coins = 30 minutes at 18 s per coin)
bool     freeEnabled    = true;     // one free retry after Rate B ...
uint8_t  freeMinRating  = 1;        // ... when the final rating is at least this (1 = everyone, for the deployment demo)
uint8_t  freeMinutes    = 2;        // ... lasting this long

// Settings - HEAT
float    helmetDetectCmC2 = 30.0f;  // helmet counts as inside when the helmet ultrasonic reads under this
uint16_t coolDownSec      = 60;     // how long the fan-only COOL DOWN lasts (also after an abort)

// Derived from the rules by recomputeDerived()
float    secPerCoinF      = 18.0f;
uint16_t secondsPerCoin   = 18;
uint32_t minCoinsRequired = 10;

// ---------------------------------------------------------------------
// Session (one customer)
// ---------------------------------------------------------------------
Box2State currentState = B2_STATE_TAPS;
Box2State returnState  = B2_STATE_TAPS;   // where ABORT / CANCEL / a safety pause "go back to"
bool     retryUsed     = false;   // the one free retry has been taken (or offered)
bool     heatFree      = false;   // the heating in progress is the free retry
uint8_t  ratingA = 0, ratingB = 0;
uint32_t requiredCoinsForSession = 0;
uint32_t activeTimer   = 0;       // seconds left of the heating / cool down (or seconds bought while paying)
uint32_t heatTotalSec  = 0;       // length of the heating, for the progress bar (capped at 9999)
uint32_t coolTotalSec  = 0;       // length of the cool down, for the progress bar
uint32_t sessionCoins  = 0;
uint32_t npInitSeconds = 0;       // the numpad's suggested time
char     timeWarnL1[32] = "", timeWarnL2[32] = "";   // numpad rejection message (empty = none)
uint16_t stepRemainingSec = 0;
char     scrBuf[220];             // scratch for screens with live numbers baked in

// per-state debounce counters (2 ticks = 2 s)
uint8_t  holdA = 0, holdB = 0, holdC = 0;
// enclosure solenoid duty cap: never energized for more than LOCK_MAX_ON_SEC in a row (a coil held on for 10+ minutes
// once overheated into a fire-risk incident)
const uint16_t LOCK_MAX_ON_SEC = 60, LOCK_REST_SEC = 5;
uint16_t unlockHeldSec = 0, lockRestSec = 0;
bool     lockResting   = false;
bool     unlockForPause = false;  // PAUSED_SAFETY with the door shut and no helmet: let them back in
uint16_t maintMask     = 0;       // MAINTENANCE: relays set by hand from the dashboard
uint16_t maintHeaterSec = 0;      // how long the hand-set heater has been on; it switches itself off after MAINT_HEATER_MAX_SEC
const uint16_t MAINT_HEATER_MAX_SEC = 300;

// session statistics for LocalServer (BoxStatusPacket)
uint32_t completedSessionSeq    = 0;
uint32_t sessionStartMillis     = 0;
uint32_t sessionCoinsAtStart    = 0;
uint16_t lastSessionDurationSec = 0;
uint16_t lastSessionCoins       = 0;

// ---------------------------------------------------------------------
// Live data from the other nodes (written by the receive callback, snapshotted in loop())
// ---------------------------------------------------------------------
volatile uint32_t c1Pulses = 0;   volatile bool c1Seen = false;
TelemetryPacket   nodeC2;         uint32_t lastC2Ms = 0;
TelemetryPacket   c2snap;         // the copy the state machine reads
uint32_t lastKnownPulsesC1 = 0;

// commands queued by the callback, handled in loop() (no heavy work or state changes in the radio task)
#define CMD_QUEUE_LEN 4
CommandPacket cmdQueue[CMD_QUEUE_LEN];
volatile uint8_t qHead = 0, qTail = 0;

// ---------------------------------------------------------------------
// Timing / broadcast state
// ---------------------------------------------------------------------
uint16_t desiredMask = 0;         // what C2 is being asked to do
unsigned long lastOneSecTick = 0, lastMaskSentMs = 0, lastCfgCsvMs = 0, lastTokenMs = 0;
const unsigned long MASK_REASSERT_MS = 2000;
unsigned long c1RedrawAtMillis = 0;
bool c1HardRefreshPending = false;
// Which relay changes force a FULL re-init of the touch screen (the white-screen cure). Bit = relay index. 'On' = the relay turns ON, 'Off' = it turns OFF
// (for the locks ON = unlocked, so locking = the lock bit going OFF). Set per relay from the dashboard (CMD_SET_CONFIG subIndex 5), kept in NVS.
uint16_t refreshOnMask  = 0;
uint16_t refreshOffMask = C2_BIT(C2_RELAY_ENCLOSURE_LOCK);
// Auto hard refresh: every N seconds S3C re-sends C1's current page as a full controller re-init (the white-screen cure). 0 = never, max 60.
// Set from the dashboard (CMD_SET_CONFIG subIndex 6), kept in NVS. Screens where the customer types or picks a rating are skipped (a refresh would wipe what was typed).
uint16_t autoRefreshSec = 2;
unsigned long lastAutoRefreshMs = 0;
bool needScreenRedraw = false;    // a coin changed the heating total: repaint so the bar follows
bool coinFlag = false;            // a coin landed during heating: status line
char statusLines[3][32] = {"", "", ""};
char serialBuf[120]; uint8_t serialLen = 0;

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------
void sendCmd(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr) {
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
    esp_now_send(BROADCAST_MAC, (uint8_t*)&cmd, sizeof(CommandPacket));
}

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

uint32_t coinsForSec(uint32_t sec) {
    float c = ceilf((float)sec / secPerCoinF - 0.0001f);
    if (c < 1.0f) c = 1.0f;
    return (uint32_t)c;
}

void recomputeDerived() {
    if (rateCoins < 1) rateCoins = 1;
    if (rateValue < 0.1f) rateValue = 0.1f;
    secPerCoinF = rateValue * (rateUnitMin ? 60.0f : 1.0f) / (float)rateCoins;
    long r = lroundf(secPerCoinF);
    secondsPerCoin = (uint16_t)(r < 1 ? 1 : r);
    minCoinsRequired = coinsForSec((uint32_t)minMinutes * 60UL);
}

// the time the Time Allot numpad opens on for a given rating (never under the minimum)
uint32_t suggestedSecondsForRating(uint8_t r) {
    uint32_t m = 0;
    for (uint8_t i = 0; i < 4; i++) {
        if (r >= bandFrom[i] && r <= bandTo[i]) { m = bandMinutes[i]; break; }
    }
    if (m < minMinutes) m = minMinutes;
    return m * 60UL;
}

bool ratingGetsFreeRetry(uint8_t r) { return freeEnabled && r >= freeMinRating; }

const char* stateName(Box2State s) {
    switch (s) {
        case B2_STATE_TAPS:            return "TAPS";
        case B2_STATE_WELCOME:         return "WELCOME";
        case B2_STATE_RATE_A:          return "RATE A";
        case B2_STATE_TIME_ALLOT:      return "TIME ALLOT";
        case B2_STATE_INSERT_COIN:     return "INSERT COIN";
        case B2_STATE_INSTRUCTIONS:    return "OPEN A";
        case B2_STATE_PLACE_HELMET:    return "SENSORS A";
        case B2_STATE_CLOSE_DOOR_A:    return "CLOSE DOOR A";
        case B2_STATE_HEATING:         return "HEATING";
        case B2_STATE_PAUSED_SAFETY:   return "SAFETY PAUSE";
        case B2_STATE_ABORT_CONFIRM:   return "ABORT";
        case B2_STATE_COOL_DOWN:       return "COOLDOWN";
        case B2_STATE_RETRIEVE:        return "OPEN B";
        case B2_STATE_TAKE_HELMET:     return "SENSORS B";
        case B2_STATE_CLOSE_DOOR_B:    return "CLOSE DOOR B";
        case B2_STATE_RATE_B:          return "RATE B";
        case B2_STATE_FREE_RETRY:      return "FREE RETRY";
        case B2_STATE_FINISH:          return "FINISH";
        case B2_STATE_CANCEL_CONFIRM:  return "CANCEL";
        case B2_STATE_MAINTENANCE:     return "MAINTENANCE";
        case B2_STATE_OUT_OF_ORDER:    return "OUT OF ORDER";
        case B2_STATE_HELMET_NOTICE_A: return "HELMET NOTICE A";
        case B2_STATE_HELMET_NOTICE_B: return "HELMET NOTICE B";
        default:                       return "?";
    }
}

// ---------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------
void saveConfigToNVS() {
    uint8_t blob[12];
    for (uint8_t i = 0; i < 4; i++) { blob[i * 3] = bandFrom[i]; blob[i * 3 + 1] = bandTo[i]; blob[i * 3 + 2] = bandMinutes[i]; }
    prefs.putBytes("bands", blob, sizeof(blob));
    prefs.putUChar("min_min", minMinutes);
    prefs.putUShort("rate_c", rateCoins);
    prefs.putFloat("rate_v", rateValue);
    prefs.putBool("rate_u", rateUnitMin);
    prefs.putUShort("max_c", maxCoins);
    prefs.putBool("free_on", freeEnabled);
    prefs.putUChar("free_r", freeMinRating);
    prefs.putUChar("free_m", freeMinutes);
    prefs.putFloat("helm_c2_cm", helmetDetectCmC2);
    prefs.putUShort("cd_sec", coolDownSec);
    prefs.putUShort("rf_on", refreshOnMask);
    prefs.putUShort("rf_off", refreshOffMask);
    prefs.putUShort("arf_sec", autoRefreshSec);
}

void loadConfigFromNVS() {
    prefs.begin("s3c_nvs", false);
    uint8_t blob[12];
    if (prefs.getBytes("bands", blob, sizeof(blob)) == sizeof(blob)) {
        for (uint8_t i = 0; i < 4; i++) { bandFrom[i] = blob[i * 3]; bandTo[i] = blob[i * 3 + 1]; bandMinutes[i] = blob[i * 3 + 2]; }
    }
    minMinutes       = prefs.getUChar("min_min", minMinutes);
    rateCoins        = prefs.getUShort("rate_c", rateCoins);
    rateValue        = prefs.getFloat("rate_v", rateValue);
    rateUnitMin      = prefs.getBool("rate_u", rateUnitMin);
    maxCoins         = prefs.getUShort("max_c", maxCoins);
    freeEnabled      = prefs.getBool("free_on", freeEnabled);
    freeMinRating    = prefs.getUChar("free_r", freeMinRating);
    freeMinutes      = prefs.getUChar("free_m", freeMinutes);
    helmetDetectCmC2 = prefs.getFloat("helm_c2_cm", helmetDetectCmC2);
    coolDownSec      = prefs.getUShort("cd_sec", coolDownSec);
    refreshOnMask  = prefs.getUShort("rf_on", refreshOnMask);
    refreshOffMask = prefs.getUShort("rf_off", refreshOffMask);
    autoRefreshSec = prefs.getUShort("arf_sec", autoRefreshSec);
    lastKnownPulsesC1 = prefs.getUInt("last_c1_pulses", 0);   // survives an S3C reboot mid-session
    recomputeDerived();
}

// ---------------------------------------------------------------------
// Settings as CSV (LocalServer <-> S3C)
// ---------------------------------------------------------------------
void rulesToCsv(char* out, size_t n) {
    size_t k = 0;
    for (uint8_t i = 0; i < 4 && k < n; i++) k += snprintf(out + k, n - k, "%u,%u,%u,", bandFrom[i], bandTo[i], bandMinutes[i]);
    if (k < n) snprintf(out + k, n - k, "%u,%u,%.2f,%u,%u,%u,%u,%u", minMinutes, rateCoins, (double)rateValue, rateUnitMin ? 1 : 0,
                        maxCoins, freeEnabled ? 1 : 0, freeMinRating, freeMinutes);
}

void heatToCsv(char* out, size_t n) {
    snprintf(out, n, "%.1f,%u", (double)helmetDetectCmC2, coolDownSec);
}

int parseFloats(const char* s, float* out, int maxN) {
    int n = 0;
    const char* p = s;
    while (*p && n < maxN) {
        char* end = nullptr;
        float v = strtof(p, &end);
        if (end == p) return -1;       // not a number
        out[n++] = v;
        p = end;
        if (*p == ',') p++;
        else if (*p != '\0') return -1;
    }
    return n;
}

bool applyRulesCsv(const char* s) {
    float v[20];
    if (parseFloats(s, v, 20) != 20) return false;
    uint8_t f[4], t[4], m[4];
    for (uint8_t i = 0; i < 4; i++) {
        if (v[i * 3] < 1 || v[i * 3] > 10 || v[i * 3 + 1] < 1 || v[i * 3 + 1] > 10 || v[i * 3] > v[i * 3 + 1]) return false;
        if (v[i * 3 + 2] < 1 || v[i * 3 + 2] > 120) return false;
        f[i] = (uint8_t)v[i * 3]; t[i] = (uint8_t)v[i * 3 + 1]; m[i] = (uint8_t)v[i * 3 + 2];
    }
    if (v[12] < 1 || v[12] > 120 || v[13] < 1 || v[13] > 500 || v[14] < 0.1f || v[14] > 9999 || v[16] < 1 || v[16] > 999) return false;
    if (v[18] < 1 || v[18] > 10 || v[19] < 1 || v[19] > 60) return false;
    for (uint8_t i = 0; i < 4; i++) { bandFrom[i] = f[i]; bandTo[i] = t[i]; bandMinutes[i] = m[i]; }
    minMinutes = (uint8_t)v[12]; rateCoins = (uint16_t)v[13]; rateValue = v[14]; rateUnitMin = v[15] >= 0.5f;
    maxCoins = (uint16_t)v[16]; freeEnabled = v[17] >= 0.5f; freeMinRating = (uint8_t)v[18]; freeMinutes = (uint8_t)v[19];
    recomputeDerived();
    return true;
}

bool applyHeatCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 1 || v[0] > 200 || v[1] < 5 || v[1] > 900) return false;
    helmetDetectCmC2 = v[0];
    coolDownSec = (uint16_t)(v[1] + 0.5f);
    return true;
}

// The old fixed-field config packet, still broadcast every second so an older dashboard keeps showing something.
void broadcastConfig() {
    Box2ConfigPacket packet;
    memset(&packet, 0, sizeof(Box2ConfigPacket));
    packet.deviceID         = DEVICE_S3C;
    packet.helmetDetectCmC2 = helmetDetectCmC2;
    packet.secondsPerCoin   = secondsPerCoin;
    packet.minCoinsRequired = minCoinsRequired;
    packet.maxCoinsAllowed  = maxCoins;
    packet.coolDownSec      = coolDownSec;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(Box2ConfigPacket));
}

// Per-relay screen refresh (subIndex 5): two numbers = bit mask of relays whose switching ON triggers a refresh, then the mask for switching OFF.
void refreshToCsv(char* out, size_t n) { snprintf(out, n, "%u,%u", (unsigned)refreshOnMask, (unsigned)refreshOffMask); }
bool applyRefreshCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 0 || v[1] < 0 || v[0] > C2_MASK_ALL || v[1] > C2_MASK_ALL) return false;
    refreshOnMask = (uint16_t)(v[0] + 0.5f);
    refreshOffMask = (uint16_t)(v[1] + 0.5f);
    return true;
}

// Auto hard refresh (subIndex 6): one number, seconds between refreshes (0 = never, 60 = max).
void autoRefreshToCsv(char* out, size_t n) { snprintf(out, n, "%u", (unsigned)autoRefreshSec); }
bool applyAutoRefreshCsv(const char* s) {
    float v[1];
    if (parseFloats(s, v, 1) != 1) return false;
    if (v[0] < 0 || v[0] > 60) return false;
    autoRefreshSec = (uint16_t)(v[0] + 0.5f);
    lastAutoRefreshMs = millis();
    return true;
}

void broadcastSettingsCsv() {
    char buf[200];
    rulesToCsv(buf, sizeof(buf));
    sendCmd(0, CMD_SET_CONFIG, 1, 0, 2, buf);       // state = 2: this is Box 2's copy (S3A sends state 0)
    delay(2);
    heatToCsv(buf, sizeof(buf));
    sendCmd(0, CMD_SET_CONFIG, 2, 0, 2, buf);
    delay(2);
    refreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 5, 0, 2, buf);
    delay(2);
    autoRefreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 6, 0, 2, buf);
}

// ---------------------------------------------------------------------
// Sensors (all read from the snapshot taken once per second)
// ---------------------------------------------------------------------
bool c2Alive() { return lastC2Ms != 0 && (millis() - lastC2Ms) < 5000UL; }
bool doorOpenNow()      { return c2snap.doorEnclosure; }
bool helmetPresentNow() { return c2snap.usHelmetDistance > 0.0f && c2snap.usHelmetDistance < helmetDetectCmC2; }

bool holdTicks(uint8_t &counter, bool cond, uint8_t need) {
    if (cond) { if (counter < 255) counter++; } else counter = 0;
    return counter >= need;
}

// ---------------------------------------------------------------------
// Messages to the other nodes
// ---------------------------------------------------------------------
void sendBuzzer(uint16_t ms) { sendCmd(DEVICE_C1, CMD_BUZZER, 0, ms, 0); }
void sendToken(uint8_t id, uint16_t value) { sendCmd(DEVICE_C1, CMD_SET_TOKEN, id, value, 0); delay(2); }

void sendStatusLines() {
    for (uint8_t i = 0; i < 3; i++) { sendCmd(DEVICE_C1, CMD_STATUS_LINE, i, 0, 0, statusLines[i]); delay(2); }
}
void pushStatus(const char* text) {
    strncpy(statusLines[2], statusLines[1], 31);
    strncpy(statusLines[1], statusLines[0], 31);
    strncpy(statusLines[0], text, 31);
    for (uint8_t i = 0; i < 3; i++) statusLines[i][31] = '\0';
    sendStatusLines();
}
void clearStatus() {
    for (uint8_t i = 0; i < 3; i++) statusLines[i][0] = '\0';
    sendStatusLines();
}

void resetCoinSession() {
    sendCmd(DEVICE_C1, CMD_RESET_COINS, 0, 0, 0);
    lastKnownPulsesC1 = 0;
    prefs.putUInt("last_c1_pulses", 0);
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------
uint8_t anyTapFlagForCurrentState() {
    return (currentState == B2_STATE_TAPS || currentState == B2_STATE_WELCOME) ? 1 : 0;
}

const char* screenForCurrentState() {
    switch (currentState) {
        case B2_STATE_TAPS:            return SCR_TAPS;
        case B2_STATE_WELCOME:         return SCR_WELCOME;
        case B2_STATE_RATE_A:          return SCR_RATE_A;
        case B2_STATE_TIME_ALLOT:
            if (timeWarnL1[0]) snprintf(scrBuf, sizeof(scrBuf), SCR_TIME_WARN, timeWarnL1, timeWarnL2);
            else snprintf(scrBuf, sizeof(scrBuf), SCR_TIME_ALLOT, (unsigned)secondsPerCoin, (unsigned)npInitSeconds);
            return scrBuf;
        case B2_STATE_INSERT_COIN:     return SCR_INSERT_COIN;
        case B2_STATE_INSTRUCTIONS:    return SCR_OPEN_A;
        case B2_STATE_PLACE_HELMET:    return SCR_SENSORS_A;
        case B2_STATE_CLOSE_DOOR_A:    return SCR_CLOSE_DOOR_A;
        case B2_STATE_HEATING:
            snprintf(scrBuf, sizeof(scrBuf), heatFree ? SCR_HEATING_FREE : SCR_HEATING, (unsigned long)heatTotalSec);
            return scrBuf;
        case B2_STATE_PAUSED_SAFETY:   return SCR_SAFETY_PAUSE;
        case B2_STATE_ABORT_CONFIRM:   return SCR_ABORT;
        case B2_STATE_COOL_DOWN:
            snprintf(scrBuf, sizeof(scrBuf), SCR_COOLDOWN, (unsigned long)coolTotalSec);
            return scrBuf;
        case B2_STATE_RETRIEVE:        return SCR_OPEN_B;
        case B2_STATE_TAKE_HELMET:     return SCR_SENSORS_B;
        case B2_STATE_CLOSE_DOOR_B:    return SCR_CLOSE_DOOR_B;
        case B2_STATE_RATE_B:          return SCR_RATE_B;
        case B2_STATE_FREE_RETRY:
            snprintf(scrBuf, sizeof(scrBuf), SCR_FREE_RETRY, (unsigned)freeMinutes, freeMinutes == 1 ? "MINUTE" : "MINUTES");
            return scrBuf;
        case B2_STATE_FINISH:          return SCR_FINISH;
        case B2_STATE_CANCEL_CONFIRM:  return SCR_CANCEL;
        case B2_STATE_MAINTENANCE:     return SCR_MAINTENANCE;
        case B2_STATE_OUT_OF_ORDER:    return SCR_OUT_OF_ORDER;
        case B2_STATE_HELMET_NOTICE_A: return SCR_HELMET_NOTICE_A;
        case B2_STATE_HELMET_NOTICE_B: return SCR_HELMET_NOTICE_B;
        default:                       return SCR_TAPS;
    }
}

void showScreen() {
    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
}

// ---------------------------------------------------------------------
// Relays: the whole 6-bit mask for C2, worked out from the state
// ---------------------------------------------------------------------
uint16_t desiredMaskForState() {
    uint16_t m = 0;
    bool unlockWanted = false;
    switch (currentState) {
        case B2_STATE_INSTRUCTIONS:
        case B2_STATE_RETRIEVE:
            unlockWanted = true;
            break;
        case B2_STATE_PAUSED_SAFETY:
            unlockWanted = unlockForPause;
            break;
        case B2_STATE_HEATING:
            m = C2_BIT(C2_RELAY_HEATER) | C2_BIT(C2_RELAY_UV_LIGHT) | C2_BIT(C2_RELAY_FAN);   // Heater + UV + Fan, the whole time
            break;
        case B2_STATE_COOL_DOWN:
            m = C2_BIT(C2_RELAY_FAN);                                                           // fan only
            break;
        case B2_STATE_MAINTENANCE:
            m = maintMask & C2_MASK_ALL;
            if (lockResting) m &= (uint16_t)(~C2_BIT(C2_RELAY_ENCLOSURE_LOCK));   // the duty cap covers hand-set relays too
            break;
        default:
            break;    // everything off, enclosure locked (the ABORT prompt and a safety pause also switch the heater off)
    }
    if (unlockWanted && !lockResting) m |= C2_BIT(C2_RELAY_ENCLOSURE_LOCK);
    return m;
}

void sendMask() {
    sendCmd(DEVICE_C2, CMD_SET_RELAY_MASK, 0, desiredMask, 0);
    lastMaskSentMs = millis();
}

void muteC1AndScheduleRedraw(bool hardRefresh) {
    sendCmd(DEVICE_C1, CMD_MUTE_COINS, 0, 250, 0);
    c1RedrawAtMillis = millis() + 300;
    if (hardRefresh) c1HardRefreshPending = true;
}

void applyDesiredMask() {
    uint16_t m = desiredMaskForState();
    if (m == desiredMask) return;
    // A relay change that is ticked in the dashboard's 'refresh screen' list (default: the enclosure lock engaging) asks C1 for a full
    // controller re-init after the change - the supply dips and the TFT can come back blank white. Every other relay change just gets a plain repaint.
    uint16_t changedBits = (uint16_t)(desiredMask ^ m);
    bool refreshNow = ((m & changedBits & refreshOnMask) != 0) || ((desiredMask & changedBits & refreshOffMask) != 0);
    desiredMask = m;
    muteC1AndScheduleRedraw(refreshNow);
    sendMask();
    Serial.printf("[S3C] relay mask -> 0x%02X\n", (unsigned)desiredMask);
}

// ---------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------
void enterState(Box2State s);

void recordCompletedSession() {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesC1 >= sessionCoinsAtStart) ? (lastKnownPulsesC1 - sessionCoinsAtStart) : 0;
    lastSessionDurationSec = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    lastSessionCoins       = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;
    completedSessionSeq++;
    Serial.printf("[S3C STATS] Session #%u: %us, %u coins, rating before %u.\n",
                  (unsigned)completedSessionSeq, (unsigned)lastSessionDurationSec, (unsigned)lastSessionCoins, ratingA);
}

void enterState(Box2State s) {
    currentState = s;
    holdA = holdB = holdC = 0;
    timeWarnL1[0] = '\0';
    switch (s) {
        case B2_STATE_TAPS:
            activeTimer = 0;
            retryUsed = false;
            heatFree = false;
            ratingA = ratingB = 0;
            sessionCoins = 0;
            heatTotalSec = coolTotalSec = 0;
            unlockForPause = false;
            clearStatus();
            resetCoinSession();
            break;
        case B2_STATE_TIME_ALLOT:
            npInitSeconds = suggestedSecondsForRating(ratingA);
            break;
        case B2_STATE_INSTRUCTIONS:
        case B2_STATE_RETRIEVE:
            unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            sendBuzzer(s == B2_STATE_RETRIEVE ? 500 : 250);
            break;
        case B2_STATE_PAUSED_SAFETY:
            unlockForPause = false; unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            break;
        case B2_STATE_FINISH:
            stepRemainingSec = 5;
            break;
        case B2_STATE_HELMET_NOTICE_A:
        case B2_STATE_HELMET_NOTICE_B:
            stepRemainingSec = 2;
            sendBuzzer(120);
            break;
        default:
            break;
    }
    applyDesiredMask();
    showScreen();
    Serial.printf("[S3C] -> %s\n", stateName(s));
}

void goOutOfOrder(const char* why) {
    Serial.printf("[S3C] OUT OF ORDER: %s\n", why);
    enterState(B2_STATE_OUT_OF_ORDER);
}

// A customer has tapped the TAPS screen.
void beginSession() {
    retryUsed = false; heatFree = false; ratingA = ratingB = 0; sessionCoins = 0;
    enterState(B2_STATE_WELCOME);
}

// START HEATING: a routing decision, not a state. The free retry picks the variant.
void beginHeating() {
    heatFree = retryUsed;
    if (heatFree) {
        activeTimer = (uint32_t)freeMinutes * 60UL;
    } else {
        sessionStartMillis  = millis();
        sessionCoinsAtStart = lastKnownPulsesC1;
    }
    heatTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    clearStatus();
    Serial.printf("[S3C] Heating starts: %s, %lus\n", heatFree ? "FREE retry" : "paid", (unsigned long)activeTimer);
    enterState(B2_STATE_HEATING);
    pushStatus("FAN: ON");
    pushStatus("UV: ON");
    pushStatus("HEATER: ON");
    sendBuzzer(250);
}

// COOL DOWN: fan only, a fixed number of seconds (setting). Also what an ABORT leads to.
void beginCoolDown() {
    activeTimer  = coolDownSec;
    coolTotalSec = coolDownSec > 9999 ? 9999 : coolDownSec;
    clearStatus();
    Serial.printf("[S3C] Cool down starts: %us\n", (unsigned)coolDownSec);
    enterState(B2_STATE_COOL_DOWN);
    pushStatus("FAN: ON");
    pushStatus("UV: OFF");
    pushStatus("HEATER: OFF");
}

void finishHeating() {
    if (!heatFree) recordCompletedSession();
    Serial.println("[S3C] Heating complete -> cool down");
    beginCoolDown();
}

// The numpad's OK: check the time against the rules, then on to paying - or say why not.
void handleNumpadOk(uint16_t sec) {
    uint32_t coins = coinsForSec(sec);
    uint32_t minSec = (uint32_t)minMinutes * 60UL;
    if (sec < minSec || coins < minCoinsRequired) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Minimum is %u min", (unsigned)minMinutes);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "(%u coins)", (unsigned)minCoinsRequired);
        npInitSeconds = sec;
        Serial.printf("[S3C] Time %us rejected: under the %u min minimum\n", sec, minMinutes);
        showScreen();
        return;
    }
    if (coins > maxCoins) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Maximum is %u coins", (unsigned)maxCoins);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "Please enter less time");
        npInitSeconds = sec;
        Serial.printf("[S3C] Time %us rejected: %u coins is over the %u maximum\n", sec, (unsigned)coins, maxCoins);
        showScreen();
        return;
    }
    requiredCoinsForSession = coins;
    activeTimer = 0;
    Serial.printf("[S3C] Time %us = %u coins accepted\n", sec, (unsigned)coins);
    enterState(B2_STATE_INSERT_COIN);
}

// A button or numpad result from C1 (queued by the radio callback, handled here in loop()).
void handleTouch(const char* a, uint16_t v) {
    Box2State st = currentState;
    Serial.printf("[S3C TOUCH] '%s' value=%u in %s\n", a, v, stateName(st));

    if (!strcmp(a, "TAP_ADVANCE")) {
        if (st == B2_STATE_TAPS) beginSession();
        else if (st == B2_STATE_WELCOME) enterState(B2_STATE_RATE_A);
    } else if (!strcmp(a, "ACTION")) {                  // CONFIRM on the rating screens: v = the rating picked
        if (st == B2_STATE_RATE_A || st == B2_STATE_RATE_B) {
            if (v < 1 || v > 10) { sendBuzzer(60); return; }   // nothing picked yet - ask again
            if (st == B2_STATE_RATE_A) {
                ratingA = (uint8_t)v;
                Serial.printf("[S3C] Rating before: %u/10\n", ratingA);
                enterState(B2_STATE_TIME_ALLOT);
            } else {
                ratingB = (uint8_t)v;
                Serial.printf("[S3C] Rating after: %u/10\n", ratingB);
                if (!retryUsed && ratingGetsFreeRetry(ratingB)) { retryUsed = true; enterState(B2_STATE_FREE_RETRY); }
                else enterState(B2_STATE_FINISH);
            }
        }
    } else if (!strcmp(a, "NP_OK")) {
        if (st == B2_STATE_TIME_ALLOT && !timeWarnL1[0]) handleNumpadOk(v);
    } else if (!strcmp(a, "NP_BACK")) {
        if (st == B2_STATE_TIME_ALLOT) enterState(B2_STATE_RATE_A);
    } else if (!strcmp(a, "NP_CANCEL")) {
        if (st == B2_STATE_TIME_ALLOT) enterState(B2_STATE_TAPS);
    } else if (!strcmp(a, "WARN_OK")) {
        if (st == B2_STATE_TIME_ALLOT && timeWarnL1[0]) { timeWarnL1[0] = '\0'; showScreen(); }   // numpad again, with the typed time
    } else if (!strcmp(a, "INSERT_CANCEL")) {
        if (st == B2_STATE_INSERT_COIN) { returnState = B2_STATE_INSERT_COIN; enterState(B2_STATE_CANCEL_CONFIRM); }
    } else if (!strcmp(a, "CANCEL_YES")) {
        if (st == B2_STATE_CANCEL_CONFIRM) { Serial.println("[S3C] Cancelled by the customer"); enterState(B2_STATE_TAPS); }
    } else if (!strcmp(a, "CANCEL_NO")) {
        if (st == B2_STATE_CANCEL_CONFIRM) enterState(returnState);
    } else if (!strcmp(a, "ABORT")) {
        if (st == B2_STATE_HEATING) { returnState = B2_STATE_HEATING; enterState(B2_STATE_ABORT_CONFIRM); }
    } else if (!strcmp(a, "ABORT_YES")) {
        if (st == B2_STATE_ABORT_CONFIRM) {
            Serial.println("[S3C] Heating aborted by the customer -> cool down first");
            beginCoolDown();
        }
    } else if (!strcmp(a, "ABORT_NO")) {
        if (st == B2_STATE_ABORT_CONFIRM) enterState(returnState);     // back to the heating, time and bar untouched
    } else if (!strcmp(a, "FREE_YES")) {
        if (st == B2_STATE_FREE_RETRY) { retryUsed = true; enterState(B2_STATE_INSTRUCTIONS); }
    } else if (!strcmp(a, "FREE_NO")) {
        if (st == B2_STATE_FREE_RETRY) enterState(B2_STATE_FINISH);
    } else if (!strcmp(a, "RESET")) {
        enterState(B2_STATE_TAPS);
    }
}

void handleMaintenance(uint8_t on) {
    if (on) {
        if (currentState == B2_STATE_TAPS || currentState == B2_STATE_OUT_OF_ORDER) { maintMask = 0; enterState(B2_STATE_MAINTENANCE); }
        else Serial.println("[S3C] Maintenance refused: only possible from the idle screen");
    } else if (currentState == B2_STATE_MAINTENANCE) {
        maintMask = 0;
        enterState(B2_STATE_TAPS);
    }
}

// ---------------------------------------------------------------------
// The once-a-second state logic
// ---------------------------------------------------------------------
void tickState() {
    bool open = doorOpenNow();
    bool closed = !open;
    bool hp = helmetPresentNow();

    switch (currentState) {
        case B2_STATE_TAPS:
            if (!c2Alive()) goOutOfOrder("relay / sensor board C2 is not answering");
            break;

        case B2_STATE_INSERT_COIN:
            if (activeTimer >= requiredCoinsForSession * secondsPerCoin) {
                activeTimer = requiredCoinsForSession * secondsPerCoin;   // anything beyond the price is discarded
                enterState(B2_STATE_INSTRUCTIONS);
            }
            break;

        case B2_STATE_INSTRUCTIONS:                    // OPEN A
            if (holdTicks(holdA, open, 2)) enterState(B2_STATE_PLACE_HELMET);
            break;

        case B2_STATE_PLACE_HELMET: {                  // SENSORS A
            // Helmet seen inside -> on to CLOSE DOOR, whether the door is still open or a quick customer has already shut it.
            bool placed = holdTicks(holdA, hp, 2);
            bool shutEmpty = holdTicks(holdB, closed && !hp, 2);
            if (placed) enterState(B2_STATE_CLOSE_DOOR_A);
            else if (shutEmpty) enterState(B2_STATE_HELMET_NOTICE_A);
            break;
        }

        case B2_STATE_CLOSE_DOOR_A: {
            bool go = holdTicks(holdA, closed && hp, 2);
            bool empty = holdTicks(holdB, closed && !hp, 2);
            if (go) beginHeating();
            else if (empty) enterState(B2_STATE_HELMET_NOTICE_A);
            break;
        }

        case B2_STATE_HELMET_NOTICE_A:                 // "HELMET NO DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(B2_STATE_INSTRUCTIONS);
            break;

        case B2_STATE_HEATING:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: heater, UV and fan off at once
                Serial.println("[S3C] SAFETY: the door opened during heating");
                returnState = B2_STATE_HEATING;
                enterState(B2_STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) finishHeating();
            }
            break;

        case B2_STATE_COOL_DOWN:
            if (holdTicks(holdA, open, 2)) {           // the customer opened the door before the fan was done
                Serial.println("[S3C] SAFETY: the door opened during cool down");
                returnState = B2_STATE_COOL_DOWN;
                enterState(B2_STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) { Serial.println("[S3C] Cool down complete -> open the door"); enterState(B2_STATE_RETRIEVE); }
            }
            break;

        case B2_STATE_PAUSED_SAFETY: {
            bool resume = holdTicks(holdA, closed && hp, 2);
            bool needIn = holdTicks(holdB, closed && !hp, 2);
            if (resume) {
                Serial.println("[S3C] Safety restored - carrying on");
                unlockForPause = false;
                enterState(returnState);               // timers and bars untouched
                break;
            }
            if (needIn && returnState == B2_STATE_COOL_DOWN) {
                // the helmet is already out during the cool down: nothing left to wait for, carry on from CLOSE DOOR B
                Serial.println("[S3C] Helmet taken out during the cool down - skipping the rest of it");
                unlockForPause = false;
                enterState(B2_STATE_CLOSE_DOOR_B);
                break;
            }
            if (needIn && !unlockForPause) { Serial.println("[S3C] Door shut but no helmet - unlocking so it can be reopened"); unlockForPause = true; unlockHeldSec = 0; }
            if (unlockForPause && holdTicks(holdC, open, 2)) unlockForPause = false;     // release once it is genuinely open
            break;
        }

        case B2_STATE_RETRIEVE:                        // OPEN B
            if (holdTicks(holdA, open, 2)) enterState(B2_STATE_TAKE_HELMET);
            break;

        case B2_STATE_TAKE_HELMET: {                   // SENSORS B
            // Helmet gone -> on to CLOSE DOOR (which asks for the door if it is still open, and carries straight on if it
            // is already shut).
            bool out = holdTicks(holdA, !hp, 2);
            bool still = holdTicks(holdB, hp && closed, 2);
            if (out) enterState(B2_STATE_CLOSE_DOOR_B);
            else if (still) enterState(B2_STATE_HELMET_NOTICE_B);
            break;
        }

        case B2_STATE_CLOSE_DOOR_B: {
            bool done = holdTicks(holdA, closed && !hp, 2);
            bool back = holdTicks(holdB, closed && hp, 2);
            if (done) enterState(retryUsed ? B2_STATE_FINISH : B2_STATE_RATE_B);
            else if (back) enterState(B2_STATE_HELMET_NOTICE_B);
            break;
        }

        case B2_STATE_HELMET_NOTICE_B:                 // "HELMET DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(B2_STATE_RETRIEVE);
            break;

        case B2_STATE_FINISH:
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(B2_STATE_TAPS);
            break;

        case B2_STATE_OUT_OF_ORDER:
            if (c2Alive()) { Serial.println("[S3C] Back in order"); enterState(B2_STATE_TAPS); }
            break;

        default:                                       // touch-driven or idle states
            break;
    }
}

// ---------------------------------------------------------------------
// Coins. Box 2 has no relay to cut the acceptor's power, so it always takes coins: only INSERT COIN and a PAID heating
// (up to the maximum) credit them; a coin at any other moment is ignored (and logged).
// ---------------------------------------------------------------------
bool coinsAcceptedNow() {
    return currentState == B2_STATE_INSERT_COIN ||
           (currentState == B2_STATE_HEATING && !heatFree && sessionCoins < maxCoins);
}

void processPulses() {
    uint32_t pc; bool seen;
    portENTER_CRITICAL(&rxMux);
    pc = c1Pulses; seen = c1Seen;
    portEXIT_CRITICAL(&rxMux);
    if (!seen) return;
    if (pc > lastKnownPulsesC1) {
        uint32_t newPulses = pc - lastKnownPulsesC1;
        lastKnownPulsesC1 = pc;
        prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
        if (coinsAcceptedNow()) {
            uint32_t added = newPulses * secondsPerCoin;
            activeTimer += added;
            sessionCoins += newPulses;
            if (currentState == B2_STATE_HEATING) {
                heatTotalSec = (heatTotalSec + added > 9999UL) ? 9999UL : heatTotalSec + added;
                needScreenRedraw = true;               // the bar's total grew
                coinFlag = true;
            }
            Serial.printf("[S3C COIN] +%lus. activeTimer = %lus, session coins %lu\n", (unsigned long)added, (unsigned long)activeTimer, (unsigned long)sessionCoins);
        } else {
            Serial.printf("[S3C COIN] %lu pulse(s) ignored in %s\n", (unsigned long)newPulses, stateName(currentState));
        }
    } else if (pc < lastKnownPulsesC1) {
        // C1's counter went back down (it rebooted or was reset) - resync rather than double-credit
        lastKnownPulsesC1 = pc;
        prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
    }
}

// ---------------------------------------------------------------------
// ESP-NOW receive: only stores - loop() does the work
// ---------------------------------------------------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData || len <= 0) return;

    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));
        if (cmd.targetDeviceID != DEVICE_S3C) return;
        if (cmd.commandID != CMD_TOUCH_ACTION && cmd.commandID != CMD_SET_CONFIG &&
            cmd.commandID != CMD_SET_MAINTENANCE && cmd.commandID != CMD_SET_RELAY_MASK &&
            cmd.commandID != CMD_TFT_HARD_REFRESH) return;
        cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&rxMux);
        uint8_t next = (uint8_t)((qHead + 1) % CMD_QUEUE_LEN);
        if (next != qTail) { cmdQueue[qHead] = cmd; qHead = next; }     // a full queue drops the newest
        portEXIT_CRITICAL(&rxMux);
        return;
    }
    if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket p;
        memcpy(&p, incomingData, sizeof(TelemetryPacket));
        if (p.deviceID == DEVICE_C1) {
            portENTER_CRITICAL(&rxMux);
            c1Pulses = p.pulseCount; c1Seen = true;
            portEXIT_CRITICAL(&rxMux);
        } else if (p.deviceID == DEVICE_C2) {
            portENTER_CRITICAL(&rxMux);
            nodeC2 = p; lastC2Ms = millis();
            portEXIT_CRITICAL(&rxMux);
        }
    }
}

void handleQueuedCommand(const CommandPacket& cmd) {
    if (cmd.commandID == CMD_TOUCH_ACTION) {
        handleTouch(cmd.payloadStr, cmd.param16);
    } else if (cmd.commandID == CMD_SET_MAINTENANCE) {
        handleMaintenance(cmd.state ? 1 : 0);
    } else if (cmd.commandID == CMD_SET_RELAY_MASK) {
        if (currentState == B2_STATE_MAINTENANCE) { maintMask = cmd.param16 & C2_MASK_ALL; applyDesiredMask(); }
    } else if (cmd.commandID == CMD_TFT_HARD_REFRESH) {
        // "Force refresh" from the dashboard: restart C1's touch screen and redraw the current page (cures a white or garbled screen)
        c1HardRefreshPending = true;
        c1RedrawAtMillis = millis() + 50;
        Serial.println("[S3C] screen refresh requested from the dashboard");
    } else if (cmd.commandID == CMD_SET_CONFIG) {
        bool ok = false;
        if (cmd.subIndex == 1) ok = applyRulesCsv(cmd.payloadStr);
        else if (cmd.subIndex == 2) ok = applyHeatCsv(cmd.payloadStr);
        else if (cmd.subIndex == 5) ok = applyRefreshCsv(cmd.payloadStr);
        else if (cmd.subIndex == 6) ok = applyAutoRefreshCsv(cmd.payloadStr);
        else {   // the old "helmCm,secPerCoin,minCoins[,maxCoins[,coolDownSec]]" line
            float v[5];
            int n = parseFloats(cmd.payloadStr, v, 5);
            if (n >= 3 && n <= 5) {
                if (v[0] >= 1 && v[0] <= 200) helmetDetectCmC2 = v[0];
                if (n >= 4 && v[3] >= 1 && v[3] <= 999) maxCoins = (uint16_t)v[3];
                if (n == 5 && v[4] >= 5 && v[4] <= 900) coolDownSec = (uint16_t)(v[4] + 0.5f);   // under 5 = an old cool-down RATIO: ignored
                ok = true;
            }
        }
        if (ok) { saveConfigToNVS(); broadcastConfig(); Serial.printf("[S3C CONFIG] Updated & saved (type %u)\n", cmd.subIndex); }
        else Serial.printf("[S3C CONFIG] REJECTED (type %u): '%s'\n", cmd.subIndex, cmd.payloadStr);
    }
}

// ---------------------------------------------------------------------
// USB serial console - tune and inspect without a dashboard
// ---------------------------------------------------------------------
void printConfig() {
    char r[200], h[60];
    rulesToCsv(r, sizeof(r)); heatToCsv(h, sizeof(h));
    Serial.println("---- S3C settings ----");
    Serial.printf("state: %s | C2 %s\n", stateName(currentState), c2Alive() ? "ok" : "SILENT");
    Serial.printf("rules : bands %u-%u=%um, %u-%u=%um, %u-%u=%um, %u-%u=%um | min %u min | %u coin(s) = %.2f %s (%.2f s/coin) | max %u coins | free retry %s from %u for %u min\n",
                  bandFrom[0], bandTo[0], bandMinutes[0], bandFrom[1], bandTo[1], bandMinutes[1], bandFrom[2], bandTo[2], bandMinutes[2], bandFrom[3], bandTo[3], bandMinutes[3],
                  minMinutes, rateCoins, (double)rateValue, rateUnitMin ? "min" : "s", (double)secPerCoinF, maxCoins, freeEnabled ? "ON" : "off", freeMinRating, freeMinutes);
    Serial.printf("heat  : helmet < %.1f cm | cool down %u s\n", (double)helmetDetectCmC2, coolDownSec);
    Serial.printf("LIVE  : helmet %.1f cm | door %s | relays now: enc %d pan %d bak %d heater %d uv %d fan %d\n",
                  (double)c2snap.usHelmetDistance, c2snap.doorEnclosure ? "OPEN" : "shut",
                  c2snap.relayStates[0], c2snap.relayStates[1], c2snap.relayStates[2], c2snap.relayStates[3], c2snap.relayStates[4], c2snap.relayStates[5]);
    Serial.printf("csv   : rules=%s\n        heat=%s\n", r, h);
}

void handleSerialLine(char* line) {
    char* cmd = strtok(line, " \t\r\n");
    if (!cmd) return;
    char* a1 = strtok(nullptr, " \t\r\n");
    char* a2 = strtok(nullptr, " \t\r\n");
    char* a3 = strtok(nullptr, " \t\r\n");
    if (!strcmp(cmd, "help")) {
        Serial.println("Commands:  cfg | reset | set <name> <value> | set bandN <from> <to> <minutes>  (N = 1..4)");
        Serial.println("Names:  helm (cm), cooldown (seconds, 5-900), minmin, coins, timevalue, unitmin 0/1, maxcoins, freeon 0/1, freerating, freemin");
        return;
    }
    if (!strcmp(cmd, "cfg")) { printConfig(); return; }
    if (!strcmp(cmd, "reset")) { enterState(B2_STATE_TAPS); return; }
    if (!strcmp(cmd, "set") && a1 && a2) {
        float v = atof(a2);
        if (!strncmp(a1, "band", 4) && a1[4] >= '1' && a1[4] <= '4' && a3) {
            uint8_t i = a1[4] - '1';
            bandFrom[i] = (uint8_t)atoi(a2); bandTo[i] = (uint8_t)atoi(a3);
            char* a4 = strtok(nullptr, " \t\r\n");
            if (a4) bandMinutes[i] = (uint8_t)atoi(a4);
        }
        else if (!strcmp(a1, "helm")) helmetDetectCmC2 = v;
        else if (!strcmp(a1, "cooldown")) { if (v < 5 || v > 900) { Serial.println("cooldown must be 5-900 seconds"); return; } coolDownSec = (uint16_t)(v + 0.5f); }
        else if (!strcmp(a1, "minmin")) minMinutes = (uint8_t)v;
        else if (!strcmp(a1, "coins")) rateCoins = (uint16_t)v;
        else if (!strcmp(a1, "timevalue")) rateValue = v;
        else if (!strcmp(a1, "unitmin")) rateUnitMin = v != 0;
        else if (!strcmp(a1, "maxcoins")) maxCoins = (uint16_t)v;
        else if (!strcmp(a1, "freeon")) freeEnabled = v != 0;
        else if (!strcmp(a1, "freerating")) freeMinRating = (uint8_t)v;
        else if (!strcmp(a1, "freemin")) freeMinutes = (uint8_t)v;
        else { Serial.printf("unknown setting '%s' - type help\n", a1); return; }
        recomputeDerived();
        saveConfigToNVS();
        broadcastConfig();
        printConfig();
        return;
    }
    Serial.println("unknown command - type help");
}

void serialConsole() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialLen > 0) { serialBuf[serialLen] = '\0'; handleSerialLine(serialBuf); serialLen = 0; }
        } else if (serialLen < sizeof(serialBuf) - 1) {
            serialBuf[serialLen++] = c;
        }
    }
}

// ---------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------
void broadcastStatus() {
    BoxStatusPacket packet;
    memset(&packet, 0, sizeof(BoxStatusPacket));
    packet.deviceID               = DEVICE_S3C;
    packet.machineState           = (uint8_t)currentState;
    packet.activeTimer            = activeTimer;
    packet.stepRemainingSec       = stepRemainingSec;
    packet.handshakeValid         = (!doorOpenNow()) && helmetPresentNow();
    packet.completedSessionSeq    = completedSessionSeq;
    packet.lastSessionDurationSec = lastSessionDurationSec;
    packet.lastSessionCoins       = lastSessionCoins;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(BoxStatusPacket));
}

void oneSecondTick() {
    unsigned long now = millis();

    // fresh copy of C2's report for this tick
    portENTER_CRITICAL(&rxMux);
    c2snap = nodeC2;
    portEXIT_CRITICAL(&rxMux);
    if (!c2Alive()) {
        // Sensor board silent: never keep acting on its last readings. "Door open, no helmet" is the safe way to be
        // wrong - a heating in progress pauses with the heater, UV and fan off until C2 is back.
        c2snap.doorEnclosure = true;
        c2snap.usHelmetDistance = 0.0f;
    }

    // enclosure solenoid duty cap
    bool lockWanted = (currentState == B2_STATE_INSTRUCTIONS || currentState == B2_STATE_RETRIEVE ||
                       (currentState == B2_STATE_PAUSED_SAFETY && unlockForPause) ||
                       (currentState == B2_STATE_MAINTENANCE && (maintMask & C2_BIT(C2_RELAY_ENCLOSURE_LOCK))));
    if (lockWanted) {
        if (!lockResting) {
            if (++unlockHeldSec >= LOCK_MAX_ON_SEC) { lockResting = true; lockRestSec = 0; Serial.println("[S3C] Enclosure lock rests for a few seconds (solenoid duty cap)"); }
        } else if (++lockRestSec >= LOCK_REST_SEC) {
            lockResting = false; unlockHeldSec = 0;
        }
    } else {
        unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
    }

    // MAINTENANCE: a heater switched on by hand turns itself off after 5 minutes, so a forgotten test cannot overheat the box
    if (currentState == B2_STATE_MAINTENANCE && (maintMask & C2_BIT(C2_RELAY_HEATER))) {
        if (++maintHeaterSec >= MAINT_HEATER_MAX_SEC) {
            maintMask &= (uint16_t)(~C2_BIT(C2_RELAY_HEATER));
            maintHeaterSec = 0;
            Serial.println("[S3C] Maintenance: the heater was on for 5 minutes - switched off");
        }
    } else {
        maintHeaterSec = 0;
    }

    tickState();
    applyDesiredMask();                       // picks up the lock duty cap

    if (coinFlag) { coinFlag = false; pushStatus("COIN INSERTED"); }

    // The once-a-second broadcasts. Small gaps: several back-to-back sends can overflow ESP-NOW's queue.
    sendCmd(DEVICE_C1, CMD_SYNC_VARS, (uint8_t)min(minCoinsRequired, (uint32_t)255), (uint16_t)activeTimer, (uint8_t)min(requiredCoinsForSession, (uint32_t)255));
    delay(2);
    sendCmd(0, CMD_HEARTBEAT, DEVICE_S3C, 0, 0);
    delay(2);
    broadcastConfig();
    delay(2);
    broadcastStatus();
    delay(2);

    if (now - lastMaskSentMs >= MASK_REASSERT_MS) { sendMask(); delay(2); }
    if (now - lastCfgCsvMs >= 2000) { lastCfgCsvMs = now; broadcastSettingsCsv(); }
    if (now - lastTokenMs >= 5000) {          // numbers C1 may have missed or lost in a reboot
        lastTokenMs = now;
        sendToken(TOKEN_MAXCOINS, maxCoins);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=======================================================");
    Serial.println("   NODE S3C: BOX 2 (HEATER) INTERNAL SERVER MANAGEMENT (ESP32-S3)");
    Serial.println("   New procedure (24-step Designer flow). Type 'help' for the console.");
    Serial.println("=======================================================");

    memset(&nodeC2, 0, sizeof(nodeC2));
    memset(&c2snap, 0, sizeof(c2snap));
    loadConfigFromNVS();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[S3C] ESP-NOW: Initialization FAILED!");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    broadcastConfig();
    printConfig();
    enterState(B2_STATE_TAPS);
    sendMask();
    sendToken(TOKEN_MAXCOINS, maxCoins);
}

// The periodic refresh skips the screens where the customer is typing the time or picking a rating: the terminal keeps what was typed / picked, and a refresh would wipe it.
bool autoRefreshAllowedNow() {
    return !(currentState == B2_STATE_RATE_A || currentState == B2_STATE_TIME_ALLOT || currentState == B2_STATE_RATE_B);
}

void loop() {
    unsigned long now = millis();

    // queued commands from C1 / LocalServer
    while (true) {
        CommandPacket cmd;
        bool have = false;
        portENTER_CRITICAL(&rxMux);
        if (qTail != qHead) { cmd = cmdQueue[qTail]; qTail = (uint8_t)((qTail + 1) % CMD_QUEUE_LEN); have = true; }
        portEXIT_CRITICAL(&rxMux);
        if (!have) break;
        handleQueuedCommand(cmd);
    }

    processPulses();
    serialConsole();

    // periodic hard refresh of C1 (setting "Auto hard refresh"): 0 = never; also not while another repaint is already pending
    if (autoRefreshSec > 0 && now - lastAutoRefreshMs >= (unsigned long)autoRefreshSec * 1000UL) {
        lastAutoRefreshMs = now;
        if (c1RedrawAtMillis == 0 && autoRefreshAllowedNow()) { c1HardRefreshPending = true; c1RedrawAtMillis = now + 1; }
    }

    // post-solenoid / post-relay repaint of C1 (the EMI-glitch mitigation), or a requested one
    if (c1RedrawAtMillis != 0 && now >= c1RedrawAtMillis) {
        c1RedrawAtMillis = 0;
        needScreenRedraw = false;
        if (c1HardRefreshPending) {
            c1HardRefreshPending = false;
            sendCmd(DEVICE_C1, CMD_TFT_HARD_REFRESH, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
        } else {
            showScreen();
        }
    } else if (needScreenRedraw) {
        needScreenRedraw = false;
        showScreen();
    }

    if (now - lastOneSecTick >= 1000) {
        lastOneSecTick = now;
        oneSecondTick();
    }
    delay(2);
}
