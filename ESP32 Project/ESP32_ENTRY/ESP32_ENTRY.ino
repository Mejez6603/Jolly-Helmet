// =====================================================================
//  ESP32_ENTRY - HISTORY FILE UPLOADER (2026-10-15).   A spare ESP32 / ESP32-S3; the LocalServer and the machines are left alone.
//
//  What it is for: getting the team's  history.json  onto the LocalServer easily, from a phone or a PC:
//    * you join this board's Wi-Fi "JollyHelmet-Entry" (password 12345678) and open http://192.168.5.1
//    * choose history.json (or load the current one from the LocalServer, or the copy this board saved, and edit the text), press Check, then Push
//    * the board joins the LocalServer's Wi-Fi by itself (JollyHelmet-Local) and sends the file to its  /history/upload  page - the same page the LocalServer's
//      Home page uses. The LocalServer checks every row again and replaces its history.json only if all rows are good (a bad file changes nothing).
//  history.json = previous customers collected by the team from its own evidence (survey forms, photos, videos ...), one row per customer (never a name):
//      { "source": "previous data collected after the upgrade", "rows": [ ["2026-09-17","14:35",1,15,"F",5], ... ] }       date, time, box 1-3, coins, F / C, minutes (optional)
//  The LocalServer draws those rows in its Daily / Weekly chart in a lighter shade with the legend "lighter = previous data collected by the team after the upgrade"; the file never changes
//  the machine log, the totals or what the Online Server gets. After a successful push this board keeps a copy (LittleFS) so the file can be loaded,
//  edited and pushed again; it can be unplugged any time, the LocalServer keeps the file in its own flash.
//
//  Board: any ESP32 / ESP32-S3, default settings (it needs the default file-system partition for the saved copy).
//  Libraries: ESP Async WebServer + Async TCP (as in the other sketches). The LocalServer's Wi-Fi name / password are below: keep them equal to
//  AP_SSID / AP_PASSWORD in ESP32_LOCALSRV.ino.
//  CAPTIVE PORTAL (2026-10-16): like the LocalServer, this board's own Wi-Fi redirects every address to the entry page (wildcard DNS + redirect), so a phone that joins it
//  opens the page by itself ("sign in to this network"). Some phones' sign-in window cannot pick a file or keep a draft: open the page in the normal browser then.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "entry_page.h"

static const char* AP_SSID = "JollyHelmet-Entry";          // this board's own Wi-Fi (for the phone / PC)
static const char* AP_PASS = "12345678";                   // change it here if you like (min 8 characters)
static const char* LS_SSID = "JollyHelmet-Local";                // the LocalServer's Wi-Fi
static const char* LS_PASS = "12345678";
static const int   LS_CHANNEL = 1;                         // the LocalServer's channel (ESPNOW_WIFI_CHANNEL)
// This board's OWN Wi-Fi must NOT use 192.168.4.x: the LocalServer's Wi-Fi is 192.168.4.1 and the board is also a station of it, so the same address on both
// sides made it talk to itself ("connection refused"). Its own network is 192.168.5.x.
static const IPAddress AP_IP(192, 168, 5, 1), AP_MASK(255, 255, 255, 0);
#define MAX_BYTES 65536                                    // the LocalServer accepts 64 KB at most
static const char* SAVED_PATH = "/saved.json";

static AsyncWebServer server(80);
static DNSServer dns;                                      // CAPTIVE PORTAL: every name a phone looks up (connectivitycheck..., captive.apple.com, anything) is answered with this board
static const char* PORTAL_URL = "http://192.168.5.1/";     // ... and every unknown address is sent here, so joining the Wi-Fi opens the entry page by itself

// ---- the upload from the browser (POST body) ----
static String   body;
static bool     bodyTooBig = false;
// ---- one job at a time, run from loop() (HTTP calls must not run inside the web server callbacks) ----
enum { JK_NONE = 0, JK_PUSH = 1, JK_PULL = 2 };
enum { JS_IDLE = 0, JS_WORKING = 1, JS_DONE = 2, JS_FAIL = 3 };
static volatile uint8_t jobKind = JK_NONE, jobState = JS_IDLE;
static String   jobMsg;
static String   pulled;                                    // the file read from the LocalServer (JK_PULL)
static String   pending;                                   // the file to send (JK_PUSH)

static bool staUp() { return WiFi.status() == WL_CONNECTED; }
static String lsUrl(const char* path) { return String("http://") + WiFi.gatewayIP().toString() + path; }

static bool savedInfo(size_t* bytes) {
    if (!LittleFS.exists(SAVED_PATH)) return false;
    File f = LittleFS.open(SAVED_PATH, "r"); if (!f) return false;
    *bytes = f.size(); f.close(); return true;
}

static void doPush() {
    if (!staUp()) { jobMsg = "Not connected to the LocalServer's Wi-Fi (" + String(LS_SSID) + "). Is the LocalServer on?"; jobState = JS_FAIL; return; }
    const String boundary = "----JollyHelmetHistory7e3a";
    String payload = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"history.json\"\r\nContent-Type: application/json\r\n\r\n" +
                     pending + "\r\n--" + boundary + "--\r\n";
    HTTPClient http;
    http.setTimeout(20000);
    if (!http.begin(lsUrl("/history/upload"))) { jobMsg = "Could not start the request."; jobState = JS_FAIL; return; }
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    int code = http.POST((uint8_t*)payload.c_str(), payload.length());
    String resp = (code > 0) ? http.getString() : String("");
    http.end();
    payload = "";
    if (code <= 0) { jobMsg = "The LocalServer did not answer (" + http.errorToString(code) + "). Wi-Fi: this board " + WiFi.localIP().toString() + ", LocalServer " + WiFi.gatewayIP().toString() + "."; jobState = JS_FAIL; return; }
    if (code != 200) { jobMsg = "The LocalServer answered HTTP " + String(code) + ". Does it have the 2026-10-14 firmware?"; jobState = JS_FAIL; return; }
    if (resp.indexOf("\"ok\":1") >= 0) {
        int a = resp.indexOf("\"rows\":"), b = resp.indexOf("\"from\":"), c = resp.indexOf("\"to\":");
        String rows = a >= 0 ? resp.substring(a + 7, resp.indexOf(',', a)) : "?";
        String from = b >= 0 ? resp.substring(b + 7, resp.indexOf(',', b)) : "?";
        String to   = c >= 0 ? resp.substring(c + 5, resp.indexOf('}', c)) : "?";
        File f = LittleFS.open(SAVED_PATH, "w");                                  // keep a copy of what the LocalServer accepted
        if (f) { f.print(pending); f.close(); }
        jobMsg = "The LocalServer accepted " + rows + " row(s), " + from + " to " + to + ". A copy is saved on this board.";
        jobState = JS_DONE;
    } else {
        int m = resp.indexOf("\"msg\":\"");
        String why = m >= 0 ? resp.substring(m + 7, resp.lastIndexOf('"')) : resp;
        jobMsg = "The LocalServer refused the file (nothing changed): " + why;
        jobState = JS_FAIL;
    }
}
static void doPull() {
    if (!staUp()) { jobMsg = "Not connected to the LocalServer's Wi-Fi (" + String(LS_SSID) + "). Is the LocalServer on?"; jobState = JS_FAIL; return; }
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(lsUrl("/history.json"))) { jobMsg = "Could not start the request."; jobState = JS_FAIL; return; }
    int code = http.GET();
    if (code != 200) { http.end(); jobMsg = code > 0 ? "The LocalServer answered HTTP " + String(code) + "." : "The LocalServer did not answer."; jobState = JS_FAIL; return; }
    pulled = http.getString();
    http.end();
    jobMsg = "Read " + String(pulled.length()) + " bytes from the LocalServer.";
    jobState = JS_DONE;
}

static void sendJson(AsyncWebServerRequest* r, const String& o, int code = 200) {
    AsyncWebServerResponse* res = r->beginResponse(code, "application/json", o);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}
static String jsonEsc(const String& s) { String o; for (unsigned i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n' || c == '\r') o += ' '; else o += c; } return o; }

// The page comes straight from flash (no copy in RAM) and the phone keeps it (a short "304 Not Modified" while this build is unchanged).
static void sendPage(AsyncWebServerRequest* r) {
    static const char ETAG[] = "\"" __DATE__ " " __TIME__ "\"";
    const AsyncWebHeader* h = r->getHeader("If-None-Match");
    if (h && h->value() == ETAG) { r->send(304); return; }
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", (const uint8_t*)ENTRY_HTML, strlen(ENTRY_HTML));
    res->addHeader("ETag", ETAG);
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[ENTRY] history file uploader starting");
    LittleFS.begin(true);
    body.reserve(1024);

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);                  // before softAP(): see above
    WiFi.softAP(AP_SSID, AP_PASS, LS_CHANNEL, 0, 4);
    WiFi.begin(LS_SSID, LS_PASS);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);                                 // captive portal DNS (answers only on this board's own Wi-Fi addresses)
    Serial.printf("[ENTRY] Wi-Fi '%s' -> http://%s  (joining '%s')\n", AP_SSID, WiFi.softAPIP().toString().c_str(), LS_SSID);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { sendPage(r); });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(204); });
    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
        size_t sb = 0; bool has = savedInfo(&sb);
        const char* st = jobState == JS_WORKING ? "working" : (jobState == JS_DONE ? "done" : (jobState == JS_FAIL ? "fail" : "idle"));
        String o = String("{\"sta\":") + (staUp() ? "true" : "false") + ",\"ssid\":\"" + LS_SSID + "\",\"saved\":{\"has\":" + (has ? "true" : "false") + ",\"bytes\":" + String((unsigned)sb) +
                   "},\"job\":{\"state\":\"" + st + "\",\"kind\":\"" + (jobKind == JK_PUSH ? "push" : "pull") + "\",\"msg\":\"" + jsonEsc(jobMsg) + "\"}}";
        sendJson(r, o);
    });
    server.on("/api/push", HTTP_POST,
        [](AsyncWebServerRequest* r) {                                                      // the whole body has arrived
            if (jobState == JS_WORKING) { sendJson(r, "{\"ok\":0,\"msg\":\"Busy with the previous request.\"}", 409); return; }
            if (bodyTooBig || body.length() == 0) { sendJson(r, "{\"ok\":0,\"msg\":\"The file is empty or bigger than 64 KB.\"}", 400); return; }
            pending = body; body = "";
            jobKind = JK_PUSH; jobState = JS_WORKING; jobMsg = "";
            sendJson(r, "{\"ok\":1}");
        },
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
            (void)r;
            if (index == 0) { body = ""; bodyTooBig = total > MAX_BYTES; if (!bodyTooBig) body.reserve(total + 1); }
            if (!bodyTooBig) body.concat((const char*)data, len);
        });
    server.on("/api/pull", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (jobState == JS_WORKING) { sendJson(r, "{\"ok\":0,\"msg\":\"Busy with the previous request.\"}", 409); return; }
        jobKind = JK_PULL; jobState = JS_WORKING; jobMsg = "";
        sendJson(r, "{\"ok\":1}");
    });
    server.on("/api/pulled", HTTP_GET, [](AsyncWebServerRequest* r) { AsyncWebServerResponse* res = r->beginResponse(200, "text/plain; charset=utf-8", pulled); res->addHeader("Cache-Control", "no-store"); r->send(res); });
    server.on("/saved.json", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!LittleFS.exists(SAVED_PATH)) { r->send(404, "text/plain", "No copy saved yet."); return; }
        r->send(LittleFS, SAVED_PATH, "application/json");
    });
    // Phones and PCs ask well-known addresses (/generate_204, /hotspot-detect.html, /connecttest.txt ...) to find out whether the Wi-Fi has internet. Anything but their
    // expected answer makes them show the "sign in to this network" screen, and a short redirect to the entry page is all it needs (same as the LocalServer's Wi-Fi).
    server.onNotFound([](AsyncWebServerRequest* r) { r->redirect(PORTAL_URL); });
    server.begin();
}

void loop() {
    static uint32_t lastTry = 0, lastLog = 0;
    uint32_t now = millis();
    dns.processNextRequest();
    if (!staUp() && now - lastTry > 6000UL) { lastTry = now; WiFi.begin(LS_SSID, LS_PASS); }       // keep trying to join the LocalServer's Wi-Fi
    if (jobState == JS_WORKING) {
        if (jobKind == JK_PUSH) { doPush(); pending = ""; }
        else if (jobKind == JK_PULL) doPull();
    }
    if (now - lastLog > 30000UL) { lastLog = now; Serial.printf("[ENTRY] LocalServer Wi-Fi: %s\n", staUp() ? "connected" : "not connected"); }
    delay(20);
}
