#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "S3A_Screens.h"

// =====================================================================
// NODE S3A - BOX 1 INTERNAL SERVER MANAGEMENT (ESP32-S3)              (core 3.3.7)
// REWRITTEN 2026-10-02 for the new Box 1 procedure designed in the Box Cycle Designer
// (BOX1_STEPS_FINAL_2026-10-02.json, 30 steps). The previous flow is archived next to this file as
// ESP32_S3A_LEGACY_old_flow.ino.txt - that is what runs on the customer's box until this one is flashed.
//
// Runs the whole customer journey by itself (no LocalServer needed):
//   TAPS -> CHECKING A/S (container levels, REFILLING A/S) -> WELCOME -> RATE A -> METHOD (mist choice) ->
//   TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> CLEANING -> OPEN B -> SENSORS B ->
//   CLOSE DOOR B -> RATE B -> (FREE RETRY) -> FINISH -> TAPS, with ABORT / CANCEL confirmations, MAINTENANCE
//   and OUT OF ORDER.
// Talks to: A1 (screen/touch/coins), A2 (sensors: A2SensorPacket), A3 (relays: one CMD_SET_RELAY_MASK).
//
// Not one screen state per Designer step: START CLEANING is a routing decision, and the six CLEANING variants
// are one STATE_CLEANING plus mistMode / cleanFree (see Shared_Common.h's MachineState comments).
//
// DYNAMIC SETTINGS (NVS, survive a LocalServer swap; LocalServer edits them with CMD_SET_CONFIG):
//   subIndex 0  the old 5-6 field line (helmet cm, ..., kept so an old dashboard does not break)
//   subIndex 1  RULES  : 4 rating bands (from,to,minutes) x4, minMinutes, coins, timeValue, unitMin(1/0),
//                        maxCoins, freeOn, freeMinRating, freeMinutes                      (20 numbers)
//   subIndex 2  LEVELS : checksOn, alcLowCm, alcHighCm, sctLowCm, sctHighCm, helmetCm, wsAlcEnter, wsAlcLeave,
//                        wsSctEnter, wsSctLeave, wsWetHigher, refillTries, refillIntervalSec, acsRequests (14)
// S3A broadcasts both back as CMD_SET_CONFIG (target 0) every 2 s so a dashboard always shows the live values.
// Until LocalServer has its page, everything can be set from the USB serial console: type "help".
// =====================================================================

Preferences prefs;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------
// Settings - RULES (defaults = the "rules" block of the final Designer export)
// ---------------------------------------------------------------------
uint8_t  bandFrom[4]    = {1, 6, 8, 10};
uint8_t  bandTo[4]      = {5, 7, 9, 10};
uint8_t  bandMinutes[4] = {5, 10, 15, 20};
uint8_t  minMinutes     = 5;       // the shortest session
uint16_t rateCoins      = 15;      // "N coins = X seconds/minutes"
float    rateValue      = 5.0f;
bool     rateUnitMin    = true;    // true = minutes, false = seconds
uint16_t maxCoins       = 90;      // most coins one session can take (90 coins = 30 min at 15 coins = 5 min)
bool     freeEnabled    = true;    // one free retry after Rate B ...
uint8_t  freeMinRating  = 8;       // ... when the final rating is at least this
uint8_t  freeMinutes    = 2;       // ... lasting this long

// ---------------------------------------------------------------------
// Settings - LEVELS / SENSORS / REFILL
// Container distances are what the Main Container ultrasonic reads (cm from the sensor down to the liquid):
// BIGGER = emptier. "Low" = at/over the Low distance (or no echo); "refilled" = at/under the High distance.
// levelChecksOn defaults to OFF so a box with uncalibrated sensors still serves customers; switch it on
// (console "set checks 1", later the LocalServer page) once the four distances below are measured.
// ---------------------------------------------------------------------
bool     levelChecksOn    = false;
float    alcLowCm         = 20.0f;  // PLACEHOLDERS - measure with the real containers
float    alcHighCm        = 8.0f;
float    sctLowCm         = 20.0f;
float    sctHighCm        = 8.0f;
float    helmetDetectCmA2 = 15.0f;  // helmet counts as inside when the helmet ultrasonic reads under this
uint16_t wsEnter[2]       = {600, 600};  // analog water sensors (alcohol, scented): raw reading that means WET
uint16_t wsLeave[2]       = {400, 400};  // raw reading that means DRY again (the gap = hysteresis)
bool     wsWetHigher      = true;   // true = the reading goes UP when the sensor is in liquid
uint8_t  refillTriesMax   = 10;     // refill attempts before giving up ...
uint8_t  refillIntervalSec = 10;    // ... this many seconds apart
bool     acsRequests      = false;  // OFF: count the tries but do not message the ACS (Box 1's ACS is still being fixed)
bool     refillBypass     = false;  // "last legs": refill failed, serve from what is left until the levels recover

// ---------------------------------------------------------------------
// Settings - CLEANING PHASES (from the Designer's "phased relays", user 2026-10-02)
// A paid cleaning is cut into a START part, a MIDDLE part (what is left) and an END part. Each cleaning relay is
// ON or OFF in each part: pattern bits 1 = start, 2 = middle, 4 = end. The defaults are the user's setup: the first
// and last minute are UV only, the humidifiers run in the middle only. Part lengths: unit 0 = % of the cleaning,
// 1 = seconds, 2 = minutes. The free retry is not phased by default (everything runs the whole time).
// Order of the pattern list: Humidifier 1, Humidifier 2, Humidifier 3, UV 1, UV 2.
// ---------------------------------------------------------------------
const uint8_t PH_START = 1, PH_MID = 2, PH_END = 4;
bool     phasePaid      = true;
bool     phaseFree      = false;
float    phaseStartVal  = 1.0f;
float    phaseEndVal    = 1.0f;
uint8_t  phaseStartUnit = 2;
uint8_t  phaseEndUnit   = 2;
uint8_t  phasePat[5]    = {PH_MID, PH_MID, PH_MID, PH_START | PH_END, PH_START | PH_END};

// Derived from the rules by recomputeDerived()
float    secPerCoinF      = 20.0f;
uint16_t secondsPerCoin   = 20;
uint32_t minCoinsRequired = 15;

// ---------------------------------------------------------------------
// Session (one customer)
// ---------------------------------------------------------------------
const uint8_t MIST_NONE = 0, MIST_ALC = 1, MIST_SCN = 2, MIST_BAL = 3;
MachineState currentMachineState = STATE_TAPS;
MachineState returnState         = STATE_TAPS;   // where ABORT / CANCEL "go back to"
uint8_t  mistMode        = MIST_NONE;
bool     retryUsed       = false;   // the one free retry has been taken (or offered)
bool     cleanFree       = false;   // the cleaning in progress is the free retry
uint8_t  ratingA         = 0, ratingB = 0;
uint32_t requiredCoinsForSession = 0;
uint32_t activeTimer     = 0;       // seconds left of the cleaning (or seconds bought while paying)
uint32_t cleanTotalSec   = 0;       // length of the cleaning, for the progress bar (capped at 9999)
uint32_t sessionCoins    = 0;
uint32_t cleanElapsedSec = 0;       // seconds of cleaning already done (pauses do not count) - drives the phases
bool     lastUvOn = false, lastMistOn = false;   // what the cleaning status feed last announced
uint32_t npInitSeconds   = 0;       // the numpad's suggested time
char     timeWarnL1[32]  = "", timeWarnL2[32] = "";   // numpad rejection message (empty = none)
uint8_t  refillTry       = 0, refillElapsed = 0;
uint16_t stepRemainingSec = 0;
char     scrBuf[200];               // scratch for screens with live numbers baked in

// per-state debounce counters (2 ticks = 2 s, same as the previous flow)
uint8_t  holdA = 0, holdB = 0, holdC = 0;
// enclosure solenoid duty cap: never energized for more than LOCK_MAX_ON_SEC in a row (a coil held on for
// 10+ minutes once overheated into a fire-risk incident)
const uint16_t LOCK_MAX_ON_SEC = 60, LOCK_REST_SEC = 5;
uint16_t unlockHeldSec = 0, lockRestSec = 0;
bool     lockResting   = false;
bool     unlockForPause = false;    // PAUSED_SAFETY with the door shut and no helmet: let them back in
uint16_t maintMask     = 0;         // MAINTENANCE: relays set by hand from the dashboard

// session statistics for LocalServer (BoxStatusPacket)
uint32_t completedSessionSeq    = 0;
uint32_t sessionStartMillis     = 0;
uint32_t sessionCoinsAtStart    = 0;
uint16_t lastSessionDurationSec = 0;
uint16_t lastSessionCoins       = 0;

// ---------------------------------------------------------------------
// Live data from the other nodes (written by the receive callback, snapshotted in loop())
// ---------------------------------------------------------------------
volatile uint32_t a1Pulses = 0;     volatile bool a1Seen = false;
A2SensorPacket    nodeA2;           uint32_t lastA2Ms = 0;
uint16_t          a3Mask = 0;       uint32_t lastA3Ms = 0;  bool a3Orphaned = false;
A2SensorPacket    a2snap;           // the copy the state machine reads
bool     wsWet[2]   = {false, false};
uint8_t  wsCnt[2]   = {0, 0};
uint32_t lastKnownPulsesA1 = 0;

// commands queued by the callback, handled in loop() (no heavy work or state changes in the radio task)
#define CMD_QUEUE_LEN 4
CommandPacket cmdQueue[CMD_QUEUE_LEN];
volatile uint8_t qHead = 0, qTail = 0;

// ---------------------------------------------------------------------
// Timing / broadcast state
// ---------------------------------------------------------------------
uint16_t desiredMask = 0;           // what A3 is being asked to do
unsigned long lastOneSecTick = 0, lastMaskSentMs = 0, lastCfgCsvMs = 0, lastTokenMs = 0;
const unsigned long MASK_REASSERT_MS = 2000;
unsigned long a1RedrawAtMillis = 0;
bool a1HardRefreshPending = false;
bool needScreenRedraw = false;      // a coin changed the cleaning total: repaint so the bar follows
bool coinFlag = false;              // a coin landed during cleaning: status line
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

const char* stateName(MachineState s) {
    switch (s) {
        case STATE_TAPS:           return "TAPS";
        case STATE_CHECKING:       return "CHECKING A";
        case STATE_CHECKING_SCN:   return "CHECKING S";
        case STATE_REFILLING:      return "REFILLING A";
        case STATE_REFILLING_SCN:  return "REFILLING S";
        case STATE_WELCOME:        return "WELCOME";
        case STATE_RATE_A:         return "RATE A";
        case STATE_METHOD:         return "METHOD";
        case STATE_TIME_ALLOT:     return "TIME ALLOT";
        case STATE_INSERT_COIN:    return "INSERT COIN";
        case STATE_INSTRUCTIONS:   return "OPEN A";
        case STATE_PLACE_HELMET:   return "SENSORS A";
        case STATE_CLOSE_DOOR_A:   return "CLOSE DOOR A";
        case STATE_CLEANING:       return "CLEANING";
        case STATE_PAUSED_SAFETY:  return "SAFETY PAUSE";
        case STATE_RETRIEVE:       return "OPEN B";
        case STATE_TAKE_HELMET:    return "SENSORS B";
        case STATE_CLOSE_DOOR_B:   return "CLOSE DOOR B";
        case STATE_RATE_B:         return "RATE B";
        case STATE_FREE_RETRY:     return "FREE RETRY";
        case STATE_FINISH:         return "FINISH";
        case STATE_ABORT_CONFIRM:  return "ABORT";
        case STATE_CANCEL_CONFIRM: return "CANCEL";
        case STATE_MAINTENANCE:    return "MAINTENANCE";
        case STATE_OUT_OF_ORDER:   return "OUT OF ORDER";
        case STATE_HELMET_NOTICE_A: return "HELMET NOTICE A";
        case STATE_HELMET_NOTICE_B: return "HELMET NOTICE B";
        default:                   return "?";
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
    prefs.putBool("chk_on", levelChecksOn);
    prefs.putFloat("alc_lo", alcLowCm);
    prefs.putFloat("alc_hi", alcHighCm);
    prefs.putFloat("sct_lo", sctLowCm);
    prefs.putFloat("sct_hi", sctHighCm);
    prefs.putFloat("helm_a2_cm", helmetDetectCmA2);
    prefs.putUShort("wsa_in", wsEnter[0]);
    prefs.putUShort("wsa_out", wsLeave[0]);
    prefs.putUShort("wss_in", wsEnter[1]);
    prefs.putUShort("wss_out", wsLeave[1]);
    prefs.putBool("ws_hi", wsWetHigher);
    prefs.putUChar("try_max", refillTriesMax);
    prefs.putUChar("try_int", refillIntervalSec);
    prefs.putBool("acs_req", acsRequests);
    prefs.putBool("ph_paid", phasePaid);
    prefs.putBool("ph_free", phaseFree);
    prefs.putFloat("ph_sv", phaseStartVal);
    prefs.putFloat("ph_ev", phaseEndVal);
    prefs.putUChar("ph_su", phaseStartUnit);
    prefs.putUChar("ph_eu", phaseEndUnit);
    prefs.putBytes("ph_pat", phasePat, sizeof(phasePat));
}

void loadConfigFromNVS() {
    prefs.begin("s3a_nvs", false);
    uint8_t blob[12];
    if (prefs.getBytes("bands", blob, sizeof(blob)) == sizeof(blob)) {
        for (uint8_t i = 0; i < 4; i++) { bandFrom[i] = blob[i * 3]; bandTo[i] = blob[i * 3 + 1]; bandMinutes[i] = blob[i * 3 + 2]; }
    }
    minMinutes     = prefs.getUChar("min_min", minMinutes);
    rateCoins      = prefs.getUShort("rate_c", rateCoins);
    rateValue      = prefs.getFloat("rate_v", rateValue);
    rateUnitMin    = prefs.getBool("rate_u", rateUnitMin);
    maxCoins       = prefs.getUShort("max_c", maxCoins);
    freeEnabled    = prefs.getBool("free_on", freeEnabled);
    freeMinRating  = prefs.getUChar("free_r", freeMinRating);
    freeMinutes    = prefs.getUChar("free_m", freeMinutes);
    levelChecksOn  = prefs.getBool("chk_on", levelChecksOn);
    alcLowCm       = prefs.getFloat("alc_lo", alcLowCm);
    alcHighCm      = prefs.getFloat("alc_hi", alcHighCm);
    sctLowCm       = prefs.getFloat("sct_lo", sctLowCm);
    sctHighCm      = prefs.getFloat("sct_hi", sctHighCm);
    helmetDetectCmA2 = prefs.getFloat("helm_a2_cm", helmetDetectCmA2);
    wsEnter[0]     = prefs.getUShort("wsa_in", wsEnter[0]);
    wsLeave[0]     = prefs.getUShort("wsa_out", wsLeave[0]);
    wsEnter[1]     = prefs.getUShort("wss_in", wsEnter[1]);
    wsLeave[1]     = prefs.getUShort("wss_out", wsLeave[1]);
    wsWetHigher    = prefs.getBool("ws_hi", wsWetHigher);
    refillTriesMax = prefs.getUChar("try_max", refillTriesMax);
    refillIntervalSec = prefs.getUChar("try_int", refillIntervalSec);
    acsRequests    = prefs.getBool("acs_req", acsRequests);
    phasePaid      = prefs.getBool("ph_paid", phasePaid);
    phaseFree      = prefs.getBool("ph_free", phaseFree);
    phaseStartVal  = prefs.getFloat("ph_sv", phaseStartVal);
    phaseEndVal    = prefs.getFloat("ph_ev", phaseEndVal);
    phaseStartUnit = prefs.getUChar("ph_su", phaseStartUnit);
    phaseEndUnit   = prefs.getUChar("ph_eu", phaseEndUnit);
    uint8_t pat[5];
    if (prefs.getBytes("ph_pat", pat, sizeof(pat)) == sizeof(pat)) { for (uint8_t i = 0; i < 5; i++) phasePat[i] = pat[i] & 7; }
    refillBypass   = prefs.getBool("bypass", false);          // survives a reboot: stay out of order until the levels recover
    lastKnownPulsesA1 = prefs.getUInt("last_a1_pulses", 0);   // survives an S3A reboot mid-session
    recomputeDerived();
}

void saveBypass() { prefs.putBool("bypass", refillBypass); }

// ---------------------------------------------------------------------
// Settings as CSV (LocalServer <-> S3A)
// ---------------------------------------------------------------------
void rulesToCsv(char* out, size_t n) {
    size_t k = 0;
    for (uint8_t i = 0; i < 4 && k < n; i++) k += snprintf(out + k, n - k, "%u,%u,%u,", bandFrom[i], bandTo[i], bandMinutes[i]);
    if (k < n) snprintf(out + k, n - k, "%u,%u,%.2f,%u,%u,%u,%u,%u", minMinutes, rateCoins, (double)rateValue, rateUnitMin ? 1 : 0,
                        maxCoins, freeEnabled ? 1 : 0, freeMinRating, freeMinutes);
}

void levelsToCsv(char* out, size_t n) {
    snprintf(out, n, "%u,%.1f,%.1f,%.1f,%.1f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u",
             levelChecksOn ? 1 : 0, (double)alcLowCm, (double)alcHighCm, (double)sctLowCm, (double)sctHighCm, (double)helmetDetectCmA2,
             wsEnter[0], wsLeave[0], wsEnter[1], wsLeave[1], wsWetHigher ? 1 : 0, refillTriesMax, refillIntervalSec, acsRequests ? 1 : 0);
}

void phasesToCsv(char* out, size_t n) {
    snprintf(out, n, "%u,%u,%.2f,%u,%.2f,%u,%u,%u,%u,%u,%u",
             phasePaid ? 1 : 0, phaseFree ? 1 : 0, (double)phaseStartVal, phaseStartUnit, (double)phaseEndVal, phaseEndUnit,
             phasePat[0], phasePat[1], phasePat[2], phasePat[3], phasePat[4]);
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

bool applyLevelsCsv(const char* s) {
    float v[14];
    if (parseFloats(s, v, 14) != 14) return false;
    if (v[1] < 0 || v[1] > 400 || v[2] < 0 || v[2] > 400 || v[3] < 0 || v[3] > 400 || v[4] < 0 || v[4] > 400 || v[5] < 1 || v[5] > 200) return false;
    for (uint8_t i = 6; i < 10; i++) if (v[i] < 0 || v[i] > 4095) return false;
    if (v[11] < 1 || v[11] > 99 || v[12] < 1 || v[12] > 250) return false;
    levelChecksOn = v[0] >= 0.5f;
    alcLowCm = v[1]; alcHighCm = v[2]; sctLowCm = v[3]; sctHighCm = v[4]; helmetDetectCmA2 = v[5];
    wsEnter[0] = (uint16_t)v[6]; wsLeave[0] = (uint16_t)v[7]; wsEnter[1] = (uint16_t)v[8]; wsLeave[1] = (uint16_t)v[9];
    wsWetHigher = v[10] >= 0.5f; refillTriesMax = (uint8_t)v[11]; refillIntervalSec = (uint8_t)v[12]; acsRequests = v[13] >= 0.5f;
    return true;
}

bool applyPhasesCsv(const char* s) {
    float v[11];
    if (parseFloats(s, v, 11) != 11) return false;
    if (v[3] < 0 || v[3] > 2 || v[5] < 0 || v[5] > 2) return false;                      // unit: 0 %, 1 seconds, 2 minutes
    if (v[2] < 0 || v[4] < 0 || (v[3] < 0.5f && v[2] > 100) || (v[5] < 0.5f && v[4] > 100) || v[2] > 7200 || v[4] > 7200) return false;
    for (uint8_t i = 6; i < 11; i++) if (v[i] < 0 || v[i] > 7) return false;
    phasePaid = v[0] >= 0.5f; phaseFree = v[1] >= 0.5f;
    phaseStartVal = v[2]; phaseStartUnit = (uint8_t)(v[3] + 0.5f);
    phaseEndVal = v[4];   phaseEndUnit = (uint8_t)(v[5] + 0.5f);
    for (uint8_t i = 0; i < 5; i++) phasePat[i] = (uint8_t)(v[6 + i] + 0.5f) & 7;
    return true;
}

// The old fixed-field config packet, still broadcast every second so an older dashboard keeps showing something.
void broadcastConfig() {
    Box1ConfigPacket packet;
    memset(&packet, 0, sizeof(Box1ConfigPacket));
    packet.deviceID         = DEVICE_S3A;
    packet.helmetDetectCmA2 = helmetDetectCmA2;
    packet.secondsPerCoin   = secondsPerCoin;
    packet.minCoinsRequired = minCoinsRequired;
    packet.maxCoinsAllowed  = maxCoins;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(Box1ConfigPacket));
}

void broadcastSettingsCsv() {
    char buf[200];
    rulesToCsv(buf, sizeof(buf));
    sendCmd(0, CMD_SET_CONFIG, 1, 0, 0, buf);
    delay(2);
    levelsToCsv(buf, sizeof(buf));
    sendCmd(0, CMD_SET_CONFIG, 2, 0, 0, buf);
    delay(2);
    phasesToCsv(buf, sizeof(buf));
    sendCmd(0, CMD_SET_CONFIG, 3, 0, 0, buf);
}

// ---------------------------------------------------------------------
// Sensors and levels (all read from the snapshot taken once per second)
// ---------------------------------------------------------------------
bool a2Alive() { return lastA2Ms != 0 && (millis() - lastA2Ms) < 5000UL; }
bool a3Alive() { return lastA3Ms != 0 && (millis() - lastA3Ms) < 5000UL; }
bool systemReady() { return a2Alive() && a3Alive(); }
bool doorOpenNow()      { return a2snap.doorEnclosure; }
bool helmetPresentNow() { return a2snap.usHelmetCm > 0.0f && a2snap.usHelmetCm < helmetDetectCmA2; }

bool holdTicks(uint8_t &counter, bool cond, uint8_t need) {
    if (cond) { if (counter < 255) counter++; } else counter = 0;
    return counter >= need;
}

// Water sensors are judged here from the raw readings A2 reports, with the calibration set on the dashboard.
void updateWaterVerdicts() {
    uint16_t raw[2] = {a2snap.wsAlsRaw, a2snap.wsSlsRaw};
    for (uint8_t k = 0; k < 2; k++) {
        if (!a2Alive()) { wsWet[k] = false; wsCnt[k] = 0; continue; }
        bool beyondEnter = wsWetHigher ? (raw[k] >= wsEnter[k]) : (raw[k] <= wsEnter[k]);
        bool beyondLeave = wsWetHigher ? (raw[k] <= wsLeave[k]) : (raw[k] >= wsLeave[k]);
        bool flip = wsWet[k] ? beyondLeave : beyondEnter;
        if (flip) { if (++wsCnt[k] >= 2) { wsWet[k] = !wsWet[k]; wsCnt[k] = 0; } } else wsCnt[k] = 0;
    }
}

bool containerLow(float cm, float lowCm) { return cm <= 0.0f || cm >= lowCm; }
bool alcoholEnough()  { return !levelChecksOn || !containerLow(a2snap.usMcAlsCm, alcLowCm) || wsWet[0]; }
bool scentedEnough()  { return !levelChecksOn || !containerLow(a2snap.usMcSlsCm, sctLowCm) || wsWet[1]; }
bool alcoholRefilled() { return !levelChecksOn || (a2snap.usMcAlsCm > 0.0f && a2snap.usMcAlsCm <= alcHighCm); }
bool scentedRefilled() { return !levelChecksOn || (a2snap.usMcSlsCm > 0.0f && a2snap.usMcSlsCm <= sctHighCm); }

// ---------------------------------------------------------------------
// Messages to the other nodes
// ---------------------------------------------------------------------
void sendBuzzer(uint16_t ms) { sendCmd(DEVICE_A1, CMD_BUZZER, 0, ms, 0); }
void sendToken(uint8_t id, uint16_t value) { sendCmd(DEVICE_A1, CMD_SET_TOKEN, id, value, 0); delay(2); }

void sendStatusLines() {
    for (uint8_t i = 0; i < 3; i++) { sendCmd(DEVICE_A1, CMD_STATUS_LINE, i, 0, 0, statusLines[i]); delay(2); }
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
    sendCmd(DEVICE_A1, CMD_RESET_COINS, 0, 0, 0);
    lastKnownPulsesA1 = 0;
    prefs.putUInt("last_a1_pulses", 0);
}

// The refill system (ACS) is asked to top a container up. OFF by default (acsRequests): Box 1's ACS is still
// being fixed and its old firmware answers any delivery request by pumping. The tries are counted either way.
void requestRefill(uint8_t container) {
    if (!acsRequests) { Serial.printf("[S3A] Refill try %u for container %u (ACS requests are OFF - waiting only)\n", refillTry, container); return; }
    Serial.printf("[S3A] Refill try %u: asking the ACS to top up container %u\n", refillTry, container);
    sendCmd(DEVICE_ACS, CMD_REQUEST_DELIVERY, container, 0, 0);
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------
uint8_t anyTapFlagForCurrentState() {
    return (currentMachineState == STATE_TAPS || currentMachineState == STATE_WELCOME) ? 1 : 0;
}

const char* cleaningTemplate() {
    if (mistMode == MIST_ALC) return cleanFree ? SCR_CLEAN_ALC_FREE : SCR_CLEAN_ALC;
    if (mistMode == MIST_SCN) return cleanFree ? SCR_CLEAN_SCN_FREE : SCR_CLEAN_SCN;
    return cleanFree ? SCR_CLEAN_BAL_FREE : SCR_CLEAN_BAL;
}

const char* screenForCurrentState() {
    switch (currentMachineState) {
        case STATE_TAPS:           return SCR_TAPS;
        case STATE_CHECKING:       return SCR_CHECKING_A;
        case STATE_CHECKING_SCN:   return SCR_CHECKING_S;
        case STATE_REFILLING:      return SCR_REFILLING_A;
        case STATE_REFILLING_SCN:  return SCR_REFILLING_S;
        case STATE_WELCOME:        return SCR_WELCOME;
        case STATE_RATE_A:         return SCR_RATE_A;
        case STATE_METHOD:         return SCR_METHOD;
        case STATE_TIME_ALLOT:
            if (timeWarnL1[0]) snprintf(scrBuf, sizeof(scrBuf), SCR_TIME_WARN, timeWarnL1, timeWarnL2);
            else snprintf(scrBuf, sizeof(scrBuf), SCR_TIME_ALLOT, (unsigned)secondsPerCoin, (unsigned)npInitSeconds);
            return scrBuf;
        case STATE_INSERT_COIN:    return SCR_INSERT_COIN;
        case STATE_INSTRUCTIONS:   return SCR_OPEN_A;
        case STATE_PLACE_HELMET:   return SCR_SENSORS_A;
        case STATE_CLOSE_DOOR_A:   return SCR_CLOSE_DOOR_A;
        case STATE_CLEANING:
            snprintf(scrBuf, sizeof(scrBuf), cleaningTemplate(), (unsigned long)cleanTotalSec);
            return scrBuf;
        case STATE_PAUSED_SAFETY:  return SCR_SAFETY_PAUSE;
        case STATE_RETRIEVE:       return SCR_OPEN_B;
        case STATE_TAKE_HELMET:    return SCR_SENSORS_B;
        case STATE_CLOSE_DOOR_B:   return SCR_CLOSE_DOOR_B;
        case STATE_RATE_B:         return SCR_RATE_B;
        case STATE_FREE_RETRY:
            snprintf(scrBuf, sizeof(scrBuf), SCR_FREE_RETRY, (unsigned)freeMinutes, freeMinutes == 1 ? "MINUTE" : "MINUTES");
            return scrBuf;
        case STATE_FINISH:         return SCR_FINISH;
        case STATE_ABORT_CONFIRM:  return SCR_ABORT;
        case STATE_CANCEL_CONFIRM: return SCR_CANCEL;
        case STATE_MAINTENANCE:    return SCR_MAINTENANCE;
        case STATE_OUT_OF_ORDER:   return SCR_OUT_OF_ORDER;
        case STATE_HELMET_NOTICE_A: return SCR_HELMET_NOTICE_A;
        case STATE_HELMET_NOTICE_B: return SCR_HELMET_NOTICE_B;
        default:                   return SCR_TAPS;
    }
}

void showScreen() {
    sendCmd(DEVICE_A1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
}

// ---------------------------------------------------------------------
// Relays: the whole 12-bit mask for A3, worked out from the state
// ---------------------------------------------------------------------
// Length of a START / END part in seconds for a cleaning that lasts `total` seconds in all.
uint32_t phaseWindowSec(float val, uint8_t unit, uint32_t total) {
    if (val < 0.0f) val = 0.0f;
    if (unit == 1) return (uint32_t)(val + 0.5f);
    if (unit == 2) return (uint32_t)(val * 60.0f + 0.5f);
    if (val > 100.0f) val = 100.0f;
    return (uint32_t)((float)total * val / 100.0f + 0.5f);
}

// Which part of the cleaning we are in right now: PH_START, PH_MID or PH_END; 0 = phases are off for this cleaning.
// Elapsed time only counts while the cleaning really runs (a safety pause or an abort prompt freezes it), and the
// total is elapsed + what is left, so coins added mid-cleaning move the END part later and keep it "the last minute".
// If the two parts would overlap, the start part wins and the end part is shortened.
uint8_t cleaningPhasePart() {
    if (!(cleanFree ? phaseFree : phasePaid)) return 0;
    uint32_t elapsed = cleanElapsedSec, remaining = activeTimer, total = elapsed + remaining;
    uint32_t st = phaseWindowSec(phaseStartVal, phaseStartUnit, total);
    uint32_t en = phaseWindowSec(phaseEndVal, phaseEndUnit, total);
    if (st > total) st = total;
    if (en > total - st) en = total - st;
    if (elapsed < st) return PH_START;
    if (en > 0 && remaining <= en) return PH_END;
    return PH_MID;
}

uint16_t desiredMaskForState() {
    uint16_t m = 0;
    bool unlockWanted = false;
    switch (currentMachineState) {
        case STATE_INSERT_COIN:
            m = A3_BIT(A3_RELAY_COIN_POWER);
            break;
        case STATE_INSTRUCTIONS:
        case STATE_RETRIEVE:
            unlockWanted = true;
            break;
        case STATE_PAUSED_SAFETY:
            unlockWanted = unlockForPause;
            break;
        case STATE_CLEANING:
            m = A3_MASK_UV;                                          // UV 1 + UV 2 together, the whole time
            m |= A3_BIT(A3_RELAY_HUMIDIFIER_3);                      // Humidifier 3 (both containers) runs in every mode
            if (mistMode == MIST_ALC || mistMode == MIST_BAL || mistMode == MIST_NONE) m |= A3_BIT(A3_RELAY_HUMIDIFIER_1);
            if (mistMode == MIST_SCN || mistMode == MIST_BAL || mistMode == MIST_NONE) m |= A3_BIT(A3_RELAY_HUMIDIFIER_2);
            {   // cleaning phases: of the relays this mode uses, keep only those ON in the part we are in
                uint8_t part = cleaningPhasePart();
                if (part) {
                    static const uint8_t PH_RELAY[5] = {A3_RELAY_HUMIDIFIER_1, A3_RELAY_HUMIDIFIER_2, A3_RELAY_HUMIDIFIER_3, A3_RELAY_UV_1, A3_RELAY_UV_2};
                    for (uint8_t i = 0; i < 5; i++) if (!(phasePat[i] & part)) m &= (uint16_t)(~A3_BIT(PH_RELAY[i]));
                }
            }
            if (!cleanFree && sessionCoins < maxCoins) m |= A3_BIT(A3_RELAY_COIN_POWER);   // paid time can still be topped up
            break;
        case STATE_MAINTENANCE:
            m = maintMask & A3_MASK_ALL;
            if (lockResting) m &= (uint16_t)(~A3_BIT(A3_RELAY_ENCLOSURE_LOCK));   // the duty cap covers hand-set relays too
            break;
        default:
            break;
    }
    if (unlockWanted && !lockResting) m |= A3_BIT(A3_RELAY_ENCLOSURE_LOCK);
    return m;
}

void sendMask() {
    sendCmd(DEVICE_A3, CMD_SET_RELAY_MASK, 0, desiredMask, 0);
    lastMaskSentMs = millis();
}

void muteA1AndScheduleRedraw(bool hardRefresh) {
    sendCmd(DEVICE_A1, CMD_MUTE_COINS, 0, 250, 0);
    a1RedrawAtMillis = millis() + 300;
    if (hardRefresh) a1HardRefreshPending = true;
}

void applyDesiredMask() {
    uint16_t m = desiredMaskForState();
    if (m == desiredMask) return;
    // The TFT white-screen glitch comes from the lock solenoid re-engaging (unlocked -> locked), so only that
    // transition asks A1 for a full controller re-init; every other relay change just gets a plain repaint.
    bool lockJustEngaged = (desiredMask & A3_BIT(A3_RELAY_ENCLOSURE_LOCK)) && !(m & A3_BIT(A3_RELAY_ENCLOSURE_LOCK));
    desiredMask = m;
    muteA1AndScheduleRedraw(lockJustEngaged);
    sendMask();
    Serial.printf("[S3A] relay mask -> 0x%03X\n", (unsigned)desiredMask);
}

// ---------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------
void enterState(MachineState s);

void goOutOfOrder(const char* why) {
    Serial.printf("[S3A] OUT OF ORDER: %s\n", why);
    enterState(STATE_OUT_OF_ORDER);
}

void recordCompletedSession() {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesA1 >= sessionCoinsAtStart) ? (lastKnownPulsesA1 - sessionCoinsAtStart) : 0;
    lastSessionDurationSec = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    lastSessionCoins       = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;
    completedSessionSeq++;
    Serial.printf("[S3A STATS] Session #%u: %us, %u coins, mist %u, rating before %u.\n",
                  (unsigned)completedSessionSeq, (unsigned)lastSessionDurationSec, (unsigned)lastSessionCoins, mistMode, ratingA);
}

void enterState(MachineState s) {
    currentMachineState = s;
    holdA = holdB = holdC = 0;
    timeWarnL1[0] = '\0';
    switch (s) {
        case STATE_TAPS:
            activeTimer = 0;
            mistMode = MIST_NONE;
            retryUsed = false;
            cleanFree = false;
            ratingA = ratingB = 0;
            sessionCoins = 0;
            cleanTotalSec = 0;
            unlockForPause = false;
            clearStatus();
            resetCoinSession();
            break;
        case STATE_REFILLING:
        case STATE_REFILLING_SCN:
            refillTry = 1;
            refillElapsed = 0;
            requestRefill(s == STATE_REFILLING ? 0 : 1);
            sendToken(TOKEN_TRY, refillTry);
            sendToken(TOKEN_TRIES, refillTriesMax);
            break;
        case STATE_TIME_ALLOT:
            npInitSeconds = suggestedSecondsForRating(ratingA);
            break;
        case STATE_INSTRUCTIONS:
        case STATE_RETRIEVE:
            unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            sendBuzzer(s == STATE_RETRIEVE ? 500 : 250);
            break;
        case STATE_PAUSED_SAFETY:
            unlockForPause = false; unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            break;
        case STATE_FINISH:
            stepRemainingSec = 5;
            break;
        case STATE_HELMET_NOTICE_A:
        case STATE_HELMET_NOTICE_B:
            stepRemainingSec = 2;
            sendBuzzer(120);
            break;
        default:
            break;
    }
    applyDesiredMask();
    showScreen();
    Serial.printf("[S3A] -> %s\n", stateName(s));
}

// A customer has tapped the TAPS screen.
void beginSession() {
    mistMode = MIST_NONE; retryUsed = false; cleanFree = false; ratingA = ratingB = 0; sessionCoins = 0;
    enterState(STATE_CHECKING);
}

const char* mistStatusLine() {
    return mistMode == MIST_ALC ? "HUMID ALC: Misting" : (mistMode == MIST_SCN ? "HUMID SCN: Misting" : "HUMID BOTH: Misting");
}

// The cleaning "kill feed": announces what the machine just did. At the start only what is ON is announced; after that
// every change of the UV or the mist (the phases switch them) gets a line, newest first on the screen.
void syncCleanStatus(bool first) {
    bool uvOn = (desiredMask & A3_MASK_UV) != 0;
    bool mistOn = (desiredMask & A3_MASK_MIST) != 0;
    if (first) {
        if (uvOn) pushStatus("UV: ON");
        if (mistOn) pushStatus(mistStatusLine());
    } else {
        if (uvOn != lastUvOn) pushStatus(uvOn ? "UV: ON" : "UV: OFF");
        if (mistOn != lastMistOn) pushStatus(mistOn ? mistStatusLine() : "HUMID: OFF");
    }
    lastUvOn = uvOn;
    lastMistOn = mistOn;
}

// START CLEANING: a routing decision, not a state. Mist mode and the free retry pick the variant.
void beginCleaning() {
    cleanFree = retryUsed;
    if (cleanFree) {
        activeTimer = (uint32_t)freeMinutes * 60UL;
    } else {
        sessionStartMillis  = millis();
        sessionCoinsAtStart = lastKnownPulsesA1;
    }
    cleanTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    cleanElapsedSec = 0;
    clearStatus();
    Serial.printf("[S3A] Cleaning starts: %s, mist %u, %lus, phases %s\n", cleanFree ? "FREE retry" : "paid", mistMode, (unsigned long)activeTimer,
                  (cleanFree ? phaseFree : phasePaid) ? "ON" : "off");
    enterState(STATE_CLEANING);
    syncCleanStatus(true);                    // after enterState: the first part of the cleaning decides what is ON
    sendBuzzer(250);
}

void finishCleaning() {
    if (!cleanFree) recordCompletedSession();
    Serial.println("[S3A] Cleaning complete -> open the door");
    enterState(STATE_RETRIEVE);
}

// The numpad's OK: check the time against the rules, then on to paying - or say why not.
void handleNumpadOk(uint16_t sec) {
    uint32_t coins = coinsForSec(sec);
    uint32_t minSec = (uint32_t)minMinutes * 60UL;
    if (sec < minSec || coins < minCoinsRequired) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Minimum is %u min", (unsigned)minMinutes);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "(%u coins)", (unsigned)minCoinsRequired);
        npInitSeconds = sec;
        Serial.printf("[S3A] Time %us rejected: under the %u min minimum\n", sec, minMinutes);
        showScreen();
        return;
    }
    if (coins > maxCoins) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Maximum is %u coins", (unsigned)maxCoins);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "Please enter less time");
        npInitSeconds = sec;
        Serial.printf("[S3A] Time %us rejected: %u coins is over the %u maximum\n", sec, (unsigned)coins, maxCoins);
        showScreen();
        return;
    }
    requiredCoinsForSession = coins;
    activeTimer = 0;
    Serial.printf("[S3A] Time %us = %u coins accepted\n", sec, (unsigned)coins);
    enterState(STATE_INSERT_COIN);
}

// A button or numpad result from A1 (queued by the radio callback, handled here in loop()).
void handleTouch(const char* a, uint16_t v) {
    MachineState st = currentMachineState;
    Serial.printf("[S3A TOUCH] '%s' value=%u in %s\n", a, v, stateName(st));

    if (!strcmp(a, "TAP_ADVANCE")) {
        if (st == STATE_TAPS) beginSession();
        else if (st == STATE_WELCOME) enterState(STATE_RATE_A);
    } else if (!strcmp(a, "ACTION")) {                  // CONFIRM on the rating screens: v = the rating picked
        if (st == STATE_RATE_A || st == STATE_RATE_B) {
            if (v < 1 || v > 10) { sendBuzzer(60); return; }   // nothing picked yet - ask again
            if (st == STATE_RATE_A) {
                ratingA = (uint8_t)v;
                Serial.printf("[S3A] Rating before: %u/10\n", ratingA);
                enterState(STATE_METHOD);
            } else {
                ratingB = (uint8_t)v;
                Serial.printf("[S3A] Rating after: %u/10\n", ratingB);
                if (!retryUsed && ratingGetsFreeRetry(ratingB)) { retryUsed = true; enterState(STATE_FREE_RETRY); }
                else enterState(STATE_FINISH);
            }
        }
    } else if (!strcmp(a, "MIST_BAL") || !strcmp(a, "MIST_ALC") || !strcmp(a, "MIST_SCN")) {
        if (st == STATE_METHOD) {
            mistMode = !strcmp(a, "MIST_ALC") ? MIST_ALC : (!strcmp(a, "MIST_SCN") ? MIST_SCN : MIST_BAL);
            enterState(STATE_TIME_ALLOT);
        }
    } else if (!strcmp(a, "NP_OK")) {
        if (st == STATE_TIME_ALLOT && !timeWarnL1[0]) handleNumpadOk(v);
    } else if (!strcmp(a, "NP_BACK")) {
        if (st == STATE_TIME_ALLOT) enterState(STATE_METHOD);
    } else if (!strcmp(a, "NP_CANCEL")) {
        if (st == STATE_TIME_ALLOT) enterState(STATE_TAPS);
    } else if (!strcmp(a, "WARN_OK")) {
        if (st == STATE_TIME_ALLOT && timeWarnL1[0]) { timeWarnL1[0] = '\0'; showScreen(); }   // numpad again, with the typed time
    } else if (!strcmp(a, "INSERT_CANCEL")) {
        if (st == STATE_INSERT_COIN) { returnState = STATE_INSERT_COIN; enterState(STATE_CANCEL_CONFIRM); }
    } else if (!strcmp(a, "CANCEL_YES")) {
        if (st == STATE_CANCEL_CONFIRM) { Serial.println("[S3A] Cancelled by the customer"); enterState(STATE_TAPS); }
    } else if (!strcmp(a, "CANCEL_NO")) {
        if (st == STATE_CANCEL_CONFIRM) enterState(returnState);
    } else if (!strcmp(a, "ABORT")) {
        if (st == STATE_CLEANING) { returnState = STATE_CLEANING; enterState(STATE_ABORT_CONFIRM); }
    } else if (!strcmp(a, "ABORT_YES")) {
        if (st == STATE_ABORT_CONFIRM) {
            Serial.println("[S3A] Cleaning aborted by the customer");
            activeTimer = 0;
            enterState(STATE_RETRIEVE);
        }
    } else if (!strcmp(a, "ABORT_NO")) {
        if (st == STATE_ABORT_CONFIRM) enterState(returnState);     // back to the cleaning, time and bar untouched
    } else if (!strcmp(a, "FREE_YES")) {
        if (st == STATE_FREE_RETRY) { retryUsed = true; enterState(STATE_INSTRUCTIONS); }
    } else if (!strcmp(a, "FREE_NO")) {
        if (st == STATE_FREE_RETRY) enterState(STATE_FINISH);
    } else if (!strcmp(a, "RESET")) {
        enterState(STATE_TAPS);
    }
}

void handleMaintenance(uint8_t on) {
    if (on) {
        if (currentMachineState == STATE_TAPS || currentMachineState == STATE_OUT_OF_ORDER) { maintMask = 0; enterState(STATE_MAINTENANCE); }
        else Serial.println("[S3A] Maintenance refused: only possible from the idle screen");
    } else if (currentMachineState == STATE_MAINTENANCE) {
        maintMask = 0;
        enterState(STATE_TAPS);
    }
}

// ---------------------------------------------------------------------
// The once-a-second state logic
// ---------------------------------------------------------------------
void tickState() {
    bool open = doorOpenNow();
    bool closed = !open;
    bool hp = helmetPresentNow();

    switch (currentMachineState) {
        case STATE_CHECKING:
            if (!systemReady()) { goOutOfOrder(a2Alive() ? "relay board A3 is not answering" : "sensor board A2 is not answering"); break; }
            if (alcoholEnough()) enterState(STATE_CHECKING_SCN);
            else if (refillBypass) goOutOfOrder("alcohol low and the refill already gave up (bypass)");
            else enterState(STATE_REFILLING);
            break;

        case STATE_CHECKING_SCN:
            if (!systemReady()) { goOutOfOrder("a node is not answering"); break; }
            if (scentedEnough()) enterState(STATE_WELCOME);
            else if (refillBypass) goOutOfOrder("scented low and the refill already gave up (bypass)");
            else enterState(STATE_REFILLING_SCN);
            break;

        case STATE_REFILLING:
        case STATE_REFILLING_SCN: {
            bool alc = (currentMachineState == STATE_REFILLING);
            bool ok = alc ? (alcoholRefilled() || wsWet[0]) : (scentedRefilled() || wsWet[1]);
            if (ok) { Serial.println("[S3A] Container refilled"); enterState(alc ? STATE_CHECKING : STATE_CHECKING_SCN); break; }
            if (++refillElapsed >= refillIntervalSec) {
                if (refillTry >= refillTriesMax) {
                    refillBypass = true; saveBypass();
                    goOutOfOrder("refill gave up after the last try - bypass ON");
                } else {
                    refillTry++; refillElapsed = 0;
                    requestRefill(alc ? 0 : 1);
                    sendToken(TOKEN_TRY, refillTry);
                }
            }
            break;
        }

        case STATE_INSERT_COIN:
            if (activeTimer >= requiredCoinsForSession * secondsPerCoin) {
                activeTimer = requiredCoinsForSession * secondsPerCoin;   // anything beyond the price is discarded
                enterState(STATE_INSTRUCTIONS);
            }
            break;

        case STATE_INSTRUCTIONS:                       // OPEN A
            if (holdTicks(holdA, open, 2)) enterState(STATE_PLACE_HELMET);
            break;

        case STATE_PLACE_HELMET: {                     // SENSORS A
            // Helmet seen inside -> on to CLOSE DOOR, whether the door is still open or a quick customer has already
            // shut it (the export waited in that case and nothing could ever move it on, with the lock engaged).
            bool placed = holdTicks(holdA, hp, 2);
            bool shutEmpty = holdTicks(holdB, closed && !hp, 2);
            if (placed) enterState(STATE_CLOSE_DOOR_A);
            else if (shutEmpty) enterState(STATE_HELMET_NOTICE_A);
            break;
        }

        case STATE_CLOSE_DOOR_A: {
            bool go = holdTicks(holdA, closed && hp, 2);
            bool empty = holdTicks(holdB, closed && !hp, 2);
            if (go) beginCleaning();
            else if (empty) enterState(STATE_HELMET_NOTICE_A);
            break;
        }

        case STATE_HELMET_NOTICE_A:                    // "HELMET NO DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(STATE_INSTRUCTIONS);
            break;

        case STATE_CLEANING:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: UV and mist off at once
                Serial.println("[S3A] SAFETY: the door opened during cleaning");
                enterState(STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                cleanElapsedSec++;
                if (activeTimer == 0) finishCleaning();
            }
            break;

        case STATE_PAUSED_SAFETY: {
            bool resume = holdTicks(holdA, closed && hp, 2);
            bool needIn = holdTicks(holdB, closed && !hp, 2);
            if (resume) { Serial.println("[S3A] Safety restored - cleaning continues"); unlockForPause = false; enterState(STATE_CLEANING); break; }
            if (needIn && !unlockForPause) { Serial.println("[S3A] Door shut but no helmet - unlocking so it can be reopened"); unlockForPause = true; unlockHeldSec = 0; }
            if (unlockForPause && holdTicks(holdC, open, 2)) unlockForPause = false;     // release once it is genuinely open
            break;
        }

        case STATE_RETRIEVE:                           // OPEN B
            if (holdTicks(holdA, open, 2)) enterState(STATE_TAKE_HELMET);
            break;

        case STATE_TAKE_HELMET: {                      // SENSORS B
            // Helmet gone -> on to CLOSE DOOR (which asks for the door if it is still open, and carries straight on if
            // it is already shut). The first export only handled "helmet out + door OPEN", so a helmet taken out and
            // the door shut quickly never moved on; the second one fixed that but lost the "please close" prompt.
            bool out = holdTicks(holdA, !hp, 2);
            bool still = holdTicks(holdB, hp && closed, 2);
            if (out) enterState(STATE_CLOSE_DOOR_B);
            else if (still) enterState(STATE_HELMET_NOTICE_B);
            break;
        }

        case STATE_CLOSE_DOOR_B: {
            bool done = holdTicks(holdA, closed && !hp, 2);
            bool back = holdTicks(holdB, closed && hp, 2);
            if (done) enterState(retryUsed ? STATE_FINISH : STATE_RATE_B);
            else if (back) enterState(STATE_HELMET_NOTICE_B);
            break;
        }

        case STATE_HELMET_NOTICE_B:                    // "HELMET DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(STATE_RETRIEVE);
            break;

        case STATE_FINISH:
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(STATE_TAPS);
            break;

        case STATE_OUT_OF_ORDER:
            if (systemReady() && alcoholEnough() && scentedEnough()) {
                Serial.println("[S3A] Back in order");
                refillBypass = false; saveBypass();
                enterState(STATE_TAPS);
            }
            break;

        default:                                       // touch-driven or idle states
            break;
    }
}

// ---------------------------------------------------------------------
// Coins
// ---------------------------------------------------------------------
bool coinsAcceptedNow() {
    return currentMachineState == STATE_INSERT_COIN ||
           (currentMachineState == STATE_CLEANING && !cleanFree && sessionCoins < maxCoins);
}

void processPulses() {
    uint32_t pc; bool seen;
    portENTER_CRITICAL(&rxMux);
    pc = a1Pulses; seen = a1Seen;
    portEXIT_CRITICAL(&rxMux);
    if (!seen) return;
    if (pc > lastKnownPulsesA1) {
        uint32_t newPulses = pc - lastKnownPulsesA1;
        lastKnownPulsesA1 = pc;
        prefs.putUInt("last_a1_pulses", lastKnownPulsesA1);
        if (coinsAcceptedNow()) {
            uint32_t added = newPulses * secondsPerCoin;
            activeTimer += added;
            sessionCoins += newPulses;
            if (currentMachineState == STATE_CLEANING) {
                cleanTotalSec = (cleanTotalSec + added > 9999UL) ? 9999UL : cleanTotalSec + added;
                needScreenRedraw = true;               // the bar's total grew
                coinFlag = true;
            }
            Serial.printf("[S3A COIN] +%lus. activeTimer = %lus, session coins %lu\n", (unsigned long)added, (unsigned long)activeTimer, (unsigned long)sessionCoins);
        } else {
            Serial.printf("[S3A COIN] %lu pulse(s) ignored in %s (the acceptor should be off)\n", (unsigned long)newPulses, stateName(currentMachineState));
        }
    } else if (pc < lastKnownPulsesA1) {
        // A1's counter went back down (it rebooted or was reset) - resync rather than double-credit
        lastKnownPulsesA1 = pc;
        prefs.putUInt("last_a1_pulses", lastKnownPulsesA1);
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
        if (cmd.targetDeviceID != DEVICE_S3A) return;
        if (cmd.commandID != CMD_TOUCH_ACTION && cmd.commandID != CMD_SET_CONFIG &&
            cmd.commandID != CMD_SET_MAINTENANCE && cmd.commandID != CMD_SET_RELAY_MASK) return;
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
        if (p.deviceID == DEVICE_A1) {
            portENTER_CRITICAL(&rxMux);
            a1Pulses = p.pulseCount; a1Seen = true;
            portEXIT_CRITICAL(&rxMux);
        }
        return;
    }
    if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, incomingData, sizeof(A2SensorPacket));
        if (p.deviceID != DEVICE_A2) return;
        portENTER_CRITICAL(&rxMux);
        nodeA2 = p; lastA2Ms = millis();
        portEXIT_CRITICAL(&rxMux);
        return;
    }
    if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, incomingData, sizeof(A3StatusPacket));
        if (p.deviceID != DEVICE_A3) return;
        portENTER_CRITICAL(&rxMux);
        a3Mask = p.relayMask; a3Orphaned = p.orphaned; lastA3Ms = millis();
        portEXIT_CRITICAL(&rxMux);
    }
}

void handleQueuedCommand(const CommandPacket& cmd) {
    if (cmd.commandID == CMD_TOUCH_ACTION) {
        handleTouch(cmd.payloadStr, cmd.param16);
    } else if (cmd.commandID == CMD_SET_MAINTENANCE) {
        handleMaintenance(cmd.state ? 1 : 0);
    } else if (cmd.commandID == CMD_SET_RELAY_MASK) {
        if (currentMachineState == STATE_MAINTENANCE) { maintMask = cmd.param16 & A3_MASK_ALL; applyDesiredMask(); }
    } else if (cmd.commandID == CMD_SET_CONFIG) {
        bool ok = false;
        if (cmd.subIndex == 1) ok = applyRulesCsv(cmd.payloadStr);
        else if (cmd.subIndex == 2) ok = applyLevelsCsv(cmd.payloadStr);
        else if (cmd.subIndex == 3) ok = applyPhasesCsv(cmd.payloadStr);
        else {   // the old "helmCm,lowPct,highPct,secPerCoin,minCoins[,maxCoins]" line: only the helmet distance and max coins still mean anything
            float v[6];
            int n = parseFloats(cmd.payloadStr, v, 6);
            if (n == 5 || n == 6) { if (v[0] >= 1 && v[0] <= 200) helmetDetectCmA2 = v[0]; if (n == 6 && v[5] >= 1) maxCoins = (uint16_t)v[5]; ok = true; }
        }
        if (ok) { saveConfigToNVS(); broadcastConfig(); Serial.printf("[S3A CONFIG] Updated & saved (type %u)\n", cmd.subIndex); }
        else Serial.printf("[S3A CONFIG] REJECTED (type %u): '%s'\n", cmd.subIndex, cmd.payloadStr);
    }
}

// ---------------------------------------------------------------------
// USB serial console - tune and inspect without a dashboard
// ---------------------------------------------------------------------
void printConfig() {
    char r[200], l[200];
    rulesToCsv(r, sizeof(r)); levelsToCsv(l, sizeof(l));
    Serial.println("---- S3A settings ----");
    Serial.printf("state: %s | A2 %s | A3 %s | bypass %s | level checks %s | ACS requests %s\n", stateName(currentMachineState),
                  a2Alive() ? "ok" : "SILENT", a3Alive() ? "ok" : "SILENT", refillBypass ? "ON" : "off", levelChecksOn ? "ON" : "off", acsRequests ? "ON" : "off");
    Serial.printf("rules : bands %u-%u=%um, %u-%u=%um, %u-%u=%um, %u-%u=%um | min %u min | %u coin(s) = %.2f %s (%.2f s/coin) | max %u coins | free retry %s from %u for %u min\n",
                  bandFrom[0], bandTo[0], bandMinutes[0], bandFrom[1], bandTo[1], bandMinutes[1], bandFrom[2], bandTo[2], bandMinutes[2], bandFrom[3], bandTo[3], bandMinutes[3],
                  minMinutes, rateCoins, (double)rateValue, rateUnitMin ? "min" : "s", (double)secPerCoinF, maxCoins, freeEnabled ? "ON" : "off", freeMinRating, freeMinutes);
    Serial.printf("levels: alcohol low %.1f / high %.1f cm | scented low %.1f / high %.1f cm | helmet < %.1f cm | tries %u x %u s\n",
                  (double)alcLowCm, (double)alcHighCm, (double)sctLowCm, (double)sctHighCm, (double)helmetDetectCmA2, refillTriesMax, refillIntervalSec);
    Serial.printf("water : alcohol wet>=%u dry<=%u | scented wet>=%u dry<=%u | wet is %s\n", wsEnter[0], wsLeave[0], wsEnter[1], wsLeave[1], wsWetHigher ? "HIGHER" : "LOWER");
    static const char* const UNIT_NAME[3] = {"% of the cleaning", "s", "min"};
    Serial.printf("phases: paid cleaning %s, free retry %s | start part %.2f %s | end part %.2f %s | patterns (1 start 2 middle 4 end) hum1 %u hum2 %u hum3 %u uv1 %u uv2 %u\n",
                  phasePaid ? "PHASED" : "plain", phaseFree ? "PHASED" : "plain", (double)phaseStartVal, UNIT_NAME[phaseStartUnit % 3], (double)phaseEndVal, UNIT_NAME[phaseEndUnit % 3],
                  phasePat[0], phasePat[1], phasePat[2], phasePat[3], phasePat[4]);
    Serial.printf("LIVE  : helmet %.1f cm | MC alcohol %.1f cm | MC scented %.1f cm | water raw %u / %u -> %s / %s | door %s\n",
                  (double)a2snap.usHelmetCm, (double)a2snap.usMcAlsCm, (double)a2snap.usMcSlsCm, a2snap.wsAlsRaw, a2snap.wsSlsRaw,
                  wsWet[0] ? "WET" : "dry", wsWet[1] ? "WET" : "dry", a2snap.doorEnclosure ? "OPEN" : "shut");
    Serial.printf("csv   : rules=%s\n        levels=%s\n", r, l);
}

void handleSerialLine(char* line) {
    char* cmd = strtok(line, " \t\r\n");
    if (!cmd) return;
    char* a1 = strtok(nullptr, " \t\r\n");
    char* a2 = strtok(nullptr, " \t\r\n");
    char* a3 = strtok(nullptr, " \t\r\n");
    if (!strcmp(cmd, "help")) {
        Serial.println("Commands:  cfg | reset | bypass 0/1 | set <name> <value> | set bandN <from> <to> <minutes>  (N = 1..4)");
        Serial.println("Names:  checks 0/1, alclow, alchigh, sctlow, scthigh (cm), helm (cm), wsa_in, wsa_out, wss_in, wss_out (raw), wshigher 0/1,");
        Serial.println("        tries, interval (s), acs 0/1, minmin, coins, timevalue, unitmin 0/1, maxcoins, freeon 0/1, freerating, freemin");
        Serial.println("Cleaning phases:  set phasepaid 0/1 | set phasefree 0/1 | set phasestart <value> <pct|sec|min> | set phaseend <value> <pct|sec|min>");
        Serial.println("        set phasepat <hum1> <hum2> <hum3> <uv1> <uv2>   (each 0-7: 1 = start, 2 = middle, 4 = end, added together; default 2 2 2 5 5)");
        return;
    }
    if (!strcmp(cmd, "cfg")) { printConfig(); return; }
    if (!strcmp(cmd, "reset")) { enterState(STATE_TAPS); return; }
    if (!strcmp(cmd, "bypass") && a1) { refillBypass = atoi(a1) != 0; saveBypass(); Serial.printf("bypass %s\n", refillBypass ? "ON" : "off"); return; }
    if (!strcmp(cmd, "set") && a1 && a2) {
        float v = atof(a2);
        if (!strcmp(a1, "phasepaid")) phasePaid = v != 0;
        else if (!strcmp(a1, "phasefree")) phaseFree = v != 0;
        else if ((!strcmp(a1, "phasestart") || !strcmp(a1, "phaseend")) && a3) {          // set phasestart <value> <pct|sec|min>
            uint8_t u = !strcmp(a3, "pct") ? 0 : (!strcmp(a3, "sec") ? 1 : (!strcmp(a3, "min") ? 2 : 255));
            if (u == 255) { Serial.println("the unit must be pct, sec or min"); return; }
            if (!strcmp(a1, "phasestart")) { phaseStartVal = v; phaseStartUnit = u; } else { phaseEndVal = v; phaseEndUnit = u; }
        }
        else if (!strcmp(a1, "phasepat") && a3) {                                          // set phasepat <hum1> <hum2> <hum3> <uv1> <uv2>
            char* p4 = strtok(nullptr, " \t\r\n");
            char* p5 = strtok(nullptr, " \t\r\n");
            char* p6 = strtok(nullptr, " \t\r\n");
            if (!p4 || !p5 || !p6) { Serial.println("usage: set phasepat <hum1> <hum2> <hum3> <uv1> <uv2>   (each 0-7: 1 = start, 2 = middle, 4 = end, added together)"); return; }
            phasePat[0] = (uint8_t)atoi(a2) & 7; phasePat[1] = (uint8_t)atoi(a3) & 7; phasePat[2] = (uint8_t)atoi(p4) & 7;
            phasePat[3] = (uint8_t)atoi(p5) & 7; phasePat[4] = (uint8_t)atoi(p6) & 7;
        }
        else if (!strncmp(a1, "band", 4) && a1[4] >= '1' && a1[4] <= '4' && a3) {
            uint8_t i = a1[4] - '1';
            bandFrom[i] = (uint8_t)atoi(a2); bandTo[i] = (uint8_t)atoi(a3);
            char* a4 = strtok(nullptr, " \t\r\n");
            if (a4) bandMinutes[i] = (uint8_t)atoi(a4);
        }
        else if (!strcmp(a1, "checks")) levelChecksOn = v != 0;
        else if (!strcmp(a1, "alclow")) alcLowCm = v;
        else if (!strcmp(a1, "alchigh")) alcHighCm = v;
        else if (!strcmp(a1, "sctlow")) sctLowCm = v;
        else if (!strcmp(a1, "scthigh")) sctHighCm = v;
        else if (!strcmp(a1, "helm")) helmetDetectCmA2 = v;
        else if (!strcmp(a1, "wsa_in")) wsEnter[0] = (uint16_t)v;
        else if (!strcmp(a1, "wsa_out")) wsLeave[0] = (uint16_t)v;
        else if (!strcmp(a1, "wss_in")) wsEnter[1] = (uint16_t)v;
        else if (!strcmp(a1, "wss_out")) wsLeave[1] = (uint16_t)v;
        else if (!strcmp(a1, "wshigher")) wsWetHigher = v != 0;
        else if (!strcmp(a1, "tries")) refillTriesMax = (uint8_t)v;
        else if (!strcmp(a1, "interval")) refillIntervalSec = (uint8_t)v;
        else if (!strcmp(a1, "acs")) acsRequests = v != 0;
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
    packet.deviceID               = DEVICE_S3A;
    packet.machineState           = (uint8_t)currentMachineState;
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

    // fresh copy of A2's report for this tick
    portENTER_CRITICAL(&rxMux);
    a2snap = nodeA2;
    portEXIT_CRITICAL(&rxMux);
    if (!a2Alive()) {
        // Sensor board silent: never keep acting on its last readings. "Door open, no helmet" is the safe
        // way to be wrong - a cleaning in progress pauses with the UV and mist off until A2 is back.
        a2snap.doorEnclosure = true;
        a2snap.usHelmetCm = 0.0f;
    }
    updateWaterVerdicts();

    // enclosure solenoid duty cap
    bool lockWanted = (currentMachineState == STATE_INSTRUCTIONS || currentMachineState == STATE_RETRIEVE ||
                       (currentMachineState == STATE_PAUSED_SAFETY && unlockForPause) ||
                       (currentMachineState == STATE_MAINTENANCE && (maintMask & A3_BIT(A3_RELAY_ENCLOSURE_LOCK))));
    if (lockWanted) {
        if (!lockResting) {
            if (++unlockHeldSec >= LOCK_MAX_ON_SEC) { lockResting = true; lockRestSec = 0; Serial.println("[S3A] Enclosure lock rests for a few seconds (solenoid duty cap)"); }
        } else if (++lockRestSec >= LOCK_REST_SEC) {
            lockResting = false; unlockHeldSec = 0;
        }
    } else {
        unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
    }

    tickState();
    applyDesiredMask();                       // picks up the lock duty cap, the max-coins cutoff and the cleaning phases
    if (currentMachineState == STATE_CLEANING) syncCleanStatus(false);

    if (coinFlag) { coinFlag = false; pushStatus("COIN INSERTED"); }

    // The once-a-second broadcasts. Small gaps: several back-to-back sends can overflow ESP-NOW's queue.
    sendCmd(DEVICE_A1, CMD_SYNC_VARS, (uint8_t)min(minCoinsRequired, (uint32_t)255), (uint16_t)activeTimer, (uint8_t)min(requiredCoinsForSession, (uint32_t)255));
    delay(2);
    sendCmd(0, CMD_HEARTBEAT, DEVICE_S3A, 0, 0);
    delay(2);
    broadcastConfig();
    delay(2);
    broadcastStatus();
    delay(2);

    if (now - lastMaskSentMs >= MASK_REASSERT_MS) { sendMask(); delay(2); }
    if (now - lastCfgCsvMs >= 2000) { lastCfgCsvMs = now; broadcastSettingsCsv(); }
    if (now - lastTokenMs >= 5000) {          // numbers A1 may have missed or lost in a reboot
        lastTokenMs = now;
        sendToken(TOKEN_MAXCOINS, maxCoins);
        sendToken(TOKEN_TRIES, refillTriesMax);
        if (currentMachineState == STATE_REFILLING || currentMachineState == STATE_REFILLING_SCN) sendToken(TOKEN_TRY, refillTry);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=======================================================");
    Serial.println("   NODE S3A: BOX 1 INTERNAL SERVER MANAGEMENT (ESP32-S3)");
    Serial.println("   New procedure (30-step Designer flow). Type 'help' for the console.");
    Serial.println("=======================================================");

    memset(&nodeA2, 0, sizeof(nodeA2));
    memset(&a2snap, 0, sizeof(a2snap));
    loadConfigFromNVS();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[S3A] ESP-NOW: Initialization FAILED!");
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
    enterState(STATE_TAPS);
    sendMask();
    sendToken(TOKEN_MAXCOINS, maxCoins);
    sendToken(TOKEN_TRIES, refillTriesMax);
}

void loop() {
    unsigned long now = millis();

    // queued commands from A1 / LocalServer
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

    // post-solenoid / post-relay repaint of A1 (same EMI-glitch mitigation as before), or a requested one
    if (a1RedrawAtMillis != 0 && now >= a1RedrawAtMillis) {
        a1RedrawAtMillis = 0;
        needScreenRedraw = false;
        if (a1HardRefreshPending) {
            a1HardRefreshPending = false;
            sendCmd(DEVICE_A1, CMD_TFT_HARD_REFRESH, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
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
