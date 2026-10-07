#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "S3B_Screens.h"

// =====================================================================
// NODE S3B - BOX 3 (CLEANING + DRYING) INTERNAL SERVER MANAGEMENT (ESP32-S3)          (core 3.3.7)
// Created 2026-10-03 from Box 1's S3A, for the Box 3 procedure designed in the Box Cycle Designer (BOX 3 STEP.txt, as agreed with the
// user: BOX3_STEPS_2026-10-03_v3_proposal.json). NOT YET FLASHED.
//
// Runs the whole customer journey by itself (no LocalServer needed):
//   TAPS -> CHECKING A/S (container levels, REFILLING A/S) -> WELCOME -> SELECT MODE (CLEANING / DRYING) ->
//     CLEANING: RATE A (odor) -> METHOD (mist choice) -> TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> CLEANING ->
//               OPEN B -> SENSORS B -> CLOSE DOOR B -> RATE B1 (dryness) -> RATE B (odor) -> (FREE RETRY) -> FINISH
//     DRYING:   RATE A (dryness) -> TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> DRYING (Heater + UV) ->
//               COOL DOWN -> OPEN B -> SENSORS B -> CLOSE DOOR B -> RATE B (dryness) -> (FREE RETRY) -> FINISH
//   with ABORT / CANCEL confirmations, MAINTENANCE and OUT OF ORDER.
// Talks to: B1 (screen/touch/coins), B2 (sensors: A2SensorPacket), B3 (relays: one CMD_SET_RELAY_MASK, 13 bits).
//
// What differs from Box 1:
//  - If a container is LOW and the refill gave up, the box is NOT out of order any more: CLEANING is greyed out (SELECT DRY ONLY) and
//    DRYING still works. A passing check clears that again. (refillBypass is the flag.)
//  - The HEATER (a hair dryer) also runs in a CLEANING, but only in its END part (the last minute by default): mist first, dry-off last.
//    Paid and free cleanings have their own part lengths and relay patterns (PHASES settings, incl. the heater). If phasing is switched OFF for a
//    cleaning the heater stays OFF (it never runs together with the mist).
//  - DRYING = Heater + UV 1 + UV 2 for the time the customer paid for; COOL DOWN afterwards is a plain wait (Box 3 has no fan).
//  - Two ratings after a cleaning (dryness, then odor). The free retry is offered when EITHER one is in the free range (the retry is another
//    cleaning - or another drying after a drying), once per session. With the default "from rating 1" everybody qualifies.
//
// DYNAMIC SETTINGS (NVS, survive a LocalServer swap; a dashboard edits them with CMD_SET_CONFIG, S3B rebroadcasts them target 0 / state 3):
//   subIndex 1  RULES  : 4 rating bands (from,to,minutes) x4, minMinutes, coins, timeValue, unitMin(1/0), maxCoins, freeOn, freeMinRating,
//                        freeMinutes                                                                    (20 numbers, same as S3A)
//   subIndex 2  LEVELS : checksOn, alcLowCm, alcHighCm, sctLowCm, sctHighCm, helmetCm, wsAlcEnter, wsAlcLeave, wsSctEnter, wsSctLeave,
//                        wsWetHigher, refillTries, refillIntervalSec, acsRequests                       (14 numbers, same as S3A)
//   subIndex 3  PHASES : paidOn, freeOn, paidStartVal, paidStartUnit, paidEndVal, paidEndUnit, freeStartVal, freeStartUnit, freeEndVal,
//                        freeEndUnit, then 6 patterns for the PAID cleaning, then 6 for the FREE one (hum1, hum2, hum3, uv1, uv2, heater;
//                        1 = start part, 2 = middle, 4 = end, added together; units 0 = %, 1 = seconds, 2 = minutes)   (22 numbers)
//   subIndex 4  DRYING : coolDownSec                                                                    (1 number)
// Until a dashboard has its page, everything can be set from the USB serial console: type "help".
// =====================================================================

Preferences prefs;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------
// Settings - RULES (defaults = a copy of Box 1's rules block; Box 3's own prices are the user's to decide)
// ---------------------------------------------------------------------
uint8_t  bandFrom[4]    = {1, 6, 8, 10};
uint8_t  bandTo[4]      = {5, 7, 9, 10};
uint8_t  bandMinutes[4] = {5, 10, 15, 20};
uint8_t  minMinutes     = 5;       // the shortest session
uint16_t rateCoins      = 15;      // "N coins = X seconds/minutes"
float    rateValue      = 5.0f;
bool     rateUnitMin    = true;    // true = minutes, false = seconds
uint16_t maxCoins       = 90;      // most coins one session can take (90 coins = 30 min at 15 coins = 5 min)
bool     freeEnabled    = true;    // one free retry after the last rating ...
uint8_t  freeMinRating  = 1;       // ... when a final rating is at least this (1 = everyone, for the deployment demo)
uint8_t  freeMinutes    = 2;       // ... lasting this long

// ---------------------------------------------------------------------
// Settings - LEVELS / SENSORS / REFILL (same meaning as on S3A)
// ---------------------------------------------------------------------
bool     levelChecksOn    = false;
float    alcLowCm         = 20.0f;  // PLACEHOLDERS - measure with the real containers
float    alcHighCm        = 8.0f;
float    sctLowCm         = 20.0f;
float    sctHighCm        = 8.0f;
float    helmetDetectCm   = 15.0f;  // helmet counts as inside when the helmet ultrasonic reads under this
uint16_t wsEnter[2]       = {600, 600};  // analog water sensors (alcohol, scented): raw reading that means WET
uint16_t wsLeave[2]       = {400, 400};  // raw reading that means DRY again (the gap = hysteresis)
bool     wsWetHigher      = true;   // true = the reading goes UP when the sensor is in liquid
uint8_t  refillTriesMax   = 10;
uint8_t  refillIntervalSec = 10;
bool     acsRequests      = false;  // OFF until the ACS is finished: count the tries but do not message it
bool     refillBypass     = false;  // refill gave up and the containers are still low: CLEANING is unavailable (greyed), DRYING works

// ---------------------------------------------------------------------
// Settings - CLEANING PHASES (from the Designer's "phased relays"). A cleaning is cut into a START part, a MIDDLE part (what is left)
// and an END part; each cleaning relay is ON or OFF in each. Pattern bits: 1 = start, 2 = middle, 4 = end. Part lengths: unit 0 = % of the
// cleaning, 1 = seconds, 2 = minutes. Order of the 6 patterns: Humidifier 1, Humidifier 2, Humidifier 3, UV 1, UV 2, HEATER.
// Defaults = the user's Box 3 export: PAID: UV the first and last minute, humidifiers in the middle, heater + UV the last minute.
// FREE (2 min): humidifiers the first minute, heater the last minute, UV the whole time (pattern 7).
// ---------------------------------------------------------------------
const uint8_t PH_START = 1, PH_MID = 2, PH_END = 4, PH_ALL = 7;
struct PhaseSet {
    bool    on;
    float   startVal;  uint8_t startUnit;
    float   endVal;    uint8_t endUnit;
    uint8_t pat[6];
};
PhaseSet phPaid = { true, 1.0f, 2, 1.0f, 2, { PH_MID,   PH_MID,   PH_MID,   PH_START | PH_END, PH_START | PH_END, PH_END } };
PhaseSet phFree = { true, 1.0f, 2, 1.0f, 2, { PH_START, PH_START, PH_START, PH_ALL,            PH_ALL,            PH_END } };

// Settings - DRYING
uint16_t coolDownSec = 60;          // how long the passive COOL DOWN lasts (also after an abort)

// Derived from the rules by recomputeDerived()
float    secPerCoinF      = 20.0f;
uint16_t secondsPerCoin   = 20;
uint32_t minCoinsRequired = 15;

// ---------------------------------------------------------------------
// Session (one customer)
// ---------------------------------------------------------------------
const uint8_t MODE_NONE = 0, MODE_CLEAN = 1, MODE_DRY = 2;
const uint8_t MIST_NONE = 0, MIST_ALC = 1, MIST_SCN = 2, MIST_BAL = 3;
MachineState currentMachineState = STATE_TAPS;
MachineState returnState         = STATE_TAPS;   // where ABORT / CANCEL / a safety pause "go back to"
uint8_t  sessionMode     = MODE_NONE;
uint8_t  mistMode        = MIST_NONE;
bool     retryUsed       = false;   // the one free retry has been taken (or offered)
bool     runFree         = false;   // the cleaning / drying in progress is the free retry
uint8_t  ratingA         = 0, ratingB = 0, ratingDry = 0;   // before; odor-or-dryness after; dryness after a cleaning
uint32_t requiredCoinsForSession = 0;
uint32_t activeTimer     = 0;       // seconds left of the cleaning / drying / cool down (or seconds bought while paying)
uint32_t runTotalSec     = 0;       // length of the cleaning / drying, for the progress bar (capped at 9999)
uint32_t coolTotalSec    = 0;
uint32_t sessionCoins    = 0;
uint32_t cleanElapsedSec = 0;       // seconds of cleaning already done (pauses do not count) - drives the phases
bool     lastUvOn = false, lastMistOn = false, lastHeatOn = false;   // what the status feed last announced
uint32_t npInitSeconds   = 0;       // the numpad's suggested time
char     timeWarnL1[32]  = "", timeWarnL2[32] = "";   // numpad rejection message (empty = none)
uint8_t  refillTry       = 0, refillElapsed = 0;
uint16_t stepRemainingSec = 0;
char     scrBuf[220];               // scratch for screens with live numbers baked in

// per-state debounce counters (2 ticks = 2 s)
uint8_t  holdA = 0, holdB = 0, holdC = 0;
// enclosure solenoid duty cap: never energized for more than LOCK_MAX_ON_SEC in a row
const uint16_t LOCK_MAX_ON_SEC = 60, LOCK_REST_SEC = 5;
uint16_t unlockHeldSec = 0, lockRestSec = 0;
bool     lockResting   = false;
bool     unlockForPause = false;    // PAUSED_SAFETY with the door shut and no helmet: let them back in
uint16_t maintMask     = 0;         // MAINTENANCE: relays set by hand from the dashboard
uint16_t maintHeaterSec = 0;        // how long the hand-set heater has been on; it switches itself off after MAINT_HEATER_MAX_SEC
const uint16_t MAINT_HEATER_MAX_SEC = 300;

// session statistics for a future dashboard (BoxStatusPacket)
uint32_t completedSessionSeq    = 0;
uint32_t sessionStartMillis     = 0;
uint32_t sessionCoinsAtStart    = 0;
uint16_t lastSessionDurationSec = 0;
uint16_t lastSessionCoins       = 0;

// ---------------------------------------------------------------------
// Live data from the other nodes (written by the receive callback, snapshotted in loop())
// ---------------------------------------------------------------------
volatile uint32_t b1Pulses = 0;     volatile bool b1Seen = false;
A2SensorPacket    nodeB2;           uint32_t lastB2Ms = 0;
uint16_t          b3Mask = 0;       uint32_t lastB3Ms = 0;  bool b3Orphaned = false;
A2SensorPacket    b2snap;           // the copy the state machine reads
bool     wsWet[2]   = {false, false};
uint8_t  wsCnt[2]   = {0, 0};
uint32_t lastKnownPulsesB1 = 0;

// commands queued by the callback, handled in loop() (no heavy work or state changes in the radio task)
#define CMD_QUEUE_LEN 4
CommandPacket cmdQueue[CMD_QUEUE_LEN];
volatile uint8_t qHead = 0, qTail = 0;

// ---------------------------------------------------------------------
// Timing / broadcast state
// ---------------------------------------------------------------------
uint16_t desiredMask = 0;           // what B3 is being asked to do
unsigned long lastOneSecTick = 0, lastMaskSentMs = 0, lastCfgCsvMs = 0, lastTokenMs = 0;
const unsigned long MASK_REASSERT_MS = 2000;
unsigned long b1RedrawAtMillis = 0;
bool b1HardRefreshPending = false;
// Which relay changes force a FULL re-init of the touch screen (the white-screen cure). Bit = relay index. 'On' = the relay turns ON, 'Off' = it turns OFF
// (for the locks ON = unlocked, so locking = the lock bit going OFF). Set per relay from the dashboard (CMD_SET_CONFIG subIndex 5), kept in NVS.
uint16_t refreshOnMask  = A3_BIT(B3_RELAY_HEATER);
uint16_t refreshOffMask = A3_BIT(A3_RELAY_ENCLOSURE_LOCK) | A3_BIT(B3_RELAY_HEATER);
// Auto hard refresh: every N seconds S3B re-sends B1's current page as a full controller re-init (the white-screen cure). 0 = never, max 60.
// Set from the dashboard (CMD_SET_CONFIG subIndex 6), kept in NVS. Screens where the customer types or picks a rating are skipped (a refresh would wipe what was typed).
uint16_t autoRefreshSec = 2;
unsigned long lastAutoRefreshMs = 0;
bool needScreenRedraw = false;      // a coin changed the total: repaint so the bar follows
bool coinFlag = false;              // a coin landed during a paid run: status line
char statusLines[3][32] = {"", "", ""};
char serialBuf[160]; uint8_t serialLen = 0;

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
        case STATE_TAPS:            return "TAPS";
        case STATE_CHECKING:        return "CHECKING A";
        case STATE_CHECKING_SCN:    return "CHECKING S";
        case STATE_REFILLING:       return "REFILLING A";
        case STATE_REFILLING_SCN:   return "REFILLING S";
        case STATE_WELCOME:         return "WELCOME";
        case STATE_SELECT_MODE:     return "SELECT MODE";
        case STATE_SELECT_DRY_ONLY: return "SELECT DRY ONLY";
        case STATE_RATE_A:          return "RATE A";
        case STATE_METHOD:          return "METHOD";
        case STATE_TIME_ALLOT:      return "TIME ALLOT";
        case STATE_INSERT_COIN:     return "INSERT COIN";
        case STATE_INSTRUCTIONS:    return "OPEN A";
        case STATE_PLACE_HELMET:    return "SENSORS A";
        case STATE_CLOSE_DOOR_A:    return "CLOSE DOOR A";
        case STATE_CLEANING:        return "CLEANING";
        case STATE_DRYING:          return "DRYING";
        case STATE_COOL_DOWN:       return "COOL DOWN";
        case STATE_PAUSED_SAFETY:   return "SAFETY PAUSE";
        case STATE_RETRIEVE:        return "OPEN B";
        case STATE_TAKE_HELMET:     return "SENSORS B";
        case STATE_CLOSE_DOOR_B:    return "CLOSE DOOR B";
        case STATE_RATE_B1:         return "RATE B1 (dryness)";
        case STATE_RATE_B:          return "RATE B";
        case STATE_FREE_RETRY:      return "FREE RETRY";
        case STATE_FINISH:          return "FINISH";
        case STATE_ABORT_CONFIRM:   return "ABORT";
        case STATE_CANCEL_CONFIRM:  return "CANCEL";
        case STATE_MAINTENANCE:     return "MAINTENANCE";
        case STATE_OUT_OF_ORDER:    return "OUT OF ORDER";
        case STATE_HELMET_NOTICE_A: return "HELMET NOTICE A";
        case STATE_HELMET_NOTICE_B: return "HELMET NOTICE B";
        default:                    return "?";
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
    prefs.putFloat("helm_cm", helmetDetectCm);
    prefs.putUShort("wsa_in", wsEnter[0]);
    prefs.putUShort("wsa_out", wsLeave[0]);
    prefs.putUShort("wss_in", wsEnter[1]);
    prefs.putUShort("wss_out", wsLeave[1]);
    prefs.putBool("ws_hi", wsWetHigher);
    prefs.putUChar("try_max", refillTriesMax);
    prefs.putUChar("try_int", refillIntervalSec);
    prefs.putBool("acs_req", acsRequests);
    prefs.putBytes("ph_paid", &phPaid, sizeof(PhaseSet));
    prefs.putBytes("ph_free", &phFree, sizeof(PhaseSet));
    prefs.putUShort("cd_sec", coolDownSec);
    prefs.putUShort("rf_on", refreshOnMask);
    prefs.putUShort("rf_off", refreshOffMask);
    prefs.putUShort("arf_sec", autoRefreshSec);
}

void loadConfigFromNVS() {
    prefs.begin("s3b_nvs", false);
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
    helmetDetectCm = prefs.getFloat("helm_cm", helmetDetectCm);
    wsEnter[0]     = prefs.getUShort("wsa_in", wsEnter[0]);
    wsLeave[0]     = prefs.getUShort("wsa_out", wsLeave[0]);
    wsEnter[1]     = prefs.getUShort("wss_in", wsEnter[1]);
    wsLeave[1]     = prefs.getUShort("wss_out", wsLeave[1]);
    wsWetHigher    = prefs.getBool("ws_hi", wsWetHigher);
    refillTriesMax = prefs.getUChar("try_max", refillTriesMax);
    refillIntervalSec = prefs.getUChar("try_int", refillIntervalSec);
    acsRequests    = prefs.getBool("acs_req", acsRequests);
    PhaseSet tmp;
    if (prefs.getBytes("ph_paid", &tmp, sizeof(PhaseSet)) == sizeof(PhaseSet)) { phPaid = tmp; for (uint8_t i = 0; i < 6; i++) phPaid.pat[i] &= 7; }
    if (prefs.getBytes("ph_free", &tmp, sizeof(PhaseSet)) == sizeof(PhaseSet)) { phFree = tmp; for (uint8_t i = 0; i < 6; i++) phFree.pat[i] &= 7; }
    coolDownSec    = prefs.getUShort("cd_sec", coolDownSec);
    refreshOnMask  = prefs.getUShort("rf_on", refreshOnMask);
    refreshOffMask = prefs.getUShort("rf_off", refreshOffMask);
    autoRefreshSec = prefs.getUShort("arf_sec", autoRefreshSec);
    refillBypass   = prefs.getBool("bypass", false);          // survives a reboot: CLEANING stays unavailable until the levels recover
    lastKnownPulsesB1 = prefs.getUInt("last_b1_pulses", 0);   // survives an S3B reboot mid-session
    recomputeDerived();
}

void saveBypass() { prefs.putBool("bypass", refillBypass); }

// ---------------------------------------------------------------------
// Settings as CSV (dashboard <-> S3B)
// ---------------------------------------------------------------------
void rulesToCsv(char* out, size_t n) {
    size_t k = 0;
    for (uint8_t i = 0; i < 4 && k < n; i++) k += snprintf(out + k, n - k, "%u,%u,%u,", bandFrom[i], bandTo[i], bandMinutes[i]);
    if (k < n) snprintf(out + k, n - k, "%u,%u,%.2f,%u,%u,%u,%u,%u", minMinutes, rateCoins, (double)rateValue, rateUnitMin ? 1 : 0,
                        maxCoins, freeEnabled ? 1 : 0, freeMinRating, freeMinutes);
}

void levelsToCsv(char* out, size_t n) {
    snprintf(out, n, "%u,%.1f,%.1f,%.1f,%.1f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u",
             levelChecksOn ? 1 : 0, (double)alcLowCm, (double)alcHighCm, (double)sctLowCm, (double)sctHighCm, (double)helmetDetectCm,
             wsEnter[0], wsLeave[0], wsEnter[1], wsLeave[1], wsWetHigher ? 1 : 0, refillTriesMax, refillIntervalSec, acsRequests ? 1 : 0);
}

void phasesToCsv(char* out, size_t n) {
    snprintf(out, n, "%u,%u,%.2f,%u,%.2f,%u,%.2f,%u,%.2f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
             phPaid.on ? 1 : 0, phFree.on ? 1 : 0,
             (double)phPaid.startVal, phPaid.startUnit, (double)phPaid.endVal, phPaid.endUnit,
             (double)phFree.startVal, phFree.startUnit, (double)phFree.endVal, phFree.endUnit,
             phPaid.pat[0], phPaid.pat[1], phPaid.pat[2], phPaid.pat[3], phPaid.pat[4], phPaid.pat[5],
             phFree.pat[0], phFree.pat[1], phFree.pat[2], phFree.pat[3], phFree.pat[4], phFree.pat[5]);
}

void dryingToCsv(char* out, size_t n) { snprintf(out, n, "%u", coolDownSec); }

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
    alcLowCm = v[1]; alcHighCm = v[2]; sctLowCm = v[3]; sctHighCm = v[4]; helmetDetectCm = v[5];
    wsEnter[0] = (uint16_t)v[6]; wsLeave[0] = (uint16_t)v[7]; wsEnter[1] = (uint16_t)v[8]; wsLeave[1] = (uint16_t)v[9];
    wsWetHigher = v[10] >= 0.5f; refillTriesMax = (uint8_t)v[11]; refillIntervalSec = (uint8_t)v[12]; acsRequests = v[13] >= 0.5f;
    return true;
}

// one start/end length pair must be sane: unit 0..2, value >= 0, and at most 100 when the unit is %
bool phasePairOk(float startVal, float startUnit, float endVal, float endUnit) {
    if (startUnit < 0 || startUnit > 2 || endUnit < 0 || endUnit > 2) return false;
    if (startVal < 0 || endVal < 0 || startVal > 7200 || endVal > 7200) return false;
    if ((startUnit < 0.5f && startVal > 100) || (endUnit < 0.5f && endVal > 100)) return false;
    return true;
}

bool applyPhasesCsv(const char* s) {
    float v[22];
    if (parseFloats(s, v, 22) != 22) return false;
    if (!phasePairOk(v[2], v[3], v[4], v[5]) || !phasePairOk(v[6], v[7], v[8], v[9])) return false;
    for (uint8_t i = 10; i < 22; i++) if (v[i] < 0 || v[i] > 7) return false;
    phPaid.on = v[0] >= 0.5f; phFree.on = v[1] >= 0.5f;
    phPaid.startVal = v[2]; phPaid.startUnit = (uint8_t)(v[3] + 0.5f); phPaid.endVal = v[4]; phPaid.endUnit = (uint8_t)(v[5] + 0.5f);
    phFree.startVal = v[6]; phFree.startUnit = (uint8_t)(v[7] + 0.5f); phFree.endVal = v[8]; phFree.endUnit = (uint8_t)(v[9] + 0.5f);
    for (uint8_t i = 0; i < 6; i++) { phPaid.pat[i] = (uint8_t)(v[10 + i] + 0.5f) & 7; phFree.pat[i] = (uint8_t)(v[16 + i] + 0.5f) & 7; }
    return true;
}

bool applyDryingCsv(const char* s) {
    float v[1];
    if (parseFloats(s, v, 1) != 1) return false;
    if (v[0] < 5 || v[0] > 900) return false;
    coolDownSec = (uint16_t)(v[0] + 0.5f);
    return true;
}

// Per-relay screen refresh (subIndex 5): two numbers = bit mask of relays whose switching ON triggers a refresh, then the mask for switching OFF.
void refreshToCsv(char* out, size_t n) { snprintf(out, n, "%u,%u", (unsigned)refreshOnMask, (unsigned)refreshOffMask); }
bool applyRefreshCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 0 || v[1] < 0 || v[0] > B3_MASK_ALL || v[1] > B3_MASK_ALL) return false;
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
    rulesToCsv(buf, sizeof(buf));   sendCmd(0, CMD_SET_CONFIG, 1, 0, 3, buf);   delay(2);   // state = 3: Box 3's copy (S3A sends 0, S3C 2)
    levelsToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 2, 0, 3, buf);   delay(2);
    phasesToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 3, 0, 3, buf);   delay(2);
    dryingToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 4, 0, 3, buf);
    delay(2);
    refreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 5, 0, 3, buf);
    delay(2);
    autoRefreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 6, 0, 3, buf);
}

// ---------------------------------------------------------------------
// Sensors and levels (all read from the snapshot taken once per second)
// ---------------------------------------------------------------------
bool b2Alive() { return lastB2Ms != 0 && (millis() - lastB2Ms) < 5000UL; }
bool b3Alive() { return lastB3Ms != 0 && (millis() - lastB3Ms) < 5000UL; }
bool systemReady() { return b2Alive() && b3Alive(); }
bool doorOpenNow()      { return b2snap.doorEnclosure; }
bool helmetPresentNow() { return b2snap.usHelmetCm > 0.0f && b2snap.usHelmetCm < helmetDetectCm; }

bool holdTicks(uint8_t &counter, bool cond, uint8_t need) {
    if (cond) { if (counter < 255) counter++; } else counter = 0;
    return counter >= need;
}

// Water sensors are judged here from the raw readings B2 reports, with the calibration set on the dashboard.
void updateWaterVerdicts() {
    uint16_t raw[2] = {b2snap.wsAlsRaw, b2snap.wsSlsRaw};
    for (uint8_t k = 0; k < 2; k++) {
        if (!b2Alive()) { wsWet[k] = false; wsCnt[k] = 0; continue; }
        bool beyondEnter = wsWetHigher ? (raw[k] >= wsEnter[k]) : (raw[k] <= wsEnter[k]);
        bool beyondLeave = wsWetHigher ? (raw[k] <= wsLeave[k]) : (raw[k] >= wsLeave[k]);
        bool flip = wsWet[k] ? beyondLeave : beyondEnter;
        if (flip) { if (++wsCnt[k] >= 2) { wsWet[k] = !wsWet[k]; wsCnt[k] = 0; } } else wsCnt[k] = 0;
    }
}

bool containerLow(float cm, float lowCm) { return cm <= 0.0f || cm >= lowCm; }
bool alcoholEnough()  { return !levelChecksOn || !containerLow(b2snap.usMcAlsCm, alcLowCm) || wsWet[0]; }
bool scentedEnough()  { return !levelChecksOn || !containerLow(b2snap.usMcSlsCm, sctLowCm) || wsWet[1]; }
bool alcoholRefilled() { return !levelChecksOn || (b2snap.usMcAlsCm > 0.0f && b2snap.usMcAlsCm <= alcHighCm); }
bool scentedRefilled() { return !levelChecksOn || (b2snap.usMcSlsCm > 0.0f && b2snap.usMcSlsCm <= sctHighCm); }

// ---------------------------------------------------------------------
// Messages to the other nodes
// ---------------------------------------------------------------------
void sendBuzzer(uint16_t ms) { sendCmd(DEVICE_B1, CMD_BUZZER, 0, ms, 0); }
void sendToken(uint8_t id, uint16_t value) { sendCmd(DEVICE_B1, CMD_SET_TOKEN, id, value, 0); delay(2); }

void sendStatusLines() {
    for (uint8_t i = 0; i < 3; i++) { sendCmd(DEVICE_B1, CMD_STATUS_LINE, i, 0, 0, statusLines[i]); delay(2); }
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
    sendCmd(DEVICE_B1, CMD_RESET_COINS, 0, 0, 0);
    lastKnownPulsesB1 = 0;
    prefs.putUInt("last_b1_pulses", 0);
}

// The refill system is asked to top a container up. Box 3's refill system is ACS 2 (DEVICE_ACS2; Box 1's ACS 1 is DEVICE_ACS).
// OFF by default (acsRequests) until ACS 2 is flashed and checked - "set acs 1". The tries are counted either way.
// state 0 = "top it up" (the ACS pumps one bounded shot per request), state 1 = "stop, it is full".
void sendRefillAsk(uint8_t container, uint8_t state) { sendCmd(DEVICE_ACS2, CMD_REQUEST_DELIVERY, container, 0, state); }
void requestRefill(uint8_t container) {
    if (!acsRequests) { Serial.printf("[S3B] Refill try %u for container %u (ACS requests are OFF - waiting only)\n", refillTry, container); return; }
    Serial.printf("[S3B] Refill try %u: asking ACS 2 to top up container %u\n", refillTry, container);
    sendRefillAsk(container, 0);
}
void cancelRefill(uint8_t container) { if (acsRequests) sendRefillAsk(container, 1); }

// While CLEANING is unavailable (the refill gave up) nothing asks ACS 2 any more - so ask quietly, once every 30 s, from the TAPS screen
// for whichever container is not refilled yet, and lift the bypass as soon as both containers read fine again (no customer needed).
uint8_t bypassAskTicks = 0;
void bypassTick() {
    if (!refillBypass || !levelChecksOn) { bypassAskTicks = 0; return; }
    if (systemReady() && alcoholEnough() && scentedEnough()) {
        refillBypass = false; saveBypass(); bypassAskTicks = 0;
        cancelRefill(0); cancelRefill(1);
        Serial.println("[S3B] Both containers are fine again - CLEANING is available");
        return;
    }
    if (++bypassAskTicks >= 30) {
        bypassAskTicks = 0;
        bool needA = !alcoholRefilled() && !wsWet[0];
        bool needS = !scentedRefilled() && !wsWet[1];
        if (acsRequests && (needA || needS)) {
            uint8_t c = needA ? 0 : 1;
            Serial.printf("[S3B] CLEANING is greyed out - asking ACS 2 again for container %u\n", c);
            sendRefillAsk(c, 0);
        }
    }
}

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------
uint8_t anyTapFlagForCurrentState() {
    return (currentMachineState == STATE_TAPS || currentMachineState == STATE_WELCOME) ? 1 : 0;
}

const char* cleaningTemplate() {
    if (mistMode == MIST_ALC) return runFree ? SCR_CLEAN_ALC_FREE : SCR_CLEAN_ALC;
    if (mistMode == MIST_SCN) return runFree ? SCR_CLEAN_SCN_FREE : SCR_CLEAN_SCN;
    return runFree ? SCR_CLEAN_BAL_FREE : SCR_CLEAN_BAL;
}

const char* screenForCurrentState() {
    bool clean = (sessionMode == MODE_CLEAN);
    switch (currentMachineState) {
        case STATE_TAPS:            return SCR_TAPS;
        case STATE_CHECKING:        return SCR_CHECKING_A;
        case STATE_CHECKING_SCN:    return SCR_CHECKING_S;
        case STATE_REFILLING:       return SCR_REFILLING_A;
        case STATE_REFILLING_SCN:   return SCR_REFILLING_S;
        case STATE_WELCOME:         return SCR_WELCOME;
        case STATE_SELECT_MODE:     return SCR_SELECT_MODE;
        case STATE_SELECT_DRY_ONLY: return SCR_SELECT_DRY_ONLY;
        case STATE_RATE_A:          return clean ? SCR_CL_RATE_A : SCR_DR_RATE_A;
        case STATE_METHOD:          return SCR_METHOD;
        case STATE_TIME_ALLOT:
            if (timeWarnL1[0]) snprintf(scrBuf, sizeof(scrBuf), SCR_TIME_WARN, timeWarnL1, timeWarnL2);
            else snprintf(scrBuf, sizeof(scrBuf), clean ? SCR_CL_TIME_ALLOT : SCR_DR_TIME_ALLOT, (unsigned)secondsPerCoin, (unsigned)npInitSeconds);
            return scrBuf;
        case STATE_INSERT_COIN:     return SCR_INSERT_COIN;
        case STATE_INSTRUCTIONS:    return SCR_OPEN_A;
        case STATE_PLACE_HELMET:    return SCR_SENSORS_A;
        case STATE_CLOSE_DOOR_A:    return SCR_CLOSE_DOOR_A;
        case STATE_CLEANING:
            snprintf(scrBuf, sizeof(scrBuf), cleaningTemplate(), (unsigned long)runTotalSec);
            return scrBuf;
        case STATE_DRYING:
            snprintf(scrBuf, sizeof(scrBuf), runFree ? SCR_DRYING_FREE : SCR_DRYING, (unsigned long)runTotalSec);
            return scrBuf;
        case STATE_COOL_DOWN:
            snprintf(scrBuf, sizeof(scrBuf), SCR_COOLDOWN, (unsigned long)coolTotalSec);
            return scrBuf;
        case STATE_PAUSED_SAFETY:   return SCR_SAFETY_PAUSE;
        case STATE_RETRIEVE:        return SCR_OPEN_B;
        case STATE_TAKE_HELMET:     return SCR_SENSORS_B;
        case STATE_CLOSE_DOOR_B:    return SCR_CLOSE_DOOR_B;
        case STATE_RATE_B1:         return SCR_CL_RATE_B1;
        case STATE_RATE_B:          return clean ? SCR_CL_RATE_B2 : SCR_DR_RATE_B;
        case STATE_FREE_RETRY:
            snprintf(scrBuf, sizeof(scrBuf), SCR_FREE_RETRY, (unsigned)freeMinutes, freeMinutes == 1 ? "MINUTE" : "MINUTES");
            return scrBuf;
        case STATE_FINISH:          return SCR_FINISH;
        case STATE_ABORT_CONFIRM:   return SCR_ABORT;
        case STATE_CANCEL_CONFIRM:  return SCR_CANCEL;
        case STATE_MAINTENANCE:     return SCR_MAINTENANCE;
        case STATE_OUT_OF_ORDER:    return SCR_OUT_OF_ORDER;
        case STATE_HELMET_NOTICE_A: return SCR_HELMET_NOTICE_A;
        case STATE_HELMET_NOTICE_B: return SCR_HELMET_NOTICE_B;
        default:                    return SCR_TAPS;
    }
}

void showScreen() {
    sendCmd(DEVICE_B1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
}

// ---------------------------------------------------------------------
// Relays: the whole 13-bit mask for B3, worked out from the state
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
// Elapsed time only counts while the cleaning really runs (a safety pause or an abort prompt freezes it), and the total is elapsed + what is
// left, so coins added mid-cleaning move the END part later and keep it "the last minute". If the two parts would overlap, the start wins.
uint8_t cleaningPhasePart() {
    const PhaseSet& ph = runFree ? phFree : phPaid;
    if (!ph.on) return 0;
    uint32_t elapsed = cleanElapsedSec, remaining = activeTimer, total = elapsed + remaining;
    uint32_t st = phaseWindowSec(ph.startVal, ph.startUnit, total);
    uint32_t en = phaseWindowSec(ph.endVal, ph.endUnit, total);
    if (st > total) st = total;
    if (en > total - st) en = total - st;
    if (elapsed < st) return PH_START;
    if (en > 0 && remaining <= en) return PH_END;
    return PH_MID;
}

bool coinPowerWanted() {
    return (currentMachineState == STATE_INSERT_COIN) ||
           ((currentMachineState == STATE_CLEANING || currentMachineState == STATE_DRYING) && !runFree && sessionCoins < maxCoins);
}

uint16_t desiredMaskForState() {
    uint16_t m = 0;
    bool unlockWanted = false;
    switch (currentMachineState) {
        case STATE_INSERT_COIN:
            break;                                                    // only the coin power, added below
        case STATE_INSTRUCTIONS:
        case STATE_RETRIEVE:
            unlockWanted = true;
            break;
        case STATE_PAUSED_SAFETY:
            unlockWanted = unlockForPause;
            break;
        case STATE_CLEANING: {
            m = A3_MASK_UV;                                          // UV 1 + UV 2 together (the phases may take them off for a part)
            m |= A3_BIT(A3_RELAY_HUMIDIFIER_3);                      // Humidifier 3 (both containers) runs in every mode
            if (mistMode == MIST_ALC || mistMode == MIST_BAL || mistMode == MIST_NONE) m |= A3_BIT(A3_RELAY_HUMIDIFIER_1);
            if (mistMode == MIST_SCN || mistMode == MIST_BAL || mistMode == MIST_NONE) m |= A3_BIT(A3_RELAY_HUMIDIFIER_2);
            uint8_t part = cleaningPhasePart();
            if (part) {
                // of the relays this mode uses, keep only those ON in the part we are in; the heater joins when its pattern says so
                static const uint8_t PH_RELAY[5] = {A3_RELAY_HUMIDIFIER_1, A3_RELAY_HUMIDIFIER_2, A3_RELAY_HUMIDIFIER_3, A3_RELAY_UV_1, A3_RELAY_UV_2};
                const PhaseSet& ph = runFree ? phFree : phPaid;
                for (uint8_t i = 0; i < 5; i++) if (!(ph.pat[i] & part)) m &= (uint16_t)(~A3_BIT(PH_RELAY[i]));
                if (ph.pat[5] & part) m |= A3_BIT(B3_RELAY_HEATER);
            }
            // phases off for this cleaning -> NO heater: it must never run together with the mist
            break;
        }
        case STATE_DRYING:
            m = A3_MASK_UV | A3_BIT(B3_RELAY_HEATER);                // Heater + UV 1 + UV 2 the whole time
            break;
        case STATE_MAINTENANCE:
            m = maintMask & B3_MASK_ALL;
            if (lockResting) m &= (uint16_t)(~A3_BIT(A3_RELAY_ENCLOSURE_LOCK));   // the duty cap covers hand-set relays too
            break;
        default:
            break;                                                    // everything off, enclosure locked (COOL DOWN, the ABORT prompt, a safety pause)
    }
    if (coinPowerWanted()) m |= A3_BIT(A3_RELAY_COIN_POWER);
    if (unlockWanted && !lockResting) m |= A3_BIT(A3_RELAY_ENCLOSURE_LOCK);
    return m;
}

void sendMask() {
    sendCmd(DEVICE_B3, CMD_SET_RELAY_MASK, 0, desiredMask, 0);
    lastMaskSentMs = millis();
}

void muteB1AndScheduleRedraw(bool hardRefresh) {
    sendCmd(DEVICE_B1, CMD_MUTE_COINS, 0, 250, 0);
    b1RedrawAtMillis = millis() + 300;
    if (hardRefresh) b1HardRefreshPending = true;
}

void applyDesiredMask() {
    uint16_t m = desiredMaskForState();
    if (m == desiredMask) return;
    // A relay change that is ticked in the dashboard's 'refresh screen' list (default: the enclosure lock engaging, and Box 3's hair dryer switching) asks B1 for a full
    // controller re-init after the change - the supply dips and the TFT can come back blank white. Every other relay change just gets a plain repaint.
    uint16_t changedBits = (uint16_t)(desiredMask ^ m);
    bool refreshNow = ((m & changedBits & refreshOnMask) != 0) || ((desiredMask & changedBits & refreshOffMask) != 0);
    desiredMask = m;
    muteB1AndScheduleRedraw(refreshNow);
    sendMask();
    Serial.printf("[S3B] relay mask -> 0x%04X\n", (unsigned)desiredMask);
}

// ---------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------
void enterState(MachineState s);

void goOutOfOrder(const char* why) {
    Serial.printf("[S3B] OUT OF ORDER: %s\n", why);
    enterState(STATE_OUT_OF_ORDER);
}

void recordCompletedSession() {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesB1 >= sessionCoinsAtStart) ? (lastKnownPulsesB1 - sessionCoinsAtStart) : 0;
    lastSessionDurationSec = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    lastSessionCoins       = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;
    completedSessionSeq++;
    Serial.printf("[S3B STATS] Session #%u: %s, %us, %u coins, mist %u, rating before %u.\n",
                  (unsigned)completedSessionSeq, sessionMode == MODE_DRY ? "DRYING" : "CLEANING", (unsigned)lastSessionDurationSec,
                  (unsigned)lastSessionCoins, mistMode, ratingA);
}

void enterState(MachineState s) {
    currentMachineState = s;
    holdA = holdB = holdC = 0;
    timeWarnL1[0] = '\0';
    switch (s) {
        case STATE_TAPS:
            activeTimer = 0;
            sessionMode = MODE_NONE;
            mistMode = MIST_NONE;
            retryUsed = false;
            runFree = false;
            ratingA = ratingB = ratingDry = 0;
            sessionCoins = 0;
            runTotalSec = coolTotalSec = 0;
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
    Serial.printf("[S3B] -> %s\n", stateName(s));
}

// A customer has tapped the TAPS screen.
void beginSession() {
    sessionMode = MODE_NONE; mistMode = MIST_NONE; retryUsed = false; runFree = false; ratingA = ratingB = ratingDry = 0; sessionCoins = 0;
    enterState(STATE_CHECKING);
}

const char* mistStatusLine() {
    return mistMode == MIST_ALC ? "HUMID ALC: Misting" : (mistMode == MIST_SCN ? "HUMID SCN: Misting" : "HUMID BOTH: Misting");
}

// The cleaning "kill feed": announces what the machine just did. At the start only what is ON is announced; after that every change of the
// UV, the mist or the heater (the phases switch them) gets a line, newest first on the screen.
void syncCleanStatus(bool first) {
    bool uvOn = (desiredMask & A3_MASK_UV) != 0;
    bool mistOn = (desiredMask & A3_MASK_MIST) != 0;
    bool heatOn = (desiredMask & A3_BIT(B3_RELAY_HEATER)) != 0;
    if (first) {
        if (uvOn) pushStatus("UV: ON");
        if (mistOn) pushStatus(mistStatusLine());
        if (heatOn) pushStatus("HEATER: ON");
    } else {
        if (uvOn != lastUvOn) pushStatus(uvOn ? "UV: ON" : "UV: OFF");
        if (mistOn != lastMistOn) pushStatus(mistOn ? mistStatusLine() : "HUMID: OFF");
        if (heatOn != lastHeatOn) pushStatus(heatOn ? "HEATER: ON" : "HEATER: OFF");
    }
    lastUvOn = uvOn; lastMistOn = mistOn; lastHeatOn = heatOn;
}

// START CLEANING: a routing decision, not a state. Mist mode and the free retry pick the variant.
void beginCleaning() {
    runFree = retryUsed;
    if (runFree) {
        activeTimer = (uint32_t)freeMinutes * 60UL;
    } else {
        sessionStartMillis  = millis();
        sessionCoinsAtStart = lastKnownPulsesB1;
    }
    runTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    cleanElapsedSec = 0;
    clearStatus();
    const PhaseSet& ph = runFree ? phFree : phPaid;
    Serial.printf("[S3B] Cleaning starts: %s, mist %u, %lus, phases %s\n", runFree ? "FREE retry" : "paid", mistMode, (unsigned long)activeTimer, ph.on ? "ON" : "off (no heater)");
    enterState(STATE_CLEANING);
    syncCleanStatus(true);                    // after enterState: the first part of the cleaning decides what is ON
    sendBuzzer(250);
}

void finishCleaning() {
    if (!runFree) recordCompletedSession();
    Serial.println("[S3B] Cleaning complete -> open the door");
    enterState(STATE_RETRIEVE);
}

// START HEATING (the DRYING procedure): routing again - the free retry picks the variant.
void beginDrying() {
    runFree = retryUsed;
    if (runFree) {
        activeTimer = (uint32_t)freeMinutes * 60UL;
    } else {
        sessionStartMillis  = millis();
        sessionCoinsAtStart = lastKnownPulsesB1;
    }
    runTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    clearStatus();
    Serial.printf("[S3B] Drying starts: %s, %lus\n", runFree ? "FREE retry" : "paid", (unsigned long)activeTimer);
    enterState(STATE_DRYING);
    pushStatus("UV: ON");
    pushStatus("HEATER: ON");
    sendBuzzer(250);
}

// COOL DOWN: a plain wait (Box 3 has no fan) - everything off. Also what an ABORT during the drying leads to.
void beginCoolDown() {
    activeTimer  = coolDownSec;
    coolTotalSec = coolDownSec > 9999 ? 9999 : coolDownSec;
    clearStatus();
    Serial.printf("[S3B] Cool down starts: %us\n", (unsigned)coolDownSec);
    enterState(STATE_COOL_DOWN);
    pushStatus("UV: OFF");
    pushStatus("HEATER: OFF");
}

void finishDrying() {
    if (!runFree) recordCompletedSession();
    Serial.println("[S3B] Drying complete -> cool down");
    beginCoolDown();
}

// Both procedures meet here after CLOSE DOOR A (the Designer's START CLEANING / START HEATING).
void beginRun() {
    if (sessionMode == MODE_DRY) beginDrying(); else beginCleaning();
}

// The numpad's OK: check the time against the rules, then on to paying - or say why not.
void handleNumpadOk(uint16_t sec) {
    uint32_t coins = coinsForSec(sec);
    uint32_t minSec = (uint32_t)minMinutes * 60UL;
    if (sec < minSec || coins < minCoinsRequired) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Minimum is %u min", (unsigned)minMinutes);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "(%u coins)", (unsigned)minCoinsRequired);
        npInitSeconds = sec;
        Serial.printf("[S3B] Time %us rejected: under the %u min minimum\n", sec, minMinutes);
        showScreen();
        return;
    }
    if (coins > maxCoins) {
        snprintf(timeWarnL1, sizeof(timeWarnL1), "Maximum is %u coins", (unsigned)maxCoins);
        snprintf(timeWarnL2, sizeof(timeWarnL2), "Please enter less time");
        npInitSeconds = sec;
        Serial.printf("[S3B] Time %us rejected: %u coins is over the %u maximum\n", sec, (unsigned)coins, maxCoins);
        showScreen();
        return;
    }
    requiredCoinsForSession = coins;
    activeTimer = 0;
    Serial.printf("[S3B] Time %us = %u coins accepted\n", sec, (unsigned)coins);
    enterState(STATE_INSERT_COIN);
}

// A button or numpad result from B1 (queued by the radio callback, handled here in loop()).
void handleTouch(const char* a, uint16_t v) {
    MachineState st = currentMachineState;
    Serial.printf("[S3B TOUCH] '%s' value=%u in %s\n", a, v, stateName(st));

    if (!strcmp(a, "TAP_ADVANCE")) {
        if (st == STATE_TAPS) beginSession();
        else if (st == STATE_WELCOME) enterState(refillBypass ? STATE_SELECT_DRY_ONLY : STATE_SELECT_MODE);
    } else if (!strcmp(a, "MODE_CLEAN")) {
        if (st == STATE_SELECT_MODE) { sessionMode = MODE_CLEAN; Serial.println("[S3B] Mode: CLEANING"); enterState(STATE_RATE_A); }
    } else if (!strcmp(a, "MODE_DRY")) {
        if (st == STATE_SELECT_MODE || st == STATE_SELECT_DRY_ONLY) { sessionMode = MODE_DRY; Serial.println("[S3B] Mode: DRYING"); enterState(STATE_RATE_A); }
    } else if (!strcmp(a, "ACTION")) {                  // CONFIRM on the rating screens: v = the rating picked
        if (st == STATE_RATE_A || st == STATE_RATE_B1 || st == STATE_RATE_B) {
            if (v < 1 || v > 10) { sendBuzzer(60); return; }   // nothing picked yet - ask again
            if (st == STATE_RATE_A) {
                ratingA = (uint8_t)v;
                Serial.printf("[S3B] Rating before: %u/10\n", ratingA);
                enterState(sessionMode == MODE_CLEAN ? STATE_METHOD : STATE_TIME_ALLOT);
            } else if (st == STATE_RATE_B1) {
                ratingDry = (uint8_t)v;
                Serial.printf("[S3B] Dryness after the clean: %u/10\n", ratingDry);
                enterState(STATE_RATE_B);
            } else {
                ratingB = (uint8_t)v;
                Serial.printf("[S3B] Rating after: %u/10 (dryness after the clean %u)\n", ratingB, ratingDry);
                // either rating in the free range earns the retry: after a cleaning the odor OR the dryness, after a drying the dryness
                bool qualifies = ratingGetsFreeRetry(ratingB) || (sessionMode == MODE_CLEAN && ratingGetsFreeRetry(ratingDry));
                if (!retryUsed && qualifies) { retryUsed = true; enterState(STATE_FREE_RETRY); }
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
        if (st == STATE_TIME_ALLOT) enterState(sessionMode == MODE_CLEAN ? STATE_METHOD : STATE_RATE_A);
    } else if (!strcmp(a, "NP_CANCEL")) {
        if (st == STATE_TIME_ALLOT) enterState(STATE_TAPS);
    } else if (!strcmp(a, "WARN_OK")) {
        if (st == STATE_TIME_ALLOT && timeWarnL1[0]) { timeWarnL1[0] = '\0'; showScreen(); }   // numpad again, with the typed time
    } else if (!strcmp(a, "INSERT_CANCEL")) {
        if (st == STATE_INSERT_COIN) { returnState = STATE_INSERT_COIN; enterState(STATE_CANCEL_CONFIRM); }
    } else if (!strcmp(a, "CANCEL_YES")) {
        if (st == STATE_CANCEL_CONFIRM) { Serial.println("[S3B] Cancelled by the customer"); enterState(STATE_TAPS); }
    } else if (!strcmp(a, "CANCEL_NO")) {
        if (st == STATE_CANCEL_CONFIRM) enterState(returnState);
    } else if (!strcmp(a, "ABORT")) {
        if (st == STATE_CLEANING || st == STATE_DRYING) { returnState = st; enterState(STATE_ABORT_CONFIRM); }
    } else if (!strcmp(a, "ABORT_YES")) {
        if (st == STATE_ABORT_CONFIRM) {
            if (returnState == STATE_DRYING) {
                Serial.println("[S3B] Drying aborted by the customer -> cool down first");
                beginCoolDown();
            } else {
                Serial.println("[S3B] Cleaning aborted by the customer");
                activeTimer = 0;
                enterState(STATE_RETRIEVE);
            }
        }
    } else if (!strcmp(a, "ABORT_NO")) {
        if (st == STATE_ABORT_CONFIRM) enterState(returnState);     // back to the cleaning / drying, time and bar untouched
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
        else Serial.println("[S3B] Maintenance refused: only possible from the idle screen");
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
            if (!systemReady()) { goOutOfOrder(b2Alive() ? "relay board B3 is not answering" : "sensor board B2 is not answering"); break; }
            if (alcoholEnough()) enterState(STATE_CHECKING_SCN);
            else if (refillBypass) enterState(STATE_WELCOME);                      // already gave up: cleaning stays unavailable, drying works
            else enterState(STATE_REFILLING);
            break;

        case STATE_CHECKING_SCN:
            if (!systemReady()) { goOutOfOrder("a node is not answering"); break; }
            if (scentedEnough()) {
                if (refillBypass) { refillBypass = false; saveBypass(); Serial.println("[S3B] Both containers are fine again - CLEANING is available"); }
                enterState(STATE_WELCOME);
            }
            else if (refillBypass) enterState(STATE_WELCOME);
            else enterState(STATE_REFILLING_SCN);
            break;

        case STATE_REFILLING:
        case STATE_REFILLING_SCN: {
            bool alc = (currentMachineState == STATE_REFILLING);
            bool ok = alc ? (alcoholRefilled() || wsWet[0]) : (scentedRefilled() || wsWet[1]);
            if (ok) { Serial.println("[S3B] Container refilled"); cancelRefill(alc ? 0 : 1); enterState(alc ? STATE_CHECKING : STATE_CHECKING_SCN); break; }
            if (++refillElapsed >= refillIntervalSec) {
                if (refillTry >= refillTriesMax) {
                    refillBypass = true; saveBypass();
                    Serial.println("[S3B] Refill gave up after the last try - CLEANING is unavailable, DRYING still works");
                    enterState(STATE_WELCOME);
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
            // Helmet seen inside -> on to CLOSE DOOR, whether the door is still open or a quick customer has already shut it.
            bool placed = holdTicks(holdA, hp, 2);
            bool shutEmpty = holdTicks(holdB, closed && !hp, 2);
            if (placed) enterState(STATE_CLOSE_DOOR_A);
            else if (shutEmpty) enterState(STATE_HELMET_NOTICE_A);
            break;
        }

        case STATE_CLOSE_DOOR_A: {
            bool go = holdTicks(holdA, closed && hp, 2);
            bool empty = holdTicks(holdB, closed && !hp, 2);
            if (go) beginRun();
            else if (empty) enterState(STATE_HELMET_NOTICE_A);
            break;
        }

        case STATE_HELMET_NOTICE_A:                    // "HELMET NO DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(STATE_INSTRUCTIONS);
            break;

        case STATE_CLEANING:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: UV, mist and heater off at once
                Serial.println("[S3B] SAFETY: the door opened during cleaning");
                returnState = STATE_CLEANING;
                enterState(STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                cleanElapsedSec++;
                if (activeTimer == 0) finishCleaning();
            }
            break;

        case STATE_DRYING:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: heater and UV off at once
                Serial.println("[S3B] SAFETY: the door opened during drying");
                returnState = STATE_DRYING;
                enterState(STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) finishDrying();
            }
            break;

        case STATE_COOL_DOWN:                          // nothing runs; the enclosure stays locked until the wait is over
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) { Serial.println("[S3B] Cool down complete -> open the door"); enterState(STATE_RETRIEVE); }
            }
            break;

        case STATE_PAUSED_SAFETY: {
            bool resume = holdTicks(holdA, closed && hp, 2);
            bool needIn = holdTicks(holdB, closed && !hp, 2);
            if (resume) { Serial.println("[S3B] Safety restored - carrying on"); unlockForPause = false; enterState(returnState); break; }
            if (needIn && !unlockForPause) { Serial.println("[S3B] Door shut but no helmet - unlocking so it can be reopened"); unlockForPause = true; unlockHeldSec = 0; }
            if (unlockForPause && holdTicks(holdC, open, 2)) unlockForPause = false;     // release once it is genuinely open
            break;
        }

        case STATE_RETRIEVE:                           // OPEN B
            if (holdTicks(holdA, open, 2)) enterState(STATE_TAKE_HELMET);
            break;

        case STATE_TAKE_HELMET: {                      // SENSORS B
            // Helmet gone -> on to CLOSE DOOR (which asks for the door if it is still open, and carries straight on if it is already shut).
            bool out = holdTicks(holdA, !hp, 2);
            bool still = holdTicks(holdB, hp && closed, 2);
            if (out) enterState(STATE_CLOSE_DOOR_B);
            else if (still) enterState(STATE_HELMET_NOTICE_B);
            break;
        }

        case STATE_CLOSE_DOOR_B: {
            bool done = holdTicks(holdA, closed && !hp, 2);
            bool back = holdTicks(holdB, closed && hp, 2);
            if (done) {
                if (retryUsed) enterState(STATE_FINISH);                                         // no rating after the free retry
                else enterState(sessionMode == MODE_CLEAN ? STATE_RATE_B1 : STATE_RATE_B);
            }
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

        case STATE_OUT_OF_ORDER:                       // a node went silent; clears itself when both answer again
            if (systemReady()) {
                Serial.println("[S3B] Back in order");
                enterState(STATE_TAPS);
            }
            break;

        case STATE_TAPS:                               // idle: keep asking ACS 2 quietly while CLEANING is greyed out
            bypassTick();
            break;

        default:                                       // touch-driven or idle states
            break;
    }
}

// ---------------------------------------------------------------------
// Coins
// ---------------------------------------------------------------------
bool coinsAcceptedNow() { return coinPowerWanted(); }

void processPulses() {
    uint32_t pc; bool seen;
    portENTER_CRITICAL(&rxMux);
    pc = b1Pulses; seen = b1Seen;
    portEXIT_CRITICAL(&rxMux);
    if (!seen) return;
    if (pc > lastKnownPulsesB1) {
        uint32_t newPulses = pc - lastKnownPulsesB1;
        lastKnownPulsesB1 = pc;
        prefs.putUInt("last_b1_pulses", lastKnownPulsesB1);
        if (coinsAcceptedNow()) {
            uint32_t added = newPulses * secondsPerCoin;
            activeTimer += added;
            sessionCoins += newPulses;
            if (currentMachineState == STATE_CLEANING || currentMachineState == STATE_DRYING) {
                runTotalSec = (runTotalSec + added > 9999UL) ? 9999UL : runTotalSec + added;
                needScreenRedraw = true;               // the bar's total grew
                coinFlag = true;
            }
            Serial.printf("[S3B COIN] +%lus. activeTimer = %lus, session coins %lu\n", (unsigned long)added, (unsigned long)activeTimer, (unsigned long)sessionCoins);
        } else {
            Serial.printf("[S3B COIN] %lu pulse(s) ignored in %s (the acceptor should be off)\n", (unsigned long)newPulses, stateName(currentMachineState));
        }
    } else if (pc < lastKnownPulsesB1) {
        // B1's counter went back down (it rebooted or was reset) - resync rather than double-credit
        lastKnownPulsesB1 = pc;
        prefs.putUInt("last_b1_pulses", lastKnownPulsesB1);
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
        if (cmd.targetDeviceID != DEVICE_S3B) return;
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
        if (p.deviceID == DEVICE_B1) {
            portENTER_CRITICAL(&rxMux);
            b1Pulses = p.pulseCount; b1Seen = true;
            portEXIT_CRITICAL(&rxMux);
        }
        return;
    }
    if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, incomingData, sizeof(A2SensorPacket));
        if (p.deviceID != DEVICE_B2) return;
        portENTER_CRITICAL(&rxMux);
        nodeB2 = p; lastB2Ms = millis();
        portEXIT_CRITICAL(&rxMux);
        return;
    }
    if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, incomingData, sizeof(A3StatusPacket));
        if (p.deviceID != DEVICE_B3) return;
        portENTER_CRITICAL(&rxMux);
        b3Mask = p.relayMask; b3Orphaned = p.orphaned; lastB3Ms = millis();
        portEXIT_CRITICAL(&rxMux);
    }
}

void handleQueuedCommand(const CommandPacket& cmd) {
    if (cmd.commandID == CMD_TOUCH_ACTION) {
        handleTouch(cmd.payloadStr, cmd.param16);
    } else if (cmd.commandID == CMD_SET_MAINTENANCE) {
        handleMaintenance(cmd.state ? 1 : 0);
    } else if (cmd.commandID == CMD_SET_RELAY_MASK) {
        if (currentMachineState == STATE_MAINTENANCE) { maintMask = cmd.param16 & B3_MASK_ALL; applyDesiredMask(); }
    } else if (cmd.commandID == CMD_TFT_HARD_REFRESH) {
        // "Force refresh" from the dashboard: restart B1's touch screen and redraw the current page (cures a white or garbled screen)
        b1HardRefreshPending = true;
        b1RedrawAtMillis = millis() + 50;
        Serial.println("[S3B] screen refresh requested from the dashboard");
    } else if (cmd.commandID == CMD_SET_CONFIG) {
        bool ok = false;
        if (cmd.subIndex == 1) ok = applyRulesCsv(cmd.payloadStr);
        else if (cmd.subIndex == 2) ok = applyLevelsCsv(cmd.payloadStr);
        else if (cmd.subIndex == 3) ok = applyPhasesCsv(cmd.payloadStr);
        else if (cmd.subIndex == 4) ok = applyDryingCsv(cmd.payloadStr);
        else if (cmd.subIndex == 5) ok = applyRefreshCsv(cmd.payloadStr);
        else if (cmd.subIndex == 6) ok = applyAutoRefreshCsv(cmd.payloadStr);
        else {   // the old "helmCm,lowPct,highPct,secPerCoin,minCoins[,maxCoins]" line: only the helmet distance and max coins still mean anything
            float v[6];
            int n = parseFloats(cmd.payloadStr, v, 6);
            if (n == 5 || n == 6) { if (v[0] >= 1 && v[0] <= 200) helmetDetectCm = v[0]; if (n == 6 && v[5] >= 1) maxCoins = (uint16_t)v[5]; ok = true; }
        }
        if (ok) { saveConfigToNVS(); Serial.printf("[S3B CONFIG] Updated & saved (type %u)\n", cmd.subIndex); }
        else Serial.printf("[S3B CONFIG] REJECTED (type %u): '%s'\n", cmd.subIndex, cmd.payloadStr);
    }
}

// ---------------------------------------------------------------------
// USB serial console - tune and inspect without a dashboard
// ---------------------------------------------------------------------
void printConfig() {
    char r[200], l[200], p[200];
    rulesToCsv(r, sizeof(r)); levelsToCsv(l, sizeof(l)); phasesToCsv(p, sizeof(p));
    Serial.println("---- S3B settings ----");
    Serial.printf("state: %s | B2 %s | B3 %s | cleaning %s (bypass %s) | level checks %s | ACS requests %s\n", stateName(currentMachineState),
                  b2Alive() ? "ok" : "SILENT", b3Alive() ? "ok" : "SILENT", refillBypass ? "UNAVAILABLE" : "available", refillBypass ? "ON" : "off",
                  levelChecksOn ? "ON" : "off", acsRequests ? "ON" : "off");
    Serial.printf("rules : bands %u-%u=%um, %u-%u=%um, %u-%u=%um, %u-%u=%um | min %u min | %u coin(s) = %.2f %s (%.2f s/coin) | max %u coins | free retry %s from %u for %u min\n",
                  bandFrom[0], bandTo[0], bandMinutes[0], bandFrom[1], bandTo[1], bandMinutes[1], bandFrom[2], bandTo[2], bandMinutes[2], bandFrom[3], bandTo[3], bandMinutes[3],
                  minMinutes, rateCoins, (double)rateValue, rateUnitMin ? "min" : "s", (double)secPerCoinF, maxCoins, freeEnabled ? "ON" : "off", freeMinRating, freeMinutes);
    Serial.printf("levels: alcohol low %.1f / high %.1f cm | scented low %.1f / high %.1f cm | helmet < %.1f cm | tries %u x %u s\n",
                  (double)alcLowCm, (double)alcHighCm, (double)sctLowCm, (double)sctHighCm, (double)helmetDetectCm, refillTriesMax, refillIntervalSec);
    Serial.printf("water : alcohol wet>=%u dry<=%u | scented wet>=%u dry<=%u | wet is %s\n", wsEnter[0], wsLeave[0], wsEnter[1], wsLeave[1], wsWetHigher ? "HIGHER" : "LOWER");
    static const char* const UNIT_NAME[3] = {"%", "s", "min"};
    Serial.printf("phases: PAID %s start %.2f%s end %.2f%s | hum1 %u hum2 %u hum3 %u uv1 %u uv2 %u heater %u\n", phPaid.on ? "PHASED" : "plain(no heater)",
                  (double)phPaid.startVal, UNIT_NAME[phPaid.startUnit % 3], (double)phPaid.endVal, UNIT_NAME[phPaid.endUnit % 3],
                  phPaid.pat[0], phPaid.pat[1], phPaid.pat[2], phPaid.pat[3], phPaid.pat[4], phPaid.pat[5]);
    Serial.printf("        FREE %s start %.2f%s end %.2f%s | hum1 %u hum2 %u hum3 %u uv1 %u uv2 %u heater %u   (1 start, 2 middle, 4 end)\n", phFree.on ? "PHASED" : "plain(no heater)",
                  (double)phFree.startVal, UNIT_NAME[phFree.startUnit % 3], (double)phFree.endVal, UNIT_NAME[phFree.endUnit % 3],
                  phFree.pat[0], phFree.pat[1], phFree.pat[2], phFree.pat[3], phFree.pat[4], phFree.pat[5]);
    Serial.printf("drying: cool down %u s\n", coolDownSec);
    Serial.printf("LIVE  : helmet %.1f cm | MC alcohol %.1f cm | MC scented %.1f cm | water raw %u / %u -> %s / %s | door %s | relays 0x%04X\n",
                  (double)b2snap.usHelmetCm, (double)b2snap.usMcAlsCm, (double)b2snap.usMcSlsCm, b2snap.wsAlsRaw, b2snap.wsSlsRaw,
                  wsWet[0] ? "WET" : "dry", wsWet[1] ? "WET" : "dry", b2snap.doorEnclosure ? "OPEN" : "shut", (unsigned)b3Mask);
    Serial.printf("csv   : rules=%s\n        levels=%s\n        phases=%s\n", r, l, p);
}

// parses "<value> <pct|sec|min>" into a start/end pair
bool parseLength(const char* val, const char* unit, float& outVal, uint8_t& outUnit) {
    if (!val || !unit) return false;
    uint8_t u = !strcmp(unit, "pct") ? 0 : (!strcmp(unit, "sec") ? 1 : (!strcmp(unit, "min") ? 2 : 255));
    if (u == 255) return false;
    outVal = atof(val); outUnit = u;
    return true;
}

void handleSerialLine(char* line) {
    char* cmd = strtok(line, " \t\r\n");
    if (!cmd) return;
    char* t[8];
    for (uint8_t i = 0; i < 8; i++) t[i] = strtok(nullptr, " \t\r\n");
    char* a1 = t[0]; char* a2 = t[1]; char* a3 = t[2];
    if (!strcmp(cmd, "help")) {
        Serial.println("Commands:  cfg | reset | bypass 0/1 | set <name> <value> | set bandN <from> <to> <minutes>  (N = 1..4)");
        Serial.println("Names:  checks 0/1, alclow, alchigh, sctlow, scthigh (cm), helm (cm), wsa_in, wsa_out, wss_in, wss_out (raw), wshigher 0/1,");
        Serial.println("        tries, interval (s), acs 0/1, minmin, coins, timevalue, unitmin 0/1, maxcoins, freeon 0/1, freerating, freemin, cooldown (s)");
        Serial.println("Cleaning phases:  set paidphase 0/1 | set freephase 0/1 | set pstart|pend|fstart|fend <value> <pct|sec|min>");
        Serial.println("        set ppat|fpat <hum1> <hum2> <hum3> <uv1> <uv2> <heater>   (each 0-7: 1 = start, 2 = middle, 4 = end, added together)");
        return;
    }
    if (!strcmp(cmd, "cfg")) { printConfig(); return; }
    if (!strcmp(cmd, "reset")) { enterState(STATE_TAPS); return; }
    if (!strcmp(cmd, "bypass") && a1) { refillBypass = atoi(a1) != 0; saveBypass(); Serial.printf("cleaning is now %s\n", refillBypass ? "UNAVAILABLE" : "available"); return; }
    if (!strcmp(cmd, "set") && a1 && a2) {
        float v = atof(a2);
        if (!strcmp(a1, "paidphase")) phPaid.on = v != 0;
        else if (!strcmp(a1, "freephase")) phFree.on = v != 0;
        else if (!strcmp(a1, "pstart") || !strcmp(a1, "pend") || !strcmp(a1, "fstart") || !strcmp(a1, "fend")) {
            float val; uint8_t u;
            if (!parseLength(a2, a3, val, u)) { Serial.println("usage: set pstart|pend|fstart|fend <value> <pct|sec|min>"); return; }
            PhaseSet& ph = (a1[0] == 'p') ? phPaid : phFree;
            if (!strcmp(a1 + 1, "start")) { ph.startVal = val; ph.startUnit = u; } else { ph.endVal = val; ph.endUnit = u; }
        }
        else if (!strcmp(a1, "ppat") || !strcmp(a1, "fpat")) {
            if (!a3 || !t[3] || !t[4] || !t[5] || !t[6]) { Serial.println("usage: set ppat|fpat <hum1> <hum2> <hum3> <uv1> <uv2> <heater>"); return; }
            PhaseSet& ph = (a1[0] == 'p') ? phPaid : phFree;
            for (uint8_t i = 0; i < 6; i++) ph.pat[i] = (uint8_t)atoi(t[1 + i]) & 7;
        }
        else if (!strncmp(a1, "band", 4) && a1[4] >= '1' && a1[4] <= '4' && a3) {
            uint8_t i = a1[4] - '1';
            bandFrom[i] = (uint8_t)atoi(a2); bandTo[i] = (uint8_t)atoi(a3);
            if (t[3]) bandMinutes[i] = (uint8_t)atoi(t[3]);
        }
        else if (!strcmp(a1, "checks")) levelChecksOn = v != 0;
        else if (!strcmp(a1, "alclow")) alcLowCm = v;
        else if (!strcmp(a1, "alchigh")) alcHighCm = v;
        else if (!strcmp(a1, "sctlow")) sctLowCm = v;
        else if (!strcmp(a1, "scthigh")) sctHighCm = v;
        else if (!strcmp(a1, "helm")) helmetDetectCm = v;
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
        else if (!strcmp(a1, "cooldown")) { if (v < 5 || v > 900) { Serial.println("cooldown must be 5-900 seconds"); return; } coolDownSec = (uint16_t)(v + 0.5f); }
        else { Serial.printf("unknown setting '%s' - type help\n", a1); return; }
        recomputeDerived();
        saveConfigToNVS();
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
    packet.deviceID               = DEVICE_S3B;
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

    // fresh copy of B2's report for this tick
    portENTER_CRITICAL(&rxMux);
    b2snap = nodeB2;
    portEXIT_CRITICAL(&rxMux);
    if (!b2Alive()) {
        // Sensor board silent: never keep acting on its last readings. "Door open, no helmet" is the safe way to be wrong - a cleaning or drying
        // in progress pauses with everything off until B2 is back.
        b2snap.doorEnclosure = true;
        b2snap.usHelmetCm = 0.0f;
    }
    updateWaterVerdicts();

    // enclosure solenoid duty cap
    bool lockWanted = (currentMachineState == STATE_INSTRUCTIONS || currentMachineState == STATE_RETRIEVE ||
                       (currentMachineState == STATE_PAUSED_SAFETY && unlockForPause) ||
                       (currentMachineState == STATE_MAINTENANCE && (maintMask & A3_BIT(A3_RELAY_ENCLOSURE_LOCK))));
    if (lockWanted) {
        if (!lockResting) {
            if (++unlockHeldSec >= LOCK_MAX_ON_SEC) { lockResting = true; lockRestSec = 0; Serial.println("[S3B] Enclosure lock rests for a few seconds (solenoid duty cap)"); }
        } else if (++lockRestSec >= LOCK_REST_SEC) {
            lockResting = false; unlockHeldSec = 0;
        }
    } else {
        unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
    }

    // MAINTENANCE: a heater switched on by hand turns itself off after 5 minutes, so a forgotten test cannot overheat the box
    if (currentMachineState == STATE_MAINTENANCE && (maintMask & A3_BIT(B3_RELAY_HEATER))) {
        if (++maintHeaterSec >= MAINT_HEATER_MAX_SEC) {
            maintMask &= (uint16_t)(~A3_BIT(B3_RELAY_HEATER));
            maintHeaterSec = 0;
            Serial.println("[S3B] Maintenance: the heater was on for 5 minutes - switched off");
        }
    } else {
        maintHeaterSec = 0;
    }

    tickState();
    applyDesiredMask();                       // picks up the lock duty cap, the max-coins cutoff and the cleaning phases
    if (currentMachineState == STATE_CLEANING) syncCleanStatus(false);

    if (coinFlag) { coinFlag = false; pushStatus("COIN INSERTED"); }

    // The once-a-second broadcasts. Small gaps: several back-to-back sends can overflow ESP-NOW's queue.
    sendCmd(DEVICE_B1, CMD_SYNC_VARS, (uint8_t)min(minCoinsRequired, (uint32_t)255), (uint16_t)activeTimer, (uint8_t)min(requiredCoinsForSession, (uint32_t)255));
    delay(2);
    sendCmd(0, CMD_HEARTBEAT, DEVICE_S3B, 0, 0);
    delay(2);
    broadcastStatus();
    delay(2);

    if (now - lastMaskSentMs >= MASK_REASSERT_MS) { sendMask(); delay(2); }
    if (now - lastCfgCsvMs >= 2000) { lastCfgCsvMs = now; broadcastSettingsCsv(); }
    if (now - lastTokenMs >= 5000) {          // numbers B1 may have missed or lost in a reboot
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
    Serial.println("   NODE S3B: BOX 3 (CLEANING + DRYING) INTERNAL SERVER MANAGEMENT (ESP32-S3)");
    Serial.println("   Type 'help' for the console.");
    Serial.println("=======================================================");

    memset(&nodeB2, 0, sizeof(nodeB2));
    memset(&b2snap, 0, sizeof(b2snap));
    loadConfigFromNVS();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[S3B] ESP-NOW: Initialization FAILED!");
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

    printConfig();
    enterState(STATE_TAPS);
    sendMask();
    sendToken(TOKEN_MAXCOINS, maxCoins);
    sendToken(TOKEN_TRIES, refillTriesMax);
}

// The periodic refresh skips the screens where the customer is typing the time or picking a rating: the terminal keeps what was typed / picked, and a refresh would wipe it.
bool autoRefreshAllowedNow() {
    return !(currentMachineState == STATE_RATE_A || currentMachineState == STATE_TIME_ALLOT || currentMachineState == STATE_RATE_B || currentMachineState == STATE_RATE_B1);
}

void loop() {
    unsigned long now = millis();

    // queued commands from B1 / a dashboard
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

    // periodic hard refresh of B1 (setting "Auto hard refresh"): 0 = never; also not while another repaint is already pending
    if (autoRefreshSec > 0 && now - lastAutoRefreshMs >= (unsigned long)autoRefreshSec * 1000UL) {
        lastAutoRefreshMs = now;
        if (b1RedrawAtMillis == 0 && autoRefreshAllowedNow()) { b1HardRefreshPending = true; b1RedrawAtMillis = now + 1; }
    }

    // post-solenoid / post-relay repaint of B1 (the EMI-glitch mitigation), or a requested one
    if (b1RedrawAtMillis != 0 && now >= b1RedrawAtMillis) {
        b1RedrawAtMillis = 0;
        needScreenRedraw = false;
        if (b1HardRefreshPending) {
            b1HardRefreshPending = false;
            sendCmd(DEVICE_B1, CMD_TFT_HARD_REFRESH, 0, 0, anyTapFlagForCurrentState(), screenForCurrentState());
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
