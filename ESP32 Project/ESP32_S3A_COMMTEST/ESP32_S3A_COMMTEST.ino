#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"

// -------------------------------------------------------------
// TEMPORARY sketch - proves an ESP32-S3 can join the SAME ESP-NOW
// broadcast mesh as the existing WROOM nodes and exchange real
// packets with them, using the exact same Shared_Common.h.
//
// This is NOT the real "Internal Server Management" firmware -
// no state machine, no settings, no relaying logic. It just:
//   1. Listens for A1's and A2's TelemetryPacket broadcasts and
//      prints them, to confirm the S3 receives real hardware data.
//   2. Sends a CMD_BUZZER to A1 every 5s, so a successful two-way
//      test is physically audible (A1's buzzer chirps), not just
//      text in a Serial Monitor.
//
// Wire it up, flash it to the S3 board, open Serial Monitor at
// 115200, and have A1/A2 powered on and broadcasting as usual.
// -------------------------------------------------------------

unsigned long lastPingMillis = 0;
const unsigned long PING_INTERVAL_MS = 5000;
uint32_t pingCount = 0;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
    Serial.printf("[S3-A TEST TX] Broadcast Status: %s\n", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL");
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0) return;
    const uint8_t* mac = info->src_addr;

    if (len == sizeof(TelemetryPacket)) {
        TelemetryPacket packet;
        memcpy(&packet, incomingData, sizeof(TelemetryPacket));

        if (packet.deviceID == DEVICE_A1) {
            Serial.printf("[S3-A TEST RX] TelemetryPacket from A1 (MAC %02X:%02X:%02X:%02X:%02X:%02X): pulses=%u touch=(%u,%u) pressed=%d\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                          packet.pulseCount, packet.touchX, packet.touchY, packet.touchPressed);
        } else if (packet.deviceID == DEVICE_A2) {
            Serial.printf("[S3-A TEST RX] TelemetryPacket from A2 (MAC %02X:%02X:%02X:%02X:%02X:%02X): alcDist=%.1fcm helmDist=%.1fcm doorEnc=%d\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                          packet.usAlcoholDistance, packet.usHelmetDistance, packet.doorEnclosure);
        } else {
            Serial.printf("[S3-A TEST RX] TelemetryPacket from unrecognized deviceID=%d (likely C1/C2, overheard on the shared broadcast channel)\n", packet.deviceID);
        }
    } else if (len == sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));
        Serial.printf("[S3-A TEST RX] CommandPacket: target=%d opcode=%d (likely Master or a touch action - overheard, not addressed to this test sketch)\n",
                      cmd.targetDeviceID, cmd.commandID);
    } else if (len == sizeof(ACSTelemetryPacket)) {
        Serial.println("[S3-A TEST RX] ACSTelemetryPacket overheard (not relevant to this test).");
    } else {
        Serial.printf("[S3-A TEST RX] Unrecognized packet length: %d bytes\n", len);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=======================================================");
    Serial.println("   ESP32-S3 COMM TEST (Box 1 / 'S3 A' role)");
    Serial.println("   Proving ESP-NOW works identically on S3 vs WROOM");
    Serial.println("=======================================================");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.println("[S3-A TEST] WiFi STA mode, channel forced to 1 (matches every other node).");

    if (esp_now_init() != ESP_OK) {
        Serial.println("[S3-A TEST] ESP-NOW: Initialization FAILED!");
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
        Serial.println("[S3-A TEST] ESP-NOW: Broadcast peer registered. Listening for A1/A2 telemetry now...");
    } else {
        Serial.println("[S3-A TEST] ESP-NOW: Failed to register broadcast peer!");
    }
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastPingMillis >= PING_INTERVAL_MS) {
        lastPingMillis = currentMillis;
        pingCount++;

        CommandPacket cmd;
        memset(&cmd, 0, sizeof(CommandPacket));
        cmd.targetDeviceID = DEVICE_A1;
        cmd.commandID      = CMD_BUZZER;
        cmd.param16        = 150; // short 150ms chirp, distinguishable from A1's own coin-beep (60ms) and step-transition beep (250ms)

        Serial.printf("[S3-A TEST TX] Sending test CMD_BUZZER #%u to A1 - listen for a ~150ms chirp on A1's buzzer...\n", pingCount);
        esp_now_send(BROADCAST_MAC, (uint8_t*)&cmd, sizeof(CommandPacket));
    }
}
