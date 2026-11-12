#pragma once
// =====================================================================
//  ONLINE HUB (2026-10-09, REWORKED 2026-11-12) - what the Online Server hears on the machine radio, and how it becomes the Blynk payload.
//
//  PASSIVE: this board only LISTENS to the ESP-NOW broadcasts the machine boards already send (the same ones the LocalServer hears) and
//  remembers the latest of each. It never sends a command. The revenue totals come from the LocalServer's HubStatsPacket (every 10 s).
//  See ONLINE_WEB/DATA_CONTRACT.md for the JSON this builds.
//
//  SINCE 2026-11-12 ALL THREE BOXES ARE THE SAME (S3 + terminal + sensor board + 13-channel relay board, the same 36-step flow), so there is one box
//  structure (SnapBox) and the "n" flags have 13 places: the 12 of before keep their places (so nothing that reads them moves) and Box 2's new relay board
//  C3 is added at the END (place 13):
//      0 A1  1 A2  2 A3  3 S3A | 4 C1  5 C2  6 S3C | 7 B1  8 B2  9 B3  10 S3B | 11 Refilling Station | 12 C3
// =====================================================================
#include "Shared_Common.h"
#include "Restate_Common.h"

static portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;
#define SNAP_FRESH_MS 8000          // a board counts as "heard" if its last packet is younger than this (they all repeat every second or so)

#define FLAG_COUNT   13             // places in the "n" string
#define FLAG_STATION 11             // the Refilling Station's place
// The boards of each box, by box index (0 = Box 1, 1 = Box 2, 2 = Box 3), and where each one sits in the "n" flags
static const uint8_t SNAP_S3[3]   = { DEVICE_S3A, DEVICE_S3C, DEVICE_S3B };
static const uint8_t SNAP_TERM[3] = { DEVICE_A1,  DEVICE_C1,  DEVICE_B1 };
static const uint8_t SNAP_SENS[3] = { DEVICE_A2,  DEVICE_C2,  DEVICE_B2 };
static const uint8_t SNAP_REL[3]  = { DEVICE_A3,  DEVICE_C3,  DEVICE_B3 };
static const uint8_t FLAG_TERM[3] = { 0, 4, 7 };
static const uint8_t FLAG_SENS[3] = { 1, 5, 8 };
static const uint8_t FLAG_REL[3]  = { 2, 12, 9 };
static const uint8_t FLAG_S3[3]   = { 3, 6, 10 };
static int snapIdxOf(const uint8_t* list, uint8_t id) { for (int i = 0; i < 3; i++) if (list[i] == id) return i; return -1; }

struct SnapTerm { bool seen; uint32_t ms; uint32_t pulses; };
struct SnapBox {                    // one box: S3 + terminal + sensor board + relay board
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    uint32_t sessSeq; uint16_t sessDur, sessCoins;      // the brain's "completed sessions" counter and the last session (BoxStatusPacket)
    A2SensorPacket a2; bool a2Seen; uint32_t a2Ms;
    bool a3Seen; uint32_t a3Ms; uint16_t a3Mask;
    SnapTerm t;
};
struct SnapRefill { RefillStatusPacket p; bool seen; uint32_t ms; };      // the Refilling Station (replaces ACS 1 and ACS 2, 2026-11-03)
struct SnapStats { HubStatsPacket p; bool seen; uint32_t ms; };

static SnapBox   snapB[3];          // [0] = Box 1, [1] = Box 2, [2] = Box 3
static SnapRefill snapR;
static SnapStats snapS;
static uint32_t  snapPackets = 0;   // every packet of a known kind (diagnostic)
// Restated old data from the spare ESP32_RESTATE board (its Update button). Kept in flash by loop() (snapRsDirty), so the board can be unplugged.
static RestatePacket    snapRs;
static bool             snapRsHave = false;
static volatile bool    snapRsDirty = false;

static void snapTerm(SnapTerm& t, const TelemetryPacket& p, uint32_t now) { t.seen = true; t.ms = now; t.pulses = p.pulseCount; }
static bool snapFresh(bool seen, uint32_t ms, uint32_t now) { return seen && (now - ms) < SNAP_FRESH_MS; }

// Called from the ESP-NOW receive callback for EVERY packet. Only remembers; never answers.
void onlineOnPacket(const uint8_t* d, int len) {
    if (!d || len <= 0) return;
    uint32_t now = millis();

    if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, d, sizeof(b));
        int bi = snapIdxOf(SNAP_S3, b.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&snapMux);
        snapB[bi].s3Seen = true; snapB[bi].s3Ms = now; snapB[bi].s3State = b.machineState; snapB[bi].s3Timer = b.activeTimer;
        snapB[bi].sessSeq = b.completedSessionSeq; snapB[bi].sessDur = b.lastSessionDurationSec; snapB[bi].sessCoins = b.lastSessionCoins;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, d, sizeof(p));
        int bi = snapIdxOf(SNAP_SENS, p.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&snapMux);
        snapB[bi].a2 = p; snapB[bi].a2Seen = true; snapB[bi].a2Ms = now;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, d, sizeof(p));
        int bi = snapIdxOf(SNAP_REL, p.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&snapMux);
        snapB[bi].a3Seen = true; snapB[bi].a3Ms = now; snapB[bi].a3Mask = p.relayMask;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, d, sizeof(t));
        int bi = snapIdxOf(SNAP_TERM, t.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&snapMux);
        snapTerm(snapB[bi].t, t, now);
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(RefillStatusPacket)) {              // the Refilling Station (about once a second)
        RefillStatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_REFILL || p.magic != REFILL_MAGIC) return;
        portENTER_CRITICAL(&snapMux);
        snapR.p = p; snapR.seen = true; snapR.ms = now;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(HubStatsPacket)) {
        HubStatsPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_SERVER) return;
        portENTER_CRITICAL(&snapMux);
        snapS.p = p; snapS.seen = true; snapS.ms = now;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(RestatePacket)) {                 // the Update button of the spare ESP32_RESTATE board
        RestatePacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_SERVER || p.magic != RESTATE_MAGIC) return;
        portENTER_CRITICAL(&snapMux);
        if (p.active != 1) {                                        // withdrawn
            if (snapRsHave) { snapRsHave = false; memset(&snapRs, 0, sizeof(snapRs)); snapRsDirty = true; }
        } else if (!snapRsHave || p.rev != snapRs.rev) {            // new (it is repeated every 10 s)
            snapRs = p; snapRsHave = true; snapRsDirty = true;
        }
        portEXIT_CRITICAL(&snapMux);
    } else {
        return;
    }
    snapPackets++;
}

// ---------------------------------------------------------------------
// One consistent copy of everything, taken under the lock
// ---------------------------------------------------------------------
struct SnapAll {
    uint32_t now;
    SnapBox b[3]; SnapRefill r; SnapStats s;
    RestatePacket rs; bool rsHave;  // the restated old data, if the spare board published it
    bool on[FLAG_COUNT];            // A1, A2, A3, S3A, C1, C2, S3C, B1, B2, B3, S3B, Refilling Station, C3
};
static void snapTake(SnapAll& s) {
    portENTER_CRITICAL(&snapMux);
    s.now = millis();
    memcpy(s.b, snapB, sizeof(snapB)); memcpy(&s.r, &snapR, sizeof(snapR)); memcpy(&s.s, &snapS, sizeof(snapS));
    memcpy(&s.rs, &snapRs, sizeof(snapRs)); s.rsHave = snapRsHave;
    portEXIT_CRITICAL(&snapMux);
    uint32_t n = s.now;
    for (int i = 0; i < 3; i++) {
        s.on[FLAG_TERM[i]] = snapFresh(s.b[i].t.seen,  s.b[i].t.ms,  n);
        s.on[FLAG_SENS[i]] = snapFresh(s.b[i].a2Seen,  s.b[i].a2Ms,  n);
        s.on[FLAG_REL[i]]  = snapFresh(s.b[i].a3Seen,  s.b[i].a3Ms,  n);
        s.on[FLAG_S3[i]]   = snapFresh(s.b[i].s3Seen,  s.b[i].s3Ms,  n);
    }
    s.on[FLAG_STATION] = snapFresh(s.r.seen, s.r.ms, n);
}
static int snapHeardCount(const SnapAll& s) { int k = 0; for (int i = 0; i < FLAG_COUNT; i++) if (s.on[i]) k++; return k; }

static const char* const NODE_NAMES[FLAG_COUNT] = {    // plain names (the codes A1.. B3, S3A.. stay in the firmware); used in the alert messages; same order as the "n" flags
    "Box 1 TFT Display", "Box 1 Sensors", "Box 1 Relay Controller", "Box 1 Main Controller", "Box 2 TFT Display", "Box 2 Sensors", "Box 2 Main Controller",
    "Box 3 TFT Display", "Box 3 Sensors", "Box 3 Relay Controller", "Box 3 Main Controller", "Refilling Station", "Box 2 Relay Controller"
};

// ---------------------------------------------------------------------
// Alerts (each has a fixed id so a new one can be told from one that was already active)
//   0-2 box out of order   3 the Refilling Station is not heard   4-6 a refill failed (Box 1..3)   7-17 a box board is not heard (flag place + 7, places 0-10)
//   18 Box 2's relay board C3 is not heard (flag place 12; it is last in the flags so the other places did not move)
// A container that is merely EMPTY or LOW is NOT an alert: the station is refilling it by itself. Only a FAILED refill is.
// ---------------------------------------------------------------------
#define ALERT_COUNT 19
static uint32_t alertsNow(const SnapAll& s, uint32_t uptimeSec) {
    uint32_t m = 0;
    for (int i = 0; i < 3; i++) if (s.b[i].s3Seen && s.on[FLAG_S3[i]] && s.b[i].s3State == STATE_OUT_OF_ORDER) m |= 1u << i;
    if (s.on[FLAG_STATION]) for (int i = 0; i < 3; i++) if (s.r.p.failMask & (1 << i)) m |= 1u << (4 + i);
    if (uptimeSec > 240 && snapHeardCount(s) > 0) {                   // not right after power-up: the boards need a moment to be heard
        if (!s.on[FLAG_STATION]) m |= 1u << 3;
        for (int i = 0; i < 11; i++) if (!s.on[i]) m |= 1u << (7 + i);
        if (!s.on[12]) m |= 1u << 18;
    }
    return m;
}
static void alertText(uint8_t id, char* out, size_t n) {
    if (id <= 2)       snprintf(out, n, "Box %d is OUT OF ORDER", id + 1);
    else if (id == 3)  snprintf(out, n, "Refilling Station is offline: every box offers DRYING only");
    else if (id <= 6)  snprintf(out, n, "Box %d: the refill failed (the level is not rising, check the supply)", id - 3);
    else if (id <= 17) snprintf(out, n, "%s is not heard", NODE_NAMES[id - 7]);
    else if (id == 18) snprintf(out, n, "%s is not heard", NODE_NAMES[12]);
    else out[0] = '\0';
}
// ---------------------------------------------------------------------
// The "live" part of the Blynk JSON (everything except "st"). Returns the length; the caller appends  ,"st":{...}}
// ---------------------------------------------------------------------
static void jadd(char* out, size_t cap, size_t& len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void jadd(char* out, size_t cap, size_t& len, const char* fmt, ...) {
    if (len >= cap) return;
    va_list ap; va_start(ap, fmt);
    int w = vsnprintf(out + len, cap - len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    len += (size_t)w;
    if (len >= cap) len = cap - 1;
}
static size_t liveJson(const SnapAll& s, char* out, size_t cap, uint32_t seq, uint32_t ts, uint32_t ivSec) {
    size_t len = 0;
    jadd(out, cap, len, "{\"seq\":%lu,\"ts\":%lu,\"iv\":%lu,\"n\":\"", (unsigned long)seq, (unsigned long)ts, (unsigned long)ivSec);
    for (int i = 0; i < FLAG_COUNT; i++) jadd(out, cap, len, "%c", s.on[i] ? '1' : '0');
    jadd(out, cap, len, "\",\"b\":[");
    // Box 1, Box 2, Box 3 (the same list of 9 numbers each)
    for (int i = 0; i < 3; i++) {
        const SnapBox& b = s.b[i];
        bool a2 = s.on[FLAG_SENS[i]], a3 = s.on[FLAG_REL[i]], s3 = s.on[FLAG_S3[i]], term = s.on[FLAG_TERM[i]];
        int doors = 0; float helmet = -1.0f;                                   // the container level is not read by the box any more: the Refilling Station sends it (rf)
        if (a2) {
            doors  = (b.a2.doorEnclosure ? 1 : 0) | (b.a2.doorPanel ? 2 : 0) | (b.a2.doorBackdoor ? 4 : 0) | (b.a2.doorAcsSide ? 8 : 0);
            helmet = b.a2.usHelmetCm;
        }
        if (i) jadd(out, cap, len, ",");
        jadd(out, cap, len, "[%u,%lu,%lu,%d,%.0f,-1,-1,0,%u]", s3 ? (unsigned)b.s3State : 0u, s3 ? (unsigned long)b.s3Timer : 0ul, term ? (unsigned long)b.t.pulses : 0ul,
             doors, (double)helmet, a3 ? (unsigned)b.a3Mask : 0u);
    }
    jadd(out, cap, len, "],\"rf\":[");                                  // the Refilling Station: per box [raw, state 0-4 (-1 unknown, -2 not watched), valve open, refill failed, mid edge, high edge, max edge, waiting in the queue]
    for (int i = 0; i < 3; i++) {
        if (i) jadd(out, cap, len, ",");
        if (s.on[FLAG_STATION]) {
            const RefillStatusPacket& p = s.r.p;
            jadd(out, cap, len, "[%u,%d,%d,%d,%u,%u,%u,%d]", (unsigned)p.raw[i], p.state[i] <= REFILL_MAX ? (int)p.state[i] : (p.state[i] == REFILL_OFF ? -2 : -1), (p.valveMask >> i) & 1, (p.failMask >> i) & 1,
                 (unsigned)p.midEdge[i], (unsigned)p.highEdge[i], (unsigned)p.maxEdge[i], (p.queueMask >> i) & 1);
        } else jadd(out, cap, len, "[0,-1,0,0,500,1000,1200,0]");
    }
    jadd(out, cap, len, "]");
    return len;
}

// ---------------------------------------------------------------------
// The PUBLIC status (2026-10-10): a tiny JSON for a SECOND Blynk device whose token may be printed on a public customer page (status.html).
// It carries only what a customer may see: for each box its step number, the seconds left and whether its brain is heard. Nothing else.
//   {"seq":N,"ts":T,"iv":60,"b":[[step,secondsLeft,heard,sessions],[...],[...]]}      order: Box 1, Box 2, Box 3 (step numbers as in js/names.js)
// "sessions" (added 2026-10-14) is the brain's completed-sessions counter: it goes up by one when a customer's cycle ends (finished or aborted). The
// customer page ties an optional customer name to the counter it saw, so the name disappears the moment the cycle ends. 0 when the brain is not heard
// (and back at 0 after the brain restarts). Older pages ignore the 4th number.
// ---------------------------------------------------------------------
static size_t publicJson(const SnapAll& s, char* out, size_t cap, uint32_t seq, uint32_t ts, uint32_t ivSec) {
    size_t len = 0;
    jadd(out, cap, len, "{\"seq\":%lu,\"ts\":%lu,\"iv\":%lu,\"b\":[", (unsigned long)seq, (unsigned long)ts, (unsigned long)ivSec);
    for (int i = 0; i < 3; i++) {
        bool s3 = s.on[FLAG_S3[i]];
        jadd(out, cap, len, "%s[%u,%lu,%d,%lu]", i ? "," : "", s3 ? (unsigned)s.b[i].s3State : 0u, s3 ? (unsigned long)s.b[i].s3Timer : 0ul, s3 ? 1 : 0, s3 ? (unsigned long)s.b[i].sessSeq : 0ul);
    }
    jadd(out, cap, len, "]}");
    return len;
}
