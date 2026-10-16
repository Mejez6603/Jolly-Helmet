#pragma once
// =====================================================================
//  SETTINGS HUB (2026-10-03) - how the LocalServer configures EVERY node.
//
//  The LocalServer listens (read-only) to the same broadcasts the three settings benches listened to: each box's S3 status, the sensor
//  board's readings (A2 / B2 / C2), the relay board's mask (A3 / B3), the coin terminal's counter (A1 / B1 / C1), every ACS's
//  ACSStatusPacket, and the settings lists the S3 nodes and the ACS boards re-broadcast (CMD_SET_CONFIG, target 0; state tag 0 = Box 1's
//  S3A, 2 = Box 2's S3C, 3 = Box 3's S3B, 4 = ACS 1, 5 = ACS 2). It serves one page per box plus one for both ACS boards
//  (/box1 /box2 /box3 /acs - see settings_pages.h) and turns the page's buttons into the same CMD_SET_CONFIG / CMD_SET_MAINTENANCE /
//  CMD_SET_RELAY_MASK messages the benches sent. The nodes keep every setting in their own flash, so nothing is lost if this board is
//  swapped or reflashed.
//
//  Page API (all GET, JSON answers):
//    /api/b1/*  /api/b3/*   state | send?kind=levels|rules|phases[|drying]&csv=  | maint?on=0/1 | mask?v= | refresh
//    /api/b2/*              state | send?kind=rules|heat&csv=                    | maint?on=0/1 | mask?v= | refresh
//    (send?kind=autorefresh = seconds between automatic hard refreshes of the touch screen, 0 = never, max 60)
//    (send?kind=beep = the OPEN A / OPEN B beep, 2 numbers: beep length ms, wait between beeps ms - 0 = a single beep; all three boxes)
//    (send?kind=refresh = which relays trigger a screen refresh, 2 numbers: on-mask,off-mask)
//    (refresh = "force refresh the touch screen": CMD_TFT_HARD_REFRESH to the box's S3 node, which re-sends its terminal's page)
//    /api/acs/*             state | send?kind=levels|timing|mix&csv= | maint?on= | retry | mixok | pump?i=&on= | req?k=&s=    (every call takes u=1|2)
//
//  Included from ESP32_LOCALSRV.ino after sendEspNowCmdDirect() / the server object are defined.
// =====================================================================
#include "settings_pages.h"
#include "hub_theme.h"

static portMUX_TYPE hubMux = portMUX_INITIALIZER_UNLOCKED;
#define HUB_FRESH_MS 5000

struct HubTerm {                 // a coin terminal (A1 / B1 / C1): its pulse counter and the raw-edge diagnostic
    bool seen; uint32_t ms; uint32_t pulses; uint32_t changeMs; bool ever; uint32_t rawEdges; float minGap;
};
struct HubBox13 {                // Box 1 (S3A, A1, A2, A3) or Box 3 (S3B, B1, B2, B3)
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    A2SensorPacket a2; bool a2Seen; uint32_t a2Ms;
    bool a3Seen; uint32_t a3Ms; uint16_t a3Mask; bool a3Orph;
    HubTerm t;
    char rules[200]; char levels[200]; char phases[200]; char drying[40]; char refresh[40]; char autorf[16]; char beep[16];
};
struct HubBox2 {                 // Box 2 (S3C, C1, C2)
    bool s3Seen; uint32_t s3Ms; uint8_t s3State; uint32_t s3Timer;
    TelemetryPacket c2; bool c2Seen; uint32_t c2Ms;
    HubTerm t;
    char rules[200]; char heat[200]; char refresh[40]; char autorf[16]; char beep[16];
};
struct HubAcs {                  // ACS 1 or ACS 2
    ACSStatusPacket p; bool seen; uint32_t ms;
    char levels[200]; char timing[200]; char mix[200];
};

HubBox13 hubB[2];                // [0] = Box 1, [1] = Box 3
HubBox2  hubC;
HubAcs   hubA[2];                // [0] = ACS 1, [1] = ACS 2
volatile uint16_t hubJoins = 0, hubLeaves = 0, hubLeaveWhy = 0;
static uint8_t hubBypass[3];         // the Maintenance tick box 'Bypass Safety 2 Seconds Relay' as last sent: [0] Box 1, [1] Box 3, [2] Box 2 (the S3 forgets it when maintenance starts or ends)
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
    t.rawEdges = p.activeTimer;              // A1 / B1 / C1 diagnostic: raw coin-wire edges before the filter
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
        if (c.state == 0 || c.state == 3) {                                          // Box 1's S3A / Box 3's S3B
            HubBox13& b = hubB[c.state == 3 ? 1 : 0];
            if (c.subIndex == 1) hubCopy(b.rules, sizeof(b.rules), c.payloadStr);
            else if (c.subIndex == 2) hubCopy(b.levels, sizeof(b.levels), c.payloadStr);
            else if (c.subIndex == 3) hubCopy(b.phases, sizeof(b.phases), c.payloadStr);
            else if (c.subIndex == 4) hubCopy(b.drying, sizeof(b.drying), c.payloadStr);
            else if (c.subIndex == 5) hubCopy(b.refresh, sizeof(b.refresh), c.payloadStr);
            else if (c.subIndex == 6) hubCopy(b.autorf, sizeof(b.autorf), c.payloadStr);
            else if (c.subIndex == 7) hubCopy(b.beep, sizeof(b.beep), c.payloadStr);
        } else if (c.state == 2) {                                                   // Box 2's S3C
            if (c.subIndex == 1) hubCopy(hubC.rules, sizeof(hubC.rules), c.payloadStr);
            else if (c.subIndex == 2) hubCopy(hubC.heat, sizeof(hubC.heat), c.payloadStr);
            else if (c.subIndex == 5) hubCopy(hubC.refresh, sizeof(hubC.refresh), c.payloadStr);
            else if (c.subIndex == 6) hubCopy(hubC.autorf, sizeof(hubC.autorf), c.payloadStr);
            else if (c.subIndex == 7) hubCopy(hubC.beep, sizeof(hubC.beep), c.payloadStr);
        } else if (c.state == 4 || c.state == 5) {                                   // ACS 1 / ACS 2
            HubAcs& a = hubA[c.state - 4];
            if (c.subIndex == 1) hubCopy(a.levels, sizeof(a.levels), c.payloadStr);
            else if (c.subIndex == 2) hubCopy(a.timing, sizeof(a.timing), c.payloadStr);
            else if (c.subIndex == 3) hubCopy(a.mix, sizeof(a.mix), c.payloadStr);
        }
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, d, sizeof(b));
        portENTER_CRITICAL(&hubMux);
        if (b.deviceID == DEVICE_S3A)      { hubB[0].s3Seen = true; hubB[0].s3Ms = now; hubB[0].s3State = b.machineState; hubB[0].s3Timer = b.activeTimer; }
        else if (b.deviceID == DEVICE_S3B) { hubB[1].s3Seen = true; hubB[1].s3Ms = now; hubB[1].s3State = b.machineState; hubB[1].s3Timer = b.activeTimer; }
        else if (b.deviceID == DEVICE_S3C) { hubC.s3Seen = true;    hubC.s3Ms = now;    hubC.s3State = b.machineState;    hubC.s3Timer = b.activeTimer; }
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_A2 && p.deviceID != DEVICE_B2) return;
        HubBox13& b = hubB[p.deviceID == DEVICE_B2 ? 1 : 0];
        portENTER_CRITICAL(&hubMux);
        b.a2 = p; b.a2Seen = true; b.a2Ms = now;
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_A3 && p.deviceID != DEVICE_B3) return;
        HubBox13& b = hubB[p.deviceID == DEVICE_B3 ? 1 : 0];
        portENTER_CRITICAL(&hubMux);
        b.a3Seen = true; b.a3Ms = now; b.a3Mask = p.relayMask; b.a3Orph = p.orphaned;
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, d, sizeof(t));
        portENTER_CRITICAL(&hubMux);
        if (t.deviceID == DEVICE_A1)      hubTermUpdate(hubB[0].t, t);
        else if (t.deviceID == DEVICE_B1) hubTermUpdate(hubB[1].t, t);
        else if (t.deviceID == DEVICE_C1) hubTermUpdate(hubC.t, t);
        else if (t.deviceID == DEVICE_C2) { hubC.c2 = t; hubC.c2Seen = true; hubC.c2Ms = now; }
        portEXIT_CRITICAL(&hubMux);
    } else if (len == (int)sizeof(ACSStatusPacket)) {
        ACSStatusPacket p;
        memcpy(&p, d, sizeof(p));
        if (p.deviceID != DEVICE_ACS && p.deviceID != DEVICE_ACS2) return;
        HubAcs& a = hubA[p.deviceID == DEVICE_ACS2 ? 1 : 0];
        portENTER_CRITICAL(&hubMux);
        a.p = p; a.seen = true; a.ms = now;
        portEXIT_CRITICAL(&hubMux);
    }
}

// ---------------------------------------------------------------------
// Answers
// ---------------------------------------------------------------------
static void hubBox13Json(uint8_t idx, char* out, size_t n) {
    HubBox13 b;
    uint32_t now;
    portENTER_CRITICAL(&hubMux);
    now = millis();
    memcpy(&b, &hubB[idx], sizeof(HubBox13));
    portEXIT_CRITICAL(&hubMux);

    bool s3Ok = hubFresh(b.s3Seen, b.s3Ms, now);
    bool a2Ok = hubFresh(b.a2Seen, b.a2Ms, now);
    bool a3Ok = hubFresh(b.a3Seen, b.a3Ms, now);
    bool t1Ok = hubFresh(b.t.seen, b.t.ms, now);
    long coinAge = b.t.ever ? (long)(now - b.t.changeMs) : -1L;
    snprintf(out, n,
             "{\"s3aok\":%d,\"s3aage\":%lu,\"state\":%u,\"timer\":%lu,"
             "\"a2ok\":%d,\"a2age\":%lu,\"helm\":%.1f,\"als\":%.1f,\"sls\":%.1f,\"door\":%d,\"raw0\":%u,\"raw1\":%u,"
             "\"a3ok\":%d,\"a3mask\":%u,\"a3orph\":%d,\"a1ok\":%d,\"coins\":%lu,\"coinage\":%ld,\"raw\":%lu,\"gap\":%.1f,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"by\":%u,\"rules\":\"%s\",\"levels\":\"%s\",\"phases\":\"%s\",\"drying\":\"%s\",\"refresh\":\"%s\",\"autorf\":\"%s\",\"beep\":\"%s\"}",
             s3Ok ? 1 : 0, (unsigned long)(b.s3Seen ? now - b.s3Ms : 0), (unsigned)b.s3State, (unsigned long)b.s3Timer,
             a2Ok ? 1 : 0, (unsigned long)(b.a2Seen ? now - b.a2Ms : 0), (double)b.a2.usHelmetCm, (double)b.a2.usMcAlsCm, (double)b.a2.usMcSlsCm,
             b.a2.doorEnclosure ? 1 : 0, (unsigned)b.a2.wsAlsRaw, (unsigned)b.a2.wsSlsRaw,
             a3Ok ? 1 : 0, (unsigned)b.a3Mask, b.a3Orph ? 1 : 0, t1Ok ? 1 : 0, (unsigned long)b.t.pulses, coinAge, (unsigned long)b.t.rawEdges, (double)b.t.minGap,
             (unsigned long)(now / 1000UL), hubResetReason(), (int)WiFi.softAPgetStationNum(), (unsigned)hubJoins, (unsigned)hubLeaves, (unsigned)hubLeaveWhy,
             (unsigned)hubBypass[idx], b.rules, b.levels, b.phases, b.drying, b.refresh, b.autorf, b.beep);
}

static void hubBox2Json(char* out, size_t n) {
    HubBox2 b;
    uint32_t now;
    portENTER_CRITICAL(&hubMux);
    now = millis();
    memcpy(&b, &hubC, sizeof(HubBox2));
    portEXIT_CRITICAL(&hubMux);

    bool s3Ok = hubFresh(b.s3Seen, b.s3Ms, now);
    bool c2Ok = hubFresh(b.c2Seen, b.c2Ms, now);
    bool c1Ok = hubFresh(b.t.seen, b.t.ms, now);
    long coinAge = b.t.ever ? (long)(now - b.t.changeMs) : -1L;
    uint16_t relayMask = 0;
    for (int i = 0; i < 6; i++) if (b.c2.relayStates[i]) relayMask |= (uint16_t)(1u << i);
    snprintf(out, n,
             "{\"s3cok\":%d,\"s3cage\":%lu,\"state\":%u,\"timer\":%lu,"
             "\"c2ok\":%d,\"c2age\":%lu,\"helm\":%.1f,\"door\":%d,\"pan\":%d,\"bak\":%d,\"mask\":%u,"
             "\"c1ok\":%d,\"coins\":%lu,\"coinage\":%ld,\"raw\":%lu,\"gap\":%.1f,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"by\":%u,\"rules\":\"%s\",\"heat\":\"%s\",\"refresh\":\"%s\",\"autorf\":\"%s\",\"beep\":\"%s\"}",
             s3Ok ? 1 : 0, (unsigned long)(b.s3Seen ? now - b.s3Ms : 0), (unsigned)b.s3State, (unsigned long)b.s3Timer,
             c2Ok ? 1 : 0, (unsigned long)(b.c2Seen ? now - b.c2Ms : 0), (double)b.c2.usHelmetDistance, b.c2.doorEnclosure ? 1 : 0, b.c2.doorPanel ? 1 : 0, b.c2.doorBackdoor ? 1 : 0, (unsigned)relayMask,
             c1Ok ? 1 : 0, (unsigned long)b.t.pulses, coinAge, (unsigned long)b.t.rawEdges, (double)b.t.minGap,
             (unsigned long)(now / 1000UL), hubResetReason(), (int)WiFi.softAPgetStationNum(), (unsigned)hubJoins, (unsigned)hubLeaves, (unsigned)hubLeaveWhy,
             (unsigned)hubBypass[2], b.rules, b.heat, b.refresh, b.autorf, b.beep);
}

static void hubAcsJson(uint8_t idx, char* out, size_t n) {
    HubAcs a;
    uint32_t now;
    portENTER_CRITICAL(&hubMux);
    now = millis();
    memcpy(&a, &hubA[idx], sizeof(HubAcs));
    portEXIT_CRITICAL(&hubMux);
    bool ok = hubFresh(a.seen, a.ms, now);
    snprintf(out, n,
             "{\"unit\":%u,\"ok\":%d,\"age\":%lu,\"step\":%u,\"sec\":%u,\"pumps\":%u,\"flags\":%u,\"empty\":%u,\"warn\":%u,"
             "\"usA\":%.1f,\"usS\":%.1f,\"wlA\":%u,\"wlS\":%u,\"pctA\":%u,\"pctS\":%u,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"levels\":\"%s\",\"timing\":\"%s\",\"mix\":\"%s\"}",
             (unsigned)(idx + 1), ok ? 1 : 0, (unsigned long)(a.seen ? now - a.ms : 0), (unsigned)a.p.step, (unsigned)a.p.stepSec, (unsigned)a.p.pumpMask, (unsigned)a.p.flags,
             (unsigned)a.p.empty, (unsigned)a.p.warn,
             (double)a.p.usAlcCm, (double)a.p.usSctCm, (unsigned)a.p.wlAlcRaw, (unsigned)a.p.wlSctRaw, (unsigned)a.p.levelAlcPct, (unsigned)a.p.levelSctPct,
             (unsigned long)(now / 1000UL), hubResetReason(), (int)WiFi.softAPgetStationNum(), (unsigned)hubJoins, (unsigned)hubLeaves, (unsigned)hubLeaveWhy,
             a.levels, a.timing, a.mix);
}

// What the home page shows: who is online, each box's step / timer / coins, each ACS's step.
void hubFillWs(JsonDocument& doc) {
    uint32_t now = millis();
    bool n[13];
    uint8_t  bs[3]; uint32_t bt[3]; uint32_t bc[3];
    uint8_t  as[2][9];
    portENTER_CRITICAL(&hubMux);
    n[0]  = hubFresh(hubB[0].t.seen, hubB[0].t.ms, now);        // A1
    n[1]  = hubFresh(hubB[0].a2Seen, hubB[0].a2Ms, now);        // A2
    n[2]  = hubFresh(hubB[0].a3Seen, hubB[0].a3Ms, now);        // A3
    n[3]  = hubFresh(hubB[0].s3Seen, hubB[0].s3Ms, now);        // S3A
    n[4]  = hubFresh(hubC.t.seen, hubC.t.ms, now);              // C1
    n[5]  = hubFresh(hubC.c2Seen, hubC.c2Ms, now);              // C2
    n[6]  = hubFresh(hubC.s3Seen, hubC.s3Ms, now);              // S3C
    n[7]  = hubFresh(hubB[1].t.seen, hubB[1].t.ms, now);        // B1
    n[8]  = hubFresh(hubB[1].a2Seen, hubB[1].a2Ms, now);        // B2
    n[9]  = hubFresh(hubB[1].a3Seen, hubB[1].a3Ms, now);        // B3
    n[10] = hubFresh(hubB[1].s3Seen, hubB[1].s3Ms, now);        // S3B
    n[11] = hubFresh(hubA[0].seen, hubA[0].ms, now);            // ACS 1
    n[12] = hubFresh(hubA[1].seen, hubA[1].ms, now);            // ACS 2
    bs[0] = hubB[0].s3State; bt[0] = hubB[0].s3Timer; bc[0] = hubB[0].t.pulses;
    bs[1] = hubC.s3State;    bt[1] = hubC.s3Timer;    bc[1] = hubC.t.pulses;
    bs[2] = hubB[1].s3State; bt[2] = hubB[1].s3Timer; bc[2] = hubB[1].t.pulses;
    for (int i = 0; i < 2; i++) {
        const ACSStatusPacket& p = hubA[i].p;
        as[i][0] = p.step; as[i][1] = p.flags; as[i][2] = p.empty; as[i][3] = p.warn; as[i][4] = p.levelAlcPct; as[i][5] = p.levelSctPct; as[i][6] = p.pumpMask;
        as[i][7] = (uint8_t)(p.stepSec & 0xFF); as[i][8] = (uint8_t)(p.stepSec >> 8);
    }
    portEXIT_CRITICAL(&hubMux);

    JsonArray on = doc.createNestedArray("n");
    for (int i = 0; i < 13; i++) on.add(n[i] ? 1 : 0);
    doc["b1s"] = bs[0]; doc["b1t"] = bt[0]; doc["b1c"] = bc[0];
    doc["b2s"] = bs[1]; doc["b2t"] = bt[1]; doc["b2c"] = bc[1];
    doc["b3s"] = bs[2]; doc["b3t"] = bt[2]; doc["b3c"] = bc[2];
    JsonArray acs = doc.createNestedArray("acs");
    for (int i = 0; i < 2; i++) {
        JsonArray a = acs.createNestedArray();
        a.add(n[11 + i] ? 1 : 0);
        for (int k = 0; k < 7; k++) a.add(as[i][k]);
        a.add((unsigned)as[i][7] | ((unsigned)as[i][8] << 8));
    }
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

// kind -> subIndex for each node's CMD_SET_CONFIG; 0 = not a kind this node has
static uint8_t hubSubFor(const String& kind, bool box3) {
    if (kind == "rules")  return 1;
    if (kind == "levels") return 2;
    if (kind == "phases") return 3;
    if (kind == "drying" && box3) return 4;
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

// ---- Box 1 / Box 3 ----
static void hubBox13Handler(AsyncWebServerRequest* r, uint8_t idx, uint8_t what) {   // what: 0 state, 1 send, 2 maint, 3 mask
    uint8_t dev = (idx == 0) ? DEVICE_S3A : DEVICE_S3B;
    if (what == 1) {
        String kind = hubArgStr(r, "kind");
        hubSendSettings(dev, hubSubFor(kind, idx == 1), hubArgStr(r, "csv"), idx == 0 ? "Box 1" : "Box 3");
    } else if (what == 2) {
        hubBypass[idx] = 0;                                                  // maintenance starting or ending: the S3 puts the 2 s safety back on
        sendEspNowCmdDirect(dev, CMD_SET_MAINTENANCE, 0, 0, hubArgInt(r, "on", 0) == 1 ? 1 : 0);
    } else if (what == 3) {
        uint16_t mask = (uint16_t)(hubArgInt(r, "v", 0) & (idx == 0 ? A3_MASK_ALL : B3_MASK_ALL));
        uint8_t by = (hubArgInt(r, "by", 0) == 1) ? 1 : 0;                   // tick box "Bypass Safety 2 Seconds Relay"; riding on the relay command as its state byte
        hubBypass[idx] = by;
        sendEspNowCmdDirect(dev, CMD_SET_RELAY_MASK, 0, mask, by);
    } else if (what == 4) {
        // Force refresh of the touch screen: the S3 node re-sends its terminal's current page as a hard refresh (controller re-init + redraw)
        sendEspNowCmdDirect(dev, CMD_TFT_HARD_REFRESH, 0, 0, 0);
    }
    hubBox13Json(idx, hubBuf, sizeof(hubBuf));
    hubAnswer(r);
}

// ---- Box 2 ----
static void hubBox2Handler(AsyncWebServerRequest* r, uint8_t what) {
    if (what == 1) {
        String kind = hubArgStr(r, "kind");
        uint8_t sub = (kind == "rules") ? 1 : ((kind == "heat") ? 2 : ((kind == "refresh") ? 5 : ((kind == "autorefresh") ? 6 : ((kind == "beep") ? 7 : 0))));
        hubSendSettings(DEVICE_S3C, sub, hubArgStr(r, "csv"), "Box 2");
    } else if (what == 2) {
        hubBypass[2] = 0;
        sendEspNowCmdDirect(DEVICE_S3C, CMD_SET_MAINTENANCE, 0, 0, hubArgInt(r, "on", 0) == 1 ? 1 : 0);
    } else if (what == 3) {
        uint8_t by = (hubArgInt(r, "by", 0) == 1) ? 1 : 0;
        hubBypass[2] = by;
        sendEspNowCmdDirect(DEVICE_S3C, CMD_SET_RELAY_MASK, 0, (uint16_t)(hubArgInt(r, "v", 0) & C2_MASK_ALL), by);
    } else if (what == 4) {
        sendEspNowCmdDirect(DEVICE_S3C, CMD_TFT_HARD_REFRESH, 0, 0, 0);   // force refresh of C1's touch screen (see the Box 1 / 3 handler)
    }
    hubBox2Json(hubBuf, sizeof(hubBuf));
    hubAnswer(r);
}

// ---- ACS 1 / ACS 2 ----
static void hubAcsHandler(AsyncWebServerRequest* r, uint8_t what) {   // what: 0 state, 1 send, 2 maint, 3 retry, 4 mixok, 5 pump, 6 req
    uint8_t idx = (hubArgInt(r, "u", 1) == 2) ? 1 : 0;
    uint8_t dev = (idx == 1) ? DEVICE_ACS2 : DEVICE_ACS;
    if (what == 1) {
        String kind = hubArgStr(r, "kind");
        uint8_t sub = (kind == "levels") ? 1 : ((kind == "timing") ? 2 : ((kind == "mix") ? 3 : 0));
        hubSendSettings(dev, sub, hubArgStr(r, "csv"), idx == 0 ? "ACS 1" : "ACS 2");
    } else if (what == 2) {
        sendEspNowCmdDirect(dev, CMD_SET_MAINTENANCE, 0, 0, hubArgInt(r, "on", 0) == 1 ? 1 : 0);
    } else if (what == 3) {
        sendEspNowCmdDirect(dev, CMD_SET_MAINTENANCE, 0, 0, 2);          // retry now: forget the EMPTY verdicts
    } else if (what == 4) {
        sendEspNowCmdDirect(dev, CMD_SET_MAINTENANCE, 0, 0, 3);          // accept both mixer containers as they are
    } else if (what == 5) {
        long i = hubArgInt(r, "i", -1);
        if (i >= 0 && i < ACS_PUMP_COUNT) sendEspNowCmdDirect(dev, CMD_SET_RELAY, (uint8_t)i, 0, hubArgInt(r, "on", 0) == 1 ? 1 : 0);
    } else if (what == 6) {
        long k = hubArgInt(r, "k", -1);
        if (k == 0 || k == 1) sendEspNowCmdDirect(dev, CMD_REQUEST_DELIVERY, (uint8_t)k, 0, hubArgInt(r, "s", 0) == 1 ? 1 : 0);   // pretend to be the box
    }
    hubAcsJson(idx, hubBuf, sizeof(hubBuf));
    hubAnswer(r);
}

void hubBegin() {
    WiFi.onEvent(hubWifiEvent);
    memset(hubB, 0, sizeof(hubB));
    memset(&hubC, 0, sizeof(hubC));
    memset(hubA, 0, sizeof(hubA));
}

void hubRegisterRoutes() {
    server.on("/box1", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX1); });
    server.on("/box2", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX2); });
    server.on("/box3", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_BOX3); });
    server.on("/acs",  HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/html", PAGE_ACS); });
    server.on("/hub.css", HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "text/css", HUB_CSS); });                  // look of the four pages (hub_theme.h)
    server.on("/hub.js",  HTTP_GET, [](AsyncWebServerRequest* r) { sendCached(r, "application/javascript", HUB_JS); });

    server.on("/api/b1/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 0, 0); });
    server.on("/api/b1/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 0, 1); });
    server.on("/api/b1/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 0, 2); });
    server.on("/api/b1/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 0, 3); });
    server.on("/api/b1/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 0, 4); });

    server.on("/api/b3/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 1, 0); });
    server.on("/api/b3/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 1, 1); });
    server.on("/api/b3/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 1, 2); });
    server.on("/api/b3/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 1, 3); });
    server.on("/api/b3/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox13Handler(r, 1, 4); });

    server.on("/api/b2/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox2Handler(r, 0); });
    server.on("/api/b2/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox2Handler(r, 1); });
    server.on("/api/b2/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox2Handler(r, 2); });
    server.on("/api/b2/mask",  HTTP_GET, [](AsyncWebServerRequest* r) { hubBox2Handler(r, 3); });
    server.on("/api/b2/refresh", HTTP_GET, [](AsyncWebServerRequest* r) { hubBox2Handler(r, 4); });

    server.on("/api/acs/state", HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 0); });
    server.on("/api/acs/send",  HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 1); });
    server.on("/api/acs/maint", HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 2); });
    server.on("/api/acs/retry", HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 3); });
    server.on("/api/acs/mixok", HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 4); });
    server.on("/api/acs/pump",  HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 5); });
    server.on("/api/acs/req",   HTTP_GET, [](AsyncWebServerRequest* r) { hubAcsHandler(r, 6); });
}
