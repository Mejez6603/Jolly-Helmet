#pragma once
// =====================================================================
//  SETTINGS HUB (2026-10-03, REWORKED 2026-11-12) - how the LocalServer configures EVERY node.
//
//  The LocalServer listens (read-only) to the broadcasts of every node: each box's S3 status, the sensor board's readings (A2 / C2 / B2), the relay
//  board's mask (A3 / C3 / B3), the coin terminal's counter (A1 / C1 / B1), the Refilling Station's RefillStatusPacket, and the settings lists the S3 nodes and
//  the station re-broadcast (CMD_SET_CONFIG, target 0; state tag 0 = Box 1's S3A, 2 = Box 2's S3C, 3 = Box 3's S3B, 4 = the Refilling Station). It serves one
//  page per box plus one for the Refilling Station (/box1 /box2 /box3 /refill - see settings_pages.h) and turns the page's buttons into the same
//  CMD_SET_CONFIG / CMD_SET_MAINTENANCE / CMD_SET_RELAY_MASK messages the benches sent. The nodes keep every setting in their own flash, so nothing is
//  lost if this board is swapped or reflashed.
//
//  SINCE 2026-11-12 ALL THREE BOXES ARE THE SAME (one S3 program, one terminal program, one sensor board, one 13-channel relay board), so there is ONE box
//  structure (HubBox), ONE box page (PAGE_BOX, which reads its box number from the address) and one set of handlers; box index = box number - 1 (0 = Box 1,
//  1 = Box 2, 2 = Box 3), which is also the box's slot on the Refilling Station.
//
//  Page API (all GET, JSON answers):
//    /api/b1/*  /api/b2/*  /api/b3/*     state | send?kind=rules|sensors|dry|drying|refresh|autorefresh|beep&csv= | maint?on=0/1 | mask?v= | refresh
//    (kinds -> the S3's subIndex: rules 1 = rateCoins,rateValue,rateUnitMin,maxCoins | sensors 2 = helmetCm,offlineSec | dry 3 = autoDrySec,heaterOffTailSec | drying 4 = coolDownSec
//     | refresh 5 = on-mask,off-mask | autorefresh 6 = seconds, 0 = never, max 60 | beep 7 = beep length ms, wait ms (0 = a single beep))
//    (refresh = "force refresh the touch screen": CMD_TFT_HARD_REFRESH to the box's S3 node, which re-sends its terminal's page)
//    /api/refill/*          state | send?kind=box1|box2|box3|test&csv=  | test?i=0..2&on=1|0      (the Refilling Station: settings per box, manual valve test)
//
//  Included from ESP32_LOCALSRV.ino after sendEspNowCmdDirect() / the server object are defined.
// =====================================================================
#include "settings_pages.h"
#include "hub_theme.h"

static portMUX_TYPE hubMux = portMUX_INITIALIZER_UNLOCKED;
#define HUB_FRESH_MS 5000

// The boards of each box, by box index (0 = Box 1, 1 = Box 2, 2 = Box 3)
static const uint8_t HUB_S3[3]   = { DEVICE_S3A, DEVICE_S3C, DEVICE_S3B };
static const uint8_t HUB_TERM[3] = { DEVICE_A1,  DEVICE_C1,  DEVICE_B1 };
static const uint8_t HUB_SENS[3] = { DEVICE_A2,  DEVICE_C2,  DEVICE_B2 };
static const uint8_t HUB_REL[3]  = { DEVICE_A3,  DEVICE_C3,  DEVICE_B3 };
static const uint8_t HUB_TAG[3]  = { 0, 2, 3 };                               // state tag of each S3's settings broadcasts
static const char* const HUB_BOXNAME[3] = { "Box 1", "Box 2", "Box 3" };
static int hubIdxOf(const uint8_t* list, uint8_t id) { for (int i = 0; i < 3; i++) if (list[i] == id) return i; return -1; }

struct HubTerm {                 // a coin terminal (A1 / C1 / B1): its pulse counter and the raw-edge diagnostic
    bool seen; uint32_t ms; uint32_t pulses; uint32_t changeMs; bool ever; uint32_t rawEdges; float minGap;
};
struct HubBox {                  // one box: S3 + terminal + sensor board + relay board, and the settings the S3 re-broadcasts
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    A2SensorPacket a2; bool a2Seen; uint32_t a2Ms;
    bool a3Seen; uint32_t a3Ms; uint16_t a3Mask; bool a3Orph;
    HubTerm t;
    char rules[200]; char levels[200]; char phases[200]; char drying[40]; char refresh[40]; char autorf[16]; char beep[16];   // levels = SENSORS (sub 2), phases = AUTO-DRY (sub 3): old names kept
};
struct HubRefill {               // the Refilling Station: its status broadcast and the settings it re-broadcasts (state tag 4)
    RefillStatusPacket p; bool seen; uint32_t ms;
    char box[3][64]; char test[16];       // subIndex 1..3 = Box 1..3 (9 numbers each), subIndex 4 = the manual test seconds
};

HubBox hubB[3];                  // [0] = Box 1, [1] = Box 2, [2] = Box 3
HubRefill hubR;                  // the one Refilling Station
volatile uint16_t hubJoins = 0, hubLeaves = 0, hubLeaveWhy = 0;
static uint8_t hubBypass[3];         // the Maintenance tick box 'Bypass Safety 2 Seconds Relay' as last sent (the S3 forgets it when maintenance starts or ends)
static char hubBuf[1800];        // one answer at a time (the web server answers from a single task)

static void hubCopy(char* dst, size_t n, const char* src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

static const char* hubResetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_SW:       return "software restart";
        case ESP_RST_PANIC:    return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
        default:               return "other";
    }
}

static void hubWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        hubJoins = hubJoins + 1;
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        hubLeaves = hubLeaves + 1;
        hubLeaveWhy = info.wifi_ap_stadisconnected.reason;
    }
}

static void hubTermUpdate(HubTerm& t, const TelemetryPacket& p) {
    uint32_t now = millis();
    if (t.seen && p.pulseCount != t.pulses) { t.changeMs = now; t.ever = true; }
    t.seen = true;
    t.ms = now;
    t.pulses = p.pulseCount;
    t.rawEdges = p.activeTimer;              // A1 / C1 / B1 diagnostic: raw coin-wire edges before the filter
    t.minGap = p.usAlcoholDistance;          // ... and the shortest gap between two edges, in ms
}

static bool hubFresh(bool seen, uint32_t ms, uint32_t now) { return seen && (now - ms) < HUB_FRESH_MS; }

// Called from the ESP-NOW receive callback for EVERY packet; only remembers what the settings pages show. Never consumes anything.
void hubOnPacket(const uint8_t* d, int len) {
    if (!d || len <= 0) return;
    uint32_t now = millis();

    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket c;
        memcpy(&c, d, sizeof(c));
        if (c.commandID != CMD_SET_CONFIG || c.targetDeviceID != 0) return;          // only the nodes' own settings broadcasts
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&hubMux);
        int bi = hubIdxOf(HUB_TAG, c.state);                                         // tag 0 / 2 / 3 = Box 1's S3A / Box 2's S3C / Box 3's S3B
        if (bi >= 0) {
            HubBox& b = hubB[bi];
            if (c.subIndex == 1) hubCopy(b.rules, sizeof(b.rules), c.payloadStr);
            else if (c.subIndex == 2) hubCopy(b.levels, sizeof(b.levels), c.payloadStr);
            else if (c.subIndex == 3) hubCopy(b.phases, sizeof(b.phases), c.payloadStr);
            else if (c.subIndex == 4) hubCopy(b.drying, sizeof(b.drying), c.payloadStr);
            else if (c.subIndex == 5) hubCopy(b.refresh, sizeof(b.refresh), c.payloadStr);
            else if (c.subIndex == 6) hubCopy(b.autorf, sizeof(b.autorf), c.payloadStr);
            else if (c.subIndex == 7) hubCopy(b.beep, sizeof(b.beep), c.payloadStr);
        } else if (c.state == 4) {                                                   // the Refilling Station
            if (c.subIndex >= 1 && c.subIndex <= 3) hubCopy(hubR.box[c.subIndex - 1], sizeof(hubR.box[0]), c.payloadStr);
            else if (c.subIndex == 4) hubCopy(hubR.test, sizeof(hubR.test), c.payloadStr);
        }
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, d, sizeof(b));
        int bi = hubIdxOf(HUB_S3, b.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&hubMux);
        hubB[bi].s3Seen = true; hubB[bi].s3Ms = now; hubB[bi].s3State = b.machineState; hubB[bi].s3Timer = b.activeTimer;
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, d, sizeof(p));
        int bi = hubIdxOf(HUB_SENS, p.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&hubMux);
        hubB[bi].a2 = p; hubB[bi].a2Seen = true; hubB[bi].a2Ms = now;
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, d, sizeof(p));
        int bi = hubIdxOf(HUB_REL, p.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&hubMux);
        hubB[bi].a3Seen = true; hubB[bi].a3Ms = now; hubB[bi].a3Mask = p.relayMask; hubB[bi].a3Orph = p.orphaned;
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, d, sizeof(t));
        int bi = hubIdxOf(HUB_TERM, t.deviceID);
        if (bi < 0) return;
        portENTER_CRITICAL(&hubMux);
        hubTermUpdate(hubB[bi].t, t);
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(RefillStatusPacket)) {
        RefillStatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_REFILL || p.magic != REFILL_MAGIC) return;
        portENTER_CRITICAL(&hubMux);
        hubR.p = p; hubR.seen = true; hubR.ms = now;
        portEXIT_CRITICAL(&hubMux);
    }
}

// ---------------------------------------------------------------------
// Answers
// ---------------------------------------------------------------------
static void hubBoxJson(uint8_t idx, char* out, size_t n) {
    HubBox b;
    RefillStatusPacket rp;
    bool rSeen; uint32_t rMs;
    uint32_t now;
    portENTER_CRITICAL(&hubMux);
    now = millis();
    memcpy(&b, &hubB[idx], sizeof(HubBox));
    memcpy(&rp, &hubR.p, sizeof(rp)); rSeen = hubR.seen; rMs = hubR.ms;
    portEXIT_CRITICAL(&hubMux);

    bool s3Ok = hubFresh(b.s3Seen, b.s3Ms, now);
    bool a2Ok = hubFresh(b.a2Seen, b.a2Ms, now);
    bool a3Ok = hubFresh(b.a3Seen, b.a3Ms, now);
    bool t1Ok = hubFresh(b.t.seen, b.t.ms, now);
    long coinAge = b.t.ever ? (long)(now - b.t.changeMs) : -1L;
    uint8_t rb = idx;                                      // this box's place on the Refilling Station: Box 1 = 0, Box 2 = 1, Box 3 = 2
    bool rOk = hubFresh(rSeen, rMs, now);
    snprintf(out, n,
             "{\"s3ok\":%d,\"s3age\":%lu,\"state\":%u,\"timer\":%lu,"
             "\"a2ok\":%d,\"a2age\":%lu,\"helm\":%.1f,\"door\":%d,\"pan\":%d,\"bak\":%d,\"side\":%d,"
             "\"a3ok\":%d,\"a3mask\":%u,\"a3orph\":%d,\"a1ok\":%d,\"coins\":%lu,\"coinage\":%ld,\"raw\":%lu,\"gap\":%.1f,"
             "\"rf\":[%d,%u,%u,%d,%d,%d],"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"by\":%u,\"rules\":\"%s\",\"levels\":\"%s\",\"phases\":\"%s\",\"drying\":\"%s\",\"refresh\":\"%s\",\"autorf\":\"%s\",\"beep\":\"%s\"}",
             s3Ok ? 1 : 0, (unsigned long)(b.s3Seen ? now - b.s3Ms : 0), (unsigned)b.s3State, (unsigned long)b.s3Timer,
             a2Ok ? 1 : 0, (unsigned long)(b.a2Seen ? now - b.a2Ms : 0), (double)b.a2.usHelmetCm,
             b.a2.doorEnclosure ? 1 : 0, b.a2.doorPanel ? 1 : 0, b.a2.doorBackdoor ? 1 : 0, b.a2.doorAcsSide ? 1 : 0,
             a3Ok ? 1 : 0, (unsigned)b.a3Mask, b.a3Orph ? 1 : 0, t1Ok ? 1 : 0, (unsigned long)b.t.pulses, coinAge, (unsigned long)b.t.rawEdges, (double)b.t.minGap,
             rOk ? 1 : 0, (unsigned)rp.raw[rb], (unsigned)rp.state[rb], (rp.valveMask >> rb) & 1, (rp.queueMask >> rb) & 1, (rp.failMask >> rb) & 1,
             (unsigned long)(now / 1000UL), hubResetReason(), (int)WiFi.softAPgetStationNum(), (unsigned)hubJoins, (unsigned)hubLeaves, (unsigned)hubLeaveWhy,
             (unsigned)hubBypass[idx], b.rules, b.levels, b.phases, b.drying, b.refresh, b.autorf, b.beep);
}

static void hubRefillJson(char* out, size_t n) {
    HubRefill r;
    uint32_t now;
    portENTER_CRITICAL(&hubMux);
    now = millis();
    memcpy(&r, &hubR, sizeof(HubRefill));
    portEXIT_CRITICAL(&hubMux);
    bool ok = hubFresh(r.seen, r.ms, now);
    const RefillStatusPacket& p = r.p;
    snprintf(out, n,
             "{\"ok\":%d,\"age\":%lu,\"seq\":%u,\"raw\":[%u,%u,%u],\"st\":[%u,%u,%u],\"valve\":%u,\"queue\":%u,\"fail\":%u,\"test\":%u,"
             "\"mid\":[%u,%u,%u],\"high\":[%u,%u,%u],\"max\":[%u,%u,%u],"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"b1\":\"%s\",\"b2\":\"%s\",\"b3\":\"%s\",\"tsec\":\"%s\"}",
             ok ? 1 : 0, (unsigned long)(r.seen ? now - r.ms : 0), (unsigned)p.seq,
             (unsigned)p.raw[0], (unsigned)p.raw[1], (unsigned)p.raw[2], (unsigned)p.state[0], (unsigned)p.state[1], (unsigned)p.state[2],
             (unsigned)p.valveMask, (unsigned)p.queueMask, (unsigned)p.failMask, (unsigned)p.testMask,
             (unsigned)p.midEdge[0], (unsigned)p.midEdge[1], (unsigned)p.midEdge[2], (unsigned)p.highEdge[0], (unsigned)p.highEdge[1], (unsigned)p.highEdge[2],
             (unsigned)p.maxEdge[0], (unsigned)p.maxEdge[1], (unsigned)p.maxEdge[2],
             (unsigned long)(now / 1000UL), hubResetReason(), (int)WiFi.softAPgetStationNum(), (unsigned)hubJoins, (unsigned)hubLeaves, (unsigned)hubLeaveWhy,
             r.box[0], r.box[1], r.box[2], r.test);
}
// What the home page shows: who is online (13 boards), each box's step / timer / coins, the Refilling Station's levels.
void hubFillWs(JsonDocument& doc) {
    uint32_t now = millis();
    bool n[13];
    uint8_t  bs[3]; uint32_t bt[3]; uint32_t bc[3];
    uint16_t rraw[3]; uint8_t rst[3], rvalve, rqueue, rfail, rtest; uint16_t rmax[3];
    portENTER_CRITICAL(&hubMux);
    // order of the flags: Box 1 = A1 A2 A3 S3A | Box 2 = C1 C2 C3 S3C | Box 3 = B1 B2 B3 S3B | Refilling Station   (the NODES list of script_js.h uses this order.
    // NOT the order of the Online Server's "n" flags: there C3 is appended as the 13th so the older twelve places did not move - see online_hub.h / names.js)
    for (int i = 0; i < 3; i++) {
        n[i * 4 + 0] = hubFresh(hubB[i].t.seen, hubB[i].t.ms, now);        // terminal
        n[i * 4 + 1] = hubFresh(hubB[i].a2Seen, hubB[i].a2Ms, now);        // sensors
        n[i * 4 + 2] = hubFresh(hubB[i].a3Seen, hubB[i].a3Ms, now);        // relays
        n[i * 4 + 3] = hubFresh(hubB[i].s3Seen, hubB[i].s3Ms, now);        // S3
        bs[i] = hubB[i].s3State; bt[i] = hubB[i].s3Timer; bc[i] = hubB[i].t.pulses;
    }
    n[12] = hubFresh(hubR.seen, hubR.ms, now);                              // Refilling Station
    for (int i = 0; i < 3; i++) { rraw[i] = hubR.p.raw[i]; rst[i] = hubR.p.state[i]; rmax[i] = hubR.p.maxEdge[i]; }
    rvalve = hubR.p.valveMask; rqueue = hubR.p.queueMask; rfail = hubR.p.failMask; rtest = hubR.p.testMask;
    portEXIT_CRITICAL(&hubMux);

    JsonArray on = doc["n"].to<JsonArray>();
    for (int i = 0; i < 13; i++) on.add(n[i] ? 1 : 0);
    doc["b1s"] = bs[0]; doc["b1t"] = bt[0]; doc["b1c"] = bc[0];
    doc["b2s"] = bs[1]; doc["b2t"] = bt[1]; doc["b2c"] = bc[1];
    doc["b3s"] = bs[2]; doc["b3t"] = bt[2]; doc["b3c"] = bc[2];
    // rf = [online, valveMask, queueMask, failMask, testMask, then per box: raw, state (0 EMPTY..4 MAX, 254 not watched, 255 sensor fault), maxEdge]
    JsonArray rf = doc["rf"].to<JsonArray>();
    rf.add(n[12] ? 1 : 0); rf.add(rvalve); rf.add(rqueue); rf.add(rfail); rf.add(rtest);
    for (int i = 0; i < 3; i++) { rf.add(rraw[i]); rf.add(rst[i]); rf.add(rmax[i]); }
}
// ---------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------
static bool hubCsvLooksSane(const String& s) {
    if (s.length() == 0 || s.length() > 190) return false;
    for (unsigned i = 0; i < s.length(); i++) {
        char ch = s[i];
        if (!((ch >= '0' && ch <= '9') || ch == ',' || ch == '.' || ch == '-')) return false;
    }
    return true;
}

static long hubArgInt(AsyncWebServerRequest* r, const char* name, long def) {
    return r->hasParam(name) ? (long)r->getParam(name)->value().toInt() : def;
}
static String hubArgStr(AsyncWebServerRequest* r, const char* name) {
    return r->hasParam(name) ? r->getParam(name)->value() : String("");
}

// kind -> subIndex for each box's CMD_SET_CONFIG; 0 = not a kind the boxes have
static uint8_t hubSubFor(const String& kind) {
    if (kind == "rules")   return 1;
    if (kind == "sensors") return 2;
    if (kind == "dry")     return 3;
    if (kind == "drying")  return 4;
    if (kind == "refresh") return 5;
    if (kind == "autorefresh") return 6;
    if (kind == "beep") return 7;
    return 0;
}

static void hubSendSettings(uint8_t dev, uint8_t sub, const String& csv, const char* who) {
    if (sub == 0 || !hubCsvLooksSane(csv)) { Serial.printf("[HUB] refused a settings message for %s: not a plain list of numbers\n", who); return; }
    sendEspNowCmdDirect(dev, CMD_SET_CONFIG, sub, 0, 0, csv.c_str());
    Serial.printf("[HUB] %s settings (type %u): %s\n", who, (unsigned)sub, csv.c_str());
}

static void hubAnswer(AsyncWebServerRequest* r) { r->send(200, "application/json", hubBuf); }

// ---- any box (idx 0 = Box 1, 1 = Box 2, 2 = Box 3) ----
static void hubBoxHandler(AsyncWebServerRequest* r, uint8_t idx, uint8_t what) {   // what: 0 state, 1 send, 2 maint, 3 mask, 4 screen refresh
    uint8_t dev = HUB_S3[idx];
    if (what == 1) {
        hubSendSettings(dev, hubSubFor(hubArgStr(r, "kind")), hubArgStr(r, "csv"), HUB_BOXNAME[idx]);
    } else if (what == 2) {
        hubBypass[idx] = 0;                                                  // maintenance starting or ending: the S3 puts the 2 s safety back on
        sendEspNowCmdDirect(dev, CMD_SET_MAINTENANCE, 0, 0, hubArgInt(r, "on", 0) == 1 ? 1 : 0);
    } else if (what == 3) {
        uint16_t mask = (uint16_t)(hubArgInt(r, "v", 0) & B3_MASK_ALL);      // the 13 relays of every box (bits 8 and 9, the old drain pumps, are ignored by the relay board)
        uint8_t by = (hubArgInt(r, "by", 0) == 1) ? 1 : 0;                   // tick box "Bypass Safety 2 Seconds Relay"; riding on the relay command as its state byte
        hubBypass[idx] = by;
        sendEspNowCmdDirect(dev, CMD_SET_RELAY_MASK, 0, mask, by);
    } else if (what == 4) {
        // Force refresh of the touch screen: the S3 node re-sends its terminal's current page as a hard refresh (controller re-init + redraw)
        sendEspNowCmdDirect(dev, CMD_TFT_HARD_REFRESH, 0, 0, 0);
    }
    hubBoxJson(idx, hubBuf, sizeof(hubBuf));
    hubAnswer(r);
}

// ---- the Refilling Station ----
static void hubRefillHandler(AsyncWebServerRequest* r, uint8_t what) {   // what: 0 state, 1 send, 2 manual valve test
    if (what == 1) {
        String kind = hubArgStr(r, "kind");
        uint8_t sub = (kind == "box1") ? 1 : ((kind == "box2") ? 2 : ((kind == "box3") ? 3 : ((kind == "test") ? 4 : 0)));
        hubSendSettings(DEVICE_REFILL, sub, hubArgStr(r, "csv"), "Refilling Station");
    } else if (what == 2) {
        long i = hubArgInt(r, "i", -1);
        if (i >= 0 && i < 3) {                                           // the station refuses it while a refill is running (the page shows its testMask / queue)
            sendEspNowCmdDirect(DEVICE_REFILL, CMD_REFILL_TEST, (uint8_t)i, 0, hubArgInt(r, "on", 1) == 1 ? 1 : 0);
            Serial.printf("[HUB] manual valve test Box %ld: %s\n", i + 1, hubArgInt(r, "on", 1) == 1 ? "start" : "stop");
        }
    }
    hubRefillJson(hubBuf, sizeof(hubBuf));
    hubAnswer(r);
}
void hubBegin() {
    WiFi.onEvent(hubWifiEvent);
    memset(hubB, 0, sizeof(hubB));
    memset(&hubR, 0, sizeof(hubR));
}

void hubRegisterRoutes() {
    // the three box pages are ONE page (PAGE_BOX); it reads which box it is from its own address
    server.on("/box1", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX); });
    server.on("/box2", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX); });
    server.on("/box3", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX); });
    server.on("/refill", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_REFILL); });
    server.on("/hub.css", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/css", HUB_CSS); });                  // look of the four pages (hub_theme.h)
    server.on("/hub.js",  HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "application/javascript", HUB_JS); });

    server.on("/api/b1/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 0, 0); });
    server.on("/api/b1/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 0, 1); });
    server.on("/api/b1/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 0, 2); });
    server.on("/api/b1/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 0, 3); });
    server.on("/api/b1/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 0, 4); });

    server.on("/api/b2/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 1, 0); });
    server.on("/api/b2/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 1, 1); });
    server.on("/api/b2/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 1, 2); });
    server.on("/api/b2/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 1, 3); });
    server.on("/api/b2/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 1, 4); });

    server.on("/api/b3/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 2, 0); });
    server.on("/api/b3/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 2, 1); });
    server.on("/api/b3/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 2, 2); });
    server.on("/api/b3/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 2, 3); });
    server.on("/api/b3/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBoxHandler(r, 2, 4); });

    server.on("/api/refill/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubRefillHandler(r, 0); });
    server.on("/api/refill/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubRefillHandler(r, 1); });
    server.on("/api/refill/test",  HTTP_GET, [](AsyncWebServerRequest* r) { hubRefillHandler(r, 2); });
}
