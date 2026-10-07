// =====================================================================
//  ESP32_S3C_CONFIG_BENCH - a stand-in for the LocalServer's Box 2 settings page          (core 3.3.7)
//  Created 2026-10-03. The Box 2 twin of ESP32_S3A_CONFIG_BENCH (Box 1).
//
//  Flash this on a SPARE ESP32. Power Box 2 as usual (S3C, C1, C2 online, the real LocalServer OFF),
//  connect a phone or PC to the Wi-Fi "S3C_CONFIG" (password 12345678) and open  http://192.168.4.1
//  so the settings can be set standing at the unit instead of with the USB cable.
//
//  What it does: shows what the S3C is doing (its step and timer), C2's live readings (the helmet distance, the
//  doors, the six relays) and C1's coin counts; edits the S3C's RULES and HEAT settings (CMD_SET_CONFIG subIndex 1 / 2,
//  see ESP32_S3C.ino's header) with a "use live reading" button for the helmet distance; and switches Maintenance mode
//  with hand-set relays. The S3C stores everything itself, so nothing is lost when this board is switched off.
//
//  It sends NO heartbeat and plays no S3C, so it can stay on next to the real box. Box 1's own bench
//  (ESP32_S3A_CONFIG_BENCH, Wi-Fi "S3A_CONFIG") ignores Box 2's lists and this one ignores Box 1's, so both boxes can run at once.
// =====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "config_page.h"   // const char PAGE[] - the web UI (kept out of the .ino, see the note in that file)

const char* AP_SSID     = "S3C_CONFIG";
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

// ---- what the radio heard (written by the receive callback, read by the web handler; guarded by mux) ----
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool     s3cSeen = false;  uint32_t s3cMs = 0;  uint8_t s3cState = 0;  uint32_t s3cTimer = 0;
TelemetryPacket rC2;       bool c2Seen = false; uint32_t c2Ms = 0;
bool     c1Seen = false;   uint32_t c1Ms = 0;   uint32_t c1Pulses = 0;  uint32_t c1PulseChangeMs = 0;  bool c1PulseEver = false;   // C1's coin count
uint32_t c1RawEdges = 0;   float c1MinGapMs = 0.0f;   // C1's coin-wire diagnostic: raw edges before filtering, shortest gap between two
char     rulesCsv[200] = "";
char     heatCsv[200]  = "";

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void copyCsv(char* dst, size_t n, const char* src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData) return;

    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket c;
        memcpy(&c, incomingData, sizeof(CommandPacket));
        // The S3C re-broadcasts its settings as CMD_SET_CONFIG to everyone (target 0, state 2): subIndex 1 = RULES, 2 = HEAT.
        if (c.commandID != CMD_SET_CONFIG || c.targetDeviceID != 0 || c.state != 2) return;   // state 0 = the same lists from Box 1's S3A
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&mux);
        if (c.subIndex == 1) copyCsv(rulesCsv, sizeof(rulesCsv), c.payloadStr);
        else if (c.subIndex == 2) copyCsv(heatCsv, sizeof(heatCsv), c.payloadStr);
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, incomingData, sizeof(BoxStatusPacket));
        if (b.deviceID != DEVICE_S3C) return;            // Box 1's S3A sends the same packet
        portENTER_CRITICAL(&mux);
        s3cSeen = true; s3cMs = millis(); s3cState = b.machineState; s3cTimer = b.activeTimer;
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, incomingData, sizeof(TelemetryPacket));
        if (t.deviceID == DEVICE_C2) {
            portENTER_CRITICAL(&mux);
            rC2 = t; c2Seen = true; c2Ms = millis();
            portEXIT_CRITICAL(&mux);
        } else if (t.deviceID == DEVICE_C1) {
            // C1 reports its coin-pulse count about once a second
            portENTER_CRITICAL(&mux);
            if (c1Seen && t.pulseCount != c1Pulses) { c1PulseChangeMs = millis(); c1PulseEver = true; }
            c1Seen = true; c1Ms = millis(); c1Pulses = t.pulseCount;
            c1RawEdges = t.activeTimer; c1MinGapMs = t.usAlcoholDistance;
            portEXIT_CRITICAL(&mux);
        }
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
    bool s3c, c2, c1; uint32_t s3cAge = 0, c2Age = 0; uint8_t state; uint32_t timer;
    TelemetryPacket p;
    char rules[200], heat[200];
    uint32_t now = millis();
    portENTER_CRITICAL(&mux);
    s3c = s3cSeen; s3cAge = s3c ? now - s3cMs : 0; state = s3cState; timer = s3cTimer;
    c2 = c2Seen;   c2Age = c2 ? now - c2Ms : 0;   p = rC2;
    c1 = c1Seen && (now - c1Ms) < 5000; uint32_t coins = c1Pulses; uint32_t rawEdges = c1RawEdges; float minGap = c1MinGapMs;
    long coinAge = c1PulseEver ? (long)(now - c1PulseChangeMs) : -1L;
    copyCsv(rules, sizeof(rules), rulesCsv);
    copyCsv(heat, sizeof(heat), heatCsv);
    portEXIT_CRITICAL(&mux);

    bool s3cOk = s3c && s3cAge < 5000;
    bool c2Ok  = c2 && c2Age < 5000;
    uint16_t relayMask = 0;
    for (int i = 0; i < 6; i++) if (p.relayStates[i]) relayMask |= (uint16_t)(1u << i);
    char buf[1300];
    snprintf(buf, sizeof(buf),
             "{\"s3cok\":%d,\"s3cage\":%lu,\"state\":%u,\"timer\":%lu,"
             "\"c2ok\":%d,\"c2age\":%lu,\"helm\":%.1f,\"door\":%d,\"pan\":%d,\"bak\":%d,\"mask\":%u,"
             "\"c1ok\":%d,\"coins\":%lu,\"coinage\":%ld,\"raw\":%lu,\"gap\":%.1f,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"rules\":\"%s\",\"heat\":\"%s\"}",
             s3cOk ? 1 : 0, (unsigned long)s3cAge, (unsigned)state, (unsigned long)timer,
             c2Ok ? 1 : 0, (unsigned long)c2Age, (double)p.usHelmetDistance, p.doorEnclosure ? 1 : 0, p.doorPanel ? 1 : 0, p.doorBackdoor ? 1 : 0, (unsigned)relayMask,
             c1 ? 1 : 0, (unsigned long)coins, coinAge, (unsigned long)rawEdges, (double)minGap,
             (unsigned long)(millis() / 1000UL), resetReasonName(), (int)WiFi.softAPgetStationNum(), (unsigned)apJoins, (unsigned)apLeaves, (unsigned)apLastLeaveReason,
             rules, heat);
    server.send(200, "application/json", buf);
}

void handleSend() {
    String kind = server.arg("kind");
    String csv  = server.arg("csv");
    if ((kind == "rules" || kind == "heat") && csvLooksSane(csv)) {
        sendCmd(DEVICE_S3C, CMD_SET_CONFIG, kind == "rules" ? 1 : 2, 0, 0, csv.c_str());
        Serial.printf("[CFG] sent %s: %s\n", kind.c_str(), csv.c_str());
    } else {
        Serial.println("[CFG] refused: not a plain list of numbers");
    }
    sendState();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== ESP32_S3C_CONFIG_BENCH (settings page for Box 2's S3C) ===");

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
    server.on("/send", HTTP_GET, handleSend);
    server.on("/maint", HTTP_GET, []() {
        sendCmd(DEVICE_S3C, CMD_SET_MAINTENANCE, 0, 0, server.arg("on") == "1" ? 1 : 0);
        sendState();
    });
    server.on("/mask", HTTP_GET, []() {
        sendCmd(DEVICE_S3C, CMD_SET_RELAY_MASK, 0, (uint16_t)(server.arg("v").toInt() & C2_MASK_ALL), 0);
        sendState();
    });
    server.begin();
    Serial.println("[CFG] Web UI ready.");
}

void loop() {
    server.handleClient();
    delay(2);
}
