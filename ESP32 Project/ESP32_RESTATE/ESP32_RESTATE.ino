// =====================================================================
//  ESP32_RESTATE - one-time PRICE RECALIBRATION board (2026-10-11)
//
//  Why: the machines used the OLD price list (5 min = 30 or 45 coins) for part of the data; the new list is 5 min = 15 coins and
//  3 min = 10 coins. The old data must be KEPT and restated at the new price list for the Analytics Document Team, so nothing may be reset.
//
//  What it does (a SPARE board, the LocalServer and every machine are left alone):
//    * It LISTENS to the LocalServer's ESP-NOW totals broadcast (HubStatsPacket, every 10 s, channel 1). It sends nothing until the Update button is pressed.
//    * "Capture" copies those totals into this board's flash = the frozen OLD data (survives power-off; cannot be overwritten until cleared).
//    * It then shows, and exports as CSV, the old data AS COLLECTED next to the old data RESTATED at the new price list, plus what was sold
//      since the capture (at the new list), plus the totals. Method and assumptions are written into the report (see restate_core.h).
//  Use: power the board, join Wi-Fi "JollyHelmet-Restate" (password 12345678), open http://192.168.4.1 in a normal browser.
//  When the report has been delivered, simply unplug the board / flash something else on it: nothing else depends on it.
//
//  Board: any ESP32 or ESP32-S3 (no PSRAM, no special partition). Libraries: ESP Async WebServer + Async TCP (the same as the other sketches).
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "restate_core.h"
#include "restate_page.h"

static const char* AP_SSID = "JollyHelmet-Restate";
static const char* AP_PASS = "12345678";                  // change it here if you like (min 8 characters)

static AsyncWebServer server(80);
static Preferences    prefs;
static portMUX_TYPE   mux = portMUX_INITIALIZER_UNLOCKED;
static RsLive         live;
static RsBase         base;
static RsPrices       prices;

// Publishing (the Update button): while pub.on the packet is re-broadcast every 10 s, so a LocalServer / Online Server that was off, or
// restarts, still gets it. They store it in their own flash, so this board can be unplugged afterwards.
static RsPub          pub;
static uint8_t        burstLeft = 0;          // quick repeats right after a press (broadcasts are not acknowledged)
static uint8_t        repeatLeft = 0;         // the slow 10 s repeats: 12 after an Apply (2 minutes, then it stops by itself), 255 = no limit (the old "show beside" mode)
static uint8_t        withdrawLeft = 0;       // quick repeats of the "withdrawn" packet
static uint32_t       pubSent = 0, pubLastMs = 0, burstLastMs = 0;

static void pubSend(bool active) {            // builds the packet from the saved capture + price lists and broadcasts it
    RsLive l; RsBase b; RsPrices p; RestatePacket k;
    portENTER_CRITICAL(&mux); l = live; b = base; p = prices; portEXIT_CRITICAL(&mux);
    rsMakePacket(k, b, l, p, pub.rev, active && b.valid, pub.pad);
    if (esp_now_send(BROADCAST_MAC, (uint8_t*)&k, sizeof(k)) == ESP_OK) { pubSent++; pubLastMs = millis(); }
}

// ---------------------------------------------------------------------
// ESP-NOW (passive): remember the newest HubStatsPacket
// ---------------------------------------------------------------------
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len != (int)sizeof(HubStatsPacket) || data[0] != DEVICE_SERVER) return;
    HubStatsPacket p;
    memcpy(&p, data, sizeof(p));
    portENTER_CRITICAL(&mux);
    live.p = p; live.seen = true; live.ms = millis(); live.packets++;
    portEXIT_CRITICAL(&mux);
}

// ---------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------
static void loadAll() {
    prefs.begin("restate", false);
    memset(&base, 0, sizeof(base));
    prices = RS_DEFAULT_PRICES;
    if (prefs.getBytesLength("base") == sizeof(base)) prefs.getBytes("base", &base, sizeof(base));
    if (prefs.getBytesLength("prices") == sizeof(prices)) prefs.getBytes("prices", &prices, sizeof(prices));
    memset(&pub, 0, sizeof(pub));
    if (prefs.getBytesLength("pub") == sizeof(pub)) prefs.getBytes("pub", &pub, sizeof(pub));
    if (!base.valid) pub.on = 0;
    repeatLeft = (pub.on && pub.pad != 1) ? 255 : 0;           // after a restart only the old "show beside" mode keeps repeating
}

// ---------------------------------------------------------------------
// Web API
// ---------------------------------------------------------------------
static void sendMsg(AsyncWebServerRequest* r, bool ok, const char* msg) {
    String o; rsAdd(o, "{\"ok\":%d,\"msg\":\"%s\"}", ok ? 1 : 0, msg);
    AsyncWebServerResponse* res = r->beginResponse(ok ? 200 : 409, "application/json", o);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}
static long argLong(AsyncWebServerRequest* r, const char* k, long def) { return r->hasParam(k) ? r->getParam(k)->value().toInt() : def; }
static float argFloat(AsyncWebServerRequest* r, const char* k, float def) { return r->hasParam(k) ? r->getParam(k)->value().toFloat() : def; }

static void apiState(AsyncWebServerRequest* r) {
    RsLive l; RsBase b; RsPrices p;
    portENTER_CRITICAL(&mux); l = live; b = base; p = prices; portEXIT_CRITICAL(&mux);
    String o; rsJson(o, b, l, p, millis(), pub.on && pub.pad == 1);
    {   // the publishing state: the packet that would go out now vs the one that went out last
        RestatePacket k; rsMakePacket(k, b, l, p, pub.rev, true, pub.pad);
        bool stale = pub.on && (k.oldRestCoins != pub.oldRest || k.cpmNewX100 != pub.cpm);
        o.remove(o.length() - 1);                                           // drop the closing }
        rsAdd(o, ",\"pub\":{\"on\":%d,\"rev\":%u,\"mode\":%u,\"sent\":%lu,\"ago\":%ld,\"stale\":%d}}", pub.on ? 1 : 0, (unsigned)pub.rev, (unsigned)pub.pad, (unsigned long)pubSent, pubSent ? (long)(millis() - pubLastMs) : -1L, stale ? 1 : 0);
    }
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", o);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}

static void apiReport(AsyncWebServerRequest* r) {
    RsLive l; RsBase b; RsPrices p;
    portENTER_CRITICAL(&mux); l = live; b = base; p = prices; portEXIT_CRITICAL(&mux);
    String o; rsCsv(o, b, l, p, pub.on && pub.pad == 1, (unsigned)pub.rev);
    AsyncWebServerResponse* res = r->beginResponse(200, "text/csv; charset=utf-8", o);
    res->addHeader("Content-Disposition", "attachment; filename=\"JollyHelmet_price_recalibration.csv\"");
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}

static void apiSettings(AsyncWebServerRequest* r) {
    RsPrices p = prices;
    char k[8];
    for (int i = 0; i < 3; i++) {
        snprintf(k, sizeof(k), "nc%d", i); long nc = argLong(r, k, p.nc[i]);
        snprintf(k, sizeof(k), "oc%d", i); long oc = argLong(r, k, p.oc[i]);
        snprintf(k, sizeof(k), "nm%d", i); float nm = argFloat(r, k, p.nm[i]);
        snprintf(k, sizeof(k), "om%d", i); float om = argFloat(r, k, p.om[i]);
        snprintf(k, sizeof(k), "sh%d", i); float sh = argFloat(r, k, p.sh[i]);
        if (nc < 1 || nc > 999 || oc < 1 || oc > 999 || nm < 0.1f || nm > 999 || om < 0.1f || om > 999 || sh < 0 || sh > 1000) { sendMsg(r, false, "A number is out of range (coins 1-999, minutes 0.1-999, share 0-1000)."); return; }
        p.nc[i] = (uint16_t)nc; p.oc[i] = (uint16_t)oc; p.nm[i] = nm; p.om[i] = om; p.sh[i] = sh;
    }
    if (p.sh[0] + p.sh[1] + p.sh[2] <= 0) { sendMsg(r, false, "At least one box needs a share above 0."); return; }
    portENTER_CRITICAL(&mux); prices = p; portEXIT_CRITICAL(&mux);
    prefs.putBytes("prices", &p, sizeof(p));
    sendMsg(r, true, "Saved.");
}

static void apiCapture(AsyncWebServerRequest* r) {
    RsLive l; bool have;
    portENTER_CRITICAL(&mux); l = live; have = base.valid; portEXIT_CRITICAL(&mux);
    if (have) { sendMsg(r, false, "Already captured. Clear the capture first if you really want to capture again."); return; }
    if (!l.seen || (millis() - l.ms) > 30000UL) { sendMsg(r, false, "The LocalServer was not heard in the last 30 s. Nothing captured."); return; }
    RsBase b; memset(&b, 0, sizeof(b));
    b.valid = 1; b.histCount = l.p.histCount > 10 ? 10 : l.p.histCount; b.cvX100 = l.p.coinValueX100;
    b.sessions = l.p.totalSessions; b.coins = l.p.totalCoins; b.durSec = l.p.totalDurationSec; b.seq = l.p.seq;
    b.at = (uint32_t)argLong(r, "at", 0); b.tzMin = (int16_t)argLong(r, "tz", 0);
    for (int i = 0; i < 10; i++) { b.histDur[i] = l.p.histDur[i]; b.histCoins[i] = l.p.histCoins[i]; }
    prefs.putBytes("base", &b, sizeof(b));
    portENTER_CRITICAL(&mux); base = b; portEXIT_CRITICAL(&mux);
    Serial.printf("[RESTATE] captured: %lu customers, %lu coins, %lu s\n", (unsigned long)b.sessions, (unsigned long)b.coins, (unsigned long)b.durSec);
    sendMsg(r, true, "Captured. The old data is saved in this board.");
}

static void apiClear(AsyncWebServerRequest* r) {
    if (!r->hasParam("sure") || r->getParam("sure")->value() != "YES") { sendMsg(r, false, "Not confirmed."); return; }
    RsBase old;
    portENTER_CRITICAL(&mux); old = base; memset(&base, 0, sizeof(base)); portEXIT_CRITICAL(&mux);
    if (old.valid) prefs.putBytes("base_prev", &old, sizeof(old));       // one safety copy of what was cleared (kept in flash, not shown)
    prefs.remove("base");
    if (pub.on) { uint8_t m = pub.pad; pub.on = 0; prefs.putBytes("pub", &pub, sizeof(pub)); if (m != 1) withdrawLeft = 5; }   // "show beside" data is withdrawn too; an applied correction stays (undo it on the LocalServer)
    sendMsg(r, true, "Cleared. You can capture again.");
}

// The Update button: (re)compute from the capture + the saved price lists and broadcast it to the LocalServer and the Online Server
static void apiPublish(AsyncWebServerRequest* r) {
    RsLive l; RsBase b; RsPrices p;
    portENTER_CRITICAL(&mux); l = live; b = base; p = prices; portEXIT_CRITICAL(&mux);
    if (!b.valid) { sendMsg(r, false, "Capture the old data first."); return; }
    uint8_t mode = (uint8_t)argLong(r, "mode", 1);                       // 1 = apply the corrected figures (default), 0 = only show them beside the collected ones
    RestatePacket k; rsMakePacket(k, b, l, p, (uint16_t)(pub.rev + 1), true, mode);
    pub.on = 1; pub.rev++; pub.pad = mode; pub.cpm = k.cpmNewX100; pub.oldRest = k.oldRestCoins;
    prefs.putBytes("pub", &pub, sizeof(pub));
    withdrawLeft = 0; burstLeft = 6; repeatLeft = (mode == 1) ? 12 : 255;
    Serial.printf("[RESTATE] Update #%u: old data restated to %lu coins (%.2f coins/min)\n", (unsigned)pub.rev, (unsigned long)k.oldRestCoins, k.cpmNewX100 / 100.0);
    sendMsg(r, true, mode == 1 ? "Applied: sent to the LocalServer and the Online Server. The LocalServer keeps the original figures and replaces its coin total; the Online page follows at its next upload." : "Update sent. It is repeated every 10 s; the LocalServer and the Online Server store it.");
}

static void apiWithdraw(AsyncWebServerRequest* r) {
    if (!pub.on) { sendMsg(r, false, "Nothing is published."); return; }
    pub.on = 0; prefs.putBytes("pub", &pub, sizeof(pub));
    burstLeft = 0; repeatLeft = 0; withdrawLeft = (pub.pad == 1) ? 0 : 6;
    sendMsg(r, true, pub.pad == 1 ? "Stopped sending. What the LocalServer already applied stays; to undo it press Restore original on its Home page." : "Withdrawn: the LocalServer and the Online Server forget the restated data.");
}

// ---------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    memset(&live, 0, sizeof(live));
    loadAll();
    Serial.printf("\n[RESTATE] starting. Captured: %s\n", base.valid ? "yes" : "no");

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_WIFI_CHANNEL, 0, 4);
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[RESTATE] Wi-Fi '%s' -> http://%s  (channel %d)\n", AP_SSID, WiFi.softAPIP().toString().c_str(), ESPNOW_WIFI_CHANNEL);

    if (esp_now_init() != ESP_OK) Serial.println("[RESTATE] ESP-NOW init FAILED");
    else {
        esp_now_register_recv_cb(onDataRecv);              // it listens all the time; it only SENDS after the Update button was pressed
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
        peerInfo.channel = ESPNOW_WIFI_CHANNEL;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", RESTATE_HTML); });
    server.on("/api/state", HTTP_GET, apiState);
    server.on("/api/settings", HTTP_GET, apiSettings);
    server.on("/api/capture", HTTP_GET, apiCapture);
    server.on("/api/clear", HTTP_GET, apiClear);
    server.on("/api/publish", HTTP_GET, apiPublish);
    server.on("/api/withdraw", HTTP_GET, apiWithdraw);
    server.on("/report.csv", HTTP_GET, apiReport);
    server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "Not found. Open http://192.168.4.1/"); });
    server.begin();
}

void loop() {
    uint32_t now = millis();
    if ((burstLeft || withdrawLeft) && now - burstLastMs >= 300) {      // quick repeats after a press (ESP-NOW broadcasts are not acknowledged)
        burstLastMs = now;
        if (burstLeft) { burstLeft--; pubSend(true); }
        else { withdrawLeft--; pubSend(false); }
    } else if (pub.on && repeatLeft && now - pubLastMs >= 10000UL) {    // then every 10 s, so a receiver that was off or restarted still gets it
        pubSend(true);
        if (repeatLeft != 255) repeatLeft--;
    }
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 30000UL) {
        lastLog = millis();
        RsLive l; portENTER_CRITICAL(&mux); l = live; portEXIT_CRITICAL(&mux);
        if (l.seen) Serial.printf("[RESTATE] LocalServer #%u: %lu customers, %lu coins, %lu s (%lu packets)\n", (unsigned)l.p.seq, (unsigned long)l.p.totalSessions, (unsigned long)l.p.totalCoins, (unsigned long)l.p.totalDurationSec, (unsigned long)l.packets);
        else Serial.println("[RESTATE] LocalServer not heard yet");
    }
    delay(50);
}
