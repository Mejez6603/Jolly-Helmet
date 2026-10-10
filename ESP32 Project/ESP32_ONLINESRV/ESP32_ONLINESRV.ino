// =====================================================================
//  ESP32_ONLINESRV - the ONLINE SERVER (ESP32-S3)                                   (core 3.3.7)
//  Created 2026-10-09.  NOT YET FLASHED - compiled only. Needs a bench test with the real machines and a Blynk device.
//
//  What it is: a passive listener on the machine radio (ESP-NOW, channel 1 - the same broadcasts the LocalServer hears) that puts a
//  snapshot of everything into ONE Blynk String datastream (V0, JSON, see ONLINE_WEB/DATA_CONTRACT.md), which the monitoring page
//  index.html reads. It NEVER sends a command to a machine and ignores everything Blynk could send back.
//  The revenue totals come from the LocalServer's HubStatsPacket (every 10 s) - the LocalServer must be on for the analytics.
//
//  WI-FI: the board has its own setup Wi-Fi "JollyHelmet-Online" (password below) with a captive portal. In it the user picks ANY Wi-Fi
//  network, pastes the Blynk token, and sees at once whether the board is connected (Wi-Fi joined? Blynk upload OK? since when?).
//
//  ONE RADIO, TWO JOBS: the machines talk on channel 1, a router is on some other channel, and one radio can only be on one channel. So the board
//  works in bursts: it LISTENS on channel 1 almost all the time, then every `iv` seconds it takes a snapshot, joins the user's Wi-Fi for a few
//  seconds, uploads over HTTPS (Blynk's HTTPS API, TLS checked against the ISRG Root X1 certificate) and goes back to channel 1. The
//  setup page therefore drops for a moment during a burst; bursts are held back while somebody is using the setup page.
//
//  Board settings (same as the LocalServer): ESP32S3 Dev Module, USB CDC On Boot Enabled, 240 MHz, QIO 80 MHz, 16 MB flash,
//  partition "16M Flash (3MB APP/9.9MB FATFS)", PSRAM OPI. Libraries: ArduinoJson 7, Async TCP, ESP Async WebServer.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "Shared_Common.h"
#include "online_hub.h"
#include "portal_page.h"
#include "isrg_root.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);   // a burst builds the payload and runs a TLS handshake inside loop()

// ---------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------
static const char*    AP_SSID = "JollyHelmet-Online";
static const char*    AP_PASS = "12345678";            // change it here if you like (min 8 characters)
static const uint32_t FIRST_UPLOAD_DELAY_MS = 25000;   // listen for a while before the first upload so the snapshot is complete
static const uint32_t WIFI_JOIN_TIMEOUT_MS  = 15000;
static const uint32_t PORTAL_BUSY_MS        = 90000;   // no bursts for this long after the setup page was used
#define DAYS 14

// ---------------------------------------------------------------------
// Settings (NVS) and status (shared between the web handlers and loop(): always touched under gLock)
// ---------------------------------------------------------------------
struct Cfg {
    char     ssid[33]; char pass[65]; char token[72]; char server[40];
    uint16_t iv;            // seconds between uploads
    float    tz;            // hours from UTC (Philippines = 8)
    bool     extra;         // also write V1..V4 (single numbers for Blynk dashboards)
    bool     events;        // also log a Blynk event "alert" when something needs attention
    bool     insecure;      // skip the TLS certificate check
    uint32_t mqmax;         // Blynk messages per month in the plan (0 = unknown, the page hides the quota bar)
};
static Cfg cfg;

struct Status {
    const char* phase;                                  // "listening" / "testing" / "uploading"
    bool wifiTried, wifiOk;  char wifiErr[64];  char ip[20]; int rssi; uint32_t wifiAtMs;
    bool blynkTried, blynkOk; char blynkErr[110]; char blynkNote[110]; uint32_t blynkAtMs;
    uint32_t nextAtMs;
    uint32_t mq;                                        // uploads counted this month
    uint32_t boots;
};
static Status st;

struct DayData {            // revenue per day, oldest first, [DAYS-1] = today; saved in NVS
    uint32_t head;          // day number (days since 1970, local) of [DAYS-1]
    uint32_t coins[DAYS];
    uint16_t sess[DAYS];
    uint32_t baseCoins, baseSess;                       // the LocalServer totals already counted
    uint8_t  haveBase;
};
static DayData day;
static bool     dayDirty = false;
static uint32_t dayLastSaveMs = 0;

static DNSServer        dnsServer;
static AsyncWebServer   server(80);
static Preferences      prefs;
static SemaphoreHandle_t gLock = nullptr;
static volatile bool    testRequested = false;
static volatile uint32_t lastPortalHitMs = 0;
static volatile uint8_t lastDiscReason = 0;
static uint32_t         uploadCounter = 0, seqBase = 0;
static uint32_t         alertsSent = 0;                 // alerts already reported as Blynk events (cleared when the alert goes away)
static uint32_t         lastAlertMask = 0;
static int              mqMonth = -1;

static void lock()   { if (gLock) xSemaphoreTake(gLock, portMAX_DELAY); }
static void unlock() { if (gLock) xSemaphoreGive(gLock); }

static const char* resetReasonText() {
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

// ---------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------
static void loadConfig() {
    prefs.begin("online", false);
    memset(&cfg, 0, sizeof(cfg));
    strlcpy(cfg.ssid,   prefs.getString("ssid", "").c_str(),   sizeof(cfg.ssid));
    strlcpy(cfg.pass,   prefs.getString("pass", "").c_str(),   sizeof(cfg.pass));
    strlcpy(cfg.token,  prefs.getString("token", "").c_str(),  sizeof(cfg.token));
    strlcpy(cfg.server, prefs.getString("server", "blynk.cloud").c_str(), sizeof(cfg.server));
    cfg.iv       = prefs.getUShort("iv", 60);
    cfg.tz       = prefs.getFloat("tz", 8.0f);
    cfg.extra    = prefs.getBool("extra", false);
    cfg.events   = prefs.getBool("events", false);
    cfg.insecure = prefs.getBool("insecure", false);
    cfg.mqmax    = prefs.getUInt("mqmax", 0);
    if (cfg.iv < 30) cfg.iv = 30;
    if (!cfg.server[0]) strlcpy(cfg.server, "blynk.cloud", sizeof(cfg.server));
    st.mq    = prefs.getUInt("mq", 0);
    mqMonth  = prefs.getInt("mqm", -1);
    st.boots = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", st.boots);
    memset(&day, 0, sizeof(day));
    if (prefs.getBytesLength("day") == sizeof(day)) prefs.getBytes("day", &day, sizeof(day));
}
static void saveConfig() {     // caller holds no lock; copies under the lock, writes outside it
    Cfg c;
    lock(); c = cfg; unlock();
    prefs.putString("ssid", c.ssid);   prefs.putString("pass", c.pass);   prefs.putString("token", c.token);   prefs.putString("server", c.server);
    prefs.putUShort("iv", c.iv);       prefs.putFloat("tz", c.tz);        prefs.putBool("extra", c.extra);      prefs.putBool("events", c.events);
    prefs.putBool("insecure", c.insecure);                                prefs.putUInt("mqmax", c.mqmax);
}

// ---------------------------------------------------------------------
// Time (NTP, only while the board is on the user's Wi-Fi; the clock keeps running afterwards)
// ---------------------------------------------------------------------
static bool timeValid() { return time(nullptr) > 1700000000; }
static uint32_t lastNtpMs = 0;
static void syncTime() {
    if (timeValid() && lastNtpMs && millis() - lastNtpMs < 6UL * 3600UL * 1000UL) return;
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    uint32_t t0 = millis();
    while (!timeValid() && millis() - t0 < 8000) delay(100);
    if (timeValid()) lastNtpMs = millis();
}
static int32_t dayNumberNow() {
    time_t t = time(nullptr);
    return (int32_t)((t + (long)(cfg.tz * 3600.0f)) / 86400L);
}
static void monthCheck() {                      // the upload counter restarts each calendar month
    if (!timeValid()) return;
    time_t t = time(nullptr) + (long)(cfg.tz * 3600.0f);
    struct tm tmv; gmtime_r(&t, &tmv);
    int m = tmv.tm_year * 12 + tmv.tm_mon;
    if (m != mqMonth) { mqMonth = m; st.mq = 0; prefs.putInt("mqm", mqMonth); prefs.putUInt("mq", 0); }
}

// ---------------------------------------------------------------------
// Revenue per day, from the LocalServer's running totals (counted only while the clock is known; nothing is lost while it is not)
// ---------------------------------------------------------------------
static void dayTick(const SnapAll& s) {
    if (!s.s.seen) return;
    uint32_t totC = s.s.p.totalCoins, totS = s.s.p.totalSessions;
    if (!day.haveBase) { day.baseCoins = totC; day.baseSess = totS; day.haveBase = 1; dayDirty = true; }
    if (timeValid()) {
        uint32_t dn = (uint32_t)dayNumberNow();
        if (day.head == 0) { day.head = dn; dayDirty = true; }
        if (dn != day.head) {
            uint32_t n = (dn > day.head) ? dn - day.head : DAYS;
            if (n >= DAYS) { memset(day.coins, 0, sizeof(day.coins)); memset(day.sess, 0, sizeof(day.sess)); }
            else {
                for (uint32_t i = 0; i + n < DAYS; i++) { day.coins[i] = day.coins[i + n]; day.sess[i] = day.sess[i + n]; }
                for (uint32_t i = DAYS - n; i < DAYS; i++) { day.coins[i] = 0; day.sess[i] = 0; }
            }
            day.head = dn; dayDirty = true;
        }
        if (totC < day.baseCoins || totS < day.baseSess) {            // the LocalServer's statistics were reset
            day.baseCoins = totC; day.baseSess = totS; dayDirty = true;
        } else if (totC != day.baseCoins || totS != day.baseSess) {
            day.coins[DAYS - 1] += totC - day.baseCoins;
            day.sess[DAYS - 1]  += (uint16_t)(totS - day.baseSess);
            day.baseCoins = totC; day.baseSess = totS; dayDirty = true;
        }
    }
    if (dayDirty && millis() - dayLastSaveMs > 60000UL) { prefs.putBytes("day", &day, sizeof(day)); dayDirty = false; dayLastSaveMs = millis(); }
}

// ---------------------------------------------------------------------
// The Blynk payload  (see ONLINE_WEB/DATA_CONTRACT.md)
// ---------------------------------------------------------------------
static size_t buildPayload(const SnapAll& s, char* out, size_t cap, uint32_t seq, bool compact) {   // compact = no history lists, minimal health (about 350 bytes shorter)
    size_t len = liveJson(s, out, cap, seq, timeValid() ? (uint32_t)time(nullptr) : 0, cfg.iv);
    jadd(out, cap, len, ",\"st\":{");
    const size_t BUDGET = 990;                                         // a Blynk String value may be 1024 bytes at most
    bool first = true;
    if (s.s.seen) {
        const HubStatsPacket& p = s.s.p;
        float cv = p.coinValueX100 / 100.0f;
        uint32_t avg = p.totalSessions ? p.totalDurationSec / p.totalSessions : 0;
        jadd(out, cap, len, "\"cv\":%.2f,\"ss\":%lu,\"cn\":%lu,\"rv\":%.2f,\"ad\":%lu", (double)cv, (unsigned long)p.totalSessions, (unsigned long)p.totalCoins, (double)(p.totalCoins * cv), (unsigned long)avg);
        first = false;
    }
    // system health
    if (compact) {
        jadd(out, cap, len, "%s\"sys\":{\"up\":%lu}", first ? "" : ",", (unsigned long)(millis() / 1000UL));
    } else {
        lock(); int rssi = st.rssi; uint32_t mq = st.mq, boots = st.boots; unlock();
        jadd(out, cap, len, "%s\"sys\":{\"up\":%lu,\"rst\":\"%s\",\"boots\":%lu,\"rssi\":%d,\"heap\":%lu,\"mq\":%lu", first ? "" : ",", (unsigned long)(millis() / 1000UL), resetReasonText(),
             (unsigned long)boots, rssi, (unsigned long)ESP.getFreeHeap(), (unsigned long)mq);
        if (cfg.mqmax) jadd(out, cap, len, ",\"mqmax\":%lu", (unsigned long)cfg.mqmax);
        jadd(out, cap, len, "}");
        first = false;
    }
    // last finished sessions (pesos), then the daily history - each only if it still fits
    if (!compact && s.s.seen && s.s.p.histCount) {
        char tmp[200]; size_t tl = 0;
        float cv = s.s.p.coinValueX100 / 100.0f;
        for (int i = 0; i < s.s.p.histCount && i < 10; i++) jadd(tmp, sizeof(tmp), tl, "%s[%u,%.0f]", i ? "," : "", (unsigned)s.s.p.histDur[i], (double)(s.s.p.histCoins[i] * cv));
        if (len + tl + 16 < BUDGET) jadd(out, cap, len, ",\"h\":[%s]", tmp);
    }
    if (!compact && day.haveBase && timeValid()) {
        char tmp[260]; size_t tl = 0;
        for (int i = 0; i < DAYS; i++) jadd(tmp, sizeof(tmp), tl, "%s[%lu,%u]", i ? "," : "", (unsigned long)day.coins[i], (unsigned)day.sess[i]);
        if (len + tl + 16 < BUDGET) jadd(out, cap, len, ",\"dy\":[%s]", tmp);
    }
    jadd(out, cap, len, "}}");
    return len;
}

// ---------------------------------------------------------------------
// Talking to Blynk (HTTPS API) - always inside a burst, while the board is on the user's Wi-Fi
// ---------------------------------------------------------------------
static void urlEncode(const char* in, char* out, size_t cap) {
    static const char* HEX_ = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 4 < cap; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') out[o++] = (char)*p;
        else { out[o++] = '%'; out[o++] = HEX_[*p >> 4]; out[o++] = HEX_[*p & 15]; }
    }
    out[o] = '\0';
}
static bool blynkGet(const char* host, bool insecure, const char* pathAndQuery, int& code, char* err, size_t errN) {
    err[0] = '\0';
    WiFiClientSecure client;
    if (insecure) client.setInsecure(); else client.setCACert(ISRG_ROOT_X1);
    client.setTimeout(20);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    http.useHTTP10(true);                       // plain HTTP/1.0 exchange: the server answers and closes, no keep-alive or chunked replies to wait for
    String url = String("https://") + host + pathAndQuery;
    if (!http.begin(client, url)) { strlcpy(err, "could not start the request", errN); code = -1; return false; }
    uint32_t t0 = millis();
    code = http.GET();
    uint32_t took = millis() - t0;
    bool ok = (code == 200);
    if (!ok) {
        if (code > 0) {
            String body = http.getString();
            JsonDocument d;
            if (deserializeJson(d, body) == DeserializationError::Ok && d["error"]["message"].is<const char*>()) snprintf(err, errN, "%s", d["error"]["message"].as<const char*>());
            else snprintf(err, errN, "Blynk answered HTTP %d", code);
        } else {
            char tls[64] = {0};
            int rawErr = client.lastError(tls, sizeof(tls));
            String why = HTTPClient::errorToString(code);
            why += " (code " + String(code) + ", " + String((unsigned long)took) + " ms";
            if (rawErr) { why += ", TLS " + String(rawErr) + ": "; why += tls; }
            why += ")";
            strlcpy(err, why.c_str(), errN);
        }
    }
    http.end();
    return ok;
}

static const char* discReasonText(uint8_t r) {
    switch (r) {
        case 0:   return "no answer (timeout)";
        case 201: return "network not found";
        case 202: case 15: case 204: case 2: case 14: case 23: return "wrong password";
        case 205: return "connection failed";
        default:  return "could not join";
    }
}

// ---------------------------------------------------------------------
// The radio: back to channel 1 after a burst
// ---------------------------------------------------------------------
static void startAp() {
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_WIFI_CHANNEL, 0, 4);
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}
static void backToMachineChannel() {
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.softAPdisconnect(false);
    delay(50);
    startAp();
}

// ---------------------------------------------------------------------
// One burst: snapshot -> join the Wi-Fi -> clock -> upload -> back to channel 1
// ---------------------------------------------------------------------
static void setPhase(const char* p) { lock(); st.phase = p; unlock(); }

static void runBurst(bool isTest) {
    uint32_t startMs = millis();
    Cfg c; lock(); c = cfg; unlock();
    SnapAll s; snapTake(s);                                           // taken BEFORE leaving channel 1
    uint32_t alertMask = alertsNow(s, millis() / 1000UL);
    dayTick(s);
    static char payload[1100];                                        // static: keeps the loop task stack small
    uint32_t seq = seqBase + (++uploadCounter);
    size_t plen = buildPayload(s, payload, sizeof(payload), seq, false);
    if (plen >= 1024) { Serial.println("[ONLINE] payload too long - trimming is not possible, check the contract"); }

    setPhase(isTest ? "testing" : "uploading");
    Serial.printf("[ONLINE] burst start (%s): joining '%s'\n", isTest ? "test" : "upload", c.ssid);

    // 1. join the Wi-Fi
    lastDiscReason = 0;
    WiFi.begin(c.ssid, c.pass[0] ? c.pass : nullptr);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) { delay(100); dnsServer.processNextRequest(); }
    bool joined = (WiFi.status() == WL_CONNECTED);
    lock();
    st.wifiTried = true; st.wifiOk = joined; st.wifiAtMs = millis();
    if (joined) { st.wifiErr[0] = '\0'; strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof(st.ip)); st.rssi = WiFi.RSSI(); }
    else { strlcpy(st.wifiErr, discReasonText(lastDiscReason), sizeof(st.wifiErr)); st.ip[0] = '\0'; }
    unlock();

    if (joined) {
        // 2. clock
        syncTime();
        monthCheck();
        bool clockOk = timeValid() || c.insecure;
        if (!clockOk) {
            lock(); st.blynkTried = true; st.blynkOk = false; st.blynkAtMs = millis(); strlcpy(st.blynkErr, "the clock could not be set (no NTP answer), so the certificate cannot be checked", sizeof(st.blynkErr)); st.blynkNote[0] = '\0'; unlock();
        } else {
            // 3. upload V0
            static char enc[2200], path[2400];
            char err[110], note[110];
            note[0] = '\0';
            urlEncode(payload, enc, sizeof(enc));
            snprintf(path, sizeof(path), "/external/api/update?token=%s&v0=%s", c.token, enc);
            int code = 0;
            bool ok = blynkGet(c.server, c.insecure, path, code, err, sizeof(err));
            if (!ok && code <= 0) {                                             // no answer at all: wait a moment and try once more with the SHORT version
                Serial.printf("[ONLINE] Blynk update: no answer (%s) - retrying with the short version\n", err);
                delay(700);
                buildPayload(s, payload, sizeof(payload), seq, true);
                urlEncode(payload, enc, sizeof(enc));
                snprintf(path, sizeof(path), "/external/api/update?token=%s&v0=%s", c.token, enc);
                char err2[110]; int code2 = 0;
                if (blynkGet(c.server, c.insecure, path, code2, err2, sizeof(err2))) {
                    ok = true; code = code2; err[0] = '\0';
                    strlcpy(note, "only the SHORT version went through (the full one got no answer)", sizeof(note));
                } else {
                    size_t el = strlen(err);
                    if (el + 12 < sizeof(err)) snprintf(err + el, sizeof(err) - el, " | retry: %.40s", err2);          // both reasons, as far as they fit
                }
            }
            lock();
            st.blynkTried = true; st.blynkOk = ok; st.blynkAtMs = millis();
            strlcpy(st.blynkErr, ok ? "" : err, sizeof(st.blynkErr));
            if (ok) { st.mq++; }
            unlock();
            Serial.printf("[ONLINE] Blynk update: %s (%d) %s %s\n", ok ? "OK" : "FAILED", code, ok ? "" : err, note);

            if (ok && c.extra) {                                                // optional single numbers for Blynk dashboards
                int running = 0;
                if (s.on[3]  && (s.b[0].s3State == STATE_CLEANING || s.b[0].s3State == STATE_DRYING || s.b[0].s3State == STATE_COOL_DOWN)) running++;
                if (s.on[6]  && (s.c.s3State == B2_STATE_HEATING || s.c.s3State == B2_STATE_COOL_DOWN)) running++;
                if (s.on[10] && (s.b[1].s3State == STATE_CLEANING || s.b[1].s3State == STATE_DRYING || s.b[1].s3State == STATE_COOL_DOWN)) running++;
                int nAlerts = __builtin_popcount(alertMask);
                float cv = s.s.seen ? s.s.p.coinValueX100 / 100.0f : 1.0f;
                snprintf(path, sizeof(path), "/external/api/batch/update?token=%s&v1=%.2f&v2=%lu&v3=%d&v4=%d", c.token, (double)(s.s.seen ? s.s.p.totalCoins * cv : 0.0f),
                         (unsigned long)(s.s.seen ? s.s.p.totalSessions : 0), nAlerts, running);
                bool ok2 = blynkGet(c.server, c.insecure, path, code, err, sizeof(err));
                Serial.printf("[ONLINE] Blynk V1-V4: %s (%d) %s\n", ok2 ? "OK" : "FAILED", code, ok2 ? "" : err);
                if (!ok2) snprintf(note, sizeof(note), "V1-V4 failed: %.90s", err);
            }
            if (ok && c.events) {                                               // a Blynk event for every NEW alert (at most 3 per burst)
                uint32_t fresh = alertMask & ~alertsSent;
                int sent = 0;
                for (uint8_t id = 0; id < ALERT_COUNT && sent < 3; id++) {
                    if (!(fresh & (1u << id))) continue;
                    char txt[96], encT[200];
                    alertText(id, txt, sizeof(txt));
                    urlEncode(txt, encT, sizeof(encT));
                    snprintf(path, sizeof(path), "/external/api/logEvent?token=%s&code=alert&description=%s", c.token, encT);
                    if (blynkGet(c.server, c.insecure, path, code, err, sizeof(err))) { alertsSent |= (1u << id); sent++; }
                    else { Serial.printf("[ONLINE] logEvent failed: %s\n", err); if (!note[0]) snprintf(note, sizeof(note), "event failed: %.90s", err); break; }
                }
            }
            lock(); strlcpy(st.blynkNote, note, sizeof(st.blynkNote)); unlock();
            if (ok && (uploadCounter % 10 == 0)) prefs.putUInt("mq", st.mq);
        }
    }
    alertsSent &= alertMask;                                                  // an alert that went away may be reported again later
    lastAlertMask = alertMask;

    // 4. back to the machines' channel
    backToMachineChannel();
    lock();
    st.phase = "listening";
    st.nextAtMs = startMs + (uint32_t)c.iv * 1000UL;
    if ((int32_t)(st.nextAtMs - millis()) < 5000) st.nextAtMs = millis() + 5000;
    unlock();
    Serial.printf("[ONLINE] burst done in %lu ms, channel back on %d\n", (unsigned long)(millis() - startMs), ESPNOW_WIFI_CHANNEL);
}

// ---------------------------------------------------------------------
// ESP-NOW (passive)
// ---------------------------------------------------------------------
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    onlineOnPacket(data, len);
}
static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) lastDiscReason = info.wifi_sta_disconnected.reason;
}

// ---------------------------------------------------------------------
// Setup page (captive portal) API
// ---------------------------------------------------------------------
static void sendJson(AsyncWebServerRequest* r, JsonDocument& d) {
    String out; serializeJson(d, out);
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", out);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}
static void apiStatus(AsyncWebServerRequest* r) {
    SnapAll s; snapTake(s);
    uint32_t now = millis();
    JsonDocument d;
    lock();
    d["state"]    = st.phase;
    d["ssid"]     = cfg.ssid;
    d["hasPass"]  = cfg.pass[0] ? 1 : 0;
    d["hasToken"] = cfg.token[0] ? 1 : 0;
    d["server"]   = cfg.server;
    d["iv"]       = cfg.iv; d["tz"] = cfg.tz; d["extra"] = cfg.extra ? 1 : 0; d["events"] = cfg.events ? 1 : 0; d["insecure"] = cfg.insecure ? 1 : 0; d["mqmax"] = cfg.mqmax;
    JsonObject w = d["wifi"].to<JsonObject>();
    w["ok"] = st.wifiOk ? 1 : 0; w["err"] = st.wifiErr; w["ip"] = st.ip; w["rssi"] = st.rssi; w["age"] = st.wifiTried ? (int)((now - st.wifiAtMs) / 1000UL) : -1;
    JsonObject b = d["blynk"].to<JsonObject>();
    b["ok"] = st.blynkOk ? 1 : 0; b["err"] = st.blynkErr; b["note"] = st.blynkNote; b["age"] = st.blynkTried ? (int)((now - st.blynkAtMs) / 1000UL) : -1;
    int32_t nx = (int32_t)(st.nextAtMs - now);
    d["next"] = nx > 0 ? nx / 1000 : 0;
    d["mq"]   = st.mq;
    unlock();
    JsonObject ra = d["radio"].to<JsonObject>();
    ra["heard"] = snapHeardCount(s); ra["stats"] = s.s.seen ? 1 : 0; ra["statsAge"] = s.s.seen ? (int)((now - s.s.ms) / 1000UL) : -1;
    if (timeValid()) {
        time_t t = time(nullptr) + (long)(cfg.tz * 3600.0f); struct tm tmv; gmtime_r(&t, &tmv);
        char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv); d["time"] = buf;
    } else d["time"] = "not set yet (set at the first successful upload)";
    d["up"]  = now / 1000UL;
    d["rst"] = resetReasonText();
    sendJson(r, d);
}
static void apiScan(AsyncWebServerRequest* r) {
    lastPortalHitMs = millis();
    JsonDocument d;
    if (r->hasParam("start")) { WiFi.scanDelete(); WiFi.scanNetworks(true, false); d["ok"] = 1; sendJson(r, d); return; }
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) { d["scanning"] = (n == WIFI_SCAN_RUNNING); d["networks"].to<JsonArray>(); sendJson(r, d); return; }
    d["scanning"] = false;
    JsonArray arr = d["networks"].to<JsonArray>();
    for (int i = 0; i < n && i < 24; i++) {
        if (WiFi.SSID(i).length() == 0) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 1 : 0;
    }
    WiFi.scanDelete();
    sendJson(r, d);
}
static String param(AsyncWebServerRequest* r, const char* k) { return r->hasParam(k) ? r->getParam(k)->value() : String(); }
static void apiSave(AsyncWebServerRequest* r) {
    lastPortalHitMs = millis();
    lock();
    if (r->hasParam("ssid")) {
        String ssid = param(r, "ssid");
        bool changed = strcmp(ssid.c_str(), cfg.ssid) != 0;
        strlcpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid));
        if (r->hasParam("pass")) strlcpy(cfg.pass, param(r, "pass").c_str(), sizeof(cfg.pass));
        else if (changed) cfg.pass[0] = '\0';                              // a different network without a password = an open network
    }
    if (param(r, "token").length()) strlcpy(cfg.token, param(r, "token").c_str(), sizeof(cfg.token));
    if (param(r, "server").length()) strlcpy(cfg.server, param(r, "server").c_str(), sizeof(cfg.server));
    int iv = param(r, "iv").toInt(); if (iv >= 30 && iv <= 3600) cfg.iv = (uint16_t)iv;
    if (r->hasParam("tz")) { float tz = param(r, "tz").toFloat(); if (tz >= -12.0f && tz <= 14.0f) cfg.tz = tz; }
    cfg.extra    = param(r, "extra")    == "1";
    cfg.events   = param(r, "events")   == "1";
    cfg.insecure = param(r, "insecure") == "1";
    if (r->hasParam("mqmax")) cfg.mqmax = (uint32_t)param(r, "mqmax").toInt();
    unlock();
    saveConfig();
    testRequested = true;
    JsonDocument d; d["ok"] = 1; sendJson(r, d);
}
static void apiForget(AsyncWebServerRequest* r) {
    lastPortalHitMs = millis();
    lock(); cfg.ssid[0] = '\0'; cfg.pass[0] = '\0'; cfg.token[0] = '\0'; st.wifiTried = st.blynkTried = false; unlock();
    saveConfig();
    JsonDocument d; d["ok"] = 1; sendJson(r, d);
}

// ---------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    gLock = xSemaphoreCreateMutex();
    memset(&st, 0, sizeof(st));
    st.phase = "listening";
    loadConfig();
    seqBase = (esp_random() & 0xFFFFF) * 16;
    Serial.printf("\n[ONLINE] Online Server starting (boot #%lu, %s)\n", (unsigned long)st.boots, resetReasonText());

    WiFi.persistent(false);
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    startAp();
    Serial.printf("[ONLINE] Setup Wi-Fi '%s' at %s on channel %d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), ESPNOW_WIFI_CHANNEL);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    if (esp_now_init() != ESP_OK) Serial.println("[ONLINE] ESP-NOW init FAILED");
    else esp_now_register_recv_cb(onDataRecv);            // listen only: no peers, nothing is ever sent

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { lastPortalHitMs = millis(); r->send(200, "text/html", PORTAL_HTML); });
    server.on("/api/status", HTTP_GET, apiStatus);                        // polled every 2 s by the page: does not count as "in use"
    server.on("/api/scan", HTTP_GET, apiScan);
    server.on("/api/save", HTTP_GET, apiSave);
    server.on("/api/forget", HTTP_GET, apiForget);
    server.onNotFound([](AsyncWebServerRequest* r) { r->redirect("http://192.168.4.1/"); });   // captive portal: anything else shows the setup page
    server.begin();

    lock(); st.nextAtMs = millis() + FIRST_UPLOAD_DELAY_MS; unlock();
}

void loop() {
    dnsServer.processNextRequest();
    uint32_t now = millis();

    static uint32_t lastTick = 0;
    if (now - lastTick >= 1000) {
        lastTick = now;
        SnapAll s; snapTake(s);
        dayTick(s);
    }

    bool ready; uint32_t nextAt;
    lock(); ready = cfg.ssid[0] && cfg.token[0]; nextAt = st.nextAtMs; unlock();
    bool portalBusy = WiFi.softAPgetStationNum() > 0 && (now - lastPortalHitMs) < PORTAL_BUSY_MS;

    if (testRequested) {
        testRequested = false;
        bool ok; lock(); ok = cfg.ssid[0]; unlock();
        if (ok) runBurst(true);
    } else if (ready && (int32_t)(now - nextAt) >= 0) {
        if (portalBusy) { lock(); st.nextAtMs = now + 5000; unlock(); }        // somebody is using the setup page: check again in a moment
        else runBurst(false);
    }
    delay(2);
}
