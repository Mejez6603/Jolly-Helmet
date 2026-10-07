// =====================================================================
//  NODE ACS 1 / ACS 2 - REFILL SYSTEM (board "E1")                 (ESP32, core 3.3.7)
//  Created 2026-10-03. REPLACES the old 4-tank ESP32_ACS.ino (archived as ESP32_ACS_LEGACY_4tank.ino.txt). NOT YET FLASHED.
//
//  ACS 1 serves Box 1 (DEVICE_ACS, asked by S3A), ACS 2 serves Box 3 (DEVICE_ACS2, asked by S3B). It is the SAME design and the
//  same code: ESP32_ACS2.ino differs from this file in the single line "#define ACS_UNIT" and nothing else.
//
//  Hardware (pin sheet "ESP32 PIN OUTS (E1)"):
//    three SUPPLY tanks (Water, Alcohol, Scented) - NO sensors;
//    two MIXER containers (Alcohol+Water, Scented+Water) - each has an ultrasonic (US) and a water-level sensor (WL) as its backing;
//    six pumps, one at a time (relays are Active-LOW):
//      GPIO18 ALCOHOL pump (alcohol supply -> alcohol mixer)        GPIO19 SCENTED pump (scented supply -> scented mixer)
//      GPIO21 WATER pump ALC (water supply -> alcohol mixer)        GPIO22 MIXER ALCOHOL pump (alcohol mixer -> the box)
//      GPIO23 WATER pump SCN (water supply -> scented mixer)        GPIO25 MIXER SCENTED pump (scented mixer -> the box)
//    US alcohol mixer: trigger 4 / echo 14      US scented mixer: trigger 26 / echo 33
//    WL alcohol mixer: GPIO34 (analog)          WL scented mixer: GPIO35 (analog)
//
//  What it does (the Designer's ACS flow, ACS1_/ACS2_STEPS_2026-10-03_draft_v1.json, with the refinements below):
//   - TOP-UP RECIPE for a mixer container, started when its level has dropped to "topUp" (or it is LOW):
//       water in until the WATER share of the top-up is reached (waterA 30 % / waterS %), then alcohol (or scented) in until FULL.
//       The recipe's progress is kept in NVS (phase 0 none, 1 water going in, 2 water done): a half-made mix is never delivered, and an
//       interrupted recipe (empty supply, power loss) resumes at the right pump instead of adding the water twice.
//   - DELIVERY: the box asks (CMD_REQUEST_DELIVERY: alcohol / scented). The ACS runs the matching MIXER pump for ONE bounded shot
//       (deliverSec) or until that mixer container is LOW; the box asks again every refill interval until its own sensors say "full"
//       (or sends the cancel). A mixer container that is LOW or holds a half-made mix delivers nothing.
//   - THE BOSS'S RULE: a fill pump runs and the level does not rise (noRiseSec) => THAT SUPPLY IS EMPTY (or its pump is dead): pump
//       off, "WATER / ALCOHOL / SCENTED EMPTY" is reported, the other supplies keep working, and the supply is tried again after
//       emptyHoldSec (default 300 s). A rise counts if the US sees it OR the WL sees it (a rise has to be seen on 2 readings in a row);
//       "no rise" needs both to be flat. The box then simply runs out of refill tries and greys CLEANING out.
//   - WL + US: the WL, when WET, means FULL and wins over the US. A US that says "full" while the WL is dry is still believed for
//       STOPPING a pump (stopping early is the safe direction); a WL that is wet while the US reads far from full raises a warning.
//   - A mixer container whose level cannot be read at all (no echo and WL dry) is never pumped into or from automatically.
//   - Timeouts: a pump never runs longer than phaseMaxSec; after such a timeout the fills pause for holdoffSec (deliveries continue).
//   - MAINTENANCE (dashboard / bench / console): the automatic work is paused and the pumps are switched by hand, still one at a time;
//       a hand-set pump switches itself off after 60 s, and maintenance ends by itself after 30 minutes without a command.
//   - Reports an ACSStatusPacket once a second; keeps its settings in NVS and re-broadcasts them (CMD_SET_CONFIG, target 0, state 4 =
//       ACS 1 / 5 = ACS 2) every few seconds so a settings page can show them.
//
//  Settings (all over USB: "help"; over the air: CMD_SET_CONFIG subIndex 1 LEVELS / 2 TIMING / 3 MIX, see Shared_Common.h):
//    LEVELS (cm, ultrasonic distances - SMALLER = FULLER): emptyX (sensor to the bottom, only for the % read-out), topupX (refill when
//      the distance reaches this), lowX (delivery stops / refill forced), fullX (FULL line)   X = A (alcohol) or S (scented)
//    MIX: waterA / waterS = the WATER share (%) of each top-up
//    TIMING: noRise s, riseMin cm, wlRise raw, phaseMax s, deliver s, emptyHold s, reqHold s, holdoff s, wlEnter, wlLeave, wlHigher
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"

#define ACS_UNIT 1                 // 1 = ACS 1 (Box 1, DEVICE_ACS)    2 = ACS 2 (Box 3, DEVICE_ACS2)
#if ACS_UNIT == 1
  #define ACS_ID         DEVICE_ACS
  #define ACS_TAG        "ACS1"
  #define ACS_CFG_STATE  4
#else
  #define ACS_ID         DEVICE_ACS2
  #define ACS_TAG        "ACS2"
  #define ACS_CFG_STATE  5
#endif

// ---------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------
#define PUMP_ACTIVE   LOW
#define PUMP_INACTIVE HIGH

const uint8_t PUMP_PIN[ACS_PUMP_COUNT] = { 18, 21, 23, 19, 22, 25 };   // index = ACSPump
const char* const PUMP_NAME[ACS_PUMP_COUNT] = {
    "ALCOHOL PUMP", "WATER PUMP (ALC)", "WATER PUMP (SCN)", "SCENTED PUMP", "MIXER ALC PUMP", "MIXER SCN PUMP"
};
const uint8_t US_TRIG[2] = { 4, 26 };      // [0] alcohol mixer, [1] scented mixer
const uint8_t US_ECHO[2] = { 14, 33 };
const uint8_t WL_PIN[2]  = { 34, 35 };     // input-only pins, ADC1 (fine next to Wi-Fi)
const char* const MIX_NAME[2] = { "ALCOHOL MIXER", "SCENTED MIXER" };
const char* const SRC_NAME[3] = { "WATER", "ALCOHOL", "SCENTED" };

// ---------------------------------------------------------------------
// Settings (defaults are PLACEHOLDERS until the real containers are measured - see BOX/ACS plan)
// ---------------------------------------------------------------------
float    emptyCm[2]  = { 14.0f, 14.0f };   // sensor -> bottom (only used for the level % read-out)
float    topUpCm[2]  = {  8.0f,  8.0f };   // distance at which a top-up recipe starts
float    lowCm[2]    = { 11.0f, 11.0f };   // distance at/above which the container is LOW
float    fullCm[2]   = {  3.3f,  3.3f };   // distance at/below which it is FULL
float    waterPct[2] = { 30.0f, 90.0f };   // water share of each top-up: alcohol mixer 70/30, scented mixer (boss decides)
uint16_t noRiseSec    = 12;                // pump running this long with no rise => supply empty
float    riseMinCm    = 0.5f;              // how far the US distance must shrink to count as a rise
uint16_t wlRiseRaw    = 150;               // how far the WL reading must move to count as a rise
uint16_t phaseMaxSec  = 120;               // hard limit for one fill pump run
uint16_t deliverSec   = 6;                 // one delivery shot to the box
uint16_t emptyHoldSec = 300;               // an "empty" supply is tried again after this
uint16_t reqHoldSec   = 30;                // a box request stays valid this long (the box repeats it every ~10 s)
uint16_t holdoffSec   = 60;                // fills pause this long after a pump hit its time limit
uint16_t wlEnter      = 600;               // WL raw reading that means WET   (same idea as A2's water sensors)
uint16_t wlLeave      = 400;               // WL raw reading that means DRY again (gap = hysteresis)
bool     wlWetHigher  = true;              // true = the reading goes UP in liquid

#define PT_FLOAT 0
#define PT_U16   1
#define PT_BOOL  2
struct Param { const char* name; void* ptr; uint8_t type; float lo; float hi; };
Param PARAMS[] = {
    // LEVELS (0..7)
    {"emptyA",    &emptyCm[0],  PT_FLOAT, 5.0f, 300.0f}, {"topupA",   &topUpCm[0],  PT_FLOAT, 1.0f, 300.0f},
    {"lowA",      &lowCm[0],    PT_FLOAT, 1.0f, 300.0f}, {"fullA",    &fullCm[0],   PT_FLOAT, 1.0f, 300.0f},
    {"emptyS",    &emptyCm[1],  PT_FLOAT, 5.0f, 300.0f}, {"topupS",   &topUpCm[1],  PT_FLOAT, 1.0f, 300.0f},
    {"lowS",      &lowCm[1],    PT_FLOAT, 1.0f, 300.0f}, {"fullS",    &fullCm[1],   PT_FLOAT, 1.0f, 300.0f},
    // MIX (8..9)
    {"waterA",    &waterPct[0], PT_FLOAT, 1.0f, 99.0f},  {"waterS",   &waterPct[1], PT_FLOAT, 1.0f, 99.0f},
    // TIMING (10..20)
    {"norise",    &noRiseSec,   PT_U16,   3.0f, 120.0f}, {"risecm",   &riseMinCm,   PT_FLOAT, 0.1f, 5.0f},
    {"wlrise",    &wlRiseRaw,   PT_U16,   20.0f, 4000.0f}, {"phasemax", &phaseMaxSec, PT_U16,  10.0f, 900.0f},
    {"deliver",   &deliverSec,  PT_U16,   1.0f, 60.0f},  {"emptyhold", &emptyHoldSec, PT_U16, 10.0f, 3600.0f},
    {"reqhold",   &reqHoldSec,  PT_U16,   5.0f, 300.0f}, {"holdoff",  &holdoffSec,  PT_U16,   5.0f, 900.0f},
    {"wlenter",   &wlEnter,     PT_U16,   0.0f, 4095.0f}, {"wlleave", &wlLeave,     PT_U16,   0.0f, 4095.0f},
    {"wlhigher",  &wlWetHigher, PT_BOOL,  0.0f, 1.0f}
};
#define PARAM_COUNT   (sizeof(PARAMS) / sizeof(PARAMS[0]))
#define GRP_LEVELS_AT 0
#define GRP_LEVELS_N  8
#define GRP_MIX_AT    8
#define GRP_MIX_N     2
#define GRP_TIMING_AT 10
#define GRP_TIMING_N  11

Preferences prefs;

// ---------------------------------------------------------------------
// Tunables that are not settings
// ---------------------------------------------------------------------
const uint32_t SENSOR_INTERVAL_MS = 500;      // one full read of both containers every half second
const uint32_t SAMPLE_GAP_MS      = 5;        // between two ultrasonic shots
const uint32_t BOOT_QUIET_MS      = 5000;     // no automatic pumping before the sensors have settled
const uint32_t STATUS_INTERVAL_MS = 1000;
const uint32_t SETTINGS_BCAST_MS  = 3000;     // one settings list per 3 s, round robin
const uint32_t SUMMARY_MS         = 5000;
const uint32_t MANUAL_PUMP_MAX_MS = 60000;    // a hand-set pump switches itself off
const uint32_t MAINT_IDLE_MS      = 1800000UL;// maintenance ends by itself after 30 min without a command
const uint8_t  WL_AVG_SAMPLES     = 8;
const float    DISAGREE_CM        = 3.0f;     // WL wet but US this far from the FULL line => warn

// ---------------------------------------------------------------------
// Radio -> loop() queue (the callback only stores)
// ---------------------------------------------------------------------
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
#define RXQ_SIZE 6
CommandPacket rxQ[RXQ_SIZE];
volatile uint8_t rxHead = 0;
volatile uint8_t rxTail = 0;

// ---------------------------------------------------------------------
// State (all touched from loop() only)
// ---------------------------------------------------------------------
float    usCm[2]   = { 0.0f, 0.0f };          // filtered ultrasonic distance, 0 = no echo
uint8_t  usBad[2]  = { 0, 0 };                // consecutive cycles without an echo
uint16_t wl[2]     = { 0, 0 };                // WL raw reading
bool     wet[2]    = { false, false };        // WL verdict (debounced)
uint8_t  wetCnt[2] = { 0, 0 };
bool     sensorsReady = false;

uint8_t  sampleState = 0;                     // 0 idle, 1 alcohol US, 2 scented US
uint8_t  sampleIndex = 0;
float    sampleBuf[5];
uint32_t lastSampleStepMs = 0;
uint32_t lastCycleMs = 0;

uint8_t  pumpMask = 0;                        // bit n = pump n running
uint8_t  step = ACS_STEP_IDLE;                // what the machine is doing (EMPTY steps are only "displayed", see shownStep())
uint32_t stepStartMs = 0;
bool     maint = false;
uint32_t maintLastCmdMs = 0;
uint32_t manualOnMs = 0;

// recipe progress per mixer container: 0 none / complete, 1 water going in, 2 water done (needs alcohol / scented) - kept in NVS
uint8_t  phase[2] = { 0, 0 };

// the fill in progress
uint8_t  fillK = 0, fillSrc = 0;
bool     fillWaterPhase = false;
float    fillStartUs = 0.0f, fillTargetCm = 0.0f, fillNeedRise = 0.5f;
uint16_t fillStartWl = 0;
bool     fillStartWet = false, fillRise = false;
uint8_t  riseTicks = 0, reachTicks = 0;
float    deliverStartUs = 0.0f;

// supplies judged empty
bool     emptyFlag[3]    = { false, false, false };
uint32_t emptyRetryAt[3] = { 0, 0, 0 };
uint32_t holdoffUntil    = 0;
bool     warnTimeout     = false;

uint32_t reqUntil[2] = { 0, 0 };              // 0 = no request waiting

uint32_t lastStatusMs = 0, lastSummaryMs = 0, lastBcastMs = 0;
uint8_t  bcastRound = 0;
char     lineBuf[96];
uint8_t  lineLen = 0;

// ---------------------------------------------------------------------
// Settings: NVS, CSV, checks
// ---------------------------------------------------------------------
bool levelsValid() {
    for (uint8_t k = 0; k < 2; k++) {
        if (!(emptyCm[k] > lowCm[k] && lowCm[k] >= topUpCm[k] && topUpCm[k] > fullCm[k] && fullCm[k] >= 1.0f)) return false;
    }
    return true;
}

float paramGet(uint8_t i) {
    const Param& p = PARAMS[i];
    if (p.type == PT_FLOAT) return *(float*)p.ptr;
    if (p.type == PT_U16)   return (float)(*(uint16_t*)p.ptr);
    return *(bool*)p.ptr ? 1.0f : 0.0f;
}
void paramPut(uint8_t i, float v) {
    const Param& p = PARAMS[i];
    if (p.type == PT_FLOAT)    *(float*)p.ptr = v;
    else if (p.type == PT_U16) *(uint16_t*)p.ptr = (uint16_t)(v + 0.5f);
    else                       *(bool*)p.ptr = (v >= 0.5f);
}
void paramSave(uint8_t i) {
    const Param& p = PARAMS[i];
    if (p.type == PT_FLOAT)    prefs.putFloat(p.name, *(float*)p.ptr);
    else if (p.type == PT_U16) prefs.putUShort(p.name, *(uint16_t*)p.ptr);
    else                       prefs.putBool(p.name, *(bool*)p.ptr);
}
void saveAll() { for (uint8_t i = 0; i < PARAM_COUNT; i++) paramSave(i); }
void loadAll() {
    for (uint8_t i = 0; i < PARAM_COUNT; i++) {
        const Param& p = PARAMS[i];
        if (p.type == PT_FLOAT)    *(float*)p.ptr = prefs.getFloat(p.name, *(float*)p.ptr);
        else if (p.type == PT_U16) *(uint16_t*)p.ptr = prefs.getUShort(p.name, *(uint16_t*)p.ptr);
        else                       *(bool*)p.ptr = prefs.getBool(p.name, *(bool*)p.ptr);
    }
    if (!levelsValid()) {                    // never run on a nonsense level list
        Serial.println("[ACS] stored LEVELS are inconsistent - back to the defaults");
        float e[2] = {14.0f, 14.0f}, t[2] = {8.0f, 8.0f}, l[2] = {11.0f, 11.0f}, f[2] = {3.3f, 3.3f};
        for (uint8_t k = 0; k < 2; k++) { emptyCm[k] = e[k]; topUpCm[k] = t[k]; lowCm[k] = l[k]; fullCm[k] = f[k]; }
        for (uint8_t i = GRP_LEVELS_AT; i < GRP_LEVELS_AT + GRP_LEVELS_N; i++) paramSave(i);
    }
    phase[0] = prefs.getUChar("phaseA", 0);
    phase[1] = prefs.getUChar("phaseS", 0);
    if (phase[0] > 2) phase[0] = 0;
    if (phase[1] > 2) phase[1] = 0;
}
void setPhase(uint8_t k, uint8_t v) {
    if (phase[k] == v) return;
    phase[k] = v;
    prefs.putUChar(k == 0 ? "phaseA" : "phaseS", v);
}

void groupToCsv(uint8_t at, uint8_t n, char* out, size_t cap) {
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < n && used + 12 < cap; i++) {
        const Param& p = PARAMS[at + i];
        int w;
        if (p.type == PT_FLOAT) w = snprintf(out + used, cap - used, "%s%.2f", i ? "," : "", (double)paramGet(at + i));
        else                    w = snprintf(out + used, cap - used, "%s%u",   i ? "," : "", (unsigned)(paramGet(at + i) + 0.5f));
        if (w < 0) break;
        used += (size_t)w;
    }
}

// Parses exactly n numbers (no empty fields), range-checks them and, for LEVELS, the whole list; applies + saves only if all is well.
bool applyGroupCsv(uint8_t at, uint8_t n, const char* csv) {
    float v[GRP_TIMING_N];
    const char* s = csv;
    for (uint8_t i = 0; i < n; i++) {
        char* end = nullptr;
        v[i] = strtof(s, &end);
        if (end == s) { Serial.printf("[ACS CONFIG] REJECTED '%s' - expected %u numbers\n", csv, n); return false; }
        s = end;
        while (*s == ' ') s++;
        if (i + 1 < n) { if (*s != ',') { Serial.printf("[ACS CONFIG] REJECTED '%s' - expected %u numbers\n", csv, n); return false; } s++; }
    }
    while (*s == ' ') s++;
    if (*s != '\0') { Serial.printf("[ACS CONFIG] REJECTED '%s' - more than %u numbers\n", csv, n); return false; }
    for (uint8_t i = 0; i < n; i++) {
        if (v[i] < PARAMS[at + i].lo || v[i] > PARAMS[at + i].hi) {
            Serial.printf("[ACS CONFIG] REJECTED: %s = %.2f is outside %.1f..%.1f\n", PARAMS[at + i].name, (double)v[i], (double)PARAMS[at + i].lo, (double)PARAMS[at + i].hi);
            return false;
        }
    }
    float old[GRP_TIMING_N];
    for (uint8_t i = 0; i < n; i++) { old[i] = paramGet(at + i); paramPut(at + i, v[i]); }
    if (at == GRP_LEVELS_AT && !levelsValid()) {
        for (uint8_t i = 0; i < n; i++) paramPut(at + i, old[i]);
        Serial.println("[ACS CONFIG] REJECTED: needs empty > low >= topup > full >= 1 for both containers");
        return false;
    }
    for (uint8_t i = 0; i < n; i++) paramSave(at + i);
    Serial.printf("[ACS CONFIG] accepted: %s\n", csv);
    return true;
}

void sendCmdPkt(uint8_t target, uint8_t opcode, uint8_t sub, uint16_t p16, uint8_t st, const char* str) {
    CommandPacket c;
    memset(&c, 0, sizeof(c));
    c.targetDeviceID = target;
    c.commandID = opcode;
    c.subIndex = sub;
    c.param16 = p16;
    c.state = st;
    if (str) { strncpy(c.payloadStr, str, sizeof(c.payloadStr) - 1); c.payloadStr[sizeof(c.payloadStr) - 1] = '\0'; }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&c, sizeof(CommandPacket));
}

void broadcastSettings() {
    char buf[160];
    uint8_t round = bcastRound;
    bcastRound = (uint8_t)((bcastRound + 1) % 3);
    if (round == 0)      { groupToCsv(GRP_LEVELS_AT, GRP_LEVELS_N, buf, sizeof(buf)); sendCmdPkt(0, CMD_SET_CONFIG, 1, 0, ACS_CFG_STATE, buf); }
    else if (round == 1) { groupToCsv(GRP_TIMING_AT, GRP_TIMING_N, buf, sizeof(buf)); sendCmdPkt(0, CMD_SET_CONFIG, 2, 0, ACS_CFG_STATE, buf); }
    else                 { groupToCsv(GRP_MIX_AT,    GRP_MIX_N,    buf, sizeof(buf)); sendCmdPkt(0, CMD_SET_CONFIG, 3, 0, ACS_CFG_STATE, buf); }
}

// ---------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------
float medianOf5(float s[5]) {
    for (uint8_t i = 1; i < 5; i++) {
        float key = s[i];
        int8_t j = (int8_t)(i - 1);
        while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; j--; }
        s[j + 1] = key;
    }
    return s[2];
}

float takeOneSample(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH, 25000);          // 25 ms timeout
    return (duration == 0) ? 0.0f : (float)duration * 0.0343f / 2.0f;
}

// Advances the ultrasonic sampling by one shot per call (median of 5 per container, alcohol then scented).
// Returns true on the pass that finishes the second container.
bool tickSensorSampling() {
    if (sampleState == 0) return false;
    uint32_t now = millis();
    if (now - lastSampleStepMs < SAMPLE_GAP_MS) return false;
    lastSampleStepMs = now;
    uint8_t k = (uint8_t)(sampleState - 1);
    sampleBuf[sampleIndex++] = takeOneSample(US_TRIG[k], US_ECHO[k]);
    if (sampleIndex < 5) return false;
    sampleIndex = 0;
    float m = medianOf5(sampleBuf);
    usCm[k] = m;
    if (m > 0.0f) usBad[k] = 0; else if (usBad[k] < 255) usBad[k]++;
    if (sampleState == 1) { sampleState = 2; return false; }
    sampleState = 0;
    return true;
}

void readWaterLevelSensors() {
    for (uint8_t k = 0; k < 2; k++) {
        uint32_t sum = 0;
        for (uint8_t n = 0; n < WL_AVG_SAMPLES; n++) sum += analogRead(WL_PIN[k]);
        wl[k] = (uint16_t)(sum / WL_AVG_SAMPLES);
        bool beyondEnter = wlWetHigher ? (wl[k] >= wlEnter) : (wl[k] <= wlEnter);
        bool beyondLeave = wlWetHigher ? (wl[k] <= wlLeave) : (wl[k] >= wlLeave);
        bool flip = wet[k] ? beyondLeave : beyondEnter;
        if (flip) { if (++wetCnt[k] >= 2) { wet[k] = !wet[k]; wetCnt[k] = 0; } } else wetCnt[k] = 0;
    }
}

bool usOk(uint8_t k)    { return usCm[k] > 0.0f; }
bool blind(uint8_t k)   { return !wet[k] && !usOk(k); }                         // the level cannot be known
bool lowNow(uint8_t k)  { return !wet[k] && usOk(k) && usCm[k] >= lowCm[k]; }
bool fullNow(uint8_t k) { return wet[k] || (usOk(k) && usCm[k] <= fullCm[k]); }
uint8_t levelPct(uint8_t k) {
    if (wet[k]) return 100;
    if (!usOk(k)) return 0;
    float span = emptyCm[k] - fullCm[k];
    float p = span > 0.1f ? (emptyCm[k] - usCm[k]) * 100.0f / span : 0.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    return (uint8_t)(p + 0.5f);
}

// ---------------------------------------------------------------------
// Pumps - one at a time, always
// ---------------------------------------------------------------------
void pumpsAllOff() {
    for (uint8_t i = 0; i < ACS_PUMP_COUNT; i++) {
        if (pumpMask & (1u << i)) {
            digitalWrite(PUMP_PIN[i], PUMP_INACTIVE);
            Serial.printf("[ACS] %-17s (GPIO %2u) -> OFF\n", PUMP_NAME[i], PUMP_PIN[i]);
        }
    }
    pumpMask = 0;
}

void pumpOn(uint8_t i) {
    if (i >= ACS_PUMP_COUNT) return;
    if (pumpMask == (uint8_t)(1u << i)) return;
    if (pumpMask) { pumpsAllOff(); delay(60); }               // a short gap between two pumps
    digitalWrite(PUMP_PIN[i], PUMP_ACTIVE);
    pumpMask = (uint8_t)(1u << i);
    Serial.printf("[ACS] %-17s (GPIO %2u) -> ON\n", PUMP_NAME[i], PUMP_PIN[i]);
}

// ---------------------------------------------------------------------
// The machine
// ---------------------------------------------------------------------
bool reqPending(uint8_t k) { return reqUntil[k] != 0 && (int32_t)(reqUntil[k] - millis()) > 0; }

uint8_t waterStepOf(uint8_t k) { return k == 0 ? ACS_STEP_ALC_WATER_IN : ACS_STEP_SCT_WATER_IN; }
uint8_t finalStepOf(uint8_t k) { return k == 0 ? ACS_STEP_ALC_ALCOHOL_IN : ACS_STEP_SCT_SCENTED_IN; }
uint8_t sourceOfStep(uint8_t s) {
    if (s == ACS_STEP_ALC_ALCOHOL_IN) return 1;
    if (s == ACS_STEP_SCT_SCENTED_IN) return 2;
    return 0;
}

bool needsRecipe(uint8_t k) {
    return phase[k] != 0 || (!wet[k] && usOk(k) && usCm[k] >= topUpCm[k]);
}
bool deliverable(uint8_t k) { return !blind(k) && !lowNow(k) && phase[k] == 0; }

void startFill(uint8_t stepId) {
    uint8_t k = (stepId == ACS_STEP_ALC_WATER_IN || stepId == ACS_STEP_ALC_ALCOHOL_IN) ? 0 : 1;
    bool waterPhase = (stepId == ACS_STEP_ALC_WATER_IN || stepId == ACS_STEP_SCT_WATER_IN);

    if (waterPhase) {
        if (wet[k]) { setPhase(k, 0); Serial.printf("[ACS] %s is already full - nothing to add\n", MIX_NAME[k]); return; }
        if (phase[k] == 0) setPhase(k, 1);
        fillTargetCm = usCm[k] - (waterPct[k] / 100.0f) * (usCm[k] - fullCm[k]);   // the water share of what still fits
        if (usCm[k] - fillTargetCm < 0.15f) { setPhase(k, 2); startFill(finalStepOf(k)); return; }
    } else {
        if (fullNow(k)) { setPhase(k, 0); warnTimeout = false; Serial.printf("[ACS] %s is already full - recipe complete\n", MIX_NAME[k]); return; }
        fillTargetCm = fullCm[k];
    }

    fillK = k;
    fillSrc = sourceOfStep(stepId);
    if (waterPhase) fillSrc = 0;
    fillWaterPhase = waterPhase;
    fillStartUs = usCm[k];
    fillStartWl = wl[k];
    fillStartWet = wet[k];
    fillRise = false;
    riseTicks = 0;
    reachTicks = 0;
    float room = fillStartUs - fillTargetCm;
    fillNeedRise = room * 0.5f;
    if (fillNeedRise > riseMinCm) fillNeedRise = riseMinCm;
    if (fillNeedRise < 0.15f) fillNeedRise = 0.15f;
    step = stepId;
    stepStartMs = millis();
    uint8_t pump = waterPhase ? (k == 0 ? ACS_PUMP_WATER_A : ACS_PUMP_WATER_S) : (k == 0 ? ACS_PUMP_ALCOHOL : ACS_PUMP_SCENTED);
    Serial.printf("[ACS] %s: %s in, from %.1f cm towards %.1f cm (a rise of %.2f cm or a WL move of %u must show within %u s)\n",
                  MIX_NAME[k], SRC_NAME[fillSrc], (double)fillStartUs, (double)fillTargetCm, (double)fillNeedRise, (unsigned)wlRiseRaw, (unsigned)noRiseSec);
    pumpOn(pump);
}

void startDeliver(uint8_t k) {
    deliverStartUs = usCm[k];
    step = (k == 0) ? ACS_STEP_DELIVER_ALCOHOL : ACS_STEP_DELIVER_SCENTED;
    stepStartMs = millis();
    Serial.printf("[ACS] %s: delivering one shot (%u s max) to the box\n", MIX_NAME[k], (unsigned)deliverSec);
    pumpOn(k == 0 ? ACS_PUMP_MIXER_A : ACS_PUMP_MIXER_S);
}

void endDeliver(uint8_t k, const char* why) {
    pumpsAllOff();
    reqUntil[k] = 0;                                         // one request = one shot; the box asks again if it needs more
    step = ACS_STEP_IDLE;
    float drop = (usOk(k) && deliverStartUs > 0.0f) ? (usCm[k] - deliverStartUs) : 0.0f;
    Serial.printf("[ACS] %s: shot ended (%s) - level %.1f cm -> %.1f cm (%+.2f cm)\n", MIX_NAME[k], why, (double)deliverStartUs, (double)usCm[k], (double)drop);
}

void idleTick(uint32_t now) {
    // 1. a request from the box that the mixer container can serve
    for (uint8_t k = 0; k < 2; k++) {
        if (reqPending(k) && deliverable(k)) { startDeliver(k); return; }
    }
    // 2. a mixer container that needs (more) mix
    if ((int32_t)(holdoffUntil - now) > 0) return;
    for (uint8_t k = 0; k < 2; k++) {
        if (blind(k) || !needsRecipe(k)) continue;
        uint8_t next = (phase[k] == 2) ? finalStepOf(k) : waterStepOf(k);
        uint8_t src = (next == finalStepOf(k)) ? sourceOfStep(next) : 0;
        if (emptyFlag[src] && (int32_t)(emptyRetryAt[src] - now) > 0) continue;   // judged empty, the retry timer is still running
        startFill(next);
        if (step != ACS_STEP_IDLE) return;
    }
}

void fillTick(uint32_t now) {
    uint8_t k = fillK;
    uint32_t elapsed = now - stepStartMs;

    if (blind(k)) {
        pumpsAllOff();
        step = ACS_STEP_IDLE;
        holdoffUntil = now + (uint32_t)holdoffSec * 1000UL;
        Serial.printf("[ACS] %s: level can no longer be read - pump stopped, fills paused %u s\n", MIX_NAME[k], (unsigned)holdoffSec);
        return;
    }

    // did the level rise?  (US or WL; seen on two readings in a row)
    if (!fillRise) {
        bool usRise  = usOk(k) && fillStartUs > 0.0f && (fillStartUs - usCm[k]) >= fillNeedRise;
        int  wlMove  = wlWetHigher ? ((int)wl[k] - (int)fillStartWl) : ((int)fillStartWl - (int)wl[k]);
        bool wlRise  = wlMove >= (int)wlRiseRaw;
        bool wetRise = wet[k] && !fillStartWet;
        if (usRise || wlRise || wetRise) { if (++riseTicks >= 2) fillRise = true; } else riseTicks = 0;
        if (fillRise) {
            if (emptyFlag[fillSrc]) Serial.printf("[ACS] %s is flowing again - EMPTY verdict cleared\n", SRC_NAME[fillSrc]);
            emptyFlag[fillSrc] = false;
        }
    }

    // reached the line?
    bool reached = fillWaterPhase ? (wet[k] || (usOk(k) && usCm[k] <= fillTargetCm)) : fullNow(k);
    if (reached) { if (reachTicks < 255) reachTicks++; } else reachTicks = 0;
    if (reached && (reachTicks >= 2 || wet[k])) {
        if (!fillRise) { fillRise = true; emptyFlag[fillSrc] = false; }
        if (fillWaterPhase) {
            if (wet[k]) {                                    // full already: nothing more can go in
                pumpsAllOff(); setPhase(k, 0); step = ACS_STEP_IDLE;
                Serial.printf("[ACS] %s: WL is wet during the water step - container full, recipe ends\n", MIX_NAME[k]);
            } else {
                Serial.printf("[ACS] %s: water share reached (%.1f cm) -> %s next\n", MIX_NAME[k], (double)usCm[k], k == 0 ? "alcohol" : "scented");
                setPhase(k, 2);
                startFill(finalStepOf(k));                   // switches the pump over (or ends the recipe if already full)
                if (step != finalStepOf(k)) { pumpsAllOff(); step = ACS_STEP_IDLE; }
            }
        } else {
            pumpsAllOff();
            setPhase(k, 0);
            warnTimeout = false;
            step = ACS_STEP_IDLE;
            Serial.printf("[ACS] %s: FULL (%.1f cm%s) - recipe complete\n", MIX_NAME[k], (double)usCm[k], wet[k] ? ", WL wet" : "");
        }
        return;
    }

    // the boss's rule
    if (!fillRise && elapsed >= (uint32_t)noRiseSec * 1000UL) {
        pumpsAllOff();
        emptyFlag[fillSrc] = true;
        emptyRetryAt[fillSrc] = now + (uint32_t)emptyHoldSec * 1000UL;
        step = ACS_STEP_IDLE;
        Serial.printf("[ACS] *** %s EMPTY *** the %s pump ran %u s and the level did not rise - pump stopped, trying again in %u s\n",
                      SRC_NAME[fillSrc], SRC_NAME[fillSrc], (unsigned)noRiseSec, (unsigned)emptyHoldSec);
        return;
    }

    if (elapsed >= (uint32_t)phaseMaxSec * 1000UL) {
        pumpsAllOff();
        warnTimeout = true;
        holdoffUntil = now + (uint32_t)holdoffSec * 1000UL;
        step = ACS_STEP_IDLE;
        Serial.printf("[ACS] %s: the pump hit its %u s limit without reaching the line (blocked tube or a wrong line setting) - fills paused %u s\n",
                      MIX_NAME[k], (unsigned)phaseMaxSec, (unsigned)holdoffSec);
    }
}

void deliverTick(uint32_t now) {
    uint8_t k = (step == ACS_STEP_DELIVER_ALCOHOL) ? 0 : 1;
    if (lowNow(k)) endDeliver(k, "container reached LOW");
    else if (blind(k)) endDeliver(k, "level unknown");
    else if (now - stepStartMs >= (uint32_t)deliverSec * 1000UL) endDeliver(k, "shot done");
}

void controlTick() {
    uint32_t now = millis();
    if (maint) return;                                        // the pumps are set by hand
    if (!sensorsReady || now < BOOT_QUIET_MS) return;
    switch (step) {
        case ACS_STEP_IDLE:           idleTick(now); break;
        case ACS_STEP_ALC_WATER_IN:
        case ACS_STEP_ALC_ALCOHOL_IN:
        case ACS_STEP_SCT_WATER_IN:
        case ACS_STEP_SCT_SCENTED_IN: fillTick(now); break;
        case ACS_STEP_DELIVER_ALCOHOL:
        case ACS_STEP_DELIVER_SCENTED: deliverTick(now); break;
        default: step = ACS_STEP_IDLE; break;
    }
}

// Checked on every pass of loop(), not only on the half-second tick: a delivery shot ends on time, a hand-set pump never runs on.
void fastLimits(uint32_t now) {
    if ((step == ACS_STEP_DELIVER_ALCOHOL || step == ACS_STEP_DELIVER_SCENTED) && now - stepStartMs >= (uint32_t)deliverSec * 1000UL) {
        endDeliver(step == ACS_STEP_DELIVER_ALCOHOL ? 0 : 1, "shot done");
    }
    if (maint && pumpMask && now - manualOnMs >= MANUAL_PUMP_MAX_MS) {
        pumpsAllOff();
        Serial.println("[ACS] a hand-set pump ran 60 s - switched off");
    }
    if (maint && now - maintLastCmdMs >= MAINT_IDLE_MS) {
        maint = false;
        pumpsAllOff();
        Serial.println("[ACS] maintenance ended by itself (30 min without a command) - automatic work resumes");
    }
}

uint8_t shownStep() {
    if (maint) return ACS_STEP_MAINTENANCE;
    if (step != ACS_STEP_IDLE) return step;
    if (emptyFlag[0]) return ACS_STEP_WATER_EMPTY;
    if (emptyFlag[1]) return ACS_STEP_ALCOHOL_EMPTY;
    if (emptyFlag[2]) return ACS_STEP_SCENTED_EMPTY;
    return ACS_STEP_IDLE;
}

// ---------------------------------------------------------------------
// Commands (from the radio queue)
// ---------------------------------------------------------------------
void enterMaintenance() {
    maint = true;
    maintLastCmdMs = millis();
    pumpsAllOff();
    step = ACS_STEP_IDLE;
    Serial.println("[ACS] MAINTENANCE ON - automatic work paused, pumps by hand (one at a time, 60 s each)");
}
void leaveMaintenance() {
    maint = false;
    pumpsAllOff();
    Serial.println("[ACS] MAINTENANCE OFF - automatic work resumes");
}
void retryNow() {
    for (uint8_t s = 0; s < 3; s++) { emptyFlag[s] = false; emptyRetryAt[s] = 0; }
    holdoffUntil = 0;
    warnTimeout = false;
    Serial.println("[ACS] EMPTY verdicts and pauses forgotten - every supply is tried again");
}
void acceptMixers() {
    setPhase(0, 0);
    setPhase(1, 0);
    Serial.println("[ACS] both mixer containers accepted as they are (half-made-mix flags cleared)");
}

void manualPump(uint8_t i, bool on) {
    if (!maint) { Serial.println("[ACS] hand-set pump ignored - switch maintenance ON first"); return; }
    if (i >= ACS_PUMP_COUNT) return;
    maintLastCmdMs = millis();
    if (!on) { if (pumpMask & (1u << i)) pumpsAllOff(); return; }
    if (pumpMask && pumpMask != (uint8_t)(1u << i)) { Serial.println("[ACS] REFUSED: another pump is running (one pump at a time)"); return; }
    manualOnMs = millis();
    pumpOn(i);
}

void handleRequest(uint8_t k, uint8_t state) {
    if (k > 1) return;
    if (state == 1) {
        reqUntil[k] = 0;
        if ((k == 0 && step == ACS_STEP_DELIVER_ALCOHOL) || (k == 1 && step == ACS_STEP_DELIVER_SCENTED)) endDeliver(k, "the box says it is full");
        else Serial.printf("[ACS] %s: request cancelled by the box\n", MIX_NAME[k]);
        return;
    }
    if (maint) { Serial.printf("[ACS] %s: request ignored - maintenance is on\n", MIX_NAME[k]); return; }
    bool fresh = !reqPending(k);
    reqUntil[k] = millis() + (uint32_t)reqHoldSec * 1000UL;
    if (fresh) Serial.printf("[ACS] %s: the box asks for a top-up%s\n", MIX_NAME[k],
                             deliverable(k) ? "" : (blind(k) ? " (level unknown - cannot)" : (phase[k] ? " (half-made mix - not until the recipe is done)" : " (LOW - not until it is refilled)")));
}

void handleCommand(const CommandPacket& c) {
    char csv[200];
    switch (c.commandID) {
        case CMD_REQUEST_DELIVERY:
            handleRequest(c.subIndex, c.state);
            break;
        case CMD_SET_MAINTENANCE:
            if (c.state == 1) enterMaintenance();
            else if (c.state == 0) leaveMaintenance();
            else if (c.state == 2) retryNow();
            else if (c.state == 3) acceptMixers();
            break;
        case CMD_SET_RELAY:
            manualPump(c.subIndex, c.state == 1);
            break;
        case CMD_SET_CONFIG:
            strncpy(csv, c.payloadStr, sizeof(csv) - 1);
            csv[sizeof(csv) - 1] = '\0';
            if (c.subIndex == 1)      applyGroupCsv(GRP_LEVELS_AT, GRP_LEVELS_N, csv);
            else if (c.subIndex == 2) applyGroupCsv(GRP_TIMING_AT, GRP_TIMING_N, csv);
            else if (c.subIndex == 3) applyGroupCsv(GRP_MIX_AT, GRP_MIX_N, csv);
            else Serial.println("[ACS CONFIG] ignored: the old 4-tank settings list (subIndex 0) means nothing to this design");
            break;
        default:
            break;
    }
}

void processRx() {
    for (;;) {
        CommandPacket c;
        bool got = false;
        portENTER_CRITICAL(&rxMux);
        if (rxTail != rxHead) { c = rxQ[rxTail]; rxTail = (uint8_t)((rxTail + 1) % RXQ_SIZE); got = true; }
        portEXIT_CRITICAL(&rxMux);
        if (!got) return;
        handleCommand(c);
    }
}

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData || len != (int)sizeof(CommandPacket)) return;
    CommandPacket cmd;
    memcpy(&cmd, incomingData, sizeof(CommandPacket));
    if (cmd.targetDeviceID != ACS_ID) return;                 // traffic for another node (or for the other ACS)
    portENTER_CRITICAL(&rxMux);
    uint8_t next = (uint8_t)((rxHead + 1) % RXQ_SIZE);
    if (next != rxTail) { rxQ[rxHead] = cmd; rxHead = next; }
    portEXIT_CRITICAL(&rxMux);
}

// ---------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------
uint8_t warnBits() {
    uint8_t w = 0;
    if (usBad[0] >= 4) w |= ACS_W_US_ALC;
    if (usBad[1] >= 4) w |= ACS_W_US_SCT;
    if (wet[0] && usOk(0) && usCm[0] > fullCm[0] + DISAGREE_CM) w |= ACS_W_DISAGREE_A;
    if (wet[1] && usOk(1) && usCm[1] > fullCm[1] + DISAGREE_CM) w |= ACS_W_DISAGREE_S;
    if (phase[0]) w |= ACS_W_INCOMPLETE_A;
    if (phase[1]) w |= ACS_W_INCOMPLETE_S;
    if (warnTimeout) w |= ACS_W_TIMEOUT;
    return w;
}

void sendStatus() {
    uint32_t now = millis();
    ACSStatusPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID = ACS_ID;
    p.step     = shownStep();
    p.pumpMask = pumpMask;
    uint8_t f = 0;
    if (lowNow(0))  f |= ACS_F_ALC_LOW;
    if (fullNow(0)) f |= ACS_F_ALC_FULL;
    if (lowNow(1))  f |= ACS_F_SCT_LOW;
    if (fullNow(1)) f |= ACS_F_SCT_FULL;
    if (wet[0]) f |= ACS_F_ALC_WET;
    if (wet[1]) f |= ACS_F_SCT_WET;
    if (maint)  f |= ACS_F_MAINT;
    if (reqPending(0) || reqPending(1)) f |= ACS_F_REQUEST;
    p.flags = f;
    p.empty = (uint8_t)((emptyFlag[0] ? ACS_E_WATER : 0) | (emptyFlag[1] ? ACS_E_ALCOHOL : 0) | (emptyFlag[2] ? ACS_E_SCENTED : 0));
    p.warn  = warnBits();
    p.usAlcCm = usCm[0];
    p.usSctCm = usCm[1];
    p.wlAlcRaw = wl[0];
    p.wlSctRaw = wl[1];
    p.levelAlcPct = levelPct(0);
    p.levelSctPct = levelPct(1);
    uint32_t secs;
    if (step == ACS_STEP_IDLE && !maint && (emptyFlag[0] || emptyFlag[1] || emptyFlag[2])) {
        uint8_t s = emptyFlag[0] ? 0 : (emptyFlag[1] ? 1 : 2);
        int32_t left = (int32_t)(emptyRetryAt[s] - now);
        secs = left > 0 ? (uint32_t)(left / 1000) : 0;
    } else {
        secs = (now - stepStartMs) / 1000UL;
    }
    p.stepSec = (uint16_t)(secs > 65535UL ? 65535UL : secs);
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(ACSStatusPacket));
}

const char* stepName(uint8_t s) {
    switch (s) {
        case ACS_STEP_IDLE:            return "IDLE";
        case ACS_STEP_ALC_WATER_IN:    return "ALC WATER IN";
        case ACS_STEP_ALC_ALCOHOL_IN:  return "ALC ALCOHOL IN";
        case ACS_STEP_SCT_WATER_IN:    return "SCT WATER IN";
        case ACS_STEP_SCT_SCENTED_IN:  return "SCT SCENTED IN";
        case ACS_STEP_DELIVER_ALCOHOL: return "DELIVER ALCOHOL";
        case ACS_STEP_DELIVER_SCENTED: return "DELIVER SCENTED";
        case ACS_STEP_WATER_EMPTY:     return "WATER EMPTY";
        case ACS_STEP_ALCOHOL_EMPTY:   return "ALCOHOL EMPTY";
        case ACS_STEP_SCENTED_EMPTY:   return "SCENTED EMPTY";
        case ACS_STEP_MAINTENANCE:     return "MAINTENANCE";
        default:                       return "?";
    }
}

void printStatus() {
    Serial.printf("[%s] %s | ALC mixer US %.1f cm (%u%%) WL %u %s phase %u | SCN mixer US %.1f cm (%u%%) WL %u %s phase %u | pumps 0x%02X | empty W%d A%d S%d | req A%d S%d\n",
                  ACS_TAG, stepName(shownStep()),
                  (double)usCm[0], (unsigned)levelPct(0), (unsigned)wl[0], wet[0] ? "WET" : "dry", (unsigned)phase[0],
                  (double)usCm[1], (unsigned)levelPct(1), (unsigned)wl[1], wet[1] ? "WET" : "dry", (unsigned)phase[1],
                  (unsigned)pumpMask, emptyFlag[0] ? 1 : 0, emptyFlag[1] ? 1 : 0, emptyFlag[2] ? 1 : 0, reqPending(0) ? 1 : 0, reqPending(1) ? 1 : 0);
}

void printConfig() {
    Serial.printf("[%s] settings:\n", ACS_TAG);
    for (uint8_t i = 0; i < PARAM_COUNT; i++) {
        if (PARAMS[i].type == PT_FLOAT) Serial.printf("   %-10s %.2f\n", PARAMS[i].name, (double)paramGet(i));
        else                            Serial.printf("   %-10s %u\n", PARAMS[i].name, (unsigned)(paramGet(i) + 0.5f));
    }
}

// ---------------------------------------------------------------------
// USB console
// ---------------------------------------------------------------------
void consoleLine(char* line) {
    char* t[4] = { nullptr, nullptr, nullptr, nullptr };
    uint8_t n = 0;
    for (char* p = strtok(line, " \t"); p && n < 4; p = strtok(nullptr, " \t")) t[n++] = p;
    if (n == 0) return;
    if (!strcmp(t[0], "help")) {
        Serial.println("  status | cfg | maint 0/1 | pump <0-5> <0/1> (maintenance) | req a|s | cancel a|s | retry | mixok | reset");
        Serial.println("  set <name> <value>   names:");
        for (uint8_t i = 0; i < PARAM_COUNT; i++) Serial.printf("      %s", PARAMS[i].name);
        Serial.println();
        return;
    }
    if (!strcmp(t[0], "status")) { printStatus(); return; }
    if (!strcmp(t[0], "cfg"))    { printConfig(); return; }
    if (!strcmp(t[0], "maint") && n >= 2) { if (atoi(t[1])) enterMaintenance(); else leaveMaintenance(); return; }
    if (!strcmp(t[0], "pump") && n >= 3)  { manualPump((uint8_t)atoi(t[1]), atoi(t[2]) != 0); return; }
    if (!strcmp(t[0], "req") && n >= 2)    { handleRequest(t[1][0] == 's' ? 1 : 0, 0); return; }
    if (!strcmp(t[0], "cancel") && n >= 2) { handleRequest(t[1][0] == 's' ? 1 : 0, 1); return; }
    if (!strcmp(t[0], "retry"))  { retryNow(); return; }
    if (!strcmp(t[0], "mixok"))  { acceptMixers(); return; }
    if (!strcmp(t[0], "reset"))  { prefs.clear(); Serial.println("[ACS] settings erased - restarting with the defaults"); delay(200); ESP.restart(); return; }
    if (!strcmp(t[0], "set") && n >= 3) {
        for (uint8_t i = 0; i < PARAM_COUNT; i++) {
            if (strcmp(t[1], PARAMS[i].name)) continue;
            float v = (float)atof(t[2]);
            if (v < PARAMS[i].lo || v > PARAMS[i].hi) { Serial.printf("[ACS] %s must be %.1f..%.1f\n", PARAMS[i].name, (double)PARAMS[i].lo, (double)PARAMS[i].hi); return; }
            float old = paramGet(i);
            paramPut(i, v);
            if (!levelsValid()) { paramPut(i, old); Serial.println("[ACS] REFUSED: needs empty > low >= topup > full >= 1 for both containers"); return; }
            paramSave(i);
            Serial.printf("[ACS] %s = %s\n", PARAMS[i].name, t[2]);
            return;
        }
        Serial.println("[ACS] unknown setting - 'help' lists them");
        return;
    }
    Serial.println("[ACS] unknown command - 'help'");
}

void pollConsole() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r' || ch == '\n') {
            if (lineLen) { lineBuf[lineLen] = '\0'; consoleLine(lineBuf); lineLen = 0; }
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ch;
        }
    }
}

// ---------------------------------------------------------------------
void setup() {
    // 1. FIRST THING: every pump pin to its inactive level before it becomes an output (no click, no pulse).
    for (uint8_t i = 0; i < ACS_PUMP_COUNT; i++) {
        digitalWrite(PUMP_PIN[i], PUMP_INACTIVE);
        pinMode(PUMP_PIN[i], OUTPUT);
        digitalWrite(PUMP_PIN[i], PUMP_INACTIVE);
    }
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.printf("   NODE %s: REFILL SYSTEM (CORE 3.3.7)\n", ACS_TAG);
    Serial.println("=======================================================");

    for (uint8_t k = 0; k < 2; k++) {
        pinMode(US_TRIG[k], OUTPUT);
        digitalWrite(US_TRIG[k], LOW);
        pinMode(US_ECHO[k], INPUT);
        pinMode(WL_PIN[k], INPUT);
    }
    analogReadResolution(12);
    for (uint8_t k = 0; k < 2; k++) analogSetPinAttenuation(WL_PIN[k], ADC_11db);   // full 0..~3.3 V range, like A2

    prefs.begin("acs_nvs", false);
    loadAll();
    Serial.printf("[%s] mixer recipe progress from NVS: alcohol %u, scented %u (0 = fine)\n", ACS_TAG, (unsigned)phase[0], (unsigned)phase[1]);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);               // modem sleep makes ESP-NOW reception drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ACS] ESP-NOW: init failed - restarting in 3 s (pumps stay OFF)");
        delay(3000);
        ESP.restart();
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.printf("[%s] READY - all 6 pumps OFF; automatic work starts %u s after power-up. 'help' lists the console.\n", ACS_TAG, (unsigned)(BOOT_QUIET_MS / 1000));
}

void loop() {
    uint32_t now = millis();

    processRx();
    pollConsole();

    if (sampleState == 0 && now - lastCycleMs >= SENSOR_INTERVAL_MS) {
        lastCycleMs = now;
        sampleState = 1;
        sampleIndex = 0;
    }
    if (tickSensorSampling()) {
        readWaterLevelSensors();
        sensorsReady = true;
        controlTick();
    }
    fastLimits(millis());

    now = millis();
    if (now - lastStatusMs >= STATUS_INTERVAL_MS) { lastStatusMs = now; sendStatus(); }
    if (now - lastBcastMs >= SETTINGS_BCAST_MS)   { lastBcastMs = now; broadcastSettings(); }
    if (now - lastSummaryMs >= SUMMARY_MS)        { lastSummaryMs = now; printStatus(); }
}
