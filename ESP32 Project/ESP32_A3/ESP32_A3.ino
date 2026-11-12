// =====================================================================
// *** ONE PROGRAM FOR THE THREE RELAY BOARDS (2026-11-12) ***  ESP32_A3 (Box 1), ESP32_B3 (Box 3) and ESP32_C3 (Box 2, NEW) contain exactly this program; the only
// file that differs is box_config.h (device IDs, the settings state tag, the flash namespace). Fix a bug here, copy the .ino to the other two folders (same text,
// only the file name changes). The comments below were written for Box 3 and say B2 / B3 / S3B: in Box 1 read A2 / A3 / S3A, in Box 2 read C2 / C3 / S3C.
// Every box has the same 13-channel relay board (12 outputs + the HEATER on GPIO26) with the same pin map. Box 1's A3 has no heater relay wired yet (GPIO26 just
// drives nothing); Box 2's C3 is a new board - its heater relay replaces the old C2's GPIO18 heater, so check the wiring against the pin list below before powering loads.
// =====================================================================
// =====================================================================
//  NODE B3 - BOX 3 RELAY CONTROLLER                       (ESP32, core 3.3.7)
//  Created 2026-10-03 from Box 1's relay controller (ESP32_A3.ino). NOT YET FLASHED - bench-test first with the loads unplugged.
//  2026-11-03: the two DRAIN PUMPS are gone from the design (a manual valve instead; their switching noise made random coin pulses). Mask bits 8 and 9
//  are therefore NEVER driven (B3_USABLE_MASK) - their pins stay inactive whatever S3B or a dashboard asks - and "ACS DOOR LOCK" is now the SIDE LOCK.
//
//  What it is: the one board that owns EVERY relay-driven output of Box 3 (13 channels: A3's 12 plus the HEATER, a hair dryer aimed
//  at the helmet, on GPIO26). It has no sensors and no screen. S3B (the brain) sends it the whole 13-bit relay mask with
//  CMD_SET_RELAY_MASK; B3 just applies it. B2 is the sensing board.
//
//  What it adds on top of "just apply the mask":
//   1. BOOT-SAFE: every relay pin is driven to its inactive level (HIGH) before it becomes an output, and everything stays OFF until
//      S3B has said otherwise.
//   2. SOFT SWITCHING: relays turning OFF switch at once; relays turning ON are staggered (RELAY_ON_STAGGER_MS apart) so the loads
//      never start in the same millisecond (the same inrush that makes the TFT go white when a lock engages). The hair dryer is
//      the biggest load: it is always the LAST one to be switched on.
//   3. S3B-LOSS WATCHDOG: no sign of S3B for 15 s -> everything is forced OFF (humidifiers, UV, HEATER, coin power,
//      panel/back/side locks). The one exception: if a helmet is trapped in a closed enclosure, the ENCLOSURE lock is released
//      (see below). Because B3 has no sensors, it listens to B2's broadcast report for the door and helmet.
//   4. STATUS: broadcasts an A3StatusPacket (deviceID MY_ID) once a second (applied mask + watchdog state).
//
//  "Proof of life" from S3B = CMD_HEARTBEAT with subIndex MY_S3, or any command addressed to MY_ID (S3B re-sends the mask
//  every ~2 s, so one lost heartbeat is harmless).
//
//  Trapped-helmet release (only while orphaned):
//   - B2 report fresh, door CLOSED for 2 ticks and helmet inside (0 < dist < helmetCm; helmetCm = the LocalServer's "helmet counts as
//         inside under" for Box 3 - S3B broadcasts it every 2 s, B3 keeps the last value in flash so it still knows it after a power cut; default 40 cm)
//         -> enclosure lock unlocked; released again once the door is confirmed OPEN (2 ticks).
//   - B2 report missing (we cannot see the door at all) -> ONE short unlock window of UNLOCK_BLIND_WINDOW_MS (15 s - nobody is
//         watching the coil), if UNLOCK_WHEN_B2_UNKNOWN is true.
//   - A solenoid is never held energized forever: UNLOCK_WINDOW_MS on, UNLOCK_REST_MS off, and at most UNLOCK_MAX_WINDOWS windows
//         until the door has been seen open or S3B comes back.
//
//  DEPENDS ON: B2 (ESP32_B2.ino) broadcasting A2SensorPacket with deviceID MY_SENS (doorEnclosure true = OPEN, usHelmetCm).
//  NOTE: pins 16/17 are unusable on ESP32-WROVER (PSRAM) - this board is a plain WROOM DevKit.
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"
#include "box_config.h"   // which board this is (the ONLY file that differs between A3 / B3 / C3)

// ---------------------------------------------------------------------
// Relay pins - index = bit position in the mask (0-11 = A3RelayIndex, 12 = heater). Active-LOW: LOW = ON/energized.
// GPIO0/1/2/3/5/12/14/15 are deliberately NOT used (they pulse or strap at boot).
// ---------------------------------------------------------------------
#define RELAY_ACTIVE   LOW
#define RELAY_INACTIVE HIGH

const uint8_t B3_PINS[B3_RELAY_COUNT] = {
    4,   // 0  ENCLOSURE LOCK
    16,  // 1  PANEL LOCK
    17,  // 2  BACKDOOR LOCK
    18,  // 3  HUMIDIFIER 1
    27,  // 4  UV 1
    19,  // 5  HUMIDIFIER 2
    25,  // 6  HUMIDIFIER 3
    21,  // 7  UV 2
    33,  // 8  (unused - was DRAIN PUMP 1; never driven)
    32,  // 9  (unused - was DRAIN PUMP 2; never driven)
    23,  // 10 SIDE LOCK
    22,  // 11 COIN POWER
    26   // 12 HEATER (hair dryer)
};

const char* const B3_NAMES[B3_RELAY_COUNT] = {
    "ENCLOSURE LOCK", "PANEL LOCK", "BACKDOOR LOCK", "HUMIDIFIER 1",
    "UV 1", "HUMIDIFIER 2", "HUMIDIFIER 3", "UV 2",
    "(UNUSED 8)", "(UNUSED 9)", "SIDE LOCK", "COIN POWER", "HEATER"
};

// The relays that really exist: every bit of the 13-bit mask except the two removed drain pumps (bits 8 and 9). Anything else is ignored.
const uint16_t B3_USABLE_MASK = (uint16_t)(B3_MASK_ALL & (uint16_t)(~(A3_BIT(A3_RELAY_DRAIN_PUMP_1) | A3_BIT(A3_RELAY_DRAIN_PUMP_2))));

// ---------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------
const uint32_t ORPHANED_TIMEOUT_MS    = 15000; // no S3B for this long -> watchdog takes over (same 15 s as the other nodes)
const uint32_t B2_STALE_MS            = 5000;  // B2 report older than this = B2 unknown
const float    HELMET_DEFAULT_CM      = 40.0f; // "helmet inside" cutoff until S3B has told us the LocalServer's value (0 = no echo = not present)
const uint32_t UNLOCK_WINDOW_MS       = 60000; // max time the enclosure solenoid stays energized per window (B2 sees door shut + helmet inside)
const uint32_t UNLOCK_BLIND_WINDOW_MS = 15000; // same, but when B2 is silent too and B3 cannot see the door at all
const uint32_t UNLOCK_REST_MS         = 5000;  // pause between windows
const uint8_t  UNLOCK_MAX_WINDOWS     = 3;     // windows per episode while the door is known to be shut
const uint8_t  CONFIRM_TICKS          = 2;     // B2 reports once a second; 2 in a row = not reed-switch bounce
const uint32_t RELAY_ON_STAGGER_MS    = 25;    // gap between two relays turning ON
const uint32_t STATUS_INTERVAL_MS     = 1000;
const uint32_t SERIAL_SUMMARY_MS      = 5000;
#define UNLOCK_WHEN_B2_UNKNOWN true            // true = if B2 is silent, still give one unlock window

// ---------------------------------------------------------------------
// State shared between the ESP-NOW receive callback (WiFi task) and loop() - guarded by rxMux.
// The callback only RECORDS; loop() is the only place that touches the relay pins.
// ---------------------------------------------------------------------
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t lastSeenS3B = 0;     // millis() of the last proof of life from S3B
volatile uint16_t cmdMask     = 0;     // the relay mask S3B last asked for
volatile uint32_t b2Seq       = 0;     // counts B2 reports (0 = never heard)
volatile uint32_t b2LastMs    = 0;     // millis() of the last B2 report
volatile bool     b2DoorOpen  = false; // B2: enclosure door OPEN (true) / CLOSED (false)
volatile float    b2Helmet    = 0.0f;  // B2: helmet distance in cm
volatile float    helmetCmHeard = 0.0f; // the cutoff S3B broadcast last (0 = nothing new); loop() adopts it and saves it

// loop()-only state
Preferences prefs;
float    helmetCm         = HELMET_DEFAULT_CM; // "helmet inside" = 0 < B2's distance < this (follows the LocalServer setting)
uint16_t appliedMask      = 0;         // what the pins are doing right now
uint32_t lastRelayOnMs    = 0;
bool     orphaned         = false;
uint32_t lastB2SeqSeen    = 0;
uint8_t  doorClosedTicks  = 0;
uint8_t  doorOpenTicks    = 0;
bool     wdLockOn         = false;     // watchdog is holding the enclosure unlocked
uint32_t wdLockSinceMs    = 0;
uint32_t wdWindowMs       = UNLOCK_WINDOW_MS; // length of the window in progress (normal or blind)
uint32_t wdRestUntilMs    = 0;
uint8_t  wdWindows        = 0;
uint32_t lastStatusMs     = 0;
uint32_t lastSummaryMs    = 0;

// ---------------------------------------------------------------------
// ESP-NOW (core 3.3.7 callback signatures, same as the other nodes)
// ---------------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0) return;

    if (len == (int)sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));

        // Broadcast heartbeat (targetDeviceID 0) - meant for every node, checked before the filter.
        if (cmd.commandID == CMD_HEARTBEAT) {
            if (cmd.subIndex == MY_S3) {
                portENTER_CRITICAL(&rxMux);
                lastSeenS3B = millis();
                portEXIT_CRITICAL(&rxMux);
            }
            return;
        }

        // S3B's settings list (every 2 s, broadcast, state 3 = Box 3's copy): subIndex 2 = SENSORS = "helmetCm,offlineSec". The first number is the LocalServer's
        // "helmet counts as inside under" - the watchdog must use the same cutoff as S3B.
        if (cmd.commandID == CMD_SET_CONFIG) {
            if (cmd.targetDeviceID == 0 && cmd.state == BOX_TAG && cmd.subIndex == 2) {
                cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
                float v = strtof(cmd.payloadStr, nullptr);
                if (v >= 1.0f && v <= 200.0f) {
                    portENTER_CRITICAL(&rxMux);
                    helmetCmHeard = v;
                    portEXIT_CRITICAL(&rxMux);
                }
            }
            return;
        }

        if (cmd.targetDeviceID != MY_ID) return; // traffic for another node

        if (cmd.commandID == CMD_SET_RELAY_MASK) {
            portENTER_CRITICAL(&rxMux);
            cmdMask     = (uint16_t)(cmd.param16 & B3_USABLE_MASK);
            lastSeenS3B = millis();
            portEXIT_CRITICAL(&rxMux);
        } else if (cmd.commandID == CMD_SET_RELAY) {
            // Single-relay command (bench / manual use). Edits S3B's mask in place.
            if (cmd.subIndex < B3_RELAY_COUNT && (B3_USABLE_MASK & A3_BIT(cmd.subIndex))) {
                portENTER_CRITICAL(&rxMux);
                uint16_t m = cmdMask;
                if (cmd.state == 1) m = (uint16_t)(m | A3_BIT(cmd.subIndex));
                else                m = (uint16_t)(m & (uint16_t)(~A3_BIT(cmd.subIndex)));
                cmdMask     = m;
                lastSeenS3B = millis();
                portEXIT_CRITICAL(&rxMux);
            }
        }
        return;
    }

    if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket t;
        memcpy(&t, incomingData, sizeof(A2SensorPacket));
        if (t.deviceID != MY_SENS) return;
        portENTER_CRITICAL(&rxMux);
        b2DoorOpen = t.doorEnclosure;
        b2Helmet   = t.usHelmetCm;
        b2LastMs   = millis();
        b2Seq      = b2Seq + 1;
        portEXIT_CRITICAL(&rxMux);
    }
}

// ---------------------------------------------------------------------
// Relay driving
// ---------------------------------------------------------------------
void driveRelay(uint8_t index, bool on) {
    if (index >= B3_RELAY_COUNT) return;
    digitalWrite(B3_PINS[index], on ? RELAY_ACTIVE : RELAY_INACTIVE);
    if (on) appliedMask = (uint16_t)(appliedMask | A3_BIT(index));
    else    appliedMask = (uint16_t)(appliedMask & (uint16_t)(~A3_BIT(index)));
    Serial.printf("[" NODE_NAME "] %-20s (GPIO %2d) -> %s\n", B3_NAMES[index], B3_PINS[index], on ? "ON" : "OFF");
}

// Turn-offs happen immediately; turn-ons one at a time, RELAY_ON_STAGGER_MS apart, lowest bit first
// (locks first, coin power next, the heater - bit 12 - last).
void applyRelays(uint16_t target) {
    target = (uint16_t)(target & B3_USABLE_MASK);

    uint16_t offs = (uint16_t)(appliedMask & (uint16_t)(~target));
    for (uint8_t i = 0; i < B3_RELAY_COUNT; i++) {
        if (offs & A3_BIT(i)) driveRelay(i, false);
    }

    uint16_t ons = (uint16_t)(target & (uint16_t)(~appliedMask));
    if (ons && (millis() - lastRelayOnMs >= RELAY_ON_STAGGER_MS)) {
        for (uint8_t i = 0; i < B3_RELAY_COUNT; i++) {
            if (ons & A3_BIT(i)) {
                driveRelay(i, true);
                lastRelayOnMs = millis();
                break;
            }
        }
    }
}

void resetWatchdogLock() {
    wdLockOn      = false;
    wdWindows     = 0;
    wdRestUntilMs = 0;
}

void setup() {
    // 1. FIRST THING: every relay pin to its inactive level, before it becomes an output, so the
    //    relays never see a LOW pulse while the pin is being configured.
    for (uint8_t i = 0; i < B3_RELAY_COUNT; i++) {
        digitalWrite(B3_PINS[i], RELAY_INACTIVE);
        pinMode(B3_PINS[i], OUTPUT);
        digitalWrite(B3_PINS[i], RELAY_INACTIVE);
    }

    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE " NODE_NAME ": " BOX_NAME " RELAY CONTROLLER (CORE 3.3.7)");
    Serial.println("=======================================================");
    Serial.println("[" NODE_NAME "] RELAYS: all 13 outputs (incl. the HEATER on GPIO26; 8 and 9 are never driven) forced HIGH (OFF / locked) before anything else.");

    prefs.begin(BOX_NVS, false);
    helmetCm = prefs.getFloat("helm_cm", HELMET_DEFAULT_CM);
    if (!(helmetCm >= 1.0f && helmetCm <= 200.0f)) helmetCm = HELMET_DEFAULT_CM;
    Serial.printf("[" NODE_NAME "] Helmet counts as inside under %.1f cm (the LocalServer value " S3_NAME " last told us; default %.0f).\n", (double)helmetCm, (double)HELMET_DEFAULT_CM);

    // 2. Wi-Fi channel 1 + ESP-NOW broadcast peer - identical to the other nodes.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW reception drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[" NODE_NAME "] ESP-NOW: init failed - restarting in 3 s (relays stay OFF).");
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
    Serial.println("[" NODE_NAME "] ESP-NOW: channel 1 broadcast peer registered.");

    // S3B gets the full 15 s from power-up to introduce itself before the watchdog may fire.
    portENTER_CRITICAL(&rxMux);
    lastSeenS3B = millis();
    portEXIT_CRITICAL(&rxMux);
    Serial.println("[" NODE_NAME "] READY - waiting for " S3_NAME " (CMD_SET_RELAY_MASK / heartbeat).");
}

void loop() {
    // ---- 1. Snapshot everything the callback may have changed ----
    uint32_t lastS3B, b2Last, seq;
    uint16_t wantMask;
    bool     doorOpen;
    float    helmet, heardCm;
    portENTER_CRITICAL(&rxMux);
    lastS3B  = lastSeenS3B;
    wantMask = cmdMask;
    seq      = b2Seq;
    b2Last   = b2LastMs;
    doorOpen = b2DoorOpen;
    helmet   = b2Helmet;
    heardCm  = helmetCmHeard;
    portEXIT_CRITICAL(&rxMux);
    if (heardCm >= 1.0f && fabsf(heardCm - helmetCm) > 0.04f) {     // the LocalServer value changed: adopt it, keep it through a power cut
        helmetCm = heardCm;
        prefs.putFloat("helm_cm", helmetCm);
        Serial.printf("[" NODE_NAME "] Helmet cutoff is now %.1f cm (from the LocalServer via " S3_NAME ").\n", (double)helmetCm);
    }
    // millis() is read AFTER the snapshot on purpose: every timestamp above is then <= now, so the
    // unsigned subtractions below can never wrap into a huge "age" (the race A1/A2 had to fix).
    uint32_t now = millis();

    // ---- 2. Orphan state (S3B silent for ORPHANED_TIMEOUT_MS) ----
    bool orphanedNow = (now - lastS3B) > ORPHANED_TIMEOUT_MS;
    if (orphanedNow && !orphaned) {
        orphaned = true;
        resetWatchdogLock();
        // Forget the old mask: when S3B comes back it must send a fresh one; a stale "HEATER ON" must
        // never be re-applied just because the heartbeat returned.
        portENTER_CRITICAL(&rxMux);
        cmdMask = 0;
        portEXIT_CRITICAL(&rxMux);
        Serial.println("[" NODE_NAME "] WATCHDOG: no sign of " S3_NAME " for 15 s -> forcing all outputs OFF (heater included).");
    } else if (!orphanedNow && orphaned) {
        orphaned = false;
        resetWatchdogLock();
        Serial.println("[" NODE_NAME "] WATCHDOG: " S3_NAME " is back - normal operation (waiting for a fresh relay mask).");
    }

    // ---- 3. B2 door/helmet ticks (B2 reports once a second) ----
    if (seq != lastB2SeqSeen) {
        lastB2SeqSeen = seq;
        if (doorOpen) {
            if (doorOpenTicks < 100) doorOpenTicks++;
            doorClosedTicks = 0;
        } else {
            if (doorClosedTicks < 100) doorClosedTicks++;
            doorOpenTicks = 0;
        }
    }
    bool b2Known       = (seq != 0) && ((now - b2Last) <= B2_STALE_MS);
    bool helmetInside  = b2Known && (helmet > 0.0f) && (helmet < helmetCm);
    bool doorShut      = b2Known && !doorOpen && (doorClosedTicks >= CONFIRM_TICKS);
    bool doorSeenOpen  = b2Known && doorOpen && (doorOpenTicks >= CONFIRM_TICKS);

    // ---- 4. Enclosure hold-open while orphaned ----
    if (orphaned) {
        if (wdLockOn) {
            if (doorSeenOpen) {
                wdLockOn      = false;       // door confirmed open - stop energizing the solenoid
                wdWindows     = 0;           // a later re-closed door with the helmet still in = new episode
                wdRestUntilMs = now;
                Serial.println("[" NODE_NAME "] WATCHDOG: door confirmed open - releasing the enclosure lock.");
            } else if (now - wdLockSinceMs >= wdWindowMs) {
                wdLockOn      = false;       // cap reached - let the coil rest
                wdRestUntilMs = now + UNLOCK_REST_MS;
                Serial.println("[" NODE_NAME "] WATCHDOG: unlock window used up - resting the solenoid.");
            }
        } else if (now >= wdRestUntilMs) {
            bool want = false;
            uint8_t maxWindows = UNLOCK_MAX_WINDOWS;
            if (b2Known) {
                want = doorShut && helmetInside;
            } else if (UNLOCK_WHEN_B2_UNKNOWN) {
                want = true;     // blind: cannot see the door, give the customer one chance
                maxWindows = 1;
            }
            if (want && wdWindows < maxWindows) {
                wdLockOn       = true;
                wdLockSinceMs  = now;
                wdWindowMs     = b2Known ? UNLOCK_WINDOW_MS : UNLOCK_BLIND_WINDOW_MS;
                wdWindows++;
                Serial.printf("[" NODE_NAME "] WATCHDOG: enclosure UNLOCKED for a possibly trapped helmet (window %u, " SENS_NAME " %s).\n",
                              (unsigned)wdWindows, b2Known ? "sees door shut + helmet inside" : "unknown");
            }
        }
    }

    // ---- 5. Decide and apply the relay pattern ----
    uint16_t target;
    if (!orphaned) target = (uint16_t)(wantMask & B3_USABLE_MASK);
    else           target = wdLockOn ? (uint16_t)A3_BIT(A3_RELAY_ENCLOSURE_LOCK) : (uint16_t)0;
    applyRelays(target);

    // ---- 6. Status broadcast + occasional serial summary ----
    if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
        lastStatusMs = now;
        A3StatusPacket st;
        memset(&st, 0, sizeof(st));
        st.deviceID      = MY_ID;
        st.relayMask     = appliedMask;
        st.orphaned      = orphaned;
        st.watchdogState = !orphaned ? 0 : (wdLockOn ? 2 : 1);
        esp_now_send(BROADCAST_MAC, (uint8_t*)&st, sizeof(A3StatusPacket));
    }
    if (now - lastSummaryMs >= SERIAL_SUMMARY_MS) {
        lastSummaryMs = now;
        Serial.printf("[" NODE_NAME "] mask applied=0x%04X wanted=0x%04X | " S3_NAME " age %lu ms%s | " SENS_NAME " %s door=%s helmet=%.1f cm (inside under %.1f)\n",
                      (unsigned)appliedMask, (unsigned)wantMask, (unsigned long)(now - lastS3B),
                      orphaned ? " (ORPHANED)" : "",
                      b2Known ? "ok" : "silent", doorOpen ? "OPEN" : "CLOSED", (double)helmet, (double)helmetCm);
    }

    delay(2); // yield; the loop is tiny and timing here is millisecond-level at most
}
