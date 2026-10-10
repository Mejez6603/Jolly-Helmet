#pragma once
// =====================================================================
//  ONLINE HUB (2026-10-09) - what the Online Server hears on the machine radio, and how it becomes the Blynk payload.
//
//  PASSIVE: this board only LISTENS to the ESP-NOW broadcasts the machine boards already send (the same ones the LocalServer hears) and
//  remembers the latest of each. It never sends a command. The revenue totals come from the LocalServer's HubStatsPacket (every 10 s).
//  See ONLINE_WEB/DATA_CONTRACT.md for the JSON this builds.
// =====================================================================
#include "Shared_Common.h"

static portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;
#define SNAP_FRESH_MS 8000          // a board counts as "heard" if its last packet is younger than this (they all repeat every second or so)

struct SnapTerm { bool seen; uint32_t ms; uint32_t pulses; };
struct SnapBox13 {                  // Box 1 (S3A, A1, A2, A3) or Box 3 (S3B, B1, B2, B3)
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    A2SensorPacket a2; bool a2Seen; uint32_t a2Ms;
    bool a3Seen; uint32_t a3Ms; uint16_t a3Mask;
    SnapTerm t;
    char levels[200];               // the S3's LEVELS settings list (re-broadcast by the S3): checksOn, alcLow, alcHigh, sctLow, sctHigh, ...
};
struct SnapBox2 {                   // Box 2 (S3C, C1, C2)
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    TelemetryPacket c2; bool c2Seen; uint32_t c2Ms;
    SnapTerm t;
};
struct SnapAcs { ACSStatusPacket p; bool seen; uint32_t ms; };
struct SnapStats { HubStatsPacket p; bool seen; uint32_t ms; };

static SnapBox13 snapB[2];          // [0] = Box 1, [1] = Box 3
static SnapBox2  snapC;
static SnapAcs   snapA[2];          // [0] = ACS 1, [1] = ACS 2
static SnapStats snapS;
static uint32_t  snapPackets = 0;   // every packet of a known kind (diagnostic)

static void snapTerm(SnapTerm& t, const TelemetryPacket& p, uint32_t now) { t.seen = true; t.ms = now; t.pulses = p.pulseCount; }
static bool snapFresh(bool seen, uint32_t ms, uint32_t now) { return seen && (now - ms) < SNAP_FRESH_MS; }

// Called from the ESP-NOW receive callback for EVERY packet. Only remembers; never answers.
void onlineOnPacket(const uint8_t* d, int len) {
    if (!d || len <= 0) return;
    uint32_t now = millis();

    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket c;
        memcpy(&c, d, sizeof(c));
        if (c.commandID != CMD_SET_CONFIG || c.targetDeviceID != 0 || c.subIndex != 2) return;   // only a box's LEVELS list is needed
        if (c.state != 0 && c.state != 3) return;                                                // Box 1's S3A / Box 3's S3B
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&snapMux);
        SnapBox13& b = snapB[c.state == 3 ? 1 : 0];
        strncpy(b.levels, c.payloadStr, sizeof(b.levels) - 1);
        b.levels[sizeof(b.levels) - 1] = '\0';
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, d, sizeof(b));
        portENTER_CRITICAL(&snapMux);
        if (b.deviceID == DEVICE_S3A)      { snapB[0].s3Seen = true; snapB[0].s3Ms = now; snapB[0].s3State = b.machineState; snapB[0].s3Timer = b.activeTimer; }
        else if (b.deviceID == DEVICE_S3B) { snapB[1].s3Seen = true; snapB[1].s3Ms = now; snapB[1].s3State = b.machineState; snapB[1].s3Timer = b.activeTimer; }
        else if (b.deviceID == DEVICE_S3C) { snapC.s3Seen = true;    snapC.s3Ms = now;    snapC.s3State = b.machineState;    snapC.s3Timer = b.activeTimer; }
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_A2 && p.deviceID != DEVICE_B2) return;
        portENTER_CRITICAL(&snapMux);
        SnapBox13& b = snapB[p.deviceID == DEVICE_B2 ? 1 : 0];
        b.a2 = p; b.a2Seen = true; b.a2Ms = now;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_A3 && p.deviceID != DEVICE_B3) return;
        portENTER_CRITICAL(&snapMux);
        SnapBox13& b = snapB[p.deviceID == DEVICE_B3 ? 1 : 0];
        b.a3Seen = true; b.a3Ms = now; b.a3Mask = p.relayMask;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, d, sizeof(t));
        portENTER_CRITICAL(&snapMux);
        if (t.deviceID == DEVICE_A1)      snapTerm(snapB[0].t, t, now);
        else if (t.deviceID == DEVICE_B1) snapTerm(snapB[1].t, t, now);
        else if (t.deviceID == DEVICE_C1) snapTerm(snapC.t, t, now);
        else if (t.deviceID == DEVICE_C2) { snapC.c2 = t; snapC.c2Seen = true; snapC.c2Ms = now; }
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(ACSStatusPacket)) {
        ACSStatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_ACS && p.deviceID != DEVICE_ACS2) return;
        portENTER_CRITICAL(&snapMux);
        SnapAcs& a = snapA[p.deviceID == DEVICE_ACS2 ? 1 : 0];
        a.p = p; a.seen = true; a.ms = now;
        portEXIT_CRITICAL(&snapMux);
    } else if (len == (int)sizeof(HubStatsPacket)) {
        HubStatsPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_SERVER) return;
        portENTER_CRITICAL(&snapMux);
        snapS.p = p; snapS.seen = true; snapS.ms = now;
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
    SnapBox13 b[2]; SnapBox2 c; SnapAcs a[2]; SnapStats s;
    bool on[13];                    // A1, A2, A3, S3A, C1, C2, S3C, B1, B2, B3, S3B, ACS 1, ACS 2 (the Control Center Home order)
};
static void snapTake(SnapAll& s) {
    portENTER_CRITICAL(&snapMux);
    s.now = millis();
    memcpy(s.b, snapB, sizeof(snapB)); memcpy(&s.c, &snapC, sizeof(snapC)); memcpy(s.a, snapA, sizeof(snapA)); memcpy(&s.s, &snapS, sizeof(snapS));
    portEXIT_CRITICAL(&snapMux);
    uint32_t n = s.now;
    s.on[0]  = snapFresh(s.b[0].t.seen, s.b[0].t.ms, n);  s.on[1]  = snapFresh(s.b[0].a2Seen, s.b[0].a2Ms, n);  s.on[2]  = snapFresh(s.b[0].a3Seen, s.b[0].a3Ms, n);
    s.on[3]  = snapFresh(s.b[0].s3Seen, s.b[0].s3Ms, n);
    s.on[4]  = snapFresh(s.c.t.seen, s.c.t.ms, n);        s.on[5]  = snapFresh(s.c.c2Seen, s.c.c2Ms, n);        s.on[6]  = snapFresh(s.c.s3Seen, s.c.s3Ms, n);
    s.on[7]  = snapFresh(s.b[1].t.seen, s.b[1].t.ms, n);  s.on[8]  = snapFresh(s.b[1].a2Seen, s.b[1].a2Ms, n);  s.on[9]  = snapFresh(s.b[1].a3Seen, s.b[1].a3Ms, n);
    s.on[10] = snapFresh(s.b[1].s3Seen, s.b[1].s3Ms, n);
    s.on[11] = snapFresh(s.a[0].seen, s.a[0].ms, n);      s.on[12] = snapFresh(s.a[1].seen, s.a[1].ms, n);
}
static int snapHeardCount(const SnapAll& s) { int k = 0; for (int i = 0; i < 13; i++) if (s.on[i]) k++; return k; }

static const char* const NODE_NAMES[13] = {
    "A1 Terminal (Box 1)", "A2 Sensors (Box 1)", "A3 Relays (Box 1)", "S3A Brain (Box 1)", "C1 Terminal (Box 2)", "C2 Hub (Box 2)", "S3C Brain (Box 2)",
    "B1 Terminal (Box 3)", "B2 Sensors (Box 3)", "B3 Relays (Box 3)", "S3B Brain (Box 3)", "ACS 1 (Box 1)", "ACS 2 (Box 3)"
};

// ---------------------------------------------------------------------
// Levels: the S3's LOW and FULL lines -> 0..100 %  (0 % = at the refill line, 100 % = full, -1 = unknown)
// ---------------------------------------------------------------------
static bool levelsParse(const char* csv, float line[4]) {          // csv = "checksOn,alcLow,alcHigh,sctLow,sctHigh,..."
    float v[5]; int n = 0; const char* p = csv;
    while (n < 5 && *p) {
        char* e = nullptr;
        v[n] = strtof(p, &e);
        if (e == p) return false;
        n++; p = e;
        if (*p == ',') p++;
    }
    if (n < 5) return false;
    for (int i = 0; i < 4; i++) line[i] = v[i + 1];
    return true;
}
static int levelPct(float cm, float lowCm, float highCm) {
    if (cm <= 0.0f || lowCm <= highCm) return -1;
    float p = 100.0f * (lowCm - cm) / (lowCm - highCm);
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    return (int)(p + 0.5f);
}

// ---------------------------------------------------------------------
// Alerts (each has a fixed id so a new one can be told from one that was already active)
//   0-2 box out of order   3-8 ACS tank empty (unit*3 + water/alcohol/scented)   9-12 ACS mixer low (unit*2 + alcohol/scented)
//   13-16 box container low (<15 %) (box13*2 + alcohol/scented)   17-29 a board is not heard (index = Home order)
// ---------------------------------------------------------------------
#define ALERT_COUNT 30
static uint32_t alertsNow(const SnapAll& s, uint32_t uptimeSec) {
    uint32_t m = 0;
    if (s.b[0].s3Seen && s.on[3] && s.b[0].s3State == STATE_OUT_OF_ORDER) m |= 1u << 0;
    if (s.c.s3Seen && s.on[6] && s.c.s3State == B2_STATE_OUT_OF_ORDER)   m |= 1u << 1;
    if (s.b[1].s3Seen && s.on[10] && s.b[1].s3State == STATE_OUT_OF_ORDER) m |= 1u << 2;
    for (int u = 0; u < 2; u++) {
        if (!s.on[11 + u]) continue;
        const ACSStatusPacket& p = s.a[u].p;
        for (int k = 0; k < 3; k++) if (p.empty & (1 << k)) m |= 1u << (3 + u * 3 + k);
        if (p.flags & ACS_F_ALC_LOW) m |= 1u << (9 + u * 2);
        if (p.flags & ACS_F_SCT_LOW) m |= 1u << (10 + u * 2);
    }
    for (int i = 0; i < 2; i++) {
        float line[4];
        if (!s.on[i == 0 ? 1 : 8] || !levelsParse(s.b[i].levels, line)) continue;
        int a = levelPct(s.b[i].a2.usMcAlsCm, line[0], line[1]), c = levelPct(s.b[i].a2.usMcSlsCm, line[2], line[3]);
        if (a >= 0 && a < 15) m |= 1u << (13 + i * 2);
        if (c >= 0 && c < 15) m |= 1u << (14 + i * 2);
    }
    if (uptimeSec > 240 && snapHeardCount(s) > 0)                     // not right after power-up: the boards need a moment to be heard
        for (int i = 0; i < 13; i++) if (!s.on[i]) m |= 1u << (17 + i);
    return m;
}
static void alertText(uint8_t id, char* out, size_t n) {
    static const char* const WHAT[3] = {"WATER", "ALCOHOL", "SCENTED"};
    if (id <= 2)       snprintf(out, n, "Box %d is OUT OF ORDER", id + 1);
    else if (id <= 8)  snprintf(out, n, "ACS %d: %s tank is empty", (id - 3) / 3 + 1, WHAT[(id - 3) % 3]);
    else if (id <= 12) snprintf(out, n, "ACS %d: %s mixer container is low", (id - 9) / 2 + 1, ((id - 9) % 2) ? "scented" : "alcohol");
    else if (id <= 16) snprintf(out, n, "Box %d: %s container is low", ((id - 13) / 2) ? 3 : 1, ((id - 13) % 2) ? "scented" : "alcohol");
    else if (id < ALERT_COUNT) snprintf(out, n, "%s is not heard", NODE_NAMES[id - 17]);
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
    for (int i = 0; i < 13; i++) jadd(out, cap, len, "%c", s.on[i] ? '1' : '0');
    jadd(out, cap, len, "\",\"b\":[");
    // Box 1 and Box 3
    for (int i = 0; i < 2; i++) {
        const SnapBox13& b = s.b[i];
        bool a2 = s.on[i == 0 ? 1 : 8], a3 = s.on[i == 0 ? 2 : 9], s3 = s.on[i == 0 ? 3 : 10], term = s.on[i == 0 ? 0 : 7];
        int doors = 0, wet = 0, alc = -1, scn = -1; float helmet = -1.0f;
        if (a2) {
            doors  = (b.a2.doorEnclosure ? 1 : 0) | (b.a2.doorPanel ? 2 : 0) | (b.a2.doorBackdoor ? 4 : 0) | (b.a2.doorAcsSide ? 8 : 0);
            wet    = (b.a2.wsAlsWet ? 1 : 0) | (b.a2.wsSlsWet ? 2 : 0);
            helmet = b.a2.usHelmetCm;
            float line[4];
            if (levelsParse(b.levels, line)) { alc = levelPct(b.a2.usMcAlsCm, line[0], line[1]); scn = levelPct(b.a2.usMcSlsCm, line[2], line[3]); }
        }
        if (i) jadd(out, cap, len, ",");
        jadd(out, cap, len, "[%u,%lu,%lu,%d,%.0f,%d,%d,%d,%u]", s3 ? (unsigned)b.s3State : 0u, s3 ? (unsigned long)b.s3Timer : 0ul, term ? (unsigned long)b.t.pulses : 0ul,
             doors, (double)helmet, alc, scn, wet, a3 ? (unsigned)b.a3Mask : 0u);
        if (i == 0) {                                                      // Box 2 goes between Box 1 and Box 3
            const SnapBox2& c = s.c;
            bool c2 = s.on[5], s3c = s.on[6], t1 = s.on[4];
            int d2 = 0; unsigned rl = 0; float h2 = -1.0f;
            if (c2) {
                d2 = (c.c2.doorEnclosure ? 1 : 0) | (c.c2.doorPanel ? 2 : 0) | (c.c2.doorBackdoor ? 4 : 0);
                for (int k = 0; k < 6; k++) if (c.c2.relayStates[k]) rl |= 1u << k;
                h2 = c.c2.usHelmetDistance;
            }
            jadd(out, cap, len, ",[%u,%lu,%lu,%d,%.0f,-1,-1,0,%u]", s3c ? (unsigned)c.s3State : 0u, s3c ? (unsigned long)c.s3Timer : 0ul, t1 ? (unsigned long)c.t.pulses : 0ul, d2, (double)h2, rl);
        }
    }
    jadd(out, cap, len, "],\"a\":[");
    for (int u = 0; u < 2; u++) {
        const ACSStatusPacket& p = s.a[u].p;
        if (u) jadd(out, cap, len, ",");
        if (s.on[11 + u]) jadd(out, cap, len, "[1,%u,%u,%u,%u,%u,%u,%u,%u]", (unsigned)p.step, (unsigned)p.flags, (unsigned)p.empty, (unsigned)p.warn, (unsigned)p.levelAlcPct, (unsigned)p.levelSctPct, (unsigned)p.pumpMask, (unsigned)p.stepSec);
        else               jadd(out, cap, len, "[0,0,0,0,0,0,0,0,0]");
    }
    jadd(out, cap, len, "]");
    return len;
}
