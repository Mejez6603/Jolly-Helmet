// =====================================================================
//  ESP32_ACS_CONFIG_BENCH - a stand-in for the LocalServer's refill-system page            (core 3.3.7)
//  Created 2026-10-03.
//
//  Flash this on a SPARE ESP32. Power ACS 1 and/or ACS 2 as usual (the real LocalServer OFF), connect a phone or PC to the Wi-Fi
//  "ACS_CONFIG" (password 12345678) and open  http://192.168.4.1 . Pick ACS 1 (Box 1) or ACS 2 (Box 3) at the top; everything on the
//  page then talks to that one.
//
//  What it does: shows what the refill system is doing (its step, both mixer containers' level / ultrasonic / water-level readings, the
//  running pump, the EMPTY verdicts and warnings); edits the saved LEVELS / MIX / TIMING settings (CMD_SET_CONFIG subIndex 1 / 3 / 2, see
//  ESP32_ACS1.ino) with "use live" buttons so a line is set by filling the container to it and tapping; switches Maintenance mode
//  with the six pumps by hand; and can pretend to be the box (ask for alcohol / scented) so the delivery can be tried without a box.
//  The ACS stores everything itself, so nothing is lost when this board is switched off.
//
//  It sends NO heartbeat and plays no box, so it can stay on next to the real machine.
// =====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "config_page.h"   // const char PAGE[] - the web UI (kept out of the .ino, see the note in that file)

const char* AP_SSID     = "ACS_CONFIG";
const char* AP_PASSWORD = "12345678";

WebServer server(80);

// ---- connection diagnostics: tells a REBOOT (power dip) apart from a Wi-Fi DROP (the phone leaving) ----
const wifi_power_t BENCH_TX_POWER = WIFI_POWER_11dBm;
volatile uint16_t apJoins = 0, apLeaves = 0, apLastLeaveReason = 0;
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        apJoins = apJoins + 1;
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        apLeaves = apLeaves + 1;
        apLastLeaveReason = info.wifi_ap_stadisconnected.reason;
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

// ---- what the radio heard (written by the receive callback, read by the web handler; guarded by mux). Index 0 = ACS 1, 1 = ACS 2 ----
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
ACSStatusPacket rS[2];
bool     seen[2]   = { false, false };
uint32_t seenMs[2] = { 0, 0 };
char     levelsCsv[2][200] = { "", "" };
char     timingCsv[2][200] = { "", "" };
char     mixCsv[2][200]    = { "", "" };
uint8_t  unit = 1;                     // 1 = ACS 1, 2 = ACS 2 (what the page is talking to)

uint8_t unitId()  { return unit == 1 ? DEVICE_ACS : DEVICE_ACS2; }

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void copyCsv(char* dst, size_t n, const char* src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData) return;
    if (len == (int)sizeof(ACSStatusPacket)) {
        ACSStatusPacket p;
        memcpy(&p, incomingData, sizeof(ACSStatusPacket));
        int u = (p.deviceID == DEVICE_ACS) ? 0 : ((p.deviceID == DEVICE_ACS2) ? 1 : -1);
        if (u < 0) return;
        portENTER_CRITICAL(&mux);
        rS[u] = p; seen[u] = true; seenMs[u] = millis();
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(CommandPacket)) {
        CommandPacket c;
        memcpy(&c, incomingData, sizeof(CommandPacket));
        // each ACS re-broadcasts its settings as CMD_SET_CONFIG (target 0): state 4 = ACS 1, 5 = ACS 2; subIndex 1 LEVELS, 2 TIMING, 3 MIX
        if (c.commandID != CMD_SET_CONFIG || c.targetDeviceID != 0 || (c.state != 4 && c.state != 5)) return;
        int u = (c.state == 4) ? 0 : 1;
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&mux);
        if (c.subIndex == 1) copyCsv(levelsCsv[u], sizeof(levelsCsv[u]), c.payloadStr);
        else if (c.subIndex == 2) copyCsv(timingCsv[u], sizeof(timingCsv[u]), c.payloadStr);
        else if (c.subIndex == 3) copyCsv(mixCsv[u], sizeof(mixCsv[u]), c.payloadStr);
        portEXIT_CRITICAL(&mux);
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

// only digits, commas, dots and minus signs may go out as settings - nothing else is ever a valid number list
bool csvLooksSane(const String& s) {
    if (s.length() == 0 || s.length() > 190) return false;
    for (unsigned i = 0; i < s.length(); i++) {
        char ch = s[i];
        if (!((ch >= '0' && ch <= '9') || ch == ',' || ch == '.' || ch == '-')) return false;
    }
    return true;
}

void sendState() {
    ACSStatusPacket p;
    bool ok; uint32_t age = 0;
    char levels[200], timing[200], mix[200];
    uint32_t now = millis();
    int u = unit - 1;
    portENTER_CRITICAL(&mux);
    p = rS[u];
    ok = seen[u] && (now - seenMs[u]) < 5000;
    age = seen[u] ? now - seenMs[u] : 0;
    copyCsv(levels, sizeof(levels), levelsCsv[u]);
    copyCsv(timing, sizeof(timing), timingCsv[u]);
    copyCsv(mix, sizeof(mix), mixCsv[u]);
    portEXIT_CRITICAL(&mux);

    char buf[1100];
    snprintf(buf, sizeof(buf),
             "{\"unit\":%u,\"ok\":%d,\"age\":%lu,\"step\":%u,\"sec\":%u,\"pumps\":%u,\"flags\":%u,\"empty\":%u,\"warn\":%u,"
             "\"usA\":%.1f,\"usS\":%.1f,\"wlA\":%u,\"wlS\":%u,\"pctA\":%u,\"pctS\":%u,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"levels\":\"%s\",\"timing\":\"%s\",\"mix\":\"%s\"}",
             (unsigned)unit, ok ? 1 : 0, (unsigned long)age, (unsigned)p.step, (unsigned)p.stepSec, (unsigned)p.pumpMask, (unsigned)p.flags,
             (unsigned)p.empty, (unsigned)p.warn,
             (double)p.usAlcCm, (double)p.usSctCm, (unsigned)p.wlAlcRaw, (unsigned)p.wlSctRaw, (unsigned)p.levelAlcPct, (unsigned)p.levelSctPct,
             (unsigned long)(millis() / 1000UL), resetReasonName(), (int)WiFi.softAPgetStationNum(), (unsigned)apJoins, (unsigned)apLeaves, (unsigned)apLastLeaveReason,
             levels, timing, mix);
    server.send(200, "application/json", buf);
}

void handleSend() {
    String kind = server.arg("kind");
    String csv  = server.arg("csv");
    if ((kind == "levels" || kind == "timing" || kind == "mix") && csvLooksSane(csv)) {
        sendCmd(unitId(), CMD_SET_CONFIG, kind == "levels" ? 1 : (kind == "timing" ? 2 : 3), 0, 0, csv.c_str());
        Serial.printf("[CFG] ACS %u: sent %s: %s\n", (unsigned)unit, kind.c_str(), csv.c_str());
    } else {
        Serial.println("[CFG] refused: not a plain list of numbers");
    }
    sendState();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== ESP32_ACS_CONFIG_BENCH (settings page for ACS 1 / ACS 2) ===");

    Serial.printf("[CFG] Last reset: %s\n", resetReasonName());
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);
    WiFi.setTxPower(BENCH_TX_POWER);
    Serial.printf("[CFG] AP '%s' / '%s' -> http://%s\n", AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[CFG] ESP-NOW init failed - restarting in 3 s");
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
    server.on("/state", HTTP_GET, sendState);
    server.on("/unit", HTTP_GET, []() {
        unit = (server.arg("v") == "2") ? 2 : 1;
        sendState();
    });
    server.on("/send", HTTP_GET, handleSend);
    server.on("/maint", HTTP_GET, []() {
        sendCmd(unitId(), CMD_SET_MAINTENANCE, 0, 0, server.arg("on") == "1" ? 1 : 0);
        sendState();
    });
    server.on("/retry", HTTP_GET, []() {           // forget the EMPTY verdicts and try the supplies again
        sendCmd(unitId(), CMD_SET_MAINTENANCE, 0, 0, 2);
        sendState();
    });
    server.on("/mixok", HTTP_GET, []() {           // accept both mixer containers as they are (clears the half-made-mix flags)
        sendCmd(unitId(), CMD_SET_MAINTENANCE, 0, 0, 3);
        sendState();
    });
    server.on("/pump", HTTP_GET, []() {
        int i = server.arg("i").toInt();
        if (i >= 0 && i < ACS_PUMP_COUNT) sendCmd(unitId(), CMD_SET_RELAY, (uint8_t)i, 0, server.arg("on") == "1" ? 1 : 0);
        sendState();
    });
    server.on("/req", HTTP_GET, []() {             // pretend to be the box: k 0 = alcohol, 1 = scented; s 0 = please top up, 1 = stop
        int k = server.arg("k").toInt();
        if (k == 0 || k == 1) sendCmd(unitId(), CMD_REQUEST_DELIVERY, (uint8_t)k, 0, server.arg("s") == "1" ? 1 : 0);
        sendState();
    });
    server.begin();
    Serial.println("[CFG] Web UI ready.");
}

void loop() {
    server.handleClient();
    delay(2);
}
