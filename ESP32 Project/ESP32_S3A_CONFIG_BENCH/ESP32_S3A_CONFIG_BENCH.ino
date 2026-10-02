// =====================================================================
//  ESP32_S3A_CONFIG_BENCH - a stand-in for the LocalServer's settings page            (core 3.3.7)
//  Created 2026-10-02.
//
//  Flash this on a SPARE ESP32. Power the box as usual (S3A, A1, A2, A3 online, the real LocalServer OFF),
//  connect a phone or PC to the Wi-Fi "S3A_CONFIG" (password 12345678) and open  http://192.168.4.1
//  so the settings can be set standing at the unit instead of with the USB cable.
//
//  What it does: shows what the S3A is doing (its step and timer), A2's live readings (the two container
//  levels, the helmet distance, the doors, the two water sensors' raw numbers) and A3's relays; edits the
//  S3A's LEVELS and RULES settings (CMD_SET_CONFIG subIndex 2 / 1, see ESP32_S3A.ino's header) with "use live
//  reading" buttons so a container line is set by filling to the line and tapping, not by typing a guess; and
//  switches Maintenance mode with hand-set relays. The S3A stores everything itself, so nothing is lost when
//  this board is switched off.
//
//  It sends NO heartbeat and plays no S3A, so it can stay on next to the real box.
// =====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "config_page.h"   // const char PAGE[] - the web UI (kept out of the .ino, see the note in that file)

const char* AP_SSID     = "S3A_CONFIG";
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
bool     s3aSeen = false;  uint32_t s3aMs = 0;  uint8_t s3aState = 0;  uint32_t s3aTimer = 0;
A2SensorPacket rA2;        bool a2Seen = false; uint32_t a2Ms = 0;
bool     a3Seen = false;   uint32_t a3Ms = 0;   uint16_t a3Mask = 0;   bool a3Orph = false;
bool     a1Seen = false;   uint32_t a1Ms = 0;   uint32_t a1Pulses = 0;  uint32_t a1PulseChangeMs = 0;  bool a1PulseEver = false;   // A1's coin count
uint32_t a1RawEdges = 0;   float a1MinGapMs = 0.0f;   // A1's coin-wire diagnostic: raw edges before filtering, shortest gap between two
char     rulesCsv[200]  = "";
char     levelsCsv[200] = "";
char     phasesCsv[200] = "";

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
        // The S3A re-broadcasts its settings as CMD_SET_CONFIG to everyone (target 0): subIndex 1 = RULES, 2 = LEVELS.
        if (c.commandID != CMD_SET_CONFIG || c.targetDeviceID != 0) return;
        c.payloadStr[sizeof(c.payloadStr) - 1] = '\0';
        portENTER_CRITICAL(&mux);
        if (c.subIndex == 1) copyCsv(rulesCsv, sizeof(rulesCsv), c.payloadStr);
        else if (c.subIndex == 2) copyCsv(levelsCsv, sizeof(levelsCsv), c.payloadStr);
        else if (c.subIndex == 3) copyCsv(phasesCsv, sizeof(phasesCsv), c.payloadStr);
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket b;
        memcpy(&b, incomingData, sizeof(BoxStatusPacket));
        if (b.deviceID != DEVICE_S3A) return;            // Box 2's S3C sends the same packet
        portENTER_CRITICAL(&mux);
        s3aSeen = true; s3aMs = millis(); s3aState = b.machineState; s3aTimer = b.activeTimer;
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, incomingData, sizeof(A2SensorPacket));
        if (p.deviceID != DEVICE_A2) return;
        portENTER_CRITICAL(&mux);
        rA2 = p; a2Seen = true; a2Ms = millis();
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket p;
        memcpy(&p, incomingData, sizeof(A3StatusPacket));
        if (p.deviceID != DEVICE_A3) return;
        portENTER_CRITICAL(&mux);
        a3Seen = true; a3Ms = millis(); a3Mask = p.relayMask; a3Orph = p.orphaned;
        portEXIT_CRITICAL(&mux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        // A1 reports its coin-pulse count about once a second (the old relay A2 sends the same size with deviceID 2)
        TelemetryPacket t;
        memcpy(&t, incomingData, sizeof(TelemetryPacket));
        if (t.deviceID != DEVICE_A1) return;
        portENTER_CRITICAL(&mux);
        if (a1Seen && t.pulseCount != a1Pulses) { a1PulseChangeMs = millis(); a1PulseEver = true; }
        a1Seen = true; a1Ms = millis(); a1Pulses = t.pulseCount;
        a1RawEdges = t.activeTimer; a1MinGapMs = t.usAlcoholDistance;
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
    bool s3a, a2, a3, orph; uint32_t s3aAge = 0, a2Age = 0; uint8_t state; uint32_t timer; uint16_t mask;
    A2SensorPacket p;
    char rules[200], levels[200], phases[200];
    uint32_t now = millis();
    portENTER_CRITICAL(&mux);
    s3a = s3aSeen; s3aAge = s3a ? now - s3aMs : 0; state = s3aState; timer = s3aTimer;
    a2 = a2Seen;   a2Age = a2 ? now - a2Ms : 0;   p = rA2;
    a3 = a3Seen && (now - a3Ms) < 5000; mask = a3Mask; orph = a3Orph;
    bool a1 = a1Seen && (now - a1Ms) < 5000; uint32_t coins = a1Pulses; uint32_t rawEdges = a1RawEdges; float minGap = a1MinGapMs;
    long coinAge = a1PulseEver ? (long)(now - a1PulseChangeMs) : -1L;
    copyCsv(rules, sizeof(rules), rulesCsv);
    copyCsv(levels, sizeof(levels), levelsCsv);
    copyCsv(phases, sizeof(phases), phasesCsv);
    portEXIT_CRITICAL(&mux);

    bool s3aOk = s3a && s3aAge < 5000;
    bool a2Ok  = a2 && a2Age < 5000;
    char buf[1500];
    snprintf(buf, sizeof(buf),
             "{\"s3aok\":%d,\"s3aage\":%lu,\"state\":%u,\"timer\":%lu,"
             "\"a2ok\":%d,\"a2age\":%lu,\"helm\":%.1f,\"als\":%.1f,\"sls\":%.1f,\"door\":%d,\"raw0\":%u,\"raw1\":%u,"
             "\"a3ok\":%d,\"a3mask\":%u,\"a3orph\":%d,\"a1ok\":%d,\"coins\":%lu,\"coinage\":%ld,\"raw\":%lu,\"gap\":%.1f,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u,"
             "\"rules\":\"%s\",\"levels\":\"%s\",\"phases\":\"%s\"}",
             s3aOk ? 1 : 0, (unsigned long)s3aAge, (unsigned)state, (unsigned long)timer,
             a2Ok ? 1 : 0, (unsigned long)a2Age, (double)p.usHelmetCm, (double)p.usMcAlsCm, (double)p.usMcSlsCm, p.doorEnclosure ? 1 : 0,
             (unsigned)p.wsAlsRaw, (unsigned)p.wsSlsRaw,
             a3 ? 1 : 0, (unsigned)mask, orph ? 1 : 0, a1 ? 1 : 0, (unsigned long)coins, coinAge, (unsigned long)rawEdges, (double)minGap,
             (unsigned long)(millis() / 1000UL), resetReasonName(), (int)WiFi.softAPgetStationNum(), (unsigned)apJoins, (unsigned)apLeaves, (unsigned)apLastLeaveReason,
             rules, levels, phases);
    server.send(200, "application/json", buf);
}

void handleSend() {
    String kind = server.arg("kind");
    String csv  = server.arg("csv");
    if ((kind == "rules" || kind == "levels" || kind == "phases") && csvLooksSane(csv)) {
        sendCmd(DEVICE_S3A, CMD_SET_CONFIG, kind == "rules" ? 1 : (kind == "levels" ? 2 : 3), 0, 0, csv.c_str());
        Serial.printf("[CFG] sent %s: %s\n", kind.c_str(), csv.c_str());
    } else {
        Serial.println("[CFG] refused: not a plain list of numbers");
    }
    sendState();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== ESP32_S3A_CONFIG_BENCH (settings page for the S3A) ===");

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
        sendCmd(DEVICE_S3A, CMD_SET_MAINTENANCE, 0, 0, server.arg("on") == "1" ? 1 : 0);
        sendState();
    });
    server.on("/mask", HTTP_GET, []() {
        sendCmd(DEVICE_S3A, CMD_SET_RELAY_MASK, 0, (uint16_t)(server.arg("v").toInt() & A3_MASK_ALL), 0);
        sendState();
    });
    server.begin();
    Serial.println("[CFG] Web UI ready.");
}

void loop() {
    server.handleClient();
    delay(2);
}
