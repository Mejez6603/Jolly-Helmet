#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Shared_Common.h"
#include "fake_page.h"

// =====================================================================
// NODE REFILL_FAKE - A STAND-IN FOR THE REFILLING STATION (spare ESP32)                          (core 3.3.7)
// Created 2026-11-03. TEST TOOL ONLY: flash it on a spare ESP32 while the real Refilling Station (ESP32_REFILL) is not finished.
// It has no sensors and no valves. It broadcasts the SAME RefillStatusPacket the real station sends (DEVICE_REFILL, magic 0xD1, every 1 s,
// and at once on a change), with the levels YOU choose, so the boxes, the LocalServer (/refill page) and the Online Server see a station.
//
// DO NOT run it together with the real station: both would broadcast and the boxes would read whichever packet came last.
//
// FROM YOUR PHONE (no computer needed): power the board, join the Wi-Fi "JollyHelmet-FakeStation" (password 12345678) - the page opens by itself
//   (captive portal), or browse to http://192.168.4.1/ . Tap a level per box, the QUEUE / VALVE / FAIL flags, or the big BROADCASTING / OFFLINE button.
//   The page shows what the BOOT button and the serial console change too. (A phone on this Wi-Fi has no internet and is not on the
//   LocalServer's Wi-Fi: rejoin "JollyHelmet-Local" afterwards.)
//
// HOW TO USE (Serial 115200, line ending Newline - or just the BOOT button):
//   help                       the list below
//   show                       what is being broadcast now
//   <1|2|3|all> <level>        level = empty, low, mid, high, max, fault      e.g.  "3 high"   "all mid"   "2 fault"
//   off / on                   stop / start broadcasting  ("off" = the station is OFFLINE: after the box's offline time, 15 s by default,
//                              Box 3 offers DRYING only)
//   queue <1|2|3>              toggle the "waiting for its turn" flag of a box  (only shows on the LocalServer / web page)
//   valve <1|2|3>              toggle the "valve open" flag of a box            (only shows on the LocalServer / web page)
//   fail  <1|2|3>              toggle the "last refill FAILED" flag of a box    (only shows on the LocalServer / web page)
//   BOOT button, short press   Box 3 level: EMPTY -> LOW -> MID -> HIGH -> MAX -> EMPTY ...
//   BOOT button, hold 1 s      toggle broadcasting on / off (same as on / off)
//
// WHAT TO TEST WITH IT (Box 3 is index 3 here):
//   "3 high" (or mid / max)    tap at WELCOME -> SELECT MODE (CLEANING and DRYING offered)
//   "3 low" or "3 empty"       tap at WELCOME -> SELECT DRY ONLY
//   "3 fault"                  same: dry only
//   "off", wait 15 s           tap at WELCOME -> SELECT DRY ONLY (station not heard)
// Default at power-up: all three boxes HIGH, broadcasting. Nothing is saved to flash. The raw numbers sent match the real station's default
// edges (EMPTY 0, MID from 500, HIGH from 1000, MAX from 1200); the settings are NOT echoed, so the LocalServer's settings tiles stay empty.
// =====================================================================

static const char* AP_SSID = "JollyHelmet-FakeStation";
static const char* AP_PASS = "12345678";          // min 8 characters
WebServer server(80);
DNSServer dnsServer;

const uint8_t  BUTTON_PIN   = 0;                  // BOOT button on the dev boards
const uint32_t STATUS_MS    = 1000;
const uint32_t LONG_PRESS_MS = 1000;
const uint32_t MIN_PRESS_MS  = 30;

const char*     LEVEL_NAME[5] = { "EMPTY", "LOW", "MID", "HIGH", "MAX" };
const uint16_t  LEVEL_RAW[5]  = { 0, 250, 700, 1100, 1300 };
const uint16_t  MID_EDGE = 500, HIGH_EDGE = 1000, MAX_EDGE = 1200;

uint8_t  level[3] = { REFILL_HIGH, REFILL_HIGH, REFILL_HIGH };
uint8_t  valveMask = 0, queueMask = 0, failMask = 0;
bool     sending = true;
uint16_t statusSeq = 0;
uint32_t lastStatusMs = 0;
bool     sendNow = true;

const char* levelName(uint8_t l) { return l <= REFILL_MAX ? LEVEL_NAME[l] : "FAULT"; }

void sendStatus() {
    RefillStatusPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID = DEVICE_REFILL;
    p.magic    = REFILL_MAGIC;
    p.seq      = ++statusSeq;
    for (uint8_t i = 0; i < 3; i++) {
        bool watched  = (i == 2);                      // like the real station (2026-11-04): only Box 3 is watched, Box 1 and 2 are reported as not watched
        p.raw[i]      = watched && level[i] <= REFILL_MAX ? LEVEL_RAW[level[i]] : 0;
        p.state[i]    = watched ? level[i] : (uint8_t)REFILL_OFF;
        p.midEdge[i]  = MID_EDGE;
        p.highEdge[i] = HIGH_EDGE;
        p.maxEdge[i]  = MAX_EDGE;
    }
    p.valveMask = valveMask;
    p.queueMask = queueMask;
    p.failMask  = failMask;
    p.testMask  = 0;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
    lastStatusMs = millis();
}

void printHelp() {
    Serial.println("[FAKE] commands: show | <1|2|3|all> <empty|low|mid|high|max|fault> | off | on | queue <n> | valve <n> | fail <n>");
    Serial.println("[FAKE] BOOT button: short press = Box 3 level up one step, hold 1 s = broadcasting on / off");
}

void printStatus() {
    Serial.printf("[FAKE] broadcasting %s | Box 1 %s | Box 2 %s | Box 3 %s | valve %02X queue %02X fail %02X (bit 0 = Box 1)\n",
                  sending ? "ON" : "OFF (station offline)", levelName(level[0]), levelName(level[1]), levelName(level[2]),
                  (unsigned)valveMask, (unsigned)queueMask, (unsigned)failMask);
}

bool parseLevel(const char* w, uint8_t& out) {
    if (!strcasecmp(w, "empty")) out = REFILL_EMPTY;
    else if (!strcasecmp(w, "low"))   out = REFILL_LOW;
    else if (!strcasecmp(w, "mid"))   out = REFILL_MID;
    else if (!strcasecmp(w, "high"))  out = REFILL_HIGH;
    else if (!strcasecmp(w, "max"))   out = REFILL_MAX;
    else if (!strcasecmp(w, "fault")) out = REFILL_UNKNOWN;
    else return false;
    return true;
}

void changed() { printStatus(); sendNow = true; }

void setSending(bool on) { sending = on; if (on) sendNow = true; printStatus(); }

void handleLine(char* line) {
    char* a = strtok(line, " \t");
    if (!a) return;
    char* b = strtok(nullptr, " \t");
    if (!strcasecmp(a, "help") || !strcmp(a, "?")) { printHelp(); return; }
    if (!strcasecmp(a, "show")) { printStatus(); return; }
    if (!strcasecmp(a, "off"))  { setSending(false); return; }
    if (!strcasecmp(a, "on"))   { setSending(true);  return; }
    if (!strcasecmp(a, "queue") || !strcasecmp(a, "valve") || !strcasecmp(a, "fail")) {
        int n = b ? atoi(b) : 0;
        if (n < 1 || n > 3) { Serial.println("[FAKE] give the box number 1, 2 or 3"); return; }
        uint8_t bit = (uint8_t)(1u << (n - 1));
        uint8_t& m = !strcasecmp(a, "queue") ? queueMask : (!strcasecmp(a, "valve") ? valveMask : failMask);
        m ^= bit;
        changed();
        return;
    }
    if ((a[0] >= '1' && a[0] <= '3' && a[1] == '\0') || !strcasecmp(a, "all")) {
        uint8_t l;
        if (!b || !parseLevel(b, l)) { Serial.println("[FAKE] level must be empty, low, mid, high, max or fault"); return; }
        if (!strcasecmp(a, "all")) { for (uint8_t i = 0; i < 3; i++) level[i] = l; }
        else level[a[0] - '1'] = l;
        changed();
        return;
    }
    Serial.println("[FAKE] unknown command, 'help' lists them");
}

// ---------------------------------------------------------------------
// The phone page (Wi-Fi access point + captive portal)
// ---------------------------------------------------------------------
void sendState() {
    char b[128];
    snprintf(b, sizeof(b), "{\"on\":%s,\"seq\":%u,\"lvl\":[%u,%u,%u],\"valve\":%u,\"queue\":%u,\"fail\":%u}",
             sending ? "true" : "false", (unsigned)statusSeq, (unsigned)level[0], (unsigned)level[1], (unsigned)level[2],
             (unsigned)valveMask, (unsigned)queueMask, (unsigned)failMask);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", b);
}

void handleLevel() {                                   // /api/level?box=0|1|2|3&v=0..4|255
    int box = server.arg("box").toInt();
    int v = server.hasArg("v") ? server.arg("v").toInt() : -1;
    if (box >= 0 && box <= 3 && ((v >= 0 && v <= REFILL_MAX) || v == REFILL_UNKNOWN)) {
        if (box == 0) { for (uint8_t i = 0; i < 3; i++) level[i] = (uint8_t)v; }
        else level[box - 1] = (uint8_t)v;
        changed();
    }
    sendState();
}

void handleFlag() {                                    // /api/flag?box=1..3&t=queue|valve|fail&v=1|0
    int box = server.arg("box").toInt();
    String t = server.arg("t");
    bool on = server.arg("v") == "1";
    if (box >= 1 && box <= 3) {
        uint8_t bit = (uint8_t)(1u << (box - 1));
        uint8_t* m = t == "queue" ? &queueMask : (t == "valve" ? &valveMask : (t == "fail" ? &failMask : nullptr));
        if (m) { if (on) *m |= bit; else *m &= (uint8_t)~bit; changed(); }
    }
    sendState();
}

void handleOn() {                                      // /api/on?v=1|0
    setSending(server.arg("v") == "1");
    sendState();
}

void startWebPage() {
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_WIFI_CHANNEL, 0, 4);
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());         // every name resolves to us, so the phone opens the page by itself
    server.on("/", HTTP_GET, []() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", FAKE_PAGE); });
    server.on("/api/state", HTTP_GET, sendState);
    server.on("/api/level", HTTP_GET, handleLevel);
    server.on("/api/flag", HTTP_GET, handleFlag);
    server.on("/api/on", HTTP_GET, handleOn);
    server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
    server.begin();
    Serial.printf("[FAKE] Phone page: join Wi-Fi '%s' (password %s) and open http://%s/\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

void pollSerial() {
    static char buf[48];
    static uint8_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (n) { buf[n] = '\0'; n = 0; handleLine(buf); }
        } else if (n < sizeof(buf) - 1) buf[n++] = c;
    }
}

void pollButton(uint32_t now) {
    static bool wasDown = false, longDone = false;
    static uint32_t downMs = 0;
    bool down = digitalRead(BUTTON_PIN) == LOW;
    if (down && !wasDown) { downMs = now; longDone = false; }
    if (down && !longDone && (uint32_t)(now - downMs) >= LONG_PRESS_MS) { longDone = true; setSending(!sending); }
    if (!down && wasDown && !longDone && (uint32_t)(now - downMs) >= MIN_PRESS_MS) {
        level[2] = (level[2] >= REFILL_MAX) ? (uint8_t)REFILL_EMPTY : (uint8_t)(level[2] + 1);
        changed();
    }
    wasDown = down;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE REFILL_FAKE: STAND-IN FOR THE REFILLING STATION");
    Serial.println("   TEST TOOL - never run it with the real station");
    Serial.println("=======================================================");
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.mode(WIFI_AP_STA);                            // STA side = ESP-NOW, AP side = the phone page; both on channel 1
    WiFi.setSleep(false);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[FAKE] ESP-NOW: init failed - restarting in 3 s");
        delay(3000);
        ESP.restart();
    }
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    startWebPage();
    printHelp();
    printStatus();
}

void loop() {
    uint32_t now = millis();
    dnsServer.processNextRequest();
    server.handleClient();
    pollSerial();
    pollButton(now);
    if (sending && (sendNow || (uint32_t)(now - lastStatusMs) >= STATUS_MS)) { sendNow = false; sendStatus(); }
    delay(2);
}
