// =====================================================================
// *** ONE PROGRAM FOR THE THREE SENSOR BOARDS (2026-11-12) ***  ESP32_A2 (Box 1), ESP32_B2 (Box 3) and ESP32_C2 (Box 2) contain exactly this program; the only file
// that differs is box_config.h (its device ID). Fix a bug here, copy the .ino to the other two folders (same text, only the file name changes). The comments below
// were written for Box 3 and say B2 / B3 / S3B: in Box 1 read A2 / A3 / S3A, in Box 2 read C2 / C3 / S3C. A2 and C2 used to carry relays / water sensors / three
// ultrasonics; since 2026-11-12 every box has the same sensing board (helmet + 4 door reeds) and a separate relay board (A3 / B3 / C3).
// =====================================================================
// =====================================================================
//  NODE B2 - BOX 3 SENSING BOARD (sensors only)           (ESP32, core 3.3.7)
//  Created 2026-10-03 from Box 1's sensing board (ESP32_A2.ino); TRIMMED 2026-11-03 for the Refilling Station: the two Main Container ultrasonics and the
//  two analog water sensors are GONE from this board (the Refilling Station reads the containers now), so B2 only measures the HELMET ultrasonic and the
//  door reeds. It still sends the same A2SensorPacket (device ID MY_ID) so nothing else on the radio changes; the container and water fields are
//  always 0 / false. There is NO legacy bridge here (that was only for the old Box 1 S3A / LocalServer).
//
//  This board drives no relay (the relays, incl. the Heater, are on B3) and accepts no commands. It measures, debounces and reports: every ~1 s, and
//  immediately when a door changes, one A2SensorPacket (S3B decides what is "helmet inside").
//
//  Sensors:
//    Helmet US          Echo 25 / Trig 26
//    Door reeds (INPUT_PULLUP, HIGH = open): Enclosure 27, Panel 14, Backdoor 19, Side door 18
//  The pins that used to serve the container ultrasonics (33/32, 22/23) and the water sensors (34/35) are left unconnected / free.
//
//  The helmet ultrasonic is read every 20 ms; the reported value is the MEDIAN of its last 5 readings (0 = no echo).
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "box_config.h"   // which board this is (the ONLY file that differs between A2 / B2 / C2)

// ---------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------
#define US_HELMET_TRIG 26
#define US_HELMET_ECHO 25

#define PIN_DOOR_ENCLOSURE 27
#define PIN_DOOR_PANEL     14
#define PIN_DOOR_BACKDOOR  19
#define PIN_DOOR_SIDE      18             // the Side door (A2SensorPacket::doorAcsSide)
#define DOOR_OPEN_LEVEL    HIGH           // reed switch + INPUT_PULLUP: contact closed (door shut) = LOW, open = HIGH

// ---------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------
const uint32_t US_STEP_MS         = 20;    // one ultrasonic ping every 20 ms
const uint32_t US_ECHO_TIMEOUT_US = 25000; // ~4.3 m; pulseIn returns 0 after this = no echo
const uint32_t DOOR_DEBOUNCE_MS   = 50;    // reed switch must hold a new level this long
const uint32_t REPORT_INTERVAL_MS = 1000;  // periodic broadcast
const uint32_t REPORT_MIN_GAP_MS  = 100;   // never faster than this, even if inputs chatter
const uint32_t SERIAL_SUMMARY_MS  = 1000;

// ---------------------------------------------------------------------
// Helmet ultrasonic
// ---------------------------------------------------------------------
float   usRing[5];                       // last 5 readings
uint8_t usRingCount = 0;
uint8_t usRingHead  = 0;
float   usHelmetCm  = 0.0f;              // current median (0 until 5 readings exist)
uint32_t lastUsStepMs = 0;

float medianOf5(const float in[5]) {
    float s[5];
    for (uint8_t i = 0; i < 5; i++) s[i] = in[i];
    for (uint8_t i = 1; i < 5; i++) {    // insertion sort
        float key = s[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; j--; }
        s[j + 1] = key;
    }
    return s[2];
}

float pingOnce(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH, US_ECHO_TIMEOUT_US);
    return (duration == 0) ? 0.0f : (float)duration * 0.0343f / 2.0f;
}

// At most one ping per call, at most once every US_STEP_MS.
void tickUltrasonic(uint32_t now) {
    if (now - lastUsStepMs < US_STEP_MS) return;
    lastUsStepMs = now;
    usRing[usRingHead] = pingOnce(US_HELMET_TRIG, US_HELMET_ECHO);
    usRingHead = (uint8_t)((usRingHead + 1) % 5);
    if (usRingCount < 5) usRingCount++;
    if (usRingCount >= 5) usHelmetCm = medianOf5(usRing);
}

// ---------------------------------------------------------------------
// Doors: 0 enclosure, 1 panel, 2 backdoor, 3 side door (debounced digital inputs)
// ---------------------------------------------------------------------
#define DOOR_COUNT 4
const uint8_t DOOR_PIN[DOOR_COUNT] = { PIN_DOOR_ENCLOSURE, PIN_DOOR_PANEL, PIN_DOOR_BACKDOOR, PIN_DOOR_SIDE };
const char* const DOOR_NAME[DOOR_COUNT] = { "ENCLOSURE DOOR", "PANEL DOOR", "BACKDOOR", "SIDE DOOR" };

int      doorStable[DOOR_COUNT];     // debounced pin level
int      doorLastRaw[DOOR_COUNT];
uint32_t doorSinceMs[DOOR_COUNT];

bool doorIsOpen(uint8_t i) { return doorStable[i] == DOOR_OPEN_LEVEL; }

// Returns true on the call where the debounced level changes.
bool updateDoor(uint8_t i, uint32_t now) {
    int raw = digitalRead(DOOR_PIN[i]);
    if (raw != doorLastRaw[i]) {
        doorLastRaw[i] = raw;
        doorSinceMs[i] = now;
        return false;
    }
    if (raw != doorStable[i] && (now - doorSinceMs[i]) >= DOOR_DEBOUNCE_MS) {
        doorStable[i] = raw;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------
volatile uint32_t sendFailCount = 0;
uint16_t txSeq         = 0;
uint32_t lastReportMs  = 0;
uint32_t lastSummaryMs = 0;
bool     reportPending = false;   // an input changed and its early report has not gone out yet

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
    if (status != ESP_NOW_SEND_SUCCESS) sendFailCount = sendFailCount + 1;
}

void sendReport(uint32_t now) {
    A2SensorPacket p;
    memset(&p, 0, sizeof(p));                // the container ultrasonics and water sensors are not on this board any more: those fields stay 0 / false
    p.deviceID      = MY_ID;
    p.usHelmetCm    = usHelmetCm;
    p.doorEnclosure = doorIsOpen(0);
    p.doorPanel     = doorIsOpen(1);
    p.doorBackdoor  = doorIsOpen(2);
    p.doorAcsSide   = doorIsOpen(3);
    p.seq           = txSeq++;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(A2SensorPacket));
    lastReportMs = now;
}

void setup() {
    // Only the TRIG pin is an output - make it quiet first.
    pinMode(US_HELMET_TRIG, OUTPUT);
    digitalWrite(US_HELMET_TRIG, LOW);
    pinMode(US_HELMET_ECHO, INPUT);
    for (uint8_t i = 0; i < DOOR_COUNT; i++) pinMode(DOOR_PIN[i], INPUT_PULLUP);

    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE " NODE_NAME ": " BOX_NAME " SENSING BOARD - helmet ultrasonic + doors (CORE 3.3.7)");
    Serial.println("=======================================================");

    delay(20); // let the pulls settle, then take the starting levels as already-debounced
    uint32_t now = millis();
    for (uint8_t i = 0; i < DOOR_COUNT; i++) {
        doorStable[i]  = digitalRead(DOOR_PIN[i]);
        doorLastRaw[i] = doorStable[i];
        doorSinceMs[i] = now;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[" NODE_NAME "] ESP-NOW: init failed - restarting in 3 s.");
        delay(3000);
        ESP.restart();
    }
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("[" NODE_NAME "] ESP-NOW: channel 1 broadcast peer registered. Reporting every 1 s.");
}

void loop() {
    uint32_t now = millis();

    tickUltrasonic(now);

    bool changed = false;
    for (uint8_t i = 0; i < DOOR_COUNT; i++) {
        if (updateDoor(i, now)) {
            changed = true;
            Serial.printf("[" NODE_NAME "] %-14s -> %s\n", DOOR_NAME[i], doorIsOpen(i) ? "OPEN" : "CLOSED");
        }
    }

    // Periodic report, plus an early one when something changed (rate-limited, never dropped).
    if (changed) reportPending = true;
    bool due = (now - lastReportMs) >= REPORT_INTERVAL_MS;
    bool early = reportPending && (now - lastReportMs) >= REPORT_MIN_GAP_MS;
    if (due || early) {
        sendReport(now);
        reportPending = false;
    }

    if (now - lastSummaryMs >= SERIAL_SUMMARY_MS) {
        lastSummaryMs = now;
        Serial.printf("[" NODE_NAME "] helmet=%.1f cm | doors E:%s P:%s B:%s SIDE:%s | tx fail=%lu\n",
                      (double)usHelmetCm,
                      doorIsOpen(0) ? "OPEN" : "shut", doorIsOpen(1) ? "OPEN" : "shut",
                      doorIsOpen(2) ? "OPEN" : "shut", doorIsOpen(3) ? "OPEN" : "shut",
                      (unsigned long)sendFailCount);
    }

    delay(1); // yield
}
