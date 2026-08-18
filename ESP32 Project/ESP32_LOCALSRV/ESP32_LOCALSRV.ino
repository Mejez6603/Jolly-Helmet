#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_now.h>
#include "index_html.h"

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

// Target A1 MAC Address
uint8_t targetMac[] = {0xA4, 0xF0, 0x0F, 0x8E, 0x94, 0x38};

typedef struct struct_command {
  char cmdType[15];
  int targetPrice;
  int sanitizeDurationSec;
  char msgWelcome[35];
  char msgInstruction[35];
  int targetPin;
  char hexColor[7];
} struct_command;

typedef struct struct_status {
  int totalCoins;
  int encDoor;
  int panelDoor;
  int backDoor;
  int helmDist;
  bool helmDetected;
} struct_status;

struct_command cmdData;
struct_status incomingStatus;

void sendPacket() {
  esp_now_send(targetMac, (uint8_t *) &cmdData, sizeof(cmdData));
}

void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingBytes, int len) {
  if (len == sizeof(struct_status)) {
    memcpy(&incomingStatus, incomingBytes, sizeof(incomingStatus));
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_DEBUG_SERVER", "12345678", 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_5dBm); // Restricted ~5m range for privacy

  dnsServer.start(DNS_PORT, "*", apIP);

  if (esp_now_init() != ESP_OK) return;

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, targetMac, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // Web Server Routes
  server.on("/update_config", []() {
    strcpy(cmdData.cmdType, "CONFIG");
    if (server.hasArg("price")) cmdData.targetPrice = server.arg("price").toInt();
    if (server.hasArg("dur")) cmdData.sanitizeDurationSec = server.arg("dur").toInt();
    if (server.hasArg("welcome")) strcpy(cmdData.msgWelcome, server.arg("welcome").c_str());
    if (server.hasArg("inst")) strcpy(cmdData.msgInstruction, server.arg("inst").c_str());
    sendPacket();
    server.send(200, "text/plain", "OK");
  });

  server.on("/toggle", []() {
    if (server.hasArg("pin")) {
      strcpy(cmdData.cmdType, "TOGGLE");
      cmdData.targetPin = server.arg("pin").toInt();
      sendPacket();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/reset_coins", []() {
    strcpy(cmdData.cmdType, "RESET_COINS");
    sendPacket();
    server.send(200, "text/plain", "OK");
  });

  server.on("/get_sensors", []() {
    strcpy(cmdData.cmdType, "READ_SENSORS");
    sendPacket();
    delay(30);

    String json = "{";
    json += "\"totalCoins\":" + String(incomingStatus.totalCoins) + ",";
    json += "\"encDoor\":" + String(incomingStatus.encDoor) + ",";
    json += "\"panelDoor\":" + String(incomingStatus.panelDoor) + ",";
    json += "\"backDoor\":" + String(incomingStatus.backDoor) + ",";
    json += "\"helmDist\":" + String(incomingStatus.helmDist) + ",";
    json += "\"helmDetected\":" + String(incomingStatus.helmDetected ? "true" : "false");
    json += "}";

    server.send(200, "application/json", json);
  });

  server.onNotFound([]() {
    server.send(200, "text/html", htmlUI);
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}