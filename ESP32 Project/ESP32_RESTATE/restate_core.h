// =====================================================================
//  RESTATE CORE (2026-10-11) - the numbers behind the price recalibration page.
//
//  METHOD (decided with the user): the OLD data is everything the LocalServer had counted at the moment of "Capture". It is restated
//  from MINUTES SERVED at the NEW price list:      restated coins = minutes served x (new coins per second)
//  The coins that were really collected are NEVER scaled, changed or deleted: both columns are kept side by side.
//  Everything counted after the capture was sold at the new price list, so it is the same in both columns.
//
//  The LocalServer only keeps TOTALS (plus the last 10 sessions), not a record per box, so the price list of the three boxes is blended
//  with a "share of the minutes served" per box (default: equal). That share is the one assumption the report carries.
//
//  Kept in a header (not in the .ino) so the Arduino auto-prototype generator never sees the structs.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <math.h>
#include "Shared_Common.h"
#include "Restate_Common.h"

struct RsLive {
    HubStatsPacket p;
    bool     seen;
    uint32_t ms;          // millis() of the last packet
    uint32_t packets;
};

struct RsBase {            // the frozen copy of the old data (saved in flash)
    uint8_t  valid;
    uint8_t  histCount;
    uint16_t cvX100;
    uint32_t sessions, coins, durSec;
    uint32_t at;           // unix time of the capture, taken from the browser's clock (this board has no clock)
    int16_t  tzMin;        // the browser's JS getTimezoneOffset() (minutes BEHIND UTC, so UTC+8 = -480)
    uint16_t seq;
    uint16_t histDur[10], histCoins[10];
};

struct RsPrices {          // per box: [0] Box 1, [1] Box 2, [2] Box 3
    uint16_t nc[3];  float nm[3];     // NEW price list: nc coins = nm minutes
    uint16_t oc[3];  float om[3];     // OLD price list: oc coins = om minutes (only used for the "explained coins" check)
    float    sh[3];                   // share of the minutes served by that box (relative weights)
};

static const RsPrices RS_DEFAULT_PRICES = {
    {15, 10, 15}, {5.0f, 3.0f, 5.0f},
    {30, 30, 45}, {5.0f, 5.0f, 5.0f},
    {310.0f, 300.0f, 0.0f}                // minutes served by Box 1 / Box 2 / Box 3 in the deployment (Box 3 was not deployed); change them on the page if needed
};

struct RsCalc {
    bool     preview;                 // no capture yet: the "old" part is the live totals (what-if)
    bool     wentDown;                // the LocalServer's totals are LOWER than at the capture (its statistics were reset)
    double   cpsNew, cpsOld;          // coins per second of the blended price lists
    // old data (at capture)
    uint32_t oSs, oCoins, oDu;  double oCv;
    double   oCollRev, oRestRev;
    uint32_t oRestCoins, oExpCoins;  int32_t oUnexpl;
    // since the capture (sold at the new price list)
    uint32_t nSs, nCoins, nDu;  double nCv, nRev;
    // totals
    uint32_t totSs, totDu, totRestCoins;  double totCollRev, totRestRev;
};

// blended coins per second of a price list: sum(share x coins / seconds) / sum(share), boxes with a bad price are left out
static double rsCps(const uint16_t* c, const float* m, const float* sh) {
    double sum = 0, w = 0;
    for (int i = 0; i < 3; i++) {
        if (c[i] < 1 || m[i] < 0.05f || sh[i] <= 0.0f) continue;
        sum += (double)sh[i] * (double)c[i] / ((double)m[i] * 60.0);
        w   += (double)sh[i];
    }
    return w > 0 ? sum / w : 0.0;
}

static void rsCompute(RsCalc& c, const RsBase& b, const RsLive& l, const RsPrices& p, bool applied = false) {   // applied = the LocalServer's coin total was replaced by the corrected one
    memset(&c, 0, sizeof(c));
    c.preview = !b.valid;
    c.cpsNew = rsCps(p.nc, p.nm, p.sh);
    c.cpsOld = rsCps(p.oc, p.om, p.sh);
    if (b.valid) { c.oSs = b.sessions; c.oCoins = b.coins; c.oDu = b.durSec; c.oCv = b.cvX100 / 100.0; }
    else if (l.seen) { c.oSs = l.p.totalSessions; c.oCoins = l.p.totalCoins; c.oDu = l.p.totalDurationSec; c.oCv = l.p.coinValueX100 / 100.0; }
    else c.oCv = 1.0;
    c.oCollRev   = c.oCoins * c.oCv;
    c.oRestCoins = (uint32_t)llround((double)c.oDu * c.cpsNew);
    c.oRestRev   = c.oRestCoins * c.oCv;
    c.oExpCoins  = (uint32_t)llround((double)c.oDu * c.cpsOld);
    c.oUnexpl    = (int32_t)c.oCoins - (int32_t)c.oExpCoins;
    c.nCv = l.seen ? l.p.coinValueX100 / 100.0 : c.oCv;
    if (b.valid && l.seen) {
        const uint32_t baseC = applied ? c.oRestCoins : b.coins;      // after "apply" the LocalServer's own total already starts from the restated old coins
        if (l.p.totalSessions >= b.sessions && l.p.totalCoins >= baseC && l.p.totalDurationSec >= b.durSec) {
            c.nSs = l.p.totalSessions - b.sessions; c.nCoins = l.p.totalCoins - baseC; c.nDu = l.p.totalDurationSec - b.durSec;
        } else c.wentDown = true;
    }
    c.nRev = c.nCoins * c.nCv;
    c.totSs = c.oSs + c.nSs;  c.totDu = c.oDu + c.nDu;
    c.totRestCoins = c.oRestCoins + c.nCoins;
    c.totCollRev = c.oCollRev + c.nRev;
    c.totRestRev = c.oRestRev + c.nRev;
}

static void rsAdd(String& o, const char* fmt, ...) {
    char b[480]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); o += b;
}

static void rsList(String& o, const char* fmt, const float* v) { for (int i = 0; i < 3; i++) rsAdd(o, fmt, i ? "," : "", (double)v[i]); }

// ---- JSON for the page -----------------------------------------------------------------------------
static void rsJson(String& o, const RsBase& b, const RsLive& l, const RsPrices& p, uint32_t nowMs, bool applied) {
    RsCalc c; rsCompute(c, b, l, p, applied);
    o.reserve(2600);
    rsAdd(o, "{\"up\":%lu,\"live\":{\"seen\":%d,\"age\":%ld,\"pk\":%lu", (unsigned long)nowMs, l.seen ? 1 : 0, l.seen ? (long)(nowMs - l.ms) : -1L, (unsigned long)l.packets);
    if (l.seen) rsAdd(o, ",\"seq\":%u,\"cv\":%.2f,\"ss\":%lu,\"cn\":%lu,\"du\":%lu", (unsigned)l.p.seq, l.p.coinValueX100 / 100.0, (unsigned long)l.p.totalSessions, (unsigned long)l.p.totalCoins, (unsigned long)l.p.totalDurationSec);
    rsAdd(o, "},\"base\":{\"valid\":%d", b.valid ? 1 : 0);
    if (b.valid) {
        rsAdd(o, ",\"at\":%lu,\"tz\":%d,\"seq\":%u,\"cv\":%.2f,\"ss\":%lu,\"cn\":%lu,\"du\":%lu,\"hd\":[", (unsigned long)b.at, (int)b.tzMin, (unsigned)b.seq, b.cvX100 / 100.0, (unsigned long)b.sessions, (unsigned long)b.coins, (unsigned long)b.durSec);
        for (int i = 0; i < b.histCount && i < 10; i++) rsAdd(o, "%s%u", i ? "," : "", (unsigned)b.histDur[i]);
        o += "],\"hc\":[";
        for (int i = 0; i < b.histCount && i < 10; i++) rsAdd(o, "%s%u", i ? "," : "", (unsigned)b.histCoins[i]);
        o += "]";
    }
    o += "},\"set\":{\"nc\":[";
    for (int i = 0; i < 3; i++) rsAdd(o, "%s%u", i ? "," : "", (unsigned)p.nc[i]);
    o += "],\"nm\":["; rsList(o, "%s%.2f", p.nm);
    o += "],\"oc\":[";
    for (int i = 0; i < 3; i++) rsAdd(o, "%s%u", i ? "," : "", (unsigned)p.oc[i]);
    o += "],\"om\":["; rsList(o, "%s%.2f", p.om);
    o += "],\"sh\":["; rsList(o, "%s%.2f", p.sh);
    rsAdd(o, "]},\"calc\":{\"preview\":%d,\"down\":%d,\"cpsNew\":%.6f,\"cpsOld\":%.6f,", c.preview ? 1 : 0, c.wentDown ? 1 : 0, c.cpsNew, c.cpsOld);
    rsAdd(o, "\"oSs\":%lu,\"oCn\":%lu,\"oDu\":%lu,\"oCollRev\":%.2f,\"oRestCn\":%lu,\"oRestRev\":%.2f,\"oExpCn\":%lu,\"oUnexpl\":%ld,", (unsigned long)c.oSs, (unsigned long)c.oCoins, (unsigned long)c.oDu, c.oCollRev, (unsigned long)c.oRestCoins, c.oRestRev, (unsigned long)c.oExpCoins, (long)c.oUnexpl);
    rsAdd(o, "\"nSs\":%lu,\"nCn\":%lu,\"nDu\":%lu,\"nRev\":%.2f,", (unsigned long)c.nSs, (unsigned long)c.nCoins, (unsigned long)c.nDu, c.nRev);
    rsAdd(o, "\"tSs\":%lu,\"tDu\":%lu,\"tCollCn\":%lu,\"tRestCn\":%lu,\"tCollRev\":%.2f,\"tRestRev\":%.2f}}", (unsigned long)c.totSs, (unsigned long)c.totDu, (unsigned long)(c.oCoins + c.nCoins), (unsigned long)c.totRestCoins, c.totCollRev, c.totRestRev);
}

// ---- CSV for the Analytics Document Team -----------------------------------------------------------
static void rsStamp(char* out, size_t n, uint32_t at, int tzMin) {
    time_t t = (time_t)((int64_t)at - (int64_t)tzMin * 60);
    struct tm tmv; gmtime_r(&t, &tmv);
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
}
static void rsMmss(char* out, size_t n, uint32_t s) { snprintf(out, n, "%.2f", s / 60.0); }   // decimal minutes: easy to use in a spreadsheet

static void rsCsv(String& o, const RsBase& b, const RsLive& l, const RsPrices& p, bool applied, unsigned appliedRev) {
    RsCalc c; rsCompute(c, b, l, p, applied);
    char st[24], m1[16], m2[16];
    o.reserve(3600);
    o += "\xEF\xBB\xBF";   // UTF-8 byte order mark so Excel opens it correctly
    o += "\"Jolly Helmet - price recalibration report\"\r\n";
    o += "\"Method\",\"Old data restated from MINUTES SERVED at the NEW price list: restated coins = minutes served x new coins per second. The coins that were really collected are never scaled, changed or deleted.\"\r\n";
    if (b.valid) { rsStamp(st, sizeof(st), b.at, b.tzMin); rsAdd(o, "\"Old data captured\",\"%s\",\"(clock of the device that pressed Capture)\"\r\n", st); }
    else o += "\"Old data captured\",\"NOT YET - this is a PREVIEW on the live totals\"\r\n";
    if (applied) rsAdd(o, "\"Corrected figures\",\"Applied to the LocalServer and the Online Server (update #%u): their coin total and revenue now start from the restated old coins. The original figures at the capture are the 'As collected' column of this report.\"\r\n", appliedRev);
    if (c.wentDown) o += "\"WARNING\",\"The LocalServer totals are lower than at the capture: its statistics were reset after the capture. Figures since the capture are not valid.\"\r\n";
    o += "\r\n\"Price list used (coins = minutes)\",\"New\",\"Old (check only)\",\"Share of minutes served\"\r\n";
    for (int i = 0; i < 3; i++) rsAdd(o, "\"Box %d\",\"%u coins = %.2f min\",\"%u coins = %.2f min\",\"%.2f\"\r\n", i + 1, (unsigned)p.nc[i], (double)p.nm[i], (unsigned)p.oc[i], (double)p.om[i], (double)p.sh[i]);
    rsAdd(o, "\"Blended coins per minute\",\"%.4f\",\"%.4f\"\r\n", c.cpsNew * 60.0, c.cpsOld * 60.0);
    o += "\r\n\"OLD DATA (up to the capture)\",\"As collected\",\"Restated at the new price list\"\r\n";
    rsMmss(m1, sizeof(m1), c.oDu);
    rsAdd(o, "\"Customers served\",\"%lu\",\"%lu\"\r\n", (unsigned long)c.oSs, (unsigned long)c.oSs);
    rsAdd(o, "\"Minutes served\",\"%s\",\"%s\"\r\n", m1, m1);
    rsAdd(o, "\"Coins\",\"%lu\",\"%lu\"\r\n", (unsigned long)c.oCoins, (unsigned long)c.oRestCoins);
    rsAdd(o, "\"Revenue (PHP, at %.2f per coin)\",\"%.2f\",\"%.2f\"\r\n", c.oCv, c.oCollRev, c.oRestRev);
    rsAdd(o, "\"Coins the OLD price list explains from the minutes served\",\"%lu\"\r\n", (unsigned long)c.oExpCoins);
    rsAdd(o, "\"Coins collected that the minutes served do not explain\",\"%ld\",\"(test or bench pulses, aborted runs, refunds, free-retry minutes: they are NOT in the restated column)\"\r\n", (long)c.oUnexpl);
    o += "\r\n\"SINCE THE CAPTURE (sold at the new price list)\",\"As collected\",\"Restated\"\r\n";
    rsMmss(m2, sizeof(m2), c.nDu);
    rsAdd(o, "\"Customers served\",\"%lu\",\"%lu\"\r\n\"Minutes served\",\"%s\",\"%s\"\r\n\"Coins\",\"%lu\",\"%lu\"\r\n\"Revenue (PHP)\",\"%.2f\",\"%.2f\"\r\n", (unsigned long)c.nSs, (unsigned long)c.nSs, m2, m2, (unsigned long)c.nCoins, (unsigned long)c.nCoins, c.nRev, c.nRev);
    o += "\r\n\"TOTAL (old + new)\",\"As collected\",\"Restated at the new price list\"\r\n";
    rsAdd(o, "\"Customers served\",\"%lu\",\"%lu\"\r\n\"Coins\",\"%lu\",\"%lu\"\r\n\"Revenue (PHP)\",\"%.2f\",\"%.2f\"\r\n", (unsigned long)c.totSs, (unsigned long)c.totSs, (unsigned long)(c.oCoins + c.nCoins), (unsigned long)c.totRestCoins, c.totCollRev, c.totRestRev);
    if (b.valid && b.histCount) {
        o += "\r\n\"LAST SESSIONS AT THE CAPTURE (oldest first)\",\"Seconds\",\"Coins paid\",\"Coins at the old list\",\"Coins at the new list\"\r\n";
        for (int i = 0; i < b.histCount && i < 10; i++) {
            long atOld = (long)ceil((double)b.histDur[i] * c.cpsOld - 0.0001), atNew = (long)ceil((double)b.histDur[i] * c.cpsNew - 0.0001);
            rsAdd(o, "\"#%d\",\"%u\",\"%u\",\"%ld\",\"%ld\"\r\n", i + 1, (unsigned)b.histDur[i], (unsigned)b.histCoins[i], atOld, atNew);
        }
    }
}

// ---- the packet the Update button broadcasts to the LocalServer and the Online Server ---------------
static void rsMakePacket(RestatePacket& k, const RsBase& b, const RsLive& l, const RsPrices& p, uint16_t rev, bool active, uint8_t mode) {
    memset(&k, 0, sizeof(k));
    k.deviceID = DEVICE_SERVER; k.magic = RESTATE_MAGIC; k.active = active ? 1 : 0; k.rev = rev; k.mode = mode;
    if (!b.valid) return;
    RsCalc c; rsCompute(c, b, l, p, false);
    k.cvX100 = b.cvX100; k.capturedAt = b.at; k.tzMin = b.tzMin;
    k.cpmNewX100 = (uint16_t)lround(c.cpsNew * 60.0 * 100.0);
    k.baseSessions = b.sessions; k.baseCoins = b.coins; k.baseDurSec = b.durSec;
    k.oldRestCoins = c.oRestCoins;
}

struct RsPub {              // what was last published (saved in flash)
    uint8_t  on;            // 1 = the packet is re-broadcast every 10 s
    uint8_t  pad;
    uint16_t rev;           // press counter of the Update button
    uint16_t cpm;           // the blended new coins per minute x100 that went out
    uint32_t oldRest;       // the restated old coins that went out
};
