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
//
// UPDATED 2026-10-03 for the new Box 2 procedure (see ESP32_S3C.ino):
//  - S3C now sends the WHOLE relay state as ONE CMD_SET_RELAY_MASK (6 bits, C2RelayIndex order) every couple of seconds, instead
//    of six separate per-relay commands. A single dropped packet can no longer leave the box half-switched. The old per-relay
//    CMD_SET_RELAY is still obeyed (the dashboard's manual relay buttons use it), but S3C's next re-send puts things back.
//  - Relay pins are driven HIGH (inactive) BEFORE they become outputs, so nothing clicks at power-up.
//  - Relays that turn ON are staggered 25 ms apart (heater + UV + fan together is a power surge on a small supply).
//  - The orphaned-S3C safety shutdown now switches the UV off too (it used to leave only heater and fan to be forced off).
//  - A door change is reported at once (not only on the next once-a-second report), so S3C can react within its 2-tick debounce.
// The previous version is archived as ESP32_C2_LEGACY_pre_mask.ino.txt.
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
unsigned long lastUSTriggerMillis = 0;
unsigned long lastEarlyReportMillis = 0;

// -------------------------------------------------------------
// Orphaned safety watchdog - if this node hasn't heard from S3C for ORPHANED_TIMEOUT_MS, nobody
// is coordinating Box 2's cycle. S3C owns the whole cycle now - Master being reachable doesn't
// matter, since Master no longer runs any of it (that's the whole point of the S3C migration).
// C2 has no screen to show anything, but it still needs to make sure nobody gets trapped: if the
// enclosure door is found closed with a headgear detected inside while genuinely orphaned, it
// forces the lock open, then releases it again once the door's confirmed open (same overheat-
// avoidance pattern used throughout this project) rather than holding it open indefinitely or
// leaving it locked. lastSeenMaster is still tracked for diagnostics, just not part of this.
// -------------------------------------------------------------
unsigned long lastSeenMaster = 0;
unsigned long lastSeenS3C    = 0;
// 15s, not 5s - a stretch of dropped ESP-NOW packets (RF interference, congestion) is a real,
// observed occurrence on this hardware, and 5s was tripping the watchdog on and off during
// completely normal operation (mirrors the same fix on C1's OUT OF ORDER detection). 15s still
// catches a genuinely dead S3C within a reasonable window, but stops false-triggering on brief
// connectivity blips.
const unsigned long ORPHANED_TIMEOUT_MS = 15000;
uint8_t watchdogClosedTicks   = 0;
uint8_t watchdogOpenTicks     = 0;
bool    watchdogForcedUnlock  = false;

float usHelmetDist_cm = 0.0f;

// A whole-board relay mask asked for by S3C: written by the receive callback, applied in loop() (the staggering needs delays,
// which must not run in the radio task). -1 = nothing pending.
volatile int16_t pendingMask = -1;

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
    if (relayStates[index] == state) return;       // nothing changes - no log spam on the 2-second re-send
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

// Whole mask at once: everything that turns OFF goes first (at once), then everything that turns ON, 25 ms apart.
void applyRelayMask(uint16_t mask) {
    for (uint8_t i = 0; i < C2_RELAY_COUNT; i++) if (!((mask >> i) & 1u)) setRelayState(i, false);
    for (uint8_t i = 0; i < C2_RELAY_COUNT; i++) {
        if ((mask >> i) & 1u) {
            bool wasOff = !relayStates[i];
            setRelayState(i, true);
            if (wasOff) delay(25);
        }
    }
}

bool magEncOpenNow = false, magPanOpenNow = false, magBakOpenNow = false;

void sendTelemetry() {
    TelemetryPacket packet;
    memset(&packet, 0, sizeof(TelemetryPacket));
    packet.deviceID         = DEVICE_C2;
    packet.usHelmetDistance = usHelmetDist_cm;
    packet.doorEnclosure    = magEncOpenNow;
    packet.doorPanel        = magPanOpenNow;
    packet.doorBackdoor     = magBakOpenNow;
    for (int i = 0; i < 6; i++) {
        packet.relayStates[i] = relayStates[i];
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(TelemetryPacket));
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

    // Broadcast (targetDeviceID 0) - checked before the strict target filter below since it's
    // meant for every node, not just C2.
    if (cmd.commandID == CMD_HEARTBEAT) {
        if (cmd.subIndex == DEVICE_SERVER) lastSeenMaster = millis();
        else if (cmd.subIndex == DEVICE_S3C) lastSeenS3C = millis();
        return;
    }

    if (cmd.targetDeviceID != DEVICE_C2) return; // broadcast traffic meant for another node - not an error

    if (cmd.commandID == CMD_SET_RELAY_MASK) {
        pendingMask = (int16_t)(cmd.param16 & C2_MASK_ALL);
        lastSeenS3C = millis();       // S3C re-sends the mask every ~2 s: each one is proof of life, so one dropped heartbeat is harmless
    } else if (cmd.commandID == CMD_SET_RELAY) {
        Serial.printf("[C2 RX] CMD_SET_RELAY %d -> %d\n", cmd.subIndex, cmd.state);
        setRelayState(cmd.subIndex, cmd.state == 1);
        // A manual relay toggle from Master's dashboard also counts here, which just means the watchdog waits a little longer to
        // fire in that case - never a safety problem, since that requires an actual person actively driving the dashboard.
        lastSeenS3C = millis();
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE C2: BOX 2 (HEATER) ACTUATOR & SENSOR HUB (CORE 3.3.7)");
    Serial.println("=======================================================");

    // 1. Configure Relays & Solenoids (HIGH = Inactive / Locked) - driven BEFORE the pin becomes an output so it never glitches LOW
    for (int i = 0; i < 6; i++) {
        digitalWrite(RELAY_PINS[i], RELAY_INACTIVE);
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
    magEncOpenNow = lastMagEnc; magPanOpenNow = lastMagPan; magBakOpenNow = lastMagBak;
    Serial.println("[C2] SENSORS: Door reed switches initialized on pins 27 (Enc), 14 (Pan), 19 (Bak) with INPUT_PULLUP.");

    // 3. Configure Ultrasonic Sensor Pin (Helmet: ECHO=25, TRIG=26) - no Alcohol sensor on this unit
    pinMode(US_TRIG_PIN, OUTPUT);
    pinMode(US_ECHO_PIN, INPUT);
    digitalWrite(US_TRIG_PIN, LOW);
    Serial.println("[C2] ULTRASONIC: Helmet (ECHO=25, TRIG=26) initialized.");

    // 4. Force Wi-Fi Channel 1 for exact Master SoftAP Alignment
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep periodically powers down the receiver to save power,
                           // even with no AP joined - causes several-second gaps in ESP-NOW
                           // reception on a roughly-periodic cycle otherwise (Master already
                           // disables this; every other node was missing it)
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

    // 0. A relay mask from S3C
    if (pendingMask >= 0) {
        uint16_t m = (uint16_t)pendingMask;
        pendingMask = -1;
        applyRelayMask(m);
    }

    // 1. Sample Ultrasonic Sensor every 100ms
    if (currentMillis - lastUSTriggerMillis >= 100) {
        lastUSTriggerMillis = currentMillis;
        usHelmetDist_cm = measureDistance(US_TRIG_PIN, US_ECHO_PIN);
    }

    // 2. Read Magnetic Reed Switches and Detect State Changes
    bool magEncOpen = (digitalRead(PIN_MAG_ENCLOSURE) == HIGH);
    bool magPanOpen = (digitalRead(PIN_MAG_PANEL) == HIGH);
    bool magBakOpen = (digitalRead(PIN_MAG_BACKDOOR) == HIGH);
    magEncOpenNow = magEncOpen; magPanOpenNow = magPanOpen; magBakOpenNow = magBakOpen;

    if (magEncOpen != lastMagEnc || magPanOpen != lastMagPan || magBakOpen != lastMagBak) {
        lastMagEnc = magEncOpen;
        lastMagPan = magPanOpen;
        lastMagBak = magBakOpen;
        Serial.printf("[C2] DOOR STATE CHANGE: Enclosure: %s | Panel: %s | Backdoor: %s\n",
                      magEncOpen ? "OPEN" : "CLOSED",
                      magPanOpen ? "OPEN" : "CLOSED",
                      magBakOpen ? "OPEN" : "CLOSED");
        if (currentMillis - lastEarlyReportMillis >= 150) {      // tell S3C right away (a bouncing reed cannot flood the radio)
            lastEarlyReportMillis = currentMillis;
            sendTelemetry();
        }
    }

    // 3. 1-Second Sensor Status Output & Telemetry Transmission
    if (currentMillis - lastTelemetryMillis >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMillis = currentMillis;

        bool helmetSafetyPass = (usHelmetDist_cm > 0.0f && usHelmetDist_cm < 30.0f);
        Serial.printf("[C2] SENSOR: Helmet %d cm (%s) | door %s | relays enc%d pan%d bak%d heat%d uv%d fan%d\n",
                      (int)usHelmetDist_cm, helmetSafetyPass ? "inside" : "no helmet", magEncOpen ? "OPEN" : "closed",
                      relayStates[0], relayStates[1], relayStates[2], relayStates[3], relayStates[4], relayStates[5]);

        sendTelemetry();

        // Orphaned safety watchdog - see the comment on lastSeenMaster/lastSeenS3C above.
        // lastSeenS3C is written from the ESP-NOW receive callback, which runs on a different
        // task/core than loop() - if a packet lands and updates it to a timestamp just ahead of
        // this loop() iteration's own currentMillis snapshot, the plain subtraction below would
        // underflow (both unsigned) into a huge number and read as "orphaned" for one tick, then
        // self-correct immediately. Guarding against currentMillis < lastSeenS3C avoids it (same
        // race identified and fixed on C1's OUT OF ORDER detection).
        bool orphaned = (currentMillis >= lastSeenS3C) && (currentMillis - lastSeenS3C > ORPHANED_TIMEOUT_MS);
        if (orphaned) {
            // Nothing is safely regulating the Heater/UV/Fan anymore - S3C owned that decision, and if it disappears
            // mid-Heating those relays would otherwise just keep running unattended and uncontrolled indefinitely
            // (and UV light must never shine on whoever opens the door). Force them off regardless of door state.
            if (relayStates[C2_RELAY_HEATER] || relayStates[C2_RELAY_FAN] || relayStates[C2_RELAY_UV_LIGHT]) {
                Serial.println("[C2] ORPHANED WATCHDOG: Forcing Heater, UV and Fan OFF - no coordinator reachable.");
                setRelayState(C2_RELAY_HEATER, false);
                setRelayState(C2_RELAY_UV_LIGHT, false);
                setRelayState(C2_RELAY_FAN, false);
            }

            if (!magEncOpen) { // door closed
                watchdogOpenTicks = 0;
                watchdogClosedTicks++;
                if (watchdogClosedTicks == 2 && helmetSafetyPass) {
                    Serial.println("[C2] ORPHANED WATCHDOG: Door closed with headgear inside and no coordinator reachable - forcing enclosure unlock.");
                    setRelayState(C2_RELAY_ENCLOSURE_LOCK, true);
                    watchdogForcedUnlock = true;
                }
                // Door closed with no headgear detected - leave it alone, nothing to do.
            } else { // door open
                watchdogClosedTicks = 0;
                if (watchdogForcedUnlock) {
                    watchdogOpenTicks++;
                    if (watchdogOpenTicks >= 2) {
                        // Confirmed open (not reed-switch bounce) - release rather than holding
                        // it energized for however long the door sits open.
                        setRelayState(C2_RELAY_ENCLOSURE_LOCK, false);
                        watchdogForcedUnlock = false;
                    }
                }
            }
        } else {
            watchdogClosedTicks  = 0;
            watchdogOpenTicks    = 0;
            watchdogForcedUnlock = false;
        }
    }
}
