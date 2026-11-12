#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "S3B_Screens.h"

// =====================================================================
// NODE S3B - BOX 3 (CLEANING + DRYING) INTERNAL SERVER MANAGEMENT (ESP32-S3)          (core 3.3.7)
// REWRITTEN 2026-11-03 for the trimmed 36-step Box 3 flow (BOX 3 STEP corrected.txt, Designer preset NEW FLOW) and the Refilling Station. NOT YET FLASHED.
// (The earlier version, a copy of Box 1's S3A with ratings, a time picker, mist choice, free retry and ACS requests, is archived as ESP32_S3B_before_trim.ino
// in the session scratchpad.)
//
// Runs the whole customer journey by itself (no LocalServer, no Refilling Station needed to keep running):
//   TAPS -> tap -> CHECKING (2 s, "Checking Main Container") -> WELCOME -> tap -> SELECT MODE (or SELECT DRY ONLY, CLEANING greyed out)
//     CLEANING: INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> CLEANING (UV + 3 humidifiers, the time the coins bought) -> AUTO DRY (UV + heater,
//               autoDrySec, heater off for the last heaterOffTailSec) -> OPEN B -> SENSORS B -> CLOSE DOOR B -> FINISH
//     DRYING:   INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> START DRYING (a "Please Wait" re-check) -> DRYING (UV + heater, the time the coins bought)
//               -> COOL DOWN (coolDownSec, everything off) -> OPEN B -> SENSORS B -> CLOSE DOOR B -> FINISH
//   with ABORT (CANCEL button while cleaning / drying: coins are not refunded, a cleaning goes straight to AUTO DRY, a drying to COOL DOWN), CANCEL at INSERT
//   COIN, SAFETY PAUSE when the door opens mid-cycle, MAINTENANCE and OUT OF ORDER (B2 or B3 silent). OUT OF ORDER never traps a helmet: door shut + helmet
//   inside -> the enclosure lock is released and the buzzer repeats (100 ms / 500 ms by default) until the door is seen open (see oooUnlock below).
// Talks to: B1 (screen/touch/coins), B2 (sensors: A2SensorPacket - the helmet ultrasonic and the doors only; the containers moved to the Refilling Station),
// B3 (relays: one CMD_SET_RELAY_MASK, 13 bits), and LISTENS to the Refilling Station (RefillStatusPacket).
//
// THE REFILLING STATION: it works alone and broadcasts the level of every box's Main Container. The S3B only reads Box 3's (index 2). On the first tap the
// CHECKING screen runs 2 s; on the tap at WELCOME the S3B offers CLEANING only when the station was heard within refillOfflineSec (default 15 s) AND the
// container reads MID, HIGH or MAX; EMPTY, LOW, a sensor fault or a silent station = SELECT DRY ONLY (DRYING still works). Nothing here ever asks the station for liquid.
//
// COINS: no ratings, no time picker. INSERT COIN asks for the REQUIRED coins = one step of the coin rate (rateCoins, e.g. 15 coins); the cleaning / drying runs for the
// time the coins bought (coins x seconds per coin); more coins can be added while it runs, up to maxCoins; coins are not accepted during AUTO DRY and COOL DOWN.
//
// DYNAMIC SETTINGS (NVS, survive a LocalServer swap; a dashboard edits them with CMD_SET_CONFIG, S3B rebroadcasts them target 0 / state 3):
//   subIndex 1  RULES    : rateCoins, rateValue, rateUnitMin(1/0), maxCoins                                  (4 numbers)
//   subIndex 2  SENSORS  : helmetCm, refillOfflineSec                                                        (2 numbers)
//   subIndex 3  AUTO-DRY : autoDrySec, heaterOffTailSec                                                      (2 numbers)
//   subIndex 4  DRYING   : coolDownSec                                                                       (1 number)
//   subIndex 5  REFRESH  : on-mask, off-mask of relays whose switching forces a full B1 screen re-init       (2 numbers)
//   subIndex 6  AUTO REFRESH : seconds between automatic hard refreshes of B1 (0 = never, 60 max)            (1 number)
//   subIndex 7  BEEP     : OPEN A / OPEN B beep length ms, wait between beeps ms (0 = one beep at the start)  (2 numbers)
// COINS FOR THE LOCALSERVER: CoinBookPacket (running total of booked coins, every second) + one session record per paid session in BoxStatusPacket
// (finished, or aborted = Cancelled flag). A coin is booked when the INSERT COIN payment completes (a cancel there books nothing) or, if it is added during
// the cycle, at once. The raw pulses of B1 no longer decide the revenue.
// Everything can also be set from the USB serial console: type "help".
// =====================================================================

Preferences prefs;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------
uint16_t rateCoins        = 15;      // "N coins = X seconds/minutes" ...
float    rateValue        = 5.0f;
bool     rateUnitMin      = true;    // true = minutes, false = seconds
uint16_t maxCoins         = 90;      // most coins one session can take (90 coins = 30 min at 15 coins = 5 min)
float    helmetDetectCm   = 15.0f;   // helmet counts as inside when the helmet ultrasonic reads under this
uint16_t refillOfflineSec = 15;      // the Refilling Station counts as offline when it was not heard for this long
uint16_t autoDrySec       = 180;     // the drying after every cleaning (CL HEATING in the Designer)
uint16_t heaterOffTailSec = 30;      // the heater is off for the last seconds of the auto-dry
uint16_t coolDownSec      = 60;      // how long the passive COOL DOWN lasts (also after an abort of a drying)

// Derived from the rules by recomputeDerived()
float    secPerCoinF      = 20.0f;
uint16_t secondsPerCoin   = 20;
uint32_t minCoinsRequired = 15;      // = rateCoins: what INSERT COIN asks for

// ---------------------------------------------------------------------
// Session (one customer)
// ---------------------------------------------------------------------
const uint8_t MODE_NONE = 0, MODE_CLEAN = 1, MODE_DRY = 2;
MachineState currentMachineState = STATE_TAPS;
MachineState returnState         = STATE_TAPS;   // where ABORT / CANCEL / a safety pause "go back to"
uint8_t  sessionMode     = MODE_NONE;
uint32_t requiredCoinsForSession = 0;
uint32_t activeTimer     = 0;       // seconds left of the cleaning / auto-dry / drying / cool down (or seconds bought while paying)
uint32_t runTotalSec     = 0;       // length of the running step, for the progress bar (capped at 9999)
uint32_t coolTotalSec    = 0;
uint32_t sessionCoins    = 0;
// COIN SETTLE (2026-11-04): a 20-peso coin is 20 pulses that arrive over 1-2 s, and B1 reports its pulse count only once a second. INSERT COIN must therefore NOT
// move on the moment the required coins are reached (the rest of the coin was thrown away: 20 pesos bought 15 coins' time). It waits until the acceptor has
// finished counting: no new pulse seen for COIN_SETTLE_MS (longer than B1's 1 s report), at most COIN_SETTLE_MAX_MS after the required coins were reached.
// Then ALL the coins counted (up to maxCoins) become time and revenue.
const unsigned long COIN_SETTLE_MS     = 1500;
const unsigned long COIN_SETTLE_MAX_MS = 6000;
unsigned long coinReachedMs = 0;     // when sessionCoins first reached the required coins on this INSERT COIN screen (0 = not yet)
unsigned long coinLastPulseMs = 0;   // when the last new pulse was seen on this INSERT COIN screen
bool     lastUvOn = false, lastMistOn = false, lastHeatOn = false;   // what the status feed last announced
uint16_t stepRemainingSec = 0;
char     scrBuf[220];               // scratch for screens with live numbers baked in

// per-state debounce counters (2 ticks = 2 s)
uint8_t  holdA = 0, holdB = 0, holdC = 0;
// enclosure solenoid duty cap: never energized for more than LOCK_MAX_ON_SEC in a row
const uint16_t LOCK_MAX_ON_SEC = 60, LOCK_REST_SEC = 5;
uint16_t unlockHeldSec = 0, lockRestSec = 0;
bool     lockResting   = false;
bool     unlockForPause = false;    // PAUSED_SAFETY with the door shut and no helmet: let them back in
// OUT OF ORDER with a helmet shut inside (B2 or B3 silent, S3B itself alive): the helmet must not stay locked in. Door shut + helmet inside (2 s) -> the enclosure
// lock is released and the buzzer repeats (beepLenMs / beepWaitMs, like OPEN A / OPEN B) until the door is seen open or the helmet is gone. At most
// OOO_RELEASE_MAX_SEC per episode (B3's own watchdog uses three 60 s windows). If B2 is silent the door cannot be seen: ONE blind window of OOO_BLIND_SEC, no beep.
// (When S3B itself is dead, B3's watchdog releases the lock and B1 does the beeping - see those sketches.)
const uint16_t OOO_RELEASE_MAX_SEC = 180, OOO_BLIND_SEC = 15, OOO_DEFAULT_WAIT_MS = 500;
bool     oooUnlock = false, oooBeep = false, oooGaveUp = false, oooBlindDone = false;
uint16_t oooUnlockSec = 0;
uint16_t maintMask     = 0;         // MAINTENANCE: relays set by hand from the dashboard
// MAINTENANCE safety: a relay switched on by hand switches itself off again after 2 s, so a forgotten tap cannot leave a UV lamp, a humidifier, the heater or a lock running.
// Not for the coin power (it must stay on until switched off) and not for anything while the dashboard's "Bypass Safety 2 Seconds Relay" box is ticked
// (the heater still has its own 5 minute limit below).
const uint16_t MAINT_AUTOOFF_MS = 2000;
const uint16_t MAINT_LATCH_MASK = (uint16_t)(A3_BIT(A3_RELAY_COIN_POWER));
bool     maintBypass   = false;     // the "Bypass Safety 2 Seconds Relay" tick box (sent with every relay command; off whenever maintenance starts or ends)
uint32_t maintOnAtMs[16];           // when each hand-set relay was switched on
uint16_t maintHeaterSec = 0;       // how long the hand-set heater has been on; it switches itself off after MAINT_HEATER_MAX_SEC
const uint16_t MAINT_HEATER_MAX_SEC = 300;

// session statistics for a future dashboard (BoxStatusPacket)
uint32_t completedSessionSeq    = 0;
uint32_t sessionStartMillis     = 0;
uint32_t sessionCoinsAtStart    = 0;
uint16_t lastSessionDurationSec = 0;
uint16_t lastSessionCoins       = 0;      // coins of the session (low 15 bits) + SESSION_CANCELLED_FLAG when the customer aborted mid-cycle

// Coins booked for the LocalServer's revenue (CoinBookPacket). The coins of the INSERT COIN screen are booked when the payment is complete (a cancel there books
// nothing); coins added during the cycle are booked one by one. Cumulative, kept in NVS, sent every second.
uint32_t bookedCoins = 0;
uint16_t bookSeq     = 0;
bool     sessionPaid = false;             // the INSERT COIN payment of this customer is complete

// OPEN A / OPEN B beep: beep for beepLenMs, wait beepWaitMs, repeat until the step ends. beepWaitMs 0 = one beep when the step starts.
// Set from the LocalServer (CMD_SET_CONFIG subIndex 7), kept in NVS.
uint16_t beepLenMs  = 100;
uint16_t beepWaitMs = 500;
unsigned long lastBeepMs = 0;

// ---------------------------------------------------------------------
// Live data from the other nodes (written by the receive callback, snapshotted in loop())
// ---------------------------------------------------------------------
volatile uint32_t b1Pulses = 0;     volatile bool b1Seen = false;
A2SensorPacket    nodeB2;           uint32_t lastB2Ms = 0;
uint16_t          b3Mask = 0;       uint32_t lastB3Ms = 0;  bool b3Orphaned = false;
A2SensorPacket    b2snap;           // the copy the state machine reads
RefillStatusPacket nodeStation;     uint32_t lastStationMs = 0;      // the Refilling Station's last broadcast (0 = never heard)
RefillStatusPacket stSnap;          // the copy the state machine reads
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
// Set from the dashboard (CMD_SET_CONFIG subIndex 6), kept in NVS.
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

void recomputeDerived() {
    if (rateCoins < 1) rateCoins = 1;
    if (rateValue < 0.1f) rateValue = 0.1f;
    secPerCoinF = rateValue * (rateUnitMin ? 60.0f : 1.0f) / (float)rateCoins;
    long r = lroundf(secPerCoinF);
    secondsPerCoin = (uint16_t)(r < 1 ? 1 : r);
    minCoinsRequired = rateCoins;
}

const char* stateName(MachineState s) {
    switch (s) {
        case STATE_TAPS:            return "TAPS";
        case STATE_CHECKING:        return "CHECKING";
        case STATE_WELCOME:         return "WELCOME";
        case STATE_SELECT_MODE:     return "SELECT MODE";
        case STATE_SELECT_DRY_ONLY: return "SELECT DRY ONLY";
        case STATE_INSERT_COIN:     return "INSERT COIN";
        case STATE_INSTRUCTIONS:    return "OPEN A";
        case STATE_PLACE_HELMET:    return "SENSORS A";
        case STATE_CLOSE_DOOR_A:    return "CLOSE DOOR A";
        case STATE_CLEANING:        return "CLEANING";
        case STATE_AUTO_DRY:        return "AUTO DRY";
        case STATE_START_DRY:       return "START DRYING";
        case STATE_DRYING:          return "DRYING";
        case STATE_COOL_DOWN:       return "COOL DOWN";
        case STATE_PAUSED_SAFETY:   return "SAFETY PAUSE";
        case STATE_RETRIEVE:        return "OPEN B";
        case STATE_TAKE_HELMET:     return "SENSORS B";
        case STATE_CLOSE_DOOR_B:    return "CLOSE DOOR B";
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
    prefs.putUShort("rate_c", rateCoins);
    prefs.putFloat("rate_v", rateValue);
    prefs.putBool("rate_u", rateUnitMin);
    prefs.putUShort("max_c", maxCoins);
    prefs.putFloat("helm_cm", helmetDetectCm);
    prefs.putUShort("off_sec", refillOfflineSec);
    prefs.putUShort("ad_sec", autoDrySec);
    prefs.putUShort("tail_sec", heaterOffTailSec);
    prefs.putUShort("cd_sec", coolDownSec);
    prefs.putUShort("rf_on", refreshOnMask);
    prefs.putUShort("rf_off", refreshOffMask);
    prefs.putUShort("arf_sec", autoRefreshSec);
    prefs.putUShort("beep_len", beepLenMs);
    prefs.putUShort("beep_wait", beepWaitMs);
}

void loadConfigFromNVS() {
    prefs.begin("s3b_nvs", false);
    rateCoins        = prefs.getUShort("rate_c", rateCoins);
    rateValue        = prefs.getFloat("rate_v", rateValue);
    rateUnitMin      = prefs.getBool("rate_u", rateUnitMin);
    maxCoins         = prefs.getUShort("max_c", maxCoins);
    helmetDetectCm   = prefs.getFloat("helm_cm", helmetDetectCm);
    refillOfflineSec = prefs.getUShort("off_sec", refillOfflineSec);
    autoDrySec       = prefs.getUShort("ad_sec", autoDrySec);
    heaterOffTailSec = prefs.getUShort("tail_sec", heaterOffTailSec);
    coolDownSec      = prefs.getUShort("cd_sec", coolDownSec);
    refreshOnMask    = prefs.getUShort("rf_on", refreshOnMask);
    refreshOffMask   = prefs.getUShort("rf_off", refreshOffMask);
    autoRefreshSec   = prefs.getUShort("arf_sec", autoRefreshSec);
    beepLenMs        = prefs.getUShort("beep_len", beepLenMs);
    beepWaitMs       = prefs.getUShort("beep_wait", beepWaitMs);
    bookedCoins      = prefs.getUInt("booked", 0);              // survives a reboot: the LocalServer books the difference to what it last saw
    lastKnownPulsesB1 = prefs.getUInt("last_b1_pulses", 0);     // survives an S3B reboot mid-session
    // sanity after an old / damaged flash: the rate must buy the required coins, and the tail cannot be longer than the auto-dry
    if (maxCoins < rateCoins) maxCoins = rateCoins;
    if (heaterOffTailSec > autoDrySec) heaterOffTailSec = autoDrySec;
    recomputeDerived();
}

// ---------------------------------------------------------------------
// Settings as CSV (dashboard <-> S3B)
// ---------------------------------------------------------------------
void rulesToCsv(char* out, size_t n)   { snprintf(out, n, "%u,%.2f,%u,%u", rateCoins, (double)rateValue, rateUnitMin ? 1 : 0, maxCoins); }
void sensorsToCsv(char* out, size_t n) { snprintf(out, n, "%.1f,%u", (double)helmetDetectCm, refillOfflineSec); }
void autoDryToCsv(char* out, size_t n) { snprintf(out, n, "%u,%u", autoDrySec, heaterOffTailSec); }
void dryingToCsv(char* out, size_t n)  { snprintf(out, n, "%u", coolDownSec); }

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
    float v[4];
    if (parseFloats(s, v, 4) != 4) return false;
    if (v[0] < 1 || v[0] > 500 || v[1] < 0.1f || v[1] > 9999 || v[3] < 1 || v[3] > 999) return false;
    if (v[3] < v[0]) return false;                                   // the most coins must cover at least the required coins
    rateCoins = (uint16_t)v[0]; rateValue = v[1]; rateUnitMin = v[2] >= 0.5f; maxCoins = (uint16_t)v[3];
    recomputeDerived();
    return true;
}

bool applySensorsCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 1 || v[0] > 200 || v[1] < 5 || v[1] > 120) return false;
    helmetDetectCm = v[0]; refillOfflineSec = (uint16_t)(v[1] + 0.5f);
    return true;
}

bool applyAutoDryCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 10 || v[0] > 900 || v[1] < 0 || v[1] > v[0]) return false;
    autoDrySec = (uint16_t)(v[0] + 0.5f); heaterOffTailSec = (uint16_t)(v[1] + 0.5f);
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

// OPEN A / OPEN B beep (subIndex 7): beep length ms, wait between beeps ms (0 = a single beep when the step starts).
void beepToCsv(char* out, size_t n) { snprintf(out, n, "%u,%u", (unsigned)beepLenMs, (unsigned)beepWaitMs); }
bool applyBeepCsv(const char* s) {
    float v[2];
    if (parseFloats(s, v, 2) != 2) return false;
    if (v[0] < 20 || v[0] > 1000 || v[1] < 0 || v[1] > 10000) return false;
    beepLenMs = (uint16_t)(v[0] + 0.5f);
    beepWaitMs = (uint16_t)(v[1] + 0.5f);
    return true;
}

void broadcastSettingsCsv() {
    char buf[200];
    rulesToCsv(buf, sizeof(buf));    sendCmd(0, CMD_SET_CONFIG, 1, 0, 3, buf);   delay(2);   // state = 3: Box 3's copy (S3A sends 0, S3C 2)
    sensorsToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 2, 0, 3, buf);   delay(2);
    autoDryToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 3, 0, 3, buf);   delay(2);
    dryingToCsv(buf, sizeof(buf));   sendCmd(0, CMD_SET_CONFIG, 4, 0, 3, buf);   delay(2);
    refreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 5, 0, 3, buf);   delay(2);
    autoRefreshToCsv(buf, sizeof(buf));  sendCmd(0, CMD_SET_CONFIG, 6, 0, 3, buf);   delay(2);
    beepToCsv(buf, sizeof(buf));     sendCmd(0, CMD_SET_CONFIG, 7, 0, 3, buf);
}

// ---------------------------------------------------------------------
// Sensors and the Refilling Station (all read from the snapshots taken once per second)
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

// Box 3 is index 2 on the Refilling Station.
bool stationHeard() { return lastStationMs != 0 && (uint32_t)(millis() - lastStationMs) < (uint32_t)refillOfflineSec * 1000UL; }
uint8_t myContainerLevel() { return stationHeard() ? stSnap.state[2] : (uint8_t)REFILL_UNKNOWN; }
// CLEANING needs liquid: MID, HIGH or MAX. EMPTY, LOW, a sensor fault or a silent station = DRYING only.
bool cleaningAllowed() { uint8_t l = myContainerLevel(); return l <= REFILL_MAX && l >= REFILL_MID; }

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

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------
uint8_t anyTapFlagForCurrentState() {
    return (currentMachineState == STATE_TAPS || currentMachineState == STATE_WELCOME) ? 1 : 0;
}

const char* screenForCurrentState() {
    switch (currentMachineState) {
        case STATE_TAPS:            return SCR_TAPS;
        case STATE_CHECKING:        return SCR_CHECKING;
        case STATE_WELCOME:         return SCR_WELCOME;
        case STATE_SELECT_MODE:     return SCR_SELECT_MODE;
        case STATE_SELECT_DRY_ONLY: return SCR_SELECT_DRY_ONLY;
        case STATE_INSERT_COIN:     return SCR_INSERT_COIN;
        case STATE_INSTRUCTIONS:    return SCR_OPEN_A;
        case STATE_PLACE_HELMET:    return SCR_SENSORS_A;
        case STATE_CLOSE_DOOR_A:    return SCR_CLOSE_DOOR_A;
        case STATE_CLEANING:
            snprintf(scrBuf, sizeof(scrBuf), SCR_CLEANING, (unsigned long)runTotalSec);
            return scrBuf;
        case STATE_AUTO_DRY:
            snprintf(scrBuf, sizeof(scrBuf), SCR_AUTO_DRY, (unsigned long)runTotalSec);
            return scrBuf;
        case STATE_START_DRY:       return SCR_START_DRY;
        case STATE_DRYING:
            snprintf(scrBuf, sizeof(scrBuf), SCR_DRYING, (unsigned long)runTotalSec);
            return scrBuf;
        case STATE_COOL_DOWN:
            snprintf(scrBuf, sizeof(scrBuf), SCR_COOLDOWN, (unsigned long)coolTotalSec);
            return scrBuf;
        case STATE_PAUSED_SAFETY:   return SCR_SAFETY_PAUSE;
        case STATE_RETRIEVE:        return SCR_OPEN_B;
        case STATE_TAKE_HELMET:     return SCR_SENSORS_B;
        case STATE_CLOSE_DOOR_B:    return SCR_CLOSE_DOOR_B;
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
bool coinPowerWanted() {
    return (currentMachineState == STATE_INSERT_COIN) ||
           ((currentMachineState == STATE_CLEANING || currentMachineState == STATE_DRYING) && sessionCoins < maxCoins);
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
        case STATE_OUT_OF_ORDER:                                      // nothing runs; only a helmet shut inside gets the lock released (tickState decides)
            unlockWanted = oooUnlock;
            break;
        case STATE_CLEANING:                                          // UV 1 + UV 2 + the three humidifiers, the whole time; the heater never runs with the mist
            m = A3_MASK_UV | A3_BIT(A3_RELAY_HUMIDIFIER_1) | A3_BIT(A3_RELAY_HUMIDIFIER_2) | A3_BIT(A3_RELAY_HUMIDIFIER_3);
            break;
        case STATE_AUTO_DRY:                                          // UV + the heater, the heater off for the last heaterOffTailSec seconds
            m = A3_MASK_UV;
            if (activeTimer > heaterOffTailSec) m |= A3_BIT(B3_RELAY_HEATER);
            break;
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

// OUT OF ORDER: let a trapped helmet out (the enclosure lock is released by desiredMaskForState while oooUnlock is set).
void startOooRelease(const char* why) {
    oooUnlock = true; oooUnlockSec = 0;
    unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
    lastBeepMs = 0;                                           // the first beep comes at once
    Serial.printf("[S3B] OUT OF ORDER: enclosure UNLOCKED (%s)\n", why);
}

// AFTER A REBOOT (a power cut takes the S3 down while B1 / B2 / B3 keep running on the UPS): a helmet that is still shut inside is handed back BEFORE anybody
// is served - otherwise the S3's first relay mask (all off) locks it in again, and the next customer's cycle would run on somebody else's helmet. Done once: from
// BOOT_CHECK_AFTER_SEC after power-up, for BOOT_CHECK_WINDOW_SEC, while nobody has paid yet; door shut + helmet inside for 3 s -> OPEN B (unlock, beep, then take the
// helmet out, close the door, FINISH, TAPS).
const uint32_t BOOT_CHECK_AFTER_SEC = 10, BOOT_CHECK_WINDOW_SEC = 8;
bool    bootCheckDone = false;
uint8_t bootHold = 0;
void bootHelmetCheck(bool closed, bool hp) {
    if (bootCheckDone) return;
    uint32_t up = millis() / 1000UL;
    if (up < BOOT_CHECK_AFTER_SEC) return;
    if (up >= BOOT_CHECK_AFTER_SEC + BOOT_CHECK_WINDOW_SEC) { bootCheckDone = true; return; }
    bool beforePayment = currentMachineState == STATE_TAPS || currentMachineState == STATE_CHECKING || currentMachineState == STATE_WELCOME ||
                         currentMachineState == STATE_SELECT_MODE || currentMachineState == STATE_SELECT_DRY_ONLY;
    if (!beforePayment || !systemReady()) { bootHold = 0; return; }
    if (holdTicks(bootHold, closed && hp, 3)) {
        bootCheckDone = true;
        Serial.println("[S3B] Reboot with a helmet still shut inside - handing it back first (OPEN B)");
        enterState(STATE_RETRIEVE);
    }
}

// Adds coins to the running total the LocalServer books revenue from (kept in NVS, sent every second as a CoinBookPacket).
void bookCoins(uint32_t n) {
    if (n == 0) return;
    bookedCoins += n;
    prefs.putUInt("booked", bookedCoins);
}

// The INSERT COIN payment is complete: from now on this customer's coins count as revenue. (A cancel on that screen never gets here.)
void commitPayment() {
    sessionPaid = true;
    bookCoins(sessionCoins);
    Serial.printf("[S3B COIN] Payment complete: %lu coin(s) booked, running total %lu\n", (unsigned long)sessionCoins, (unsigned long)bookedCoins);
}

// One record per paid session, for the LocalServer's Finished / Cancelled statistics: aborted = the customer pressed ABORT mid-cycle.
// Coins = every coin this customer paid (the INSERT COIN ones and any added during the cycle).
void recordSession(bool aborted) {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = sessionCoins > 0x7FFFUL ? 0x7FFFUL : sessionCoins;
    lastSessionDurationSec = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    lastSessionCoins       = (uint16_t)coinsUsed | (aborted ? SESSION_CANCELLED_FLAG : 0);
    completedSessionSeq++;
    Serial.printf("[S3B STATS] Session #%u %s: %s, %us, %u coins.\n",
                  (unsigned)completedSessionSeq, aborted ? "ABORTED (cancelled)" : "finished", sessionMode == MODE_DRY ? "DRYING" : "CLEANING",
                  (unsigned)lastSessionDurationSec, (unsigned)coinsUsed);
}

void enterState(MachineState s) {
    currentMachineState = s;
    holdA = holdB = holdC = 0;
    switch (s) {
        case STATE_TAPS:
            activeTimer = 0;
            sessionMode = MODE_NONE;
            sessionCoins = 0;
            sessionPaid = false;
            runTotalSec = coolTotalSec = 0;
            unlockForPause = false;
            clearStatus();
            resetCoinSession();
            break;
        case STATE_CHECKING:
            stepRemainingSec = 2;                                    // "Checking Main Container" for 2 seconds, then WELCOME
            break;
        case STATE_INSTRUCTIONS:
        case STATE_RETRIEVE:
            unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            if (beepWaitMs > 0) { sendBuzzer(beepLenMs); lastBeepMs = millis(); }     // loop() repeats it until the step ends
            else sendBuzzer(s == STATE_RETRIEVE ? 500 : 250);
            break;
        case STATE_PAUSED_SAFETY:
            unlockForPause = false; unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
            break;
        case STATE_OUT_OF_ORDER:
            oooUnlock = oooBeep = oooGaveUp = oooBlindDone = false; oooUnlockSec = 0;
            unlockHeldSec = 0; lockRestSec = 0; lockResting = false;
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
    sessionMode = MODE_NONE; sessionCoins = 0; sessionPaid = false;
    enterState(STATE_CHECKING);
}

// The status feed on the cleaning / drying screens: announces what the machine just did. At the start only what is ON is announced; after that every change
// of the UV, the mist or the heater gets a line, newest first on the screen.
void syncRunStatus(bool first) {
    bool uvOn = (desiredMask & A3_MASK_UV) != 0;
    bool mistOn = (desiredMask & A3_MASK_MIST) != 0;
    bool heatOn = (desiredMask & A3_BIT(B3_RELAY_HEATER)) != 0;
    if (first) {
        if (uvOn) pushStatus("UV: ON");
        if (mistOn) pushStatus("HUMID: Misting");
        if (heatOn) pushStatus("HEATER: ON");
    } else {
        if (uvOn != lastUvOn) pushStatus(uvOn ? "UV: ON" : "UV: OFF");
        if (mistOn != lastMistOn) pushStatus(mistOn ? "HUMID: Misting" : "HUMID: OFF");
        if (heatOn != lastHeatOn) pushStatus(heatOn ? "HEATER: ON" : "HEATER: OFF");
    }
    lastUvOn = uvOn; lastMistOn = mistOn; lastHeatOn = heatOn;
}

// CLEANING BAL: the bought time. The paid session starts here (stats, booked coins already counted).
void beginCleaning() {
    sessionStartMillis  = millis();
    sessionCoinsAtStart = lastKnownPulsesB1;
    runTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    clearStatus();
    Serial.printf("[S3B] Cleaning starts: %lus\n", (unsigned long)activeTimer);
    enterState(STATE_CLEANING);
    syncRunStatus(true);                      // after enterState: the state decides what is ON
    sendBuzzer(250);
}

// CL HEATING: the drying that follows every cleaning (also after an abort of the cleaning, because the helmet is wet).
void beginAutoDry() {
    activeTimer = autoDrySec;
    runTotalSec = autoDrySec > 9999 ? 9999 : autoDrySec;
    clearStatus();
    Serial.printf("[S3B] Auto-dry starts: %us (heater off for the last %us)\n", (unsigned)autoDrySec, (unsigned)heaterOffTailSec);
    enterState(STATE_AUTO_DRY);
    syncRunStatus(true);
}

void finishCleaning() {
    recordSession(false);
    beginAutoDry();
}

// DR HEATING: the bought time.
void beginDrying() {
    sessionStartMillis  = millis();
    sessionCoinsAtStart = lastKnownPulsesB1;
    runTotalSec = activeTimer > 9999UL ? 9999UL : activeTimer;
    clearStatus();
    Serial.printf("[S3B] Drying starts: %lus\n", (unsigned long)activeTimer);
    enterState(STATE_DRYING);
    syncRunStatus(true);
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
    recordSession(false);
    Serial.println("[S3B] Drying complete -> cool down");
    beginCoolDown();
}

// Both procedures meet here after CLOSE DOOR A (the Designer's CL / DR branches): a cleaning starts at once, a drying goes through START DRYING.
void beginRun() {
    if (sessionMode == MODE_DRY) enterState(STATE_START_DRY); else beginCleaning();
}

// The customer picked CLEANING or DRYING: the INSERT COIN screen asks for the required coins (one step of the coin rate).
void beginPayment(uint8_t mode) {
    sessionMode = mode;
    requiredCoinsForSession = minCoinsRequired;
    activeTimer = 0;
    Serial.printf("[S3B] Mode: %s, %u coin(s) required\n", mode == MODE_DRY ? "DRYING" : "CLEANING", (unsigned)requiredCoinsForSession);
    enterState(STATE_INSERT_COIN);
}

// A button from B1 (queued by the radio callback, handled here in loop()).
void handleTouch(const char* a, uint16_t v) {
    MachineState st = currentMachineState;
    Serial.printf("[S3B TOUCH] '%s' value=%u in %s\n", a, v, stateName(st));

    if (!strcmp(a, "TAP_ADVANCE")) {
        if (st == STATE_TAPS) beginSession();
        else if (st == STATE_WELCOME) enterState(cleaningAllowed() ? STATE_SELECT_MODE : STATE_SELECT_DRY_ONLY);
    } else if (!strcmp(a, "MODE_CLEAN")) {
        if (st == STATE_SELECT_MODE) beginPayment(MODE_CLEAN);
    } else if (!strcmp(a, "MODE_DRY")) {
        if (st == STATE_SELECT_MODE || st == STATE_SELECT_DRY_ONLY) beginPayment(MODE_DRY);
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
            recordSession(true);                       // a paid session that was cut short: recorded as Cancelled, its coins stay booked
            if (returnState == STATE_DRYING) { Serial.println("[S3B] Drying aborted by the customer -> cool down first"); beginCoolDown(); }
            else { Serial.println("[S3B] Cleaning aborted by the customer -> auto-dry (the helmet is wet)"); beginAutoDry(); }
        }
    } else if (!strcmp(a, "ABORT_NO")) {
        if (st == STATE_ABORT_CONFIRM) enterState(returnState);     // back to the cleaning / drying, time and bar untouched
    } else if (!strcmp(a, "RESET")) {
        enterState(STATE_TAPS);
    }
}

void handleMaintenance(uint8_t on) {
    if (on) {
        if (currentMachineState == STATE_TAPS || currentMachineState == STATE_OUT_OF_ORDER) { maintMask = 0; maintBypass = false; enterState(STATE_MAINTENANCE); }
        else Serial.println("[S3B] Maintenance refused: only possible from the idle screen");
    } else if (currentMachineState == STATE_MAINTENANCE) {
        maintMask = 0; maintBypass = false;
        enterState(STATE_TAPS);
    }
}

// Maintenance: switch a hand-set relay off again 2 s after it was switched on (see MAINT_AUTOOFF_MS)
void maintAutoOffTick(unsigned long now) {
    if (currentMachineState != STATE_MAINTENANCE || maintBypass) return;
    uint16_t timed = (uint16_t)(maintMask & ~MAINT_LATCH_MASK);
    if (!timed) return;
    bool changed = false;
    for (uint8_t b = 0; b < 16; b++) {
        if ((timed & A3_BIT(b)) && (uint32_t)(now - maintOnAtMs[b]) >= MAINT_AUTOOFF_MS) { maintMask &= (uint16_t)(~A3_BIT(b)); changed = true; }
    }
    if (changed) { Serial.println("[S3B] Maintenance: a hand-set relay was on for 2 s - switched off (safety)"); applyDesiredMask(); }
}

// ---------------------------------------------------------------------
// The once-a-second state logic
// ---------------------------------------------------------------------
void tickState() {
    bool open = doorOpenNow();
    bool closed = !open;
    bool hp = helmetPresentNow();

    switch (currentMachineState) {
        case STATE_CHECKING:                           // "Checking Main Container" for 2 s; the level itself is judged at the tap on WELCOME
            if (!systemReady()) { goOutOfOrder(b2Alive() ? "relay board B3 is not answering" : "sensor board B2 is not answering"); break; }
            if (stepRemainingSec > 0) stepRemainingSec--;
            if (stepRemainingSec == 0) enterState(STATE_WELCOME);
            break;

        case STATE_WELCOME:                            // before the customer pays: a dead sensor / relay board must not take their coins
        case STATE_SELECT_MODE:
        case STATE_SELECT_DRY_ONLY:
            if (!systemReady()) goOutOfOrder(b2Alive() ? "relay board B3 is not answering" : "sensor board B2 is not answering");
            break;

        case STATE_INSERT_COIN:
            break;                                         // the payment is completed by coinSettleCheck() (called from loop): it waits for the acceptor to finish counting

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

        case STATE_START_DRY:                          // "Please Wait...": one last look before the heater comes on
            if (open) enterState(STATE_CLOSE_DOOR_A);
            else if (!hp) enterState(STATE_INSTRUCTIONS);
            else beginDrying();
            break;

        case STATE_HELMET_NOTICE_A:                    // "HELMET NO DETECTED" for 2 s, then open the door again
            if (stepRemainingSec > 0) stepRemainingSec--;
            else enterState(STATE_INSTRUCTIONS);
            break;

        case STATE_CLEANING:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: UV and mist off at once
                Serial.println("[S3B] SAFETY: the door opened during cleaning");
                returnState = STATE_CLEANING;
                enterState(STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) finishCleaning();
            }
            break;

        case STATE_AUTO_DRY:
            if (holdTicks(holdA, open, 2)) {           // the door opened mid-cycle: heater and UV off at once
                Serial.println("[S3B] SAFETY: the door opened during the auto-dry");
                returnState = STATE_AUTO_DRY;
                enterState(STATE_PAUSED_SAFETY);
                break;
            }
            if (activeTimer > 0) {
                activeTimer--;
                if (activeTimer == 0) { Serial.println("[S3B] Auto-dry complete -> open the door"); enterState(STATE_RETRIEVE); }
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
            if (done) enterState(STATE_FINISH);
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

        case STATE_OUT_OF_ORDER: {                     // a node went silent; clears itself when both answer again
            if (systemReady()) {
                Serial.println("[S3B] Back in order");
                enterState(STATE_TAPS);
                break;
            }
            bool trapped = holdTicks(holdA, closed && hp, 2);     // door shut with a helmet inside
            bool opened  = holdTicks(holdB, open, 2);
            bool gone    = holdTicks(holdC, !hp, 2);
            if (!b2Alive()) {                                     // the door and the helmet cannot be seen: one short unlock window per outage, no beep
                oooBeep = false;
                if (!oooBlindDone) { oooBlindDone = true; startOooRelease("sensor board B2 silent - one blind unlock window"); }
                else if (oooUnlock && ++oooUnlockSec >= OOO_BLIND_SEC) oooUnlock = false;
            } else {
                if (oooBlindDone) { oooBlindDone = false; oooUnlock = false; }       // B2 is back: only what it sees counts now
                if (opened || gone) { oooUnlock = false; oooGaveUp = false; }        // door seen open / helmet taken: the next shut door with a helmet is a new episode
                else if (trapped && !oooUnlock && !oooGaveUp) startOooRelease("door shut with a helmet inside");
                if (oooUnlock && ++oooUnlockSec >= OOO_RELEASE_MAX_SEC) { oooUnlock = false; oooGaveUp = true; Serial.println("[S3B] OUT OF ORDER: release time used up - lock engaged again"); }
                oooBeep = oooUnlock && closed;
            }
            break;
        }

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
            coinLastPulseMs = millis();
            if (sessionPaid) bookCoins(newPulses);     // a coin added during the cycle is revenue at once (the INSERT COIN ones are booked when the payment completes)
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

// INSERT COIN: the required coins are in, but the acceptor may still be counting (a big coin is many pulses). Wait until no new pulse has been seen for
// COIN_SETTLE_MS (or COIN_SETTLE_MAX_MS have passed), then every coin counted so far is the customer's time and revenue.
void coinSettleCheck() {
    if (currentMachineState != STATE_INSERT_COIN) { coinReachedMs = 0; return; }
    if (sessionCoins < requiredCoinsForSession || requiredCoinsForSession == 0) { coinReachedMs = 0; return; }
    unsigned long now = millis();
    if (coinReachedMs == 0) {
        coinReachedMs = now; if (coinLastPulseMs == 0 || (long)(coinLastPulseMs - now) > 0) coinLastPulseMs = now;
        Serial.printf("[S3B COIN] %lu of %lu required coin(s) in - waiting for the acceptor to finish counting\n", (unsigned long)sessionCoins, (unsigned long)requiredCoinsForSession);
    }
    bool quiet  = (unsigned long)(now - coinLastPulseMs) >= COIN_SETTLE_MS;
    bool capped = (unsigned long)(now - coinReachedMs)   >= COIN_SETTLE_MAX_MS;
    if (!quiet && !capped) return;
    uint32_t timeCoins = sessionCoins > maxCoins ? maxCoins : sessionCoins;     // time never goes past the most coins one session can take
    activeTimer = timeCoins * secondsPerCoin;                                   // ALL the coins counted buy time (20 pesos for 15 required = 5 min + 5 coins more)
    Serial.printf("[S3B COIN] Counting finished: %lu coin(s) = %lu s%s\n", (unsigned long)sessionCoins, (unsigned long)activeTimer, capped && !quiet ? " (stopped waiting after the cap)" : "");
    coinReachedMs = 0;
    commitPayment();                                                            // the coins are revenue from here on
    enterState(STATE_INSTRUCTIONS);
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
        return;
    }
    if (len == (int)sizeof(RefillStatusPacket)) {      // the Refilling Station
        RefillStatusPacket p;
        memcpy(&p, incomingData, sizeof(RefillStatusPacket));
        if (p.deviceID != DEVICE_REFILL || p.magic != REFILL_MAGIC) return;
        portENTER_CRITICAL(&rxMux);
        nodeStation = p; lastStationMs = millis();
        portEXIT_CRITICAL(&rxMux);
    }
}

void handleQueuedCommand(const CommandPacket& cmd) {
    if (cmd.commandID == CMD_TOUCH_ACTION) {
        handleTouch(cmd.payloadStr, cmd.param16);
    } else if (cmd.commandID == CMD_SET_MAINTENANCE) {
        handleMaintenance(cmd.state ? 1 : 0);
    } else if (cmd.commandID == CMD_SET_RELAY_MASK) {
        if (currentMachineState == STATE_MAINTENANCE) {
            uint16_t nm = cmd.param16 & B3_MASK_ALL;
            uint32_t t = millis();
            for (uint8_t b = 0; b < 16; b++) if ((nm & A3_BIT(b)) && !(maintMask & A3_BIT(b))) maintOnAtMs[b] = t;   // newly switched on: the 2 s start now
            maintBypass = (cmd.state & 1) != 0;       // the dashboard's "Bypass Safety 2 Seconds Relay" box (an old dashboard sends 0 = safety on)
            maintMask = nm; applyDesiredMask();
        }
    } else if (cmd.commandID == CMD_TFT_HARD_REFRESH) {
        // "Force refresh" from the dashboard: restart B1's touch screen and redraw the current page (cures a white or garbled screen)
        b1HardRefreshPending = true;
        b1RedrawAtMillis = millis() + 50;
        Serial.println("[S3B] screen refresh requested from the dashboard");
    } else if (cmd.commandID == CMD_SET_CONFIG) {
        bool ok = false;
        if (cmd.subIndex == 1) ok = applyRulesCsv(cmd.payloadStr);
        else if (cmd.subIndex == 2) ok = applySensorsCsv(cmd.payloadStr);
        else if (cmd.subIndex == 3) ok = applyAutoDryCsv(cmd.payloadStr);
        else if (cmd.subIndex == 4) ok = applyDryingCsv(cmd.payloadStr);
        else if (cmd.subIndex == 5) ok = applyRefreshCsv(cmd.payloadStr);
        else if (cmd.subIndex == 6) ok = applyAutoRefreshCsv(cmd.payloadStr);
        else if (cmd.subIndex == 7) ok = applyBeepCsv(cmd.payloadStr);
        if (ok) { saveConfigToNVS(); Serial.printf("[S3B CONFIG] Updated & saved (type %u)\n", cmd.subIndex); }
        else Serial.printf("[S3B CONFIG] REJECTED (type %u): '%s'\n", cmd.subIndex, cmd.payloadStr);
    }
}

// ---------------------------------------------------------------------
// USB serial console - tune and inspect without a dashboard
// ---------------------------------------------------------------------
void printConfig() {
    char r[100], s[100], a[100];
    rulesToCsv(r, sizeof(r)); sensorsToCsv(s, sizeof(s)); autoDryToCsv(a, sizeof(a));
    static const char* const LVL[5] = {"EMPTY", "LOW", "MID", "HIGH", "MAX"};
    uint8_t lv = myContainerLevel();
    Serial.println("---- S3B settings ----");
    Serial.printf("state: %s | B2 %s | B3 %s | Refilling Station %s, Box 3 container %s -> CLEANING %s\n", stateName(currentMachineState),
                  b2Alive() ? "ok" : "SILENT", b3Alive() ? "ok" : "SILENT", stationHeard() ? "heard" : "NOT HEARD", lv <= REFILL_MAX ? LVL[lv] : "unknown",
                  cleaningAllowed() ? "available" : "UNAVAILABLE (drying only)");
    Serial.printf("rules : %u coin(s) = %.2f %s (%.2f s/coin) | required %u coins = %us | max %u coins\n", rateCoins, (double)rateValue, rateUnitMin ? "min" : "s",
                  (double)secPerCoinF, (unsigned)minCoinsRequired, (unsigned)(minCoinsRequired * secondsPerCoin), maxCoins);
    Serial.printf("sensor: helmet < %.1f cm | the Refilling Station counts as offline after %u s\n", (double)helmetDetectCm, refillOfflineSec);
    Serial.printf("dry   : auto-dry after a cleaning %u s, heater off for its last %u s | cool down after a drying %u s\n", autoDrySec, heaterOffTailSec, coolDownSec);
    Serial.printf("beep  : OPEN A / OPEN B beep %u ms, then wait %u ms%s | booked coins %lu (running total sent to the LocalServer)\n",
                  (unsigned)beepLenMs, (unsigned)beepWaitMs, beepWaitMs ? ", repeating" : " (0 = a single beep)", (unsigned long)bookedCoins);
    Serial.printf("LIVE  : helmet %.1f cm | door %s | relays 0x%04X\n", (double)b2snap.usHelmetCm, b2snap.doorEnclosure ? "OPEN" : "shut", (unsigned)b3Mask);
    Serial.printf("csv   : rules=%s sensors=%s autodry=%s drying=%u\n", r, s, a, (unsigned)coolDownSec);
}

void handleSerialLine(char* line) {
    char* cmd = strtok(line, " \t\r\n");
    if (!cmd) return;
    char* a1 = strtok(nullptr, " \t\r\n");
    char* a2 = strtok(nullptr, " \t\r\n");
    if (!strcmp(cmd, "help")) {
        Serial.println("Commands:  cfg | reset | set <name> <value>");
        Serial.println("Names:  coins, timevalue, unitmin 0/1, maxcoins, helm (cm), offline (s), autodry (s), tail (s), cooldown (s)");
        Serial.println("        beeplen (ms), beepwait (ms, 0 = one beep only)   - the repeating beep of the OPEN A / OPEN B steps");
        return;
    }
    if (!strcmp(cmd, "cfg")) { printConfig(); return; }
    if (!strcmp(cmd, "reset")) { enterState(STATE_TAPS); return; }
    if (!strcmp(cmd, "set") && a1 && a2) {
        float v = atof(a2);
        if (!strcmp(a1, "coins")) rateCoins = (uint16_t)v;
        else if (!strcmp(a1, "timevalue")) rateValue = v;
        else if (!strcmp(a1, "unitmin")) rateUnitMin = v != 0;
        else if (!strcmp(a1, "maxcoins")) maxCoins = (uint16_t)v;
        else if (!strcmp(a1, "helm")) helmetDetectCm = v;
        else if (!strcmp(a1, "offline")) { if (v < 5 || v > 120) { Serial.println("offline must be 5-120 seconds"); return; } refillOfflineSec = (uint16_t)(v + 0.5f); }
        else if (!strcmp(a1, "autodry")) { if (v < 10 || v > 900 || v < heaterOffTailSec) { Serial.println("autodry must be 10-900 seconds and not shorter than the tail"); return; } autoDrySec = (uint16_t)(v + 0.5f); }
        else if (!strcmp(a1, "tail")) { if (v < 0 || v > autoDrySec) { Serial.println("tail must be 0 to the auto-dry length"); return; } heaterOffTailSec = (uint16_t)(v + 0.5f); }
        else if (!strcmp(a1, "beeplen")) beepLenMs = (uint16_t)(v < 20 ? 20 : (v > 1000 ? 1000 : v));
        else if (!strcmp(a1, "beepwait")) beepWaitMs = (uint16_t)(v < 0 ? 0 : (v > 10000 ? 10000 : v));
        else if (!strcmp(a1, "cooldown")) { if (v < 5 || v > 900) { Serial.println("cooldown must be 5-900 seconds"); return; } coolDownSec = (uint16_t)(v + 0.5f); }
        else { Serial.printf("unknown setting '%s' - type help\n", a1); return; }
        if (maxCoins < rateCoins) maxCoins = rateCoins;
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

// The running total of booked coins, once a second (the LocalServer books the difference to what it saw last).
void broadcastBooked() {
    CoinBookPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID    = DEVICE_S3B;
    p.magic       = COINBOOK_MAGIC;
    p.bookedCoins = bookedCoins;
    p.seq         = ++bookSeq;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
}

void oneSecondTick() {
    unsigned long now = millis();

    // fresh copy of B2's report and of the Refilling Station's for this tick
    portENTER_CRITICAL(&rxMux);
    b2snap = nodeB2;
    stSnap = nodeStation;
    portEXIT_CRITICAL(&rxMux);
    if (!b2Alive()) {
        // Sensor board silent: never keep acting on its last readings. "Door open, no helmet" is the safe way to be wrong - a cleaning or drying
        // in progress pauses with everything off until B2 is back.
        b2snap.doorEnclosure = true;
        b2snap.usHelmetCm = 0.0f;
    }

    // enclosure solenoid duty cap
    bool lockWanted = (currentMachineState == STATE_INSTRUCTIONS || currentMachineState == STATE_RETRIEVE ||
                       (currentMachineState == STATE_PAUSED_SAFETY && unlockForPause) ||
                       (currentMachineState == STATE_OUT_OF_ORDER && oooUnlock) ||
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

    bootHelmetCheck(!doorOpenNow(), helmetPresentNow());
    tickState();
    applyDesiredMask();                       // picks up the lock duty cap, the max-coins cutoff and the auto-dry's heater tail
    if (currentMachineState == STATE_CLEANING || currentMachineState == STATE_AUTO_DRY || currentMachineState == STATE_DRYING) syncRunStatus(false);

    if (coinFlag) { coinFlag = false; pushStatus("COIN INSERTED"); }

    // The once-a-second broadcasts. Small gaps: several back-to-back sends can overflow ESP-NOW's queue.
    sendCmd(DEVICE_B1, CMD_SYNC_VARS, (uint8_t)min(minCoinsRequired, (uint32_t)255), (uint16_t)activeTimer, (uint8_t)min(requiredCoinsForSession, (uint32_t)255));
    delay(2);
    sendCmd(0, CMD_HEARTBEAT, DEVICE_S3B, 0, 0);
    delay(2);
    broadcastStatus();
    delay(2);
    broadcastBooked();
    delay(2);

    if (now - lastMaskSentMs >= MASK_REASSERT_MS) { sendMask(); delay(2); }
    if (now - lastCfgCsvMs >= 2000) { lastCfgCsvMs = now; broadcastSettingsCsv(); }
    if (now - lastTokenMs >= 5000) {          // numbers B1 may have missed or lost in a reboot
        lastTokenMs = now;
        sendToken(TOKEN_MAXCOINS, maxCoins);
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
    memset(&nodeStation, 0, sizeof(nodeStation));
    memset(&stSnap, 0, sizeof(stSnap));
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
    coinSettleCheck();
    serialConsole();
    maintAutoOffTick(now);

    // OPEN A / OPEN B: keep beeping (beep length + wait) until the step ends. beepWaitMs 0 = only the one beep at the start of the step.
    if (beepWaitMs > 0 && (currentMachineState == STATE_INSTRUCTIONS || currentMachineState == STATE_RETRIEVE) &&
        now - lastBeepMs >= (unsigned long)beepLenMs + (unsigned long)beepWaitMs) {
        lastBeepMs = now;
        sendBuzzer(beepLenMs);
    }

    // OUT OF ORDER with a helmet shut inside: the same repeating beep, so somebody nearby comes and takes the helmet (a single-beep setting repeats every 500 ms here)
    if (currentMachineState == STATE_OUT_OF_ORDER && oooBeep &&
        now - lastBeepMs >= (unsigned long)beepLenMs + (unsigned long)(beepWaitMs > 0 ? beepWaitMs : OOO_DEFAULT_WAIT_MS)) {
        lastBeepMs = now;
        sendBuzzer(beepLenMs);
    }

    // periodic hard refresh of B1 (setting "Auto hard refresh"): 0 = never; also not while another repaint is already pending
    if (autoRefreshSec > 0 && now - lastAutoRefreshMs >= (unsigned long)autoRefreshSec * 1000UL) {
        lastAutoRefreshMs = now;
        if (b1RedrawAtMillis == 0) { b1HardRefreshPending = true; b1RedrawAtMillis = now + 1; }
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
