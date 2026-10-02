// =====================================================================
//  ESP32_A1_WIDGET_TEST - plays the S3A for Node A1, to try the new screen widgets      (core 3.3.7)
//  Created 2026-10-02.
//
//  Flash this on a SPARE ESP32 (the one the A3 bench tool used will do). Power A1 next to it, connect a phone
//  or PC to the Wi-Fi "A1_TEST" (password 12345678) and open  http://192.168.4.1
//  Keep the real S3A, LocalServer and every other node OFF while testing - they would talk over this.
//
//  What it does: sends A1 any of the 46 screens in a1_screens.h (the 30 NEW ones from the Box 1 flow, two
//  variants, and the OLD screens the box runs today), plays the S3A's once-a-second update (heartbeat, the
//  countdown, {MINCOINS}/{REQUIREDCOIN}), pushes the three status lines and the {TRY}/{TRIES}/{MAXCOINS}
//  numbers, and lists every touch action A1 sends back - including the rating / seconds it attaches.
//
//  SUGGESTED CHECKLIST
//   1. OLD screens first (regression): send OLD TAPS, OLD INSERT_COIN, OLD CLEANING ... - they must look
//      exactly as they do on the box today. Tapping a button must list its action (value 0).
//   2. NEW RATE YOUR HELMET A: tap boxes - the tapped box turns green, the number stays; press CONFIRM: the
//      log shows ACTION with value = the picked rating (value 0 if nothing was picked).
//   3. NEW TIME ALLOT: type digits (MM:SS fills from the right, "N coin(s) needed" follows, DEL deletes,
//      a 5th digit rolls the first one off), press OK: log shows NP_OK with value = seconds. BACK and CANCEL
//      list NP_BACK / NP_CANCEL. "NEW TIME ALLOT (suggest 10:00)" opens on 10:00; the first digit typed
//      replaces it, DEL edits it.
//   4. NEW CLEANING ALC: set Time left 300, press Count down: {MINUTE}:{SECOND} counts down and the white bar
//      fills left to right without flicker. Send the status lines: the newest shows under the bar.
//      "(bar total 300 s)" does the same with the total given on the screen.
//   5. NEW REFILLING A: send {TRY}/{TRIES} and watch "Try 3/10" change.
//   6. Stop the Heartbeat (power this board off for 15 s): A1 must show its own blinking OUT OF ORDER and
//      recover by itself when this board is back.
// =====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "a1_screens.h"     // TEST_SCREENS[] - generated, see that file
#include "a1test_page.h"    // const char PAGE[] - the web UI

const char* AP_SSID     = "A1_TEST";
const char* AP_PASSWORD = "12345678";

WebServer server(80);

// ---- connection diagnostics: tells a REBOOT (power dip) apart from a Wi-Fi DROP (the phone leaving) ----
const wifi_power_t BENCH_TX_POWER = WIFI_POWER_11dBm;   // lower than the default 19.5 dBm: smaller current spikes, plenty at bench distance
volatile uint16_t apJoins = 0, apLeaves = 0, apLastLeaveReason = 0;
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        apJoins = apJoins + 1;
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        apLeaves = apLeaves + 1;
        apLastLeaveReason = info.wifi_ap_stadisconnected.reason;
        Serial.printf("[WIFI] a phone left the access point (reason %u)\n", (unsigned)apLastLeaveReason);
    }
}
const char* resetReasonName() {
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

// ---- what this tool is pretending to be ----
int      selScreen = 0;
uint16_t timerSec  = 300;
bool     running   = false;     // true = the countdown ticks down once a second
uint8_t  minCoins  = 15;
uint8_t  reqCoins  = 15;
uint16_t tokTry = 3, tokTries = 10, tokMax = 60;
uint32_t lastTickMs = 0;

// ---- what A1 told us (written by the receive callback, read by the web handler) ----
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t a1Count = 0, a1LastMs = 0, a1Pulses = 0;
char     logLines[10][80];
uint8_t  logNext = 0, logCount = 0;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData) return;
    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket c;
        memcpy(&c, incomingData, sizeof(CommandPacket));
        if (c.commandID != CMD_TOUCH_ACTION || c.targetDeviceID != DEVICE_S3A) return;
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&rxMux);
        snprintf(logLines[logNext], sizeof(logLines[0]), "%lus  %.40s  value=%u", (unsigned long)(millis() / 1000), c.payloadStr, (unsigned)c.param16);
        logNext = (uint8_t)((logNext + 1) % 10);
        if (logCount < 10) logCount++;
        portEXIT_CRITICAL(&rxMux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, incomingData, sizeof(TelemetryPacket));
        if (t.deviceID != DEVICE_A1) return;
        portENTER_CRITICAL(&rxMux);
        a1Pulses = t.pulseCount;
        a1LastMs = millis();
        a1Count  = a1Count + 1;
        portEXIT_CRITICAL(&rxMux);
    }
}

void sendCmd(uint8_t target, uint8_t opcode, uint8_t sub, uint16_t p16, uint8_t st, const char* str = nullptr) {
    CommandPacket c;
    memset(&c, 0, sizeof(c));
    c.targetDeviceID = target;
    c.commandID      = opcode;
    c.subIndex       = sub;
    c.param16        = p16;
    c.state          = st;
    if (str) {
        strncpy(c.payloadStr, str, sizeof(c.payloadStr) - 1);
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&c, sizeof(CommandPacket));
}

void sendScreen(int idx, bool hard) {
    if (idx < 0 || idx >= TEST_SCREEN_COUNT) return;
    selScreen = idx;
    sendCmd(DEVICE_A1, hard ? CMD_TFT_HARD_REFRESH : CMD_STEP_RENDER, 0, 0, TEST_SCREENS[idx].tapAnywhere, TEST_SCREENS[idx].payload);
    Serial.printf("[TEST] sent screen %d '%s' (%u chars)%s\n", idx + 1, TEST_SCREENS[idx].name, (unsigned)strlen(TEST_SCREENS[idx].payload), hard ? " with hard refresh" : "");
}

// What the real S3A broadcasts to A1 every second: the heartbeat and the synced numbers.
void sendTick() {
    sendCmd(0, CMD_HEARTBEAT, DEVICE_S3A, 0, 0);
    delay(2);
    sendCmd(DEVICE_A1, CMD_SYNC_VARS, minCoins, timerSec, reqCoins);
}

void sendTokens() {
    sendCmd(DEVICE_A1, CMD_SET_TOKEN, TOKEN_TRY, tokTry, 0);      delay(2);
    sendCmd(DEVICE_A1, CMD_SET_TOKEN, TOKEN_TRIES, tokTries, 0);  delay(2);
    sendCmd(DEVICE_A1, CMD_SET_TOKEN, TOKEN_MAXCOINS, tokMax, 0);
}

void sendStatusLine(uint8_t line, const String& text) {
    sendCmd(DEVICE_A1, CMD_STATUS_LINE, line, 0, 0, text.c_str());
    delay(2);
}

void sendState() {
    uint32_t cnt, last, pulses; uint8_t n, next;
    char lines[10][80];
    portENTER_CRITICAL(&rxMux);
    cnt = a1Count; last = a1LastMs; pulses = a1Pulses; n = logCount; next = logNext;
    for (uint8_t i = 0; i < n; i++) {   // newest first
        uint8_t idx = (uint8_t)((next + 10 - 1 - i) % 10);
        memcpy(lines[i], logLines[idx], sizeof(lines[0]));
    }
    portEXIT_CRITICAL(&rxMux);

    String json = "{\"sel\":" + String(selScreen) + ",\"timer\":" + String(timerSec) + ",\"run\":" + String(running ? 1 : 0) +
                  ",\"coins\":" + String(pulses) + ",\"a1ok\":" + String(cnt ? 1 : 0) +
                  ",\"a1age\":" + String(cnt ? (millis() - last) : 0) + ",\"log\":[";
    for (uint8_t i = 0; i < n; i++) {
        if (i) json += ",";
        json += "\"";
        for (const char* p = lines[i]; *p; p++) {
            if (*p == '"' || *p == '\\') json += '\\';
            if ((uint8_t)*p >= 32) json += *p;
        }
        json += "\"";
    }
    json += "],\"up\":" + String((unsigned long)(millis() / 1000UL)) + ",\"rst\":\"" + String(resetReasonName()) + "\",\"clients\":" + String((int)WiFi.softAPgetStationNum()) +
            ",\"joins\":" + String((unsigned)apJoins) + ",\"leaves\":" + String((unsigned)apLeaves) + ",\"why\":" + String((unsigned)apLastLeaveReason) + "}";
    server.send(200, "application/json", json);
}

void handleScreens() {
    String json = "[";
    for (int i = 0; i < TEST_SCREEN_COUNT; i++) {
        if (i) json += ",";
        json += "\"";
        json += TEST_SCREENS[i].name;
        json += "\"";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handlePayload() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= TEST_SCREEN_COUNT) { server.send(404, "text/plain", "no such screen"); return; }
    String t = String(TEST_SCREENS[i].name) + "  (" + String((unsigned)strlen(TEST_SCREENS[i].payload)) + " of 199 characters" +
               (TEST_SCREENS[i].tapAnywhere ? ", tap-anywhere screen" : "") + ")\n" + TEST_SCREENS[i].payload;
    server.send(200, "text/plain", t);
}

void handleDo() {
    if (server.hasArg("send")) sendScreen(server.arg("send").toInt(), false);
    if (server.hasArg("hard")) sendScreen(server.arg("hard").toInt(), true);
    if (server.hasArg("timer")) { timerSec = (uint16_t)constrain(server.arg("timer").toInt(), 0, 5999); sendTick(); }
    if (server.hasArg("run"))   running = server.arg("run") == "1";
    if (server.hasArg("minc") || server.hasArg("reqc")) {
        if (server.hasArg("minc")) minCoins = (uint8_t)constrain(server.arg("minc").toInt(), 0, 255);
        if (server.hasArg("reqc")) reqCoins = (uint8_t)constrain(server.arg("reqc").toInt(), 0, 255);
        sendTick();
    }
    if (server.hasArg("try") || server.hasArg("tries") || server.hasArg("maxc")) {
        if (server.hasArg("try"))   tokTry   = (uint16_t)constrain(server.arg("try").toInt(), 0, 9999);
        if (server.hasArg("tries")) tokTries = (uint16_t)constrain(server.arg("tries").toInt(), 0, 9999);
        if (server.hasArg("maxc"))  tokMax   = (uint16_t)constrain(server.arg("maxc").toInt(), 0, 9999);
        sendTokens();
    }
    if (server.hasArg("st0")) sendStatusLine(0, server.arg("st0"));
    if (server.hasArg("st1")) sendStatusLine(1, server.arg("st1"));
    if (server.hasArg("st2")) sendStatusLine(2, server.arg("st2"));
    if (server.hasArg("resetcoins")) sendCmd(DEVICE_A1, CMD_RESET_COINS, 0, 0, 0);
    if (server.hasArg("beep"))       sendCmd(DEVICE_A1, CMD_BUZZER, 0, 200, 0);
    sendState();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== ESP32_A1_WIDGET_TEST (plays the S3A for Node A1) ===");

    Serial.printf("[TEST] Last reset: %s\n", resetReasonName());
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);
    WiFi.setTxPower(BENCH_TX_POWER);
    Serial.printf("[TEST] AP '%s' / '%s' -> http://%s  (%d screens)\n", AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str(), TEST_SCREEN_COUNT);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[TEST] ESP-NOW init failed - restarting in 3 s");
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

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", PAGE); });
    server.on("/screens", HTTP_GET, handleScreens);
    server.on("/payload", HTTP_GET, handlePayload);
    server.on("/state", HTTP_GET, sendState);
    server.on("/do", HTTP_GET, handleDo);
    server.begin();
    Serial.println("[TEST] Web UI ready.");
}

void loop() {
    server.handleClient();
    uint32_t now = millis();
    if (now - lastTickMs >= 1000) {
        lastTickMs = now;
        if (running && timerSec > 0) timerSec--;
        sendTick();
    }
    delay(2);
}
