#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"

// =====================================================================
// NODE REFILL - THE REFILLING STATION (ESP32-WROOM)                          (core 3.3.7)
// Created 2026-11-03. It REPLACES ACS 1 and ACS 2. NOT YET FLASHED. Pin-out DECIDED 2026-11-03 (see below).
//
// Hardware: 3 analog water-level sensors (one in each box's Main Container) and 3 relay-driven NORMALLY CLOSED solenoid valves (one per box),
// all fed from ONE water supply. Nothing else: no pumps, no ultrasonic, no display.
//
// It works ALONE. The boxes never ask it for anything - every second it broadcasts a RefillStatusPacket (levels, valves, queue, failures) and the
// boxes' S3 nodes read their own container from it (CHECKING 2 s -> WELCOME -> SELECT MODE, or SELECT DRY ONLY when it is EMPTY / LOW or the
// station is silent for 15 s). The LocalServer and the Online Server only listen. The rules (Shared_Common.h has the same text next to the packet):
//   * LEVELS per box from the smoothed raw value: EMPTY = raw <= emptyMax (0), LOW = above that up to midEdge-1, MID = midEdge..highEdge-1,
//     HIGH = highEdge..maxEdge-1, MAX = maxEdge and above (defaults 0 / 500 / 1000 / 1200, per box, settable).
//   * A box whose container is EMPTY or LOW joins the QUEUE. ONE VALVE AT A TIME (shared supply): EMPTY boxes go first, LOW boxes second, inside a
//     group the one that waited longest goes first (then Box 1-2-3). A running refill is never interrupted. The next valve opens VALVE_GAP_MS after
//     the previous one closed.
//   * The valve closes when the level reaches MAX (the refill WORKED, the failure flag is cleared). It also closes - the refill FAILED, failMask
//     bit set, retry after retryS seconds, for ever - when the level does not RISE (it has to climb minRise above the last counted point) for
//     stallEmptyS seconds while EMPTY (the liquid has not reached the sensor yet) / stallLowS seconds once it reads LOW, or the valve has been open
//     maxRefillS seconds. When the level first reaches the sensor (EMPTY -> LOW) the stall timer starts afresh.
//   * A failure flag also clears when the container reads MID or higher for 3 s (somebody filled it by hand).
//   * MANUAL TEST (CMD_REFILL_TEST from the LocalServer): opens one valve for testSeconds, only while no valve is open and the gap after the last one has
//     passed (otherwise refused). A test does not touch the queue or the failure flags; queued boxes wait until it ends.
//   * Valves are closed at power-up and stay closed until the sensors have a full 1 s average and BOOT_QUIET_MS has passed.
//
// SETTINGS (NVS, survive a power cut; the LocalServer edits them with CMD_SET_CONFIG, target DEVICE_REFILL, payloadStr CSV; the station re-broadcasts
// what it holds as CMD_SET_CONFIG target 0 state 4, one list every 2 s, so the LocalServer page always shows what the station really has):
//   subIndex 1 / 2 / 3 = Box 1 / 2 / 3: emptyMax,midEdge,highEdge,maxEdge,stallEmptyS,stallLowS,maxRefillS,retryS,minRise    (defaults 0,500,1000,1200,30,5,180,60,10)
//   subIndex 4         = testSeconds                                                                                           (default 2)
// Everything can also be done on the USB serial console: type "help".
//
// HARDWARE NOTES for the hardware team:
//  * PIN-OUT (decided 2026-11-03, ESP32-WROOM + expansion board; only the two arrays below change if a pin is ever moved):
//        water-level sensor  Box 1 = GPIO 34   Box 2 = GPIO 35   Box 3 = GPIO 32      (signal wire only; power + GND from the board)
//        valve relay input   Box 1 = GPIO 21   Box 2 = GPIO 19   Box 3 = GPIO 18      (relay IN1 / IN2 / IN3)
//    Sensors MUST be on ADC1 pins (GPIO 32-39): ADC2 cannot be read while Wi-Fi / ESP-NOW is running. The relay pins are not strapping pins
//    (0, 2, 5, 12, 15), not flash pins (6-11) and stay quiet at power-up.
//  * RELAY LEVEL: the project's relay modules are all ACTIVE-LOW (LOW = relay on = coil energised = valve OPEN), so VALVE_ACTIVE_LOW is 1. If the
//    modules you fit are active-HIGH, set it to 0. First power-up with the valves UNPLUGGED: the three relay LEDs must be OFF while the station idles.
//  * Power the sensors from 3.3 V (the expansion board's voltage jumper): the ADC input must never see more than 3.3 V.
//  * The station stands apart from Boxes 1-3 and has no coin signal, so no extra coil suppression was asked for (2026-11-03). If the station resets
//    when a valve switches, put a flyback diode (DC valve) or RC snubber (AC valve) across that coil.
// =====================================================================

// ---------------------------------------------------------------------
// Pins (decided 2026-11-03 for ESP32-WROOM + expansion board)
// ---------------------------------------------------------------------
const uint8_t SENSOR_PIN[3] = { 34, 35, 32 };     // water-level sensors of Box 1 / 2 / 3 (ADC1; 34/35 are input-only, 32 is ADC1 CH4)
const uint8_t VALVE_PIN[3]  = { 21, 19, 18 };     // valve relays of Box 1 / 2 / 3 (quiet at boot, not strapping pins)
// WHICH BOXES THE STATION WATCHES AND REFILLS: since 2026-11-12 ALL THREE (Box 1 and Box 2 now run the same firmware as Box 3 and read the station the same way).
// Until 2026-11-12 it was only Box 3. A box set to false is never read for a fault, never queued, its valve is never opened (not even by the manual test) and
// the station reports it as REFILL_OFF ("not watched") with raw 0 - set an entry to false while that box's sensor / valve is not wired yet, then reflash the
// station. NOTE: a watched box whose sensor / valve is not wired shows up as EMPTY or SENSOR FAULT (and a refill that cannot raise the level ends as FAILED), so leave
// a box at false until its water-level sensor and valve are connected.
const bool WATCH_BOX[3] = { true, true, true };
#define VALVE_ACTIVE_LOW 1                        // 1 = active-LOW relay modules (like every other relay board of the project), 0 = active-HIGH
#if VALVE_ACTIVE_LOW
  #define VALVE_OPEN_LEVEL   LOW                  // relay on = coil energised = valve OPEN (the valves are normally closed)
  #define VALVE_CLOSED_LEVEL HIGH
#else
  #define VALVE_OPEN_LEVEL   HIGH
  #define VALVE_CLOSED_LEVEL LOW
#endif

// ---------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------
const uint32_t SAMPLE_MS         = 50;            // one ADC sample per sensor every 50 ms ...
const uint8_t  AVG_N             = 20;            // ... averaged over the last 20 = a 1 second moving average (the "raw" of the packet)
const uint32_t TICK_MS           = 200;           // the logic runs five times a second
const uint32_t STATUS_MS         = 1000;
const uint32_t STATUS_MIN_GAP_MS = 250;           // a state change is announced at once, but not faster than this
const uint32_t SETTINGS_BCAST_MS = 2000;          // one settings list per 2 s, round robin
const uint32_t BOOT_QUIET_MS     = 3000;
const uint32_t VALVE_GAP_MS      = 1000;          // pause between one valve closing and the next opening
const uint8_t  CALM_TICKS        = 15;            // 3 s of "no refill needed" before a box leaves the queue / its failure flag clears

// SENSOR HEALTH (2026-11-04). A water-level sensor that sits on the 3.3 V rail, or whose reading jumps about wildly while its valve is CLOSED (a floating or loose
// SIG wire), is a SENSOR FAULT: the box reports level UNKNOWN (255: the boxes then offer DRYING only, the pages say SENSOR FAULT), leaves the queue and its valve is
// closed. It clears by itself after 10 s of good readings. What the value alone cannot tell: a dry sensor that reads 0 and a dead sensor that reads 0 - that case
// shows up as a refill that does not rise (FAILED). Console: "sensors" prints min / max / spread of every sensor.
const uint16_t HEALTH_RAIL_RAW     = 4090;        // at or above this the input sits on the 3.3 V rail ...
const uint16_t HEALTH_RAIL_MAXEDGE = 4000;        // ... unless the box's MAX edge is set that high (a sensor that really saturates when wet)
const uint16_t HEALTH_NOISE_RAW    = 1500;        // biggest minus smallest reading within the last second, valve closed
const uint8_t  HEALTH_BAD_TICKS    = 25;          // 5 s of bad readings declare the fault
const uint8_t  HEALTH_GOOD_TICKS   = 50;          // 10 s of good readings clear it

// ---------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------
struct BoxCfg { uint16_t emptyMax, midEdge, highEdge, maxEdge, stallEmptyS, stallLowS, maxRefillS, retryS, minRise; };
static_assert(sizeof(BoxCfg) == 18, "BoxCfg must be 9 uint16_t");
BoxCfg   cfg[3] = { {0, 500, 1000, 1200, 30, 5, 180, 60, 10}, {0, 500, 1000, 1200, 30, 5, 180, 60, 10}, {0, 500, 1000, 1200, 30, 5, 180, 60, 10} };
uint16_t testSeconds = 2;
const char* FIELD_NAME[9] = { "empty", "mid", "high", "max", "stalle", "stalll", "cap", "retry", "rise" };
Preferences prefs;

// ---------------------------------------------------------------------
// Live data and the refill logic's memory
// ---------------------------------------------------------------------
enum BoxPhase : uint8_t { PH_IDLE = 0, PH_QUEUED, PH_REFILL, PH_RETRY };
struct BoxRt {
    uint8_t  phase;
    uint32_t queuedAtMs;      // when the box joined the queue
    uint32_t openedMs;        // when its valve opened
    uint32_t progressMs;      // when the level last RAISED by minRise (or the refill started / the liquid reached the sensor)
    uint32_t retryAtMs;       // when a failed box may try again
    uint16_t peak;            // the raw value of the last counted progress
    uint8_t  lastLvl;
    uint8_t  calm;            // consecutive ticks without a refill need
    bool     failed;          // the last refill FAILED (stays until one works)
};
BoxRt    box[3];
uint16_t ring[3][AVG_N];
uint8_t  ringPos = 0, ringCount = 0;
uint16_t rawAvg[3] = { 0, 0, 0 };
uint16_t rawMin[3] = { 0, 0, 0 }, rawMax[3] = { 0, 0, 0 };     // smallest / biggest reading inside the last second (sensor health)
bool     sensorFault[3] = { false, false, false };
uint8_t  badTicks[3] = { 0, 0, 0 }, goodTicks[3] = { 0, 0, 0 };
const char* faultWhy[3] = { "", "", "" };
uint8_t  lvl[3] = { REFILL_UNKNOWN, REFILL_UNKNOWN, REFILL_UNKNOWN };
uint8_t  valveMask = 0;
int8_t   runBox = -1;                 // the box whose REFILL valve is open
int8_t   testBox = -1;                // the box whose TEST valve is open
uint32_t testEndMs = 0;
uint32_t lastCloseMs = 0;
uint32_t lastSampleMs = 0, lastTickMs = 0, lastStatusMs = 0, lastBcastMs = 0, lastSummaryMs = 0, lastRawPrintMs = 0;
uint16_t statusSeq = 0;
uint8_t  bcastRound = 0;
uint16_t lastSentSig = 0xFFFF;        // what the last status announced (valve + queue + fail + test bits), to send changes at once
bool     rawStream = false;           // console "raw": print the readings twice a second

static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
#define RXQ_SIZE 6
CommandPacket rxQ[RXQ_SIZE];
volatile uint8_t rxHead = 0, rxTail = 0;
char    lineBuf[96];
uint8_t lineLen = 0;

const char* LVL_NAME[5] = { "EMPTY", "LOW", "MID", "HIGH", "MAX" };
const char* lvlName(uint8_t s) { return s < 5 ? LVL_NAME[s] : (s == REFILL_OFF ? "NOT WATCHED" : "UNKNOWN"); }

// ---------------------------------------------------------------------
// Settings: NVS, CSV, checks
// ---------------------------------------------------------------------
bool boxCfgOk(const long* v) {
    if (v[0] < 0 || v[0] > 4094) return false;
    if (!(v[0] < v[1] && v[1] < v[2] && v[2] < v[3] && v[3] <= 4095)) return false;     // empty < mid < high < max
    if (v[4] < 1 || v[4] > 600 || v[5] < 1 || v[5] > 600) return false;
    if (v[6] < 10 || v[6] > 1800) return false;
    if (v[7] < 5 || v[7] > 3600) return false;
    if (v[8] < 1 || v[8] > 500) return false;
    return true;
}

// parse up to n whole numbers separated by commas; returns how many (or -1 if something is not a number)
int parseInts(const char* s, long* out, int maxN) {
    int n = 0;
    const char* p = s;
    while (*p && n < maxN) {
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) return -1;
        out[n++] = v;
        p = end;
        if (*p == '.') { while (*p && *p != ',') p++; }      // "30.0" from a page: the fraction is dropped
        if (*p == ',') p++;
        else if (*p != '\0') return -1;
    }
    return n;
}

void saveBox(uint8_t i) { char k[8]; snprintf(k, sizeof(k), "box%u", (unsigned)(i + 1)); prefs.putBytes(k, &cfg[i], sizeof(BoxCfg)); }
void saveTest() { prefs.putUShort("tsec", testSeconds); }

void loadAll() {
    for (uint8_t i = 0; i < 3; i++) {
        char k[8]; snprintf(k, sizeof(k), "box%u", (unsigned)(i + 1));
        BoxCfg t;
        if (prefs.getBytes(k, &t, sizeof(BoxCfg)) == sizeof(BoxCfg)) {
            long v[9]; const uint16_t* f = (const uint16_t*)&t;
            for (uint8_t j = 0; j < 9; j++) v[j] = f[j];
            if (boxCfgOk(v)) cfg[i] = t;
        }
    }
    uint16_t ts = prefs.getUShort("tsec", testSeconds);
    if (ts >= 1 && ts <= 30) testSeconds = ts;
}

void boxToCsv(uint8_t i, char* out, size_t n) {
    const uint16_t* f = (const uint16_t*)&cfg[i];
    snprintf(out, n, "%u,%u,%u,%u,%u,%u,%u,%u,%u", f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
}

bool applyBoxCsv(uint8_t i, const char* csv) {
    long v[9];
    if (parseInts(csv, v, 9) != 9 || !boxCfgOk(v)) { Serial.printf("[REFILL] REFUSED the settings of Box %u: %s\n", (unsigned)(i + 1), csv); return false; }
    uint16_t* f = (uint16_t*)&cfg[i];
    for (uint8_t j = 0; j < 9; j++) f[j] = (uint16_t)v[j];
    saveBox(i);
    Serial.printf("[REFILL] Box %u settings: %s\n", (unsigned)(i + 1), csv);
    return true;
}

bool applyTestCsv(const char* csv) {
    long v[1];
    if (parseInts(csv, v, 1) != 1 || v[0] < 1 || v[0] > 30) { Serial.printf("[REFILL] REFUSED the test time: %s\n", csv); return false; }
    testSeconds = (uint16_t)v[0];
    saveTest();
    Serial.printf("[REFILL] valve test time: %u s\n", (unsigned)testSeconds);
    return true;
}

// ---------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------
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
    char buf[64];
    uint8_t round = bcastRound;
    bcastRound = (uint8_t)((bcastRound + 1) % 4);
    if (round < 3) { boxToCsv(round, buf, sizeof(buf)); sendCmdPkt(0, CMD_SET_CONFIG, (uint8_t)(round + 1), 0, 4, buf); }
    else           { snprintf(buf, sizeof(buf), "%u", (unsigned)testSeconds); sendCmdPkt(0, CMD_SET_CONFIG, 4, 0, 4, buf); }
}

uint8_t queueMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < 3; i++) if (box[i].phase == PH_QUEUED) m |= (uint8_t)(1u << i);
    return m;
}
uint8_t failMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < 3; i++) if (box[i].failed) m |= (uint8_t)(1u << i);
    return m;
}
uint8_t testMask() { return testBox >= 0 ? (uint8_t)(1u << testBox) : 0; }
uint16_t signature() { return (uint16_t)(valveMask | (queueMask() << 3) | (failMask() << 6) | (testMask() << 9)); }

void sendStatus() {
    RefillStatusPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID = DEVICE_REFILL;
    p.magic    = REFILL_MAGIC;
    p.seq      = ++statusSeq;
    for (uint8_t i = 0; i < 3; i++) {
        p.raw[i]      = WATCH_BOX[i] ? rawAvg[i] : 0;
        p.state[i]    = WATCH_BOX[i] ? lvl[i] : (uint8_t)REFILL_OFF;
        p.midEdge[i]  = cfg[i].midEdge;
        p.highEdge[i] = cfg[i].highEdge;
        p.maxEdge[i]  = cfg[i].maxEdge;
    }
    p.valveMask = valveMask;
    p.queueMask = queueMask();
    p.failMask  = failMask();
    p.testMask  = testMask();
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
    lastSentSig = signature();
    lastStatusMs = millis();
}

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData || len != (int)sizeof(CommandPacket)) return;
    CommandPacket cmd;
    memcpy(&cmd, incomingData, sizeof(CommandPacket));
    if (cmd.targetDeviceID != DEVICE_REFILL) return;
    portENTER_CRITICAL(&rxMux);
    uint8_t next = (uint8_t)((rxHead + 1) % RXQ_SIZE);
    if (next != rxTail) { rxQ[rxHead] = cmd; rxHead = next; }
    portEXIT_CRITICAL(&rxMux);
}

// ---------------------------------------------------------------------
// Valves (never more than one open - openValve() refuses)
// ---------------------------------------------------------------------
bool openValve(uint8_t i) {
    if (valveMask & ~(uint8_t)(1u << i)) { Serial.printf("[REFILL] SAFETY: valve %u NOT opened, another valve is open\n", (unsigned)(i + 1)); return false; }
    digitalWrite(VALVE_PIN[i], VALVE_OPEN_LEVEL);
    valveMask |= (uint8_t)(1u << i);
    return true;
}
void closeValve(uint8_t i) {
    digitalWrite(VALVE_PIN[i], VALVE_CLOSED_LEVEL);
    valveMask &= (uint8_t)~(1u << i);
}
void closeAllValves() { for (uint8_t i = 0; i < 3; i++) closeValve(i); }

// ---------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------
void sampleSensors() {
    for (uint8_t i = 0; i < 3; i++) {
        uint32_t s = 0;
        for (uint8_t k = 0; k < 4; k++) s += analogRead(SENSOR_PIN[i]);
        ring[i][ringPos] = (uint16_t)(s / 4);
    }
    ringPos = (uint8_t)((ringPos + 1) % AVG_N);
    if (ringCount < AVG_N) ringCount++;
    for (uint8_t i = 0; i < 3; i++) {
        uint32_t s = 0;
        uint16_t lo = 65535, hi = 0;
        for (uint8_t k = 0; k < ringCount; k++) { s += ring[i][k]; if (ring[i][k] < lo) lo = ring[i][k]; if (ring[i][k] > hi) hi = ring[i][k]; }
        rawAvg[i] = (uint16_t)(s / ringCount);
        rawMin[i] = lo; rawMax[i] = hi;
    }
}
bool sensorsReady() { return ringCount >= AVG_N; }

uint8_t levelOf(uint8_t i, uint16_t raw) {
    const BoxCfg& c = cfg[i];
    if (raw >= c.maxEdge)  return REFILL_MAX;
    if (raw >= c.highEdge) return REFILL_HIGH;
    if (raw >= c.midEdge)  return REFILL_MID;
    if (raw > c.emptyMax)  return REFILL_LOW;
    return REFILL_EMPTY;
}
bool needs(uint8_t i) { return lvl[i] == REFILL_EMPTY || lvl[i] == REFILL_LOW; }

// ---------------------------------------------------------------------
// The refill logic (called every TICK_MS)
// ---------------------------------------------------------------------
void finishRefill(uint8_t i, uint32_t now) {
    closeValve(i);
    runBox = -1;
    lastCloseMs = now;
    box[i].phase = PH_IDLE;
    box[i].failed = false;
    box[i].calm = 0;
    Serial.printf("[REFILL] Box %u: MAX reached after %lu s - valve CLOSED, refill worked\n", (unsigned)(i + 1), (unsigned long)((now - box[i].openedMs) / 1000UL));
}

void failRefill(uint8_t i, uint32_t now, const char* why) {
    closeValve(i);
    runBox = -1;
    lastCloseMs = now;
    box[i].phase = PH_RETRY;
    box[i].failed = true;
    box[i].retryAtMs = now + 1000UL * cfg[i].retryS;
    Serial.printf("[REFILL] Box %u: refill FAILED (%s) after %lu s - valve CLOSED, trying again in %u s\n", (unsigned)(i + 1), why, (unsigned long)((now - box[i].openedMs) / 1000UL), (unsigned)cfg[i].retryS);
}

// is box a ahead of box b in the queue? EMPTY first, then LOW; inside a group the one that waited longest, then the lower box number
bool aheadOf(uint8_t a, uint8_t b) {
    uint8_t ca = (lvl[a] == REFILL_EMPTY) ? 0 : 1, cb = (lvl[b] == REFILL_EMPTY) ? 0 : 1;
    if (ca != cb) return ca < cb;
    int32_t d = (int32_t)(box[a].queuedAtMs - box[b].queuedAtMs);
    if (d != 0) return d < 0;
    return a < b;
}

// A sensor fault is not a failed refill: the valve just closes and the box leaves the queue (its level reads UNKNOWN until the sensor is good again).
void stopRunForFault(uint8_t i, uint32_t now) {
    closeValve(i);
    runBox = -1;
    lastCloseMs = now;
    box[i].phase = PH_IDLE;
    box[i].calm = 0;
}

void updateHealth(uint32_t now) {
    for (uint8_t i = 0; i < 3; i++) {
        if (!WATCH_BOX[i]) { sensorFault[i] = false; badTicks[i] = goodTicks[i] = 0; continue; }       // not watched: no sensor wired, nothing to judge
        bool valveOpen = ((valveMask >> i) & 1) != 0;
        const char* why = nullptr;
        if (rawAvg[i] >= HEALTH_RAIL_RAW && cfg[i].maxEdge < HEALTH_RAIL_MAXEDGE) why = "reads the 3.3 V rail (shorted wire or broken sensor)";
        else if (!valveOpen && (uint16_t)(rawMax[i] - rawMin[i]) > HEALTH_NOISE_RAW) why = "jumps about wildly (floating or loose wire)";
        if (why) {
            goodTicks[i] = 0;
            if (badTicks[i] < 255) badTicks[i]++;
            if (!sensorFault[i] && badTicks[i] >= HEALTH_BAD_TICKS) {
                sensorFault[i] = true;
                faultWhy[i] = why;
                Serial.printf("[REFILL] Box %u: SENSOR FAULT - the water-level sensor %s. Reporting UNKNOWN, no refill for this box.\n", (unsigned)(i + 1), why);
                if (runBox == (int8_t)i) stopRunForFault(i, now);
            }
        } else {
            badTicks[i] = 0;
            if (goodTicks[i] < 255) goodTicks[i]++;
            if (sensorFault[i] && goodTicks[i] >= HEALTH_GOOD_TICKS) {
                sensorFault[i] = false;
                Serial.printf("[REFILL] Box %u: the water-level sensor reads normally again - fault cleared\n", (unsigned)(i + 1));
            }
        }
    }
}

void logicTick(uint32_t now) {
    if (!sensorsReady() || now < BOOT_QUIET_MS) return;
    updateHealth(now);
    for (uint8_t i = 0; i < 3; i++) lvl[i] = !WATCH_BOX[i] ? (uint8_t)REFILL_OFF : (sensorFault[i] ? (uint8_t)REFILL_UNKNOWN : levelOf(i, rawAvg[i]));

    // 1. the refill whose valve is open
    if (runBox >= 0) {
        uint8_t r = (uint8_t)runBox;
        BoxRt& b = box[r];
        const BoxCfg& c = cfg[r];
        if (lvl[r] == REFILL_MAX) {
            finishRefill(r, now);
        } else {
            if (b.lastLvl == REFILL_EMPTY && lvl[r] != REFILL_EMPTY) b.progressMs = now;          // the liquid just reached the sensor: a fresh allowance
            b.lastLvl = lvl[r];
            if ((uint32_t)rawAvg[r] >= (uint32_t)b.peak + c.minRise) { b.peak = rawAvg[r]; b.progressMs = now; }
            uint32_t stallMs = 1000UL * (lvl[r] == REFILL_EMPTY ? c.stallEmptyS : c.stallLowS);
            if (now - b.progressMs >= stallMs)                    failRefill(r, now, "the level does not rise");
            else if (now - b.openedMs >= 1000UL * c.maxRefillS)   failRefill(r, now, "time limit");
        }
    }

    // 2. the other boxes: queue in / out, retry waits, failure flags
    for (uint8_t i = 0; i < 3; i++) {
        if ((int8_t)i == runBox) continue;
        BoxRt& b = box[i];
        bool need = needs(i);
        if (need) b.calm = 0; else if (b.calm < 255) b.calm++;
        if (b.failed && lvl[i] >= REFILL_MID && lvl[i] <= REFILL_MAX && b.calm >= CALM_TICKS) {            // filled by hand: the problem is gone
            b.failed = false;
            if (b.phase == PH_RETRY) b.phase = PH_IDLE;
            Serial.printf("[REFILL] Box %u: reads %s again - failure flag cleared\n", (unsigned)(i + 1), lvlName(lvl[i]));
        }
        if (b.phase == PH_RETRY && (int32_t)(now - b.retryAtMs) >= 0) b.phase = PH_IDLE;
        if (b.phase == PH_IDLE && need) {
            b.phase = PH_QUEUED;
            b.queuedAtMs = now;
            Serial.printf("[REFILL] Box %u: %s - waiting in the queue\n", (unsigned)(i + 1), lvlName(lvl[i]));
        } else if (b.phase == PH_QUEUED && !need && b.calm >= CALM_TICKS) {
            b.phase = PH_IDLE;
            Serial.printf("[REFILL] Box %u: reads %s again - left the queue\n", (unsigned)(i + 1), lvlName(lvl[i]));
        }
    }

    // 3. the manual test ends
    if (testBox >= 0 && (int32_t)(now - testEndMs) >= 0) {
        closeValve((uint8_t)testBox);
        Serial.printf("[REFILL] valve %d test over - CLOSED\n", testBox + 1);
        testBox = -1;
        lastCloseMs = now;
    }

    // 4. nobody has a valve open: the first box of the queue gets its turn
    if (runBox < 0 && testBox < 0 && valveMask == 0 && now - lastCloseMs >= VALVE_GAP_MS) {
        int8_t pick = -1;
        for (uint8_t i = 0; i < 3; i++) {
            if (box[i].phase != PH_QUEUED) continue;
            if (pick < 0 || aheadOf(i, (uint8_t)pick)) pick = (int8_t)i;
        }
        if (pick >= 0 && openValve((uint8_t)pick)) {
            BoxRt& b = box[pick];
            runBox = pick;
            b.phase = PH_REFILL;
            b.openedMs = b.progressMs = now;
            b.peak = rawAvg[pick];
            b.lastLvl = lvl[pick];
            Serial.printf("[REFILL] Box %d: %s (raw %u) - valve OPEN\n", pick + 1, lvlName(lvl[pick]), (unsigned)rawAvg[pick]);
        }
    }
}

// ---------------------------------------------------------------------
// Manual test
// ---------------------------------------------------------------------
void startTest(uint8_t i, uint32_t now) {
    if (i > 2) return;
    if (!WATCH_BOX[i]) { Serial.printf("[REFILL] test of valve %u REFUSED: Box %u is not watched for now (WATCH_BOX)\n", (unsigned)(i + 1), (unsigned)(i + 1)); return; }
    if (!sensorsReady() || now < BOOT_QUIET_MS || valveMask != 0 || testBox >= 0 || now - lastCloseMs < VALVE_GAP_MS) {
        Serial.printf("[REFILL] test of valve %u REFUSED: busy (a valve is open or just closed)\n", (unsigned)(i + 1));
        return;
    }
    if (!openValve(i)) return;
    testBox = (int8_t)i;
    testEndMs = now + 1000UL * testSeconds;
    Serial.printf("[REFILL] valve %u TEST: open for %u s\n", (unsigned)(i + 1), (unsigned)testSeconds);
}
void stopTest(uint8_t i, uint32_t now) {
    if (testBox != (int8_t)i) return;
    closeValve(i);
    testBox = -1;
    lastCloseMs = now;
    Serial.printf("[REFILL] valve %u test stopped - CLOSED\n", (unsigned)(i + 1));
}

// ---------------------------------------------------------------------
// Commands from the LocalServer
// ---------------------------------------------------------------------
void handleCommand(const CommandPacket& c) {
    char csv[sizeof(c.payloadStr) + 1];
    switch (c.commandID) {
        case CMD_SET_CONFIG:
            memcpy(csv, c.payloadStr, sizeof(c.payloadStr));
            csv[sizeof(c.payloadStr)] = '\0';
            if (c.subIndex >= 1 && c.subIndex <= 3) applyBoxCsv((uint8_t)(c.subIndex - 1), csv);
            else if (c.subIndex == 4)               applyTestCsv(csv);
            break;
        case CMD_REFILL_TEST:
            if (c.state == 1) startTest(c.subIndex, millis());
            else              stopTest(c.subIndex, millis());
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

// ---------------------------------------------------------------------
// USB console
// ---------------------------------------------------------------------
void printStatus() {
    Serial.printf("[REFILL] ");
    for (uint8_t i = 0; i < 3; i++) {
        if (!WATCH_BOX[i]) { Serial.printf("Box %u NOT WATCHED%s", (unsigned)(i + 1), i < 2 ? " | " : ""); continue; }
        const char* ph = box[i].phase == PH_REFILL ? "REFILLING" : (box[i].phase == PH_QUEUED ? "queued" : (box[i].phase == PH_RETRY ? "retry wait" : "idle"));
        Serial.printf("Box %u %s raw %u (%s%s%s)%s", (unsigned)(i + 1), sensorFault[i] ? "SENSOR FAULT" : lvlName(lvl[i]), (unsigned)rawAvg[i], ph, box[i].failed ? ", last refill FAILED" : "", sensorFault[i] ? ", sensor fault" : "", i < 2 ? " | " : "");
    }
    Serial.printf("\n[REFILL] valves open: %s%s\n", valveMask ? "" : "none", testBox >= 0 ? " (manual test)" : "");
}

void printConfig() {
    Serial.println("[REFILL] settings (empty,mid,high,max,stalle,stalll,cap,retry,rise):");
    char buf[64];
    for (uint8_t i = 0; i < 3; i++) { boxToCsv(i, buf, sizeof(buf)); Serial.printf("   Box %u: %s\n", (unsigned)(i + 1), buf); }
    Serial.printf("   valve test: %u s\n", (unsigned)testSeconds);
}

void consoleLine(char* line) {
    char* t[4] = { nullptr, nullptr, nullptr, nullptr };
    uint8_t n = 0;
    for (char* p = strtok(line, " \t"); p && n < 4; p = strtok(nullptr, " \t")) t[n++] = p;
    if (n == 0) return;
    if (!strcmp(t[0], "help")) {
        Serial.println("  status | cfg | sensors (health) | raw (live readings on/off) | test <1-3> | stop | set <1-3> <name> <value> | settest <s> | reset");
        Serial.print("  set names:"); for (uint8_t j = 0; j < 9; j++) Serial.printf(" %s", FIELD_NAME[j]);
        Serial.println();
        return;
    }
    if (!strcmp(t[0], "sensors")) {
        Serial.println("[REFILL] sensor health (raw 0..4095; a dry sensor is about 0, a wire that floats jumps about, 4095 = on the 3.3 V rail):");
        for (uint8_t i = 0; i < 3; i++) {
            if (!WATCH_BOX[i]) { Serial.printf("   Box %u: not watched for now (WATCH_BOX), no sensor is judged\n", (unsigned)(i + 1)); continue; }
            Serial.printf("   Box %u (GPIO %u): average %u | last second min %u max %u spread %u | %s%s\n", (unsigned)(i + 1), (unsigned)SENSOR_PIN[i], (unsigned)rawAvg[i], (unsigned)rawMin[i], (unsigned)rawMax[i],
                          (unsigned)(rawMax[i] - rawMin[i]), sensorFault[i] ? "FAULT: " : "looks fine", sensorFault[i] ? faultWhy[i] : "");
        }
        return;
    }
    if (!strcmp(t[0], "status")) { printStatus(); return; }
    if (!strcmp(t[0], "cfg"))    { printConfig(); return; }
    if (!strcmp(t[0], "raw"))    { rawStream = !rawStream; Serial.printf("[REFILL] live readings %s\n", rawStream ? "ON" : "off"); return; }
    if (!strcmp(t[0], "test") && n >= 2) { startTest((uint8_t)(atoi(t[1]) - 1), millis()); return; }
    if (!strcmp(t[0], "stop"))   { if (testBox >= 0) stopTest((uint8_t)testBox, millis()); return; }
    if (!strcmp(t[0], "settest") && n >= 2) { char b[8]; snprintf(b, sizeof(b), "%d", atoi(t[1])); applyTestCsv(b); return; }
    if (!strcmp(t[0], "reset"))  { prefs.clear(); Serial.println("[REFILL] settings erased - restarting with the defaults"); delay(200); ESP.restart(); return; }
    if (!strcmp(t[0], "set") && n >= 4) {
        int b = atoi(t[1]);
        if (b < 1 || b > 3) { Serial.println("[REFILL] box must be 1..3"); return; }
        for (uint8_t j = 0; j < 9; j++) {
            if (strcmp(t[2], FIELD_NAME[j])) continue;
            char csv[64];
            uint16_t keep[9];
            uint16_t* f = (uint16_t*)&cfg[b - 1];
            memcpy(keep, f, sizeof(keep));
            f[j] = (uint16_t)atoi(t[3]);
            boxToCsv((uint8_t)(b - 1), csv, sizeof(csv));
            memcpy(f, keep, sizeof(keep));
            applyBoxCsv((uint8_t)(b - 1), csv);
            return;
        }
        Serial.println("[REFILL] unknown name - 'help' lists them");
        return;
    }
    Serial.println("[REFILL] unknown command - 'help'");
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
    // 1. FIRST THING: every valve relay to its closed level before the pin becomes an output (no click, no open valve).
    for (uint8_t i = 0; i < 3; i++) {
        digitalWrite(VALVE_PIN[i], VALVE_CLOSED_LEVEL);
        pinMode(VALVE_PIN[i], OUTPUT);
        digitalWrite(VALVE_PIN[i], VALVE_CLOSED_LEVEL);
    }
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE REFILL: THE REFILLING STATION (CORE 3.3.7)");
    Serial.println("=======================================================");

    analogReadResolution(12);
    for (uint8_t i = 0; i < 3; i++) { pinMode(SENSOR_PIN[i], INPUT); analogSetPinAttenuation(SENSOR_PIN[i], ADC_11db); }
    memset(box, 0, sizeof(box));

    prefs.begin("refill_nvs", false);
    loadAll();
    printConfig();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);               // modem sleep makes ESP-NOW reception drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[REFILL] ESP-NOW: init failed - restarting in 3 s (valves stay CLOSED)");
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

    Serial.printf("[REFILL] READY - all 3 valves CLOSED; refilling starts %lu s after power-up. Sensors on GPIO %u/%u/%u, valves on GPIO %u/%u/%u. 'help' lists the console.\n",
                  (unsigned long)(BOOT_QUIET_MS / 1000), (unsigned)SENSOR_PIN[0], (unsigned)SENSOR_PIN[1], (unsigned)SENSOR_PIN[2], (unsigned)VALVE_PIN[0], (unsigned)VALVE_PIN[1], (unsigned)VALVE_PIN[2]);
    Serial.printf("[REFILL] WATCHED for now: Box 1 %s, Box 2 %s, Box 3 %s (WATCH_BOX). A box that is not watched is never refilled and is reported as NOT WATCHED.\n",
                  WATCH_BOX[0] ? "YES" : "no", WATCH_BOX[1] ? "YES" : "no", WATCH_BOX[2] ? "YES" : "no");
    Serial.printf("[REFILL] Relay modules: %s - a valve opens when its pin goes %s, so the relay LEDs must be OFF now. If they are ON, set VALVE_ACTIVE_LOW to %d.\n",
                  VALVE_ACTIVE_LOW ? "ACTIVE-LOW" : "ACTIVE-HIGH", VALVE_ACTIVE_LOW ? "LOW" : "HIGH", VALVE_ACTIVE_LOW ? 0 : 1);
}

void loop() {
    uint32_t now = millis();

    processRx();
    pollConsole();

    if (now - lastSampleMs >= SAMPLE_MS) { lastSampleMs = now; sampleSensors(); }
    if (now - lastTickMs >= TICK_MS)     { lastTickMs = now; logicTick(now); }

    now = millis();
    bool changed = (signature() != lastSentSig) && (now - lastStatusMs >= STATUS_MIN_GAP_MS);
    if (now - lastStatusMs >= STATUS_MS || changed) sendStatus();
    if (now - lastBcastMs >= SETTINGS_BCAST_MS) { lastBcastMs = now; broadcastSettings(); }
    if (now - lastSummaryMs >= 10000UL) { lastSummaryMs = now; printStatus(); }
    if (rawStream && now - lastRawPrintMs >= 500) { lastRawPrintMs = now; Serial.printf("[RAW] %u  %u  %u\n", (unsigned)rawAvg[0], (unsigned)rawAvg[1], (unsigned)rawAvg[2]); }
    delay(1);
}
