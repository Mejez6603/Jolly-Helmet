// =====================================================================
//  LOCAL SERVER - the technician's CONTROL CENTER                         (ESP32, core 3.3.7)
//  Rewritten 2026-10-03 for the deployed system: Box 1 (S3A/A1/A2/A3), Box 2 (S3C/C1/C2), Box 3 (S3B/B1/B2/B3), ACS 1 and ACS 2.
//
//  What it is: a Wi-Fi access point ("ESP32_LOCAL" / "12345678") with a captive portal. Open http://192.168.4.1 on a phone:
//    /       home - who is online, what each box is doing, revenue statistics
//    /box1   Box 1 settings + calibration + maintenance      /box2   Box 2      /box3   Box 3      /acs   ACS 1 and ACS 2
//  The boxes run entirely on their own (each S3 node owns its box's state machine and keeps its settings in its own flash); this
//  node only LISTENS, shows, and sends settings / maintenance commands - so it can be switched off, swapped or reflashed at any
//  time without the machines stopping or forgetting a setting. The only things it stores itself are the revenue statistics.
//  See settings_hub.h for the page API. The old A2-relay / Box-1-tab / 4-tank-ACS dashboard was removed on purpose: its Settings
//  tab pushed the OLD settings list into the new nodes.
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "Shared_Common.h"
#include "index_html.h"
#include "style_css.h"
#include "script_js.h"

// The main loop() does all the radio work and builds the status JSON: give it a roomy stack (default 8 KB).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Access Point Configurations
const char* AP_SSID     = "ESP32_LOCAL";
const char* AP_PASSWORD = "12345678";
const byte  DNS_PORT    = 53;

DNSServer      dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences    prefs;

unsigned long lastOneSecTick = 0;

// CommandPacket carries no "who sent this" field, only "who it's addressed to". The node MACs are learned from each node's own
// broadcasts (which carry a deviceID) so commands to S3A / S3C / A1 / C1 can go out as unicast (more reliable than broadcast).
// Indexed by DeviceID; ids >= 8 (Box 3's nodes, ACS 2) are simply broadcast to.
uint8_t knownMac[8][6] = {0};
bool    macKnown[8]    = {false};

// -------------------------------------------------------------
// Statistics & revenue (persisted to NVS) - the only state this node owns
// -------------------------------------------------------------
float coinValuePeso = 1.0f; // PHP per pulse
#define STAT_HISTORY_SIZE 10

struct SessionRecord {
    uint16_t durationSec;
    uint16_t coinsUsed;
};
SessionRecord statHistory[STAT_HISTORY_SIZE];
uint8_t  statHistoryCount = 0;
uint8_t  statHistoryHead  = 0;

uint32_t statTotalSessions    = 0;
uint32_t statTotalCoins       = 0;
uint32_t statTotalDurationSec = 0;

// Revenue is tallied from each coin terminal's own pulse counter (A1 = Box 1, C1 = Box 2, B1 = Box 3). The last count seen is kept in
// NVS so a reboot here does not double-count pulses already tallied (this exact bug happened once).
uint32_t lastKnownPulses[3] = {0, 0, 0};
const char* const PULSE_KEYS[3]  = {"last_a1_pulses", "last_c1_pulses", "last_b1_pulses"};
const char* const PULSE_NAMES[3] = {"A1", "C1", "B1"};

// Completed-session tracking: each S3 node's completedSessionSeq changes once per finished session. The first packet ever seen only
// sets the starting point (otherwise the node's whole history would be replayed as one phantom session).
uint32_t lastKnownSessionSeq[3] = {0, 0, 0};   // 0 = S3A (Box 1), 1 = S3C (Box 2), 2 = S3B (Box 3)
bool     sessionSeqInitialized[3] = {false, false, false};

// Forward declaration - settings_hub.h calls this; it is defined further down. (An explicit prototype is needed because of the default
// argument: the IDE's auto-prototype generator does not handle that case reliably.)
void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr);

void loadSettingsFromNVS() {
    prefs.begin("vending_nvs", false);

    statTotalSessions    = prefs.getUInt("st_sess", 0);
    statTotalCoins       = prefs.getUInt("st_coins", 0);
    statTotalDurationSec = prefs.getUInt("st_dur", 0);
    coinValuePeso        = prefs.getFloat("coin_val", 1.0f);
    for (uint8_t i = 0; i < 3; i++) lastKnownPulses[i] = prefs.getUInt(PULSE_KEYS[i], 0);

    Serial.println("[MASTER NVS] Statistics loaded.");
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Direct Command Dispatcher
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

// Registers a specific peer MAC for unicast sending, if it isn't already registered.
// Broadcast doesn't need this - BROADCAST_MAC is implicitly always sendable.
void ensureUnicastPeer(const uint8_t* peerMac) {
    if (esp_now_is_peer_exist(peerMac)) return;
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void sendEspNowCmdDirect(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str) {
    CommandPacket cmd;
    memset(&cmd, 0, sizeof(CommandPacket));
    cmd.targetDeviceID = targetDev;
    cmd.commandID      = cmdId;
    cmd.subIndex       = subIdx;
    cmd.param16        = param;
    cmd.state          = state;
    if (str != nullptr) {
        strncpy(cmd.payloadStr, str, sizeof(cmd.payloadStr) - 1);
        cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
    }

    // Unicast once the target's MAC is known - only that node's radio has to process it. Falls back to broadcast until a MAC is learned.
    bool haveUnicast = (targetDev < 8) && macKnown[targetDev];
    const uint8_t* destMac = haveUnicast ? knownMac[targetDev] : BROADCAST_MAC;
    if (haveUnicast) ensureUnicastPeer(destMac);

    Serial.printf("[MASTER ESP-NOW TX] opcode %d -> device %d (%s) (sub %d, param %d, state %d)\n",
                  cmdId, targetDev, haveUnicast ? "unicast" : "broadcast", subIdx, param, state);

    esp_err_t result = esp_now_send(destMac, (uint8_t*)&cmd, sizeof(CommandPacket));
    if (result != ESP_OK) {
        Serial.printf("[MASTER ESP-NOW TX] ERROR: send failed with code %d\n", result);
    }
}

void learnMac(uint8_t deviceId, const uint8_t* mac) {
    if (deviceId >= 8 || macKnown[deviceId]) return; // learn once - a node's MAC never changes
    memcpy(knownMac[deviceId], mac, 6);
    macKnown[deviceId] = true;
    Serial.printf("[MASTER] Learned MAC for device %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  deviceId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Records what an S3 node reported about the session it just finished (one combined revenue dashboard for all three boxes).
void recordCompletedSession(uint16_t durSec16, uint16_t coins16) {
    statTotalSessions++;
    statTotalDurationSec += durSec16;
    statHistory[statHistoryHead] = { durSec16, coins16 };
    statHistoryHead = (statHistoryHead + 1) % STAT_HISTORY_SIZE;
    if (statHistoryCount < STAT_HISTORY_SIZE) statHistoryCount++;

    prefs.putUInt("st_sess", statTotalSessions);
    prefs.putUInt("st_dur", statTotalDurationSec);
    Serial.printf("[MASTER STATS] Session #%d recorded: %ds, %d coins.\n", statTotalSessions, durSec16, coins16);
}

void trackCoins(uint8_t i, uint32_t pulseCount) {
    if (pulseCount > lastKnownPulses[i]) {
        uint32_t newPulses = pulseCount - lastKnownPulses[i];
        lastKnownPulses[i] = pulseCount;
        prefs.putUInt(PULSE_KEYS[i], pulseCount);
        statTotalCoins += newPulses;
        prefs.putUInt("st_coins", statTotalCoins);
        Serial.printf("[MASTER STATS] +%u coin(s) from %s.\n", (unsigned)newPulses, PULSE_NAMES[i]);
    } else if (pulseCount < lastKnownPulses[i]) {
        // the terminal itself rebooted (its own counter restarted) - move the baseline down instead of double-counting or freezing
        Serial.printf("[MASTER STATS] %s pulse count dropped (%u -> %u) - it likely rebooted, resyncing.\n", PULSE_NAMES[i], (unsigned)lastKnownPulses[i], (unsigned)pulseCount);
        lastKnownPulses[i] = pulseCount;
        prefs.putUInt(PULSE_KEYS[i], pulseCount);
    }
}

void trackSession(uint8_t i, const BoxStatusPacket& s) {
    if (!sessionSeqInitialized[i]) {
        lastKnownSessionSeq[i] = s.completedSessionSeq;
        sessionSeqInitialized[i] = true;
    } else if (s.completedSessionSeq != lastKnownSessionSeq[i]) {
        lastKnownSessionSeq[i] = s.completedSessionSeq;
        recordCompletedSession(s.lastSessionDurationSec, s.lastSessionCoins);
    }
}

// The settings pages (settings_hub.h) - included here because they use the objects and functions defined above.
#include "settings_hub.h"

// -------------------------------------------------------------
// WebSocket (home page): only two commands remain - revenue housekeeping.
// -------------------------------------------------------------
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    if (!arg || !data || len == 0) return;
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (!info) return;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        char* msg = (char*)data;
        if (msg[0] != '{') return;

        JsonDocument doc;
        if (deserializeJson(doc, msg) != DeserializationError::Ok) return;
        const char* cmd = doc["cmd"];
        if (!cmd) return;

        if (strcmp(cmd, "reset_stats") == 0) {
            statTotalSessions = 0;
            statTotalCoins = 0;
            statTotalDurationSec = 0;
            statHistoryCount = 0;
            statHistoryHead = 0;
            prefs.putUInt("st_sess", 0);
            prefs.putUInt("st_coins", 0);
            prefs.putUInt("st_dur", 0);
            Serial.println("[MASTER WS] Statistics counters reset.");
        } else if (strcmp(cmd, "save_coin_value") == 0) {
            float v = doc["val"] | coinValuePeso;
            if (v >= 0.1f && v <= 1000.0f) {
                coinValuePeso = v;
                prefs.putFloat("coin_val", coinValuePeso);
                Serial.printf("[MASTER WS] Coin value set to PHP %.2f per pulse.\n", coinValuePeso);
            }
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len);
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Receive Callback
//
// The callback runs inside the Wi-Fi task (a small stack, and everything the radio does waits for it), and with all three boxes and both
// ACS boards online this node hears dozens of packets a second. So the callback does NOTHING but copy the packet into a queue; loop()
// does all the work (remembering it for the pages, the revenue tally with its flash writes, the log lines). Same pattern as the S3 nodes.
// -------------------------------------------------------------
struct RxItem {
    uint8_t len;
    uint8_t mac[6];
    uint8_t data[sizeof(CommandPacket)];
};
#define RXQ_SIZE 32
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
RxItem rxQueue[RXQ_SIZE];
volatile uint8_t  rxHead = 0;
volatile uint8_t  rxTail = 0;
volatile uint32_t rxDropped = 0;    // packets lost because loop() was busy for too long (shown on the home page)

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0 || len > (int)sizeof(CommandPacket)) return;
    portENTER_CRITICAL(&rxMux);
    uint8_t next = (uint8_t)((rxHead + 1) % RXQ_SIZE);
    if (next != rxTail) {
        RxItem& it = rxQueue[rxHead];
        it.len = (uint8_t)len;
        memcpy(it.mac, info->src_addr, 6);
        memcpy(it.data, incomingData, (size_t)len);
        rxHead = next;
    } else {
        rxDropped = rxDropped + 1;
    }
    portEXIT_CRITICAL(&rxMux);
}

// Everything that used to run inside the callback; now runs from loop().
void processPacket(const uint8_t* mac, const uint8_t* incomingData, int len) {
    hubOnPacket(incomingData, len);                     // the settings pages remember everything they display

    if (len == (int)sizeof(CommandPacket)) return;      // commands / the nodes' settings broadcasts: handled by the hub, nothing else to do

    if (len == (int)sizeof(Box1ConfigPacket)) { learnMac(DEVICE_S3A, mac); return; }   // legacy config broadcasts: only useful to learn the MAC
    if (len == (int)sizeof(Box2ConfigPacket)) { learnMac(DEVICE_S3C, mac); return; }

    // Live status of each box's S3 node (same packet shape, the deviceID says which one)
    if (len == (int)sizeof(BoxStatusPacket)) {
        BoxStatusPacket status;
        memcpy(&status, incomingData, sizeof(BoxStatusPacket));
        if (status.deviceID == DEVICE_S3A)      { learnMac(DEVICE_S3A, mac); trackSession(0, status); }
        else if (status.deviceID == DEVICE_S3C) { learnMac(DEVICE_S3C, mac); trackSession(1, status); }
        else if (status.deviceID == DEVICE_S3B) { trackSession(2, status); }
        return;
    }

    if (len != (int)sizeof(TelemetryPacket)) return;

    TelemetryPacket packet;
    memcpy(&packet, incomingData, sizeof(TelemetryPacket));

    if (packet.deviceID == DEVICE_A1)      { learnMac(DEVICE_A1, mac); trackCoins(0, packet.pulseCount); }
    else if (packet.deviceID == DEVICE_C1) { learnMac(DEVICE_C1, mac); trackCoins(1, packet.pulseCount); }
    else if (packet.deviceID == DEVICE_B1) { trackCoins(2, packet.pulseCount); }
}

// Called from loop(): handles up to 16 queued packets per pass.
void drainRxQueue() {
    for (uint8_t n = 0; n < 16; n++) {
        RxItem it;
        bool got = false;
        portENTER_CRITICAL(&rxMux);
        if (rxTail != rxHead) {
            memcpy(&it, &rxQueue[rxTail], sizeof(RxItem));
            rxTail = (uint8_t)((rxTail + 1) % RXQ_SIZE);
            got = true;
        }
        portEXIT_CRITICAL(&rxMux);
        if (!got) return;
        processPacket(it.mac, it.data, it.len);
    }
}

// Why did this board last restart? Shown on the home page so a power dip (BROWNOUT) can be told apart from a crash or a watchdog.
uint32_t bootCount = 0;      // every start since the sketch was first flashed
uint32_t crashCount = 0;     // starts that were NOT a normal power-on / reset button / software restart

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   LOCAL SERVER: CONTROL CENTER (CORE 3.3.7)");
    Serial.println("=======================================================");

    loadSettingsFromNVS();
    {
        esp_reset_reason_t why = esp_reset_reason();
        bool unclean = !(why == ESP_RST_POWERON || why == ESP_RST_SW || why == ESP_RST_EXT);
        bootCount = prefs.getUInt("boot_cnt", 0) + 1;
        prefs.putUInt("boot_cnt", bootCount);
        crashCount = prefs.getUInt("crash_cnt", 0);
        if (unclean) { crashCount++; prefs.putUInt("crash_cnt", crashCount); }
        Serial.printf("[BOOT] last restart: %s | starts %u | unexpected restarts %u%s\n", hubResetReason(), (unsigned)bootCount, (unsigned)crashCount,
                      why == ESP_RST_BROWNOUT ? "  <-- BROWNOUT: the power supply dipped, check the supply / cable" : "");
    }
    hubBegin();

    // 1. SoftAP Setup
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);

    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[MASTER WIFI] SoftAP Online -> SSID: '%s' | IP: %s\n", AP_SSID, apIP.toString().c_str());

    // 2. Captive Portal DNS
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("[MASTER DNS] Captive Portal active on Port 53.");

    // 3. ESP-NOW Initialization & Broadcast Peer Registration
    if (esp_now_init() != ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Initialization Failed!");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("[MASTER ESP-NOW] Broadcast Peer {FF:FF:FF:FF:FF:FF} Registered on Channel 1.");
    }

    // 4. Web server: home page, the four settings pages and their API, then everything else falls back to the home page (captive portal)
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/html", INDEX_HTML);
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/css", STYLE_CSS);
    });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "application/javascript", SCRIPT_JS);
    });

    hubRegisterRoutes();

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request) request->send_P(200, "text/html", INDEX_HTML);
    });

    server.begin();
    Serial.println("[MASTER HTTP] Web Server Online.");
}

void loop() {
    drainRxQueue();                       // everything the radio heard since the last pass
    dnsServer.processNextRequest();
    ws.cleanupClients();

    unsigned long currentMillis = millis();

    // 1-Second Master Execution Tick
    if (currentMillis - lastOneSecTick >= 1000) {
        lastOneSecTick = currentMillis;

        // Heartbeat: every node can tell whether anybody is coordinating it (the S3 nodes broadcast their own equivalent).
        // subIndex identifies the sender since CommandPacket has no sender field.
        {
            CommandPacket hb;
            memset(&hb, 0, sizeof(CommandPacket));
            hb.targetDeviceID = 0; // broadcast - every node listens for this opcode regardless
            hb.commandID      = CMD_HEARTBEAT;
            hb.subIndex       = DEVICE_SERVER;
            esp_now_send(BROADCAST_MAC, (uint8_t*)&hb, sizeof(CommandPacket));
        }

        // Real-time status for the home page
        if (ws.count() > 0) {
            JsonDocument doc;
            doc["heap"]       = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            doc["free_psram"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // 0 if PSRAM isn't actually initialized
            hubFillWs(doc);

            // board health for the home page
            doc["up"]       = (uint32_t)(currentMillis / 1000UL);
            doc["rst"]      = hubResetReason();
            doc["boots"]    = bootCount;
            doc["crashes"]  = crashCount;
            doc["minheap"]  = (uint32_t)esp_get_minimum_free_heap_size();
            doc["rxdrop"]   = (uint32_t)rxDropped;
            doc["clients"]  = (int)WiFi.softAPgetStationNum();

            doc["coin_value"]    = coinValuePeso;
            doc["stat_sessions"] = statTotalSessions;
            doc["stat_revenue"]  = statTotalCoins * coinValuePeso;
            doc["stat_coins"]    = statTotalCoins;
            doc["stat_avg_dur"]  = (statTotalSessions > 0) ? (statTotalDurationSec / statTotalSessions) : 0;

            JsonArray hist = doc.createNestedArray("stat_history");
            for (uint8_t i = 0; i < statHistoryCount; i++) {
                uint8_t idx = (statHistoryHead + STAT_HISTORY_SIZE - statHistoryCount + i) % STAT_HISTORY_SIZE;
                JsonObject rec = hist.createNestedObject();
                rec["d"] = statHistory[idx].durationSec;
                rec["r"] = statHistory[idx].coinsUsed * coinValuePeso;
            }

            String output;
            serializeJson(doc, output);
            ws.textAll(output);
        }
    }
}
