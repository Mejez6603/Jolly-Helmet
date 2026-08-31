#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"

// -------------------------------------------------------------
// NODE C2 - BOX 2 (HEATER) ACTUATOR & SENSOR HUB
// Same pin contract as Node A2 for doors/locks/helmet sensing.
// Differences from A2: no Alcohol tank sensor (this unit doesn't
// use alcohol), the old Humidifier relay pin is now a Heater
// relay (same pin, different load), and there's a 6th relay (Fan)
// that A2 doesn't have.
// -------------------------------------------------------------

// Ultrasonic Sensor (Helmet Detection): ECHO=25, TRIG=26 - same as A2
#define US_TRIG_PIN 26
#define US_ECHO_PIN 25

// Magnetic Door Reed Switches: Enclosure=27, Panel=14, Backdoor=19 (INPUT_PULLUP: LOW = Closed, HIGH = Open)
#define PIN_MAG_ENCLOSURE 27
#define PIN_MAG_PANEL     14
#define PIN_MAG_BACKDOOR  19

// Solenoid Locks (Active-LOW: LOW=Open, HIGH=Locked): Enclosure=4, Panel=16, Backdoor=17
#define PIN_RELAY_ENCLOSURE_LOCK 4
#define PIN_RELAY_PANEL_LOCK     16
#define PIN_RELAY_BACKDOOR_LOCK  17

// Relays (Active-LOW): Heater=18 (was Humidifier's pin on A2), UV Lights=5, Fan=23 (new)
#define PIN_RELAY_HEATER    18
#define PIN_RELAY_UV_LIGHT  5
#define PIN_RELAY_FAN       23

#define RELAY_ACTIVE   LOW
#define RELAY_INACTIVE HIGH

const uint8_t RELAY_PINS[6] = {
    PIN_RELAY_ENCLOSURE_LOCK,
    PIN_RELAY_PANEL_LOCK,
    PIN_RELAY_BACKDOOR_LOCK,
    PIN_RELAY_HEATER,
    PIN_RELAY_UV_LIGHT,
    PIN_RELAY_FAN
};
bool relayStates[6] = {false, false, false, false, false, false};

// Edge Detection for Door Switches
bool lastMagEnc = false;
bool lastMagPan = false;
bool lastMagBak = false;

// Non-blocking Timers
unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 1000;
unsigned long lastHeartbeatMillis = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 2000;
unsigned long lastUSTriggerMillis = 0;

float usHelmetDist_cm = 0.0f;

float measureDistance(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout (~4.3m max)
    if (duration == 0) return 0.0f;
    return (float)duration * 0.0343f / 2.0f;
}

void setRelayState(uint8_t index, bool state) {
    if (index >= 6) return;
    relayStates[index] = state;
    digitalWrite(RELAY_PINS[index], state ? RELAY_ACTIVE : RELAY_INACTIVE);

    if (index <= 2) {
        // Solenoids (Pins 4, 16, 17)
        Serial.printf("[C2] SOLENOID TRIGGERED: Pin %d set to %s\n",
                      RELAY_PINS[index], state ? "LOW=OPEN" : "HIGH=LOCKED");
    } else {
        // Relays (Pins 18, 5, 23)
        Serial.printf("[C2] RELAY TRIGGERED: Pin %d set to %s\n",
                      RELAY_PINS[index], state ? "ON" : "OFF");
    }
}

// -------------------------------------------------------------
// Core 3.3.7 ESP-NOW Callbacks
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0) return;
    if (len != sizeof(CommandPacket)) return;

    CommandPacket cmd;
    memcpy(&cmd, incomingData, sizeof(CommandPacket));
    if (cmd.targetDeviceID != DEVICE_C2) return; // broadcast traffic meant for another node - not an error

    Serial.printf("[C2 RX] Received ESP-NOW Packet! Type: %d\n", cmd.commandID);

    if (cmd.commandID == CMD_SET_RELAY) {
        setRelayState(cmd.subIndex, cmd.state == 1);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE C2: BOX 2 (HEATER) ACTUATOR & SENSOR HUB (CORE 3.3.7)");
    Serial.println("=======================================================");

    // 1. Configure Relays & Solenoids (Default to HIGH = Inactive / Locked)
    for (int i = 0; i < 6; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], RELAY_INACTIVE);
    }
    Serial.println("[C2] RELAYS: Solenoids (4, 16, 17) and Relays (18 Heater, 5 UV, 23 Fan) set to HIGH (Locked/Inactive).");

    // 2. Configure Magnetic Door Reed Switches (Pins 27, 14, 19 with INPUT_PULLUP)
    pinMode(PIN_MAG_ENCLOSURE, INPUT_PULLUP);
    pinMode(PIN_MAG_PANEL, INPUT_PULLUP);
    pinMode(PIN_MAG_BACKDOOR, INPUT_PULLUP);
    lastMagEnc = (digitalRead(PIN_MAG_ENCLOSURE) == HIGH);
    lastMagPan = (digitalRead(PIN_MAG_PANEL) == HIGH);
    lastMagBak = (digitalRead(PIN_MAG_BACKDOOR) == HIGH);
    Serial.println("[C2] SENSORS: Door reed switches initialized on pins 27 (Enc), 14 (Pan), 19 (Bak) with INPUT_PULLUP.");

    // 3. Configure Ultrasonic Sensor Pin (Helmet: ECHO=25, TRIG=26) - no Alcohol sensor on this unit
    pinMode(US_TRIG_PIN, OUTPUT);
    pinMode(US_ECHO_PIN, INPUT);
    digitalWrite(US_TRIG_PIN, LOW);
    Serial.println("[C2] ULTRASONIC: Helmet (ECHO=25, TRIG=26) initialized.");

    // 4. Force Wi-Fi Channel 1 for exact Master SoftAP Alignment
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.println("[C2] WIFI INIT: Setting STA Mode... Channel forced to 1 to match Master SoftAP.");

    // 5. Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[C2] ESP-NOW: Initialization Failed!");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("[C2] ESP-NOW: Channel 1 Broadcast Peer registered.");
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Sample Ultrasonic Sensor every 100ms
    if (currentMillis - lastUSTriggerMillis >= 100) {
        lastUSTriggerMillis = currentMillis;
        usHelmetDist_cm = measureDistance(US_TRIG_PIN, US_ECHO_PIN);
    }

    // 2. Read Magnetic Reed Switches and Detect State Changes
    bool magEncOpen = (digitalRead(PIN_MAG_ENCLOSURE) == HIGH);
    bool magPanOpen = (digitalRead(PIN_MAG_PANEL) == HIGH);
    bool magBakOpen = (digitalRead(PIN_MAG_BACKDOOR) == HIGH);

    if (magEncOpen != lastMagEnc || magPanOpen != lastMagPan || magBakOpen != lastMagBak) {
        lastMagEnc = magEncOpen;
        lastMagPan = magPanOpen;
        lastMagBak = magBakOpen;
        Serial.printf("[C2] DOOR STATE CHANGE: Enclosure: %s | Panel: %s | Backdoor: %s\n",
                      magEncOpen ? "OPEN" : "CLOSED",
                      magPanOpen ? "OPEN" : "CLOSED",
                      magBakOpen ? "OPEN" : "CLOSED");
    }

    // 3. Heartbeat Ping every 2000ms
    if (currentMillis - lastHeartbeatMillis >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatMillis = currentMillis;
        Serial.println("[C2] HEARTBEAT: Sending Ping to Master MAC Address...");
    }

    // 4. 1-Second Sensor Status Output & Telemetry Transmission
    if (currentMillis - lastTelemetryMillis >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMillis = currentMillis;

        bool helmetSafetyPass = (usHelmetDist_cm > 0.0f && usHelmetDist_cm < 30.0f);
        Serial.printf("[C2] SENSOR: Helmet Distance: %d cm\n", (int)usHelmetDist_cm);
        Serial.printf("[C2] SAFETY CHECK: Helmet Present? %s (<30cm requirement)\n",
                      helmetSafetyPass ? "PASS" : "FAIL");

        TelemetryPacket packet;
        memset(&packet, 0, sizeof(TelemetryPacket));
        packet.deviceID         = DEVICE_C2;
        packet.usHelmetDistance = usHelmetDist_cm;
        packet.doorEnclosure    = magEncOpen;
        packet.doorPanel        = magPanOpen;
        packet.doorBackdoor     = magBakOpen;
        for (int i = 0; i < 6; i++) {
            packet.relayStates[i] = relayStates[i];
        }

        Serial.println("[C2 TX] Sending Periodic Ping to Master...");
        esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(TelemetryPacket));
    }
}
