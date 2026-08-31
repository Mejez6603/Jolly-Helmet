#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"

// -------------------------------------------------------------
// HARDWARE PIN CONTRACT - NODE ACS (Alcohol Container System / D1)
// -------------------------------------------------------------
// Relays (Active-LOW: LOW = ON/Open, HIGH = OFF/Locked)
#define PIN_SIDE_LOCK      23
#define PIN_WATER_PUMP     21
#define PIN_SCENTED_PUMP   19
#define PIN_ALCOHOL_PUMP   18
#define PIN_MIXER_PUMP     5
#define PIN_MIXING_MACHINE 25 // Inferred from pinout sheet (D25 "MIXER") - CONFIRM before relying on it

const uint8_t RELAY_PINS[6] = {
    PIN_SIDE_LOCK, PIN_WATER_PUMP, PIN_SCENTED_PUMP, PIN_ALCOHOL_PUMP, PIN_MIXER_PUMP, PIN_MIXING_MACHINE
};

#define RELAY_ACTIVE   LOW
#define RELAY_INACTIVE HIGH

// Ultrasonic Sensors - dedicated trigger pins (chosen from the "or X" alternates in the pinout sheet)
#define US_WATER_TRIG    4
#define US_WATER_ECHO    14
#define US_SCENTED_TRIG  26
#define US_SCENTED_ECHO  33
#define US_ALCOHOL_TRIG  27
#define US_ALCOHOL_ECHO  32
#define US_MIXER_TRIG    17
#define US_MIXER_ECHO    16

// Low/Full thresholds for all 4 ACS tanks. The empty-tank baseline readings were noisy
// (10-17cm across sensors due to beam bounce in the narrow tanks), so these give margin:
// 10cm+ reads as Low, 3.3cm or less reads as Safe/Full.
const float ACS_LOW_DIST_CM  = 11.0f;
const float ACS_FULL_DIST_CM = 3.3f;

const unsigned long SIDE_LOCK_AUTO_MS = 5000; // auto-relock after 5s so the solenoid doesn't burn

// -------------------------------------------------------------
// Tank Geometry (all measured against the physical unit)
// All 4 tanks share the same Height and Width; only Length differs.
// -------------------------------------------------------------
const float TANK_HEIGHT_CM    = 14.0f;
const float TANK_WIDTH_CM     = 7.5f;
const float WATER_LENGTH_CM   = 8.0f;
const float SCENTED_LENGTH_CM = 8.0f;
const float ALCOHOL_LENGTH_CM = 8.0f;
const float MIXER_LENGTH_CM   = 20.0f;

// Mixer target ratio: 70% Alcohol, 28% Water, 2% Scented Liquid
const float MIX_RATIO_ALCOHOL = 0.70f;
const float MIX_RATIO_WATER   = 0.28f;
const float MIX_RATIO_SCENTED = 0.02f;

bool relayStates[6] = {false, false, false, false, false, false};
bool sideLockAutoTimerActive = false;
unsigned long sideLockOffAtMillis = 0;

// Maintenance Mode: pauses the automatic refill sequence entirely and lifts the "auto is
// running" restriction on manual relay commands - for flushing tanks before transport, etc.
// The one-at-a-time relay guard in setACSRelay() still applies always, this never bypasses that.
bool maintenanceMode = false;

unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 1000;
unsigned long lastSensorMillis = 0;
const unsigned long SENSOR_INTERVAL_MS = 1000;

float waterDist = 0, scentedDist = 0, alcoholDist = 0, mixerDist = 0;

// -------------------------------------------------------------
// Automatic Mixer Refill Sequence State (Steps 1d -> 4)
// -------------------------------------------------------------
ACSAutoState acsAutoState = ACS_AUTO_IDLE;
float phaseStartRemainML = 0;   // source tank's remaining volume when the current pump phase began
float phaseTargetVolumeML = 0;  // how much this phase needs to dispense

// Pending delivery to Box 1's Humidifier, requested by the Master (A2 low -> Master -> here).
// If the Mixer doesn't have enough yet, this just waits - the normal opportunistic refill
// keeps topping the Mixer up on its own, and delivery fires as soon as it's able to.
bool deliveryPending = false;
uint16_t deliveryTargetML = 0;
float pendingTargetWaterML = 0, pendingTargetScentedML = 0, pendingTargetAlcoholML = 0;
uint16_t mixingTicks = 0;
unsigned long phaseStartMillis = 0;          // when the current pump phase began (for the timeout backstop only)
const unsigned long MAX_PHASE_DURATION_MS = 90000; // hard ceiling per pump phase - anomaly, not a routine cutoff

// -------------------------------------------------------------
// Sensor Reading (median of 5 samples - filters the wall-bounce noise)
// -------------------------------------------------------------
float measureDistanceMedian(uint8_t trigPin, uint8_t echoPin) {
    float samples[5];
    for (uint8_t i = 0; i < 5; i++) {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);

        long duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout
        samples[i] = (duration == 0) ? 0.0f : (float)duration * 0.0343f / 2.0f;
        delay(5);
    }
    // Insertion sort (5 elements), then take the middle value.
    for (uint8_t i = 1; i < 5; i++) {
        float key = samples[i];
        int8_t j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }
    return samples[2];
}

bool isLow(float distCm) {
    return distCm <= 0.0f || distCm >= ACS_LOW_DIST_CM;
}

// -------------------------------------------------------------
// Tank Volume Math
// Remain (mL) = L x W x (H_total - liveUltrasonicReading)
// Capacity (mL) = L x W x (H_total - ACS_FULL_DIST_CM)   <- the "safe fill" line
//
// Source tanks (Water/Scented/Alcohol) treat ACS_LOW_DIST_CM (11cm) as the SAME line for
// everything - the dashboard's LOW flag, "empty" for the automation, and the mid-pump
// safety floor. One threshold, one meaning, everywhere - no second invented margin.
// -------------------------------------------------------------
float tankRemainML(float lengthCm, float distCm) {
    float liquidHeight = TANK_HEIGHT_CM - distCm;
    if (liquidHeight < 0.0f) liquidHeight = 0.0f;
    return lengthCm * TANK_WIDTH_CM * liquidHeight;
}

float tankCapacityML(float lengthCm) {
    float safeHeight = TANK_HEIGHT_CM - ACS_FULL_DIST_CM;
    return lengthCm * TANK_WIDTH_CM * safeHeight;
}

// How much of a source tank's current content is above the LOW line (0 once it's flagged Low).
float tankUsableML(float lengthCm, float distCm) {
    float usable = tankRemainML(lengthCm, distCm) - tankRemainML(lengthCm, ACS_LOW_DIST_CM);
    if (usable < 0.0f) usable = 0.0f;
    return usable;
}

// -------------------------------------------------------------
// Relay Control - enforces the "one relay at a time" rule for
// both manual commands and the automatic refill sequence.
// -------------------------------------------------------------
bool anyOtherRelayActive(uint8_t excludeIdx) {
    for (uint8_t i = 0; i < 6; i++) {
        if (i != excludeIdx && relayStates[i]) return true;
    }
    return false;
}

bool setACSRelay(uint8_t idx, bool state) {
    if (idx >= 6) return false;

    if (state && anyOtherRelayActive(idx)) {
        Serial.printf("[ACS] REFUSED: relay %d requested ON while another relay is active (one-at-a-time rule).\n", idx);
        return false;
    }

    relayStates[idx] = state;
    digitalWrite(RELAY_PINS[idx], state ? RELAY_ACTIVE : RELAY_INACTIVE);
    Serial.printf("[ACS] Relay %d set to %s\n", idx, state ? "ON" : "OFF");

    if (idx == ACS_RELAY_SIDE_LOCK) {
        sideLockAutoTimerActive = state;
        if (state) {
            sideLockOffAtMillis = millis() + SIDE_LOCK_AUTO_MS;
            Serial.println("[ACS] Side Lock opened - will auto-relock in 5s.");
        }
    }
    return true;
}

// -------------------------------------------------------------
// Automatic Mixer Refill Sequence
// Runs once per second, aligned with fresh sensor readings.
// Steps 1a-1c (low supply alerts) are already surfaced via the
// *_low telemetry flags - no separate logic needed for those.
// -------------------------------------------------------------
const float MIN_USEFUL_BATCH_ML = 50.0f;

bool phaseTimedOut() {
    return (millis() - phaseStartMillis) > MAX_PHASE_DURATION_MS;
}

// One-shot check used only right before a pump turns ON (see startSourcePhase below) -
// NOT used as a repeated check during an active phase. The threshold sits inside a noisy
// band the sensors jitter through, so treating it as a live tick-by-tick interrupt causes
// exactly the false-abort looping this comment used to warn about; a single check at the
// moment of activation is safe because the worst case is just "don't start this tick, try
// again next cycle" - not stopping a pump that's already correctly mid-target.
bool sourceAtReserveFloor(float lengthCm, float distCm) {
    return isLow(distCm);
}

// Timeout only - the pre-check already guarantees there's enough for this batch, and the
// dispensed-vs-target check is noise-resistant (relative to the phase's own start reading).
// If a phase still can't reach its target within MAX_PHASE_DURATION_MS, that's a genuine
// anomaly (blocked tube, dead pump) worth stopping for, not routine sensor jitter.
void abortAutoSequence(uint8_t activeRelay, const char* phaseName) {
    setACSRelay(activeRelay, false);
    Serial.printf("[ACS AUTO] ABORT: %s phase exceeded %lus without reaching its target - check for a blocked tube or dead pump.\n",
                  phaseName, MAX_PHASE_DURATION_MS / 1000);
    acsAutoState = ACS_AUTO_IDLE;
}

// Starts a source pump phase - but only after re-checking the reserve floor against the
// freshest possible reading FIRST. Closes the gap where a pump could otherwise be switched
// on for up to 1 second before the in-phase floor check gets a chance to catch it.
void startSourcePhase(uint8_t relayIdx, float lengthCm, float distCm, float targetML, ACSAutoState nextState, const char* name) {
    if (sourceAtReserveFloor(lengthCm, distCm)) {
        Serial.printf("[ACS AUTO] %s already at/below reserve right before pump-on - refusing to start, waiting for refill instead.\n", name);
        acsAutoState = ACS_AUTO_IDLE;
        return;
    }
    phaseStartRemainML = tankRemainML(lengthCm, distCm);
    phaseTargetVolumeML = targetML;
    phaseStartMillis = millis();
    if (setACSRelay(relayIdx, true)) {
        acsAutoState = nextState;
    } else {
        acsAutoState = ACS_AUTO_IDLE;
    }
}

void runAutoRefillTick() {
    if (maintenanceMode) return; // paused - manual control only until turned back off

    switch (acsAutoState) {
        case ACS_AUTO_IDLE: {
            if (anyOtherRelayActive(255)) break;

            // Priority 1: fulfill a pending delivery to Box 1 if the Mixer currently has enough.
            // If it doesn't yet, fall through to the opportunistic refill below, which is
            // already working on topping the Mixer up - delivery fires as soon as it's able to.
            if (deliveryPending) {
                float mixerRemain = tankRemainML(MIXER_LENGTH_CM, mixerDist);
                if (mixerRemain >= (float)deliveryTargetML) {
                    Serial.printf("[ACS AUTO] Delivering %umL to Box 1 Humidifier.\n", deliveryTargetML);
                    phaseStartRemainML = mixerRemain;
                    phaseTargetVolumeML = (float)deliveryTargetML;
                    phaseStartMillis = millis();
                    if (setACSRelay(ACS_RELAY_MIXER_PUMP, true)) {
                        acsAutoState = ACS_AUTO_DELIVERING;
                    }
                    break;
                }
            }

            // Priority 2: opportunistic top-up - don't wait for the Mixer to become critically
            // low, as long as there's meaningful headroom, keep it topped off.
            float headroom = tankCapacityML(MIXER_LENGTH_CM) - tankRemainML(MIXER_LENGTH_CM, mixerDist);
            if (headroom > MIN_USEFUL_BATCH_ML) {
                Serial.printf("[ACS AUTO] Mixer has %.0fmL of headroom -> pre-checking source tanks before touching any pump.\n", headroom);
                acsAutoState = ACS_AUTO_PRECHECK;
            }
            break;
        }

        case ACS_AUTO_PRECHECK: {
            float idealNeeded = tankCapacityML(MIXER_LENGTH_CM) - tankRemainML(MIXER_LENGTH_CM, mixerDist);
            if (idealNeeded < MIN_USEFUL_BATCH_ML) {
                // Mixer topped off enough in the meantime (e.g. manual refill) - stand down.
                acsAutoState = ACS_AUTO_IDLE;
                break;
            }

            float waterUsable   = tankUsableML(WATER_LENGTH_CM, waterDist);
            float scentedUsable = tankUsableML(SCENTED_LENGTH_CM, scentedDist);
            float alcoholUsable = tankUsableML(ALCOHOL_LENGTH_CM, alcoholDist);

            // Refuse entirely only if an ingredient is genuinely empty (zero usable volume).
            // "Low" is not the same as "empty" - low ingredients still get used below.
            bool waterEmpty   = waterUsable   <= 0.0f;
            bool scentedEmpty = scentedUsable <= 0.0f;
            bool alcoholEmpty = alcoholUsable <= 0.0f;

            if (waterEmpty || scentedEmpty || alcoholEmpty) {
                static unsigned long lastWarnMillis = 0;
                if (millis() - lastWarnMillis > 5000) {
                    lastWarnMillis = millis();
                    Serial.printf("[ACS AUTO] Cannot mix - EMPTY, needs refilling:%s%s%s\n",
                                  waterEmpty ? " Water" : "", scentedEmpty ? " Scented-Liquid" : "", alcoholEmpty ? " Alcohol" : "");
                }
                break;
            }

            // All three have SOME usable volume - deliver the largest batch possible while
            // keeping the 70/28/2 ratio exactly correct, limited by whichever ingredient
            // (typically the lowest one) runs out first. Shrinks the whole batch together
            // rather than ever pumping one ingredient without the others.
            float batch = idealNeeded;
            if (waterUsable   / MIX_RATIO_WATER   < batch) batch = waterUsable   / MIX_RATIO_WATER;
            if (scentedUsable / MIX_RATIO_SCENTED < batch) batch = scentedUsable / MIX_RATIO_SCENTED;
            if (alcoholUsable / MIX_RATIO_ALCOHOL < batch) batch = alcoholUsable / MIX_RATIO_ALCOHOL;

            if (batch < MIN_USEFUL_BATCH_ML) {
                // Technically not empty, just too little left to bother with yet.
                static unsigned long lastTinyWarnMillis = 0;
                if (millis() - lastTinyWarnMillis > 5000) {
                    lastTinyWarnMillis = millis();
                    Serial.printf("[ACS AUTO] Only %.0fmL deliverable in-ratio - too small to bother with yet.\n", batch);
                }
                break;
            }

            pendingTargetWaterML   = batch * MIX_RATIO_WATER;
            pendingTargetScentedML = batch * MIX_RATIO_SCENTED;
            pendingTargetAlcoholML = batch * MIX_RATIO_ALCOHOL;

            Serial.printf("[ACS AUTO] Batch %.0fmL of %.0fmL ideal (limited by supply). Targets: Water=%.0fmL Scented=%.0fmL Alcohol=%.0fmL\n",
                          batch, idealNeeded, pendingTargetWaterML, pendingTargetScentedML, pendingTargetAlcoholML);

            startSourcePhase(ACS_RELAY_WATER_PUMP, WATER_LENGTH_CM, waterDist, pendingTargetWaterML, ACS_AUTO_REFILL_WATER, "Water");
            break;
        }

        case ACS_AUTO_REFILL_WATER: {
            if (phaseTimedOut()) { abortAutoSequence(ACS_RELAY_WATER_PUMP, "Water"); break; }
            float dispensed = phaseStartRemainML - tankRemainML(WATER_LENGTH_CM, waterDist);
            if (dispensed >= phaseTargetVolumeML) {
                setACSRelay(ACS_RELAY_WATER_PUMP, false);
                Serial.println("[ACS AUTO] Water target reached -> Scented Liquid.");
                startSourcePhase(ACS_RELAY_SCENTED_PUMP, SCENTED_LENGTH_CM, scentedDist, pendingTargetScentedML, ACS_AUTO_REFILL_SCENTED, "Scented Liquid");
            }
            break;
        }

        case ACS_AUTO_REFILL_SCENTED: {
            if (phaseTimedOut()) { abortAutoSequence(ACS_RELAY_SCENTED_PUMP, "Scented Liquid"); break; }
            float dispensed = phaseStartRemainML - tankRemainML(SCENTED_LENGTH_CM, scentedDist);
            if (dispensed >= phaseTargetVolumeML) {
                setACSRelay(ACS_RELAY_SCENTED_PUMP, false);
                Serial.println("[ACS AUTO] Scented Liquid target reached -> Alcohol.");
                startSourcePhase(ACS_RELAY_ALCOHOL_PUMP, ALCOHOL_LENGTH_CM, alcoholDist, pendingTargetAlcoholML, ACS_AUTO_REFILL_ALCOHOL, "Alcohol");
            }
            break;
        }

        case ACS_AUTO_REFILL_ALCOHOL: {
            if (phaseTimedOut()) { abortAutoSequence(ACS_RELAY_ALCOHOL_PUMP, "Alcohol"); break; }
            float dispensed = phaseStartRemainML - tankRemainML(ALCOHOL_LENGTH_CM, alcoholDist);
            if (dispensed >= phaseTargetVolumeML) {
                setACSRelay(ACS_RELAY_ALCOHOL_PUMP, false);
                Serial.println("[ACS AUTO] Alcohol target reached -> Mixing.");
                mixingTicks = 0;
                if (setACSRelay(ACS_RELAY_MIXING_MACHINE, true)) {
                    acsAutoState = ACS_AUTO_MIXING;
                } else {
                    acsAutoState = ACS_AUTO_IDLE;
                }
            }
            break;
        }

        case ACS_AUTO_MIXING:
            mixingTicks++;
            if (mixingTicks >= 30) {
                setACSRelay(ACS_RELAY_MIXING_MACHINE, false);
                Serial.println("[ACS AUTO] Mixing complete (30s) -> Finishing.");
                acsAutoState = ACS_AUTO_FINISHING;
            }
            break;

        case ACS_AUTO_FINISHING:
            Serial.println("[ACS AUTO] Refill cycle complete -> back to Idle.");
            acsAutoState = ACS_AUTO_IDLE;
            break;

        case ACS_AUTO_DELIVERING: {
            if (phaseTimedOut()) { abortAutoSequence(ACS_RELAY_MIXER_PUMP, "Delivery to Box 1"); deliveryPending = false; break; }
            float dispensed = phaseStartRemainML - tankRemainML(MIXER_LENGTH_CM, mixerDist);
            // Unlike the ingredient phases, there's no ratio to corrupt here - it's a single
            // volume target - so it's safe to also stop early if the Mixer itself runs low,
            // rather than draining it dry mid-delivery.
            if (dispensed >= phaseTargetVolumeML || isLow(mixerDist)) {
                setACSRelay(ACS_RELAY_MIXER_PUMP, false);
                deliveryPending = false;
                Serial.printf("[ACS AUTO] Delivery to Box 1 complete (%.0fmL dispensed).\n", dispensed);
                acsAutoState = ACS_AUTO_IDLE;
            }
            break;
        }
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
    if (cmd.targetDeviceID != DEVICE_ACS) return; // broadcast traffic meant for another node - not an error

    Serial.printf("[ACS RX] Received command! Opcode: %d\n", cmd.commandID);

    if (cmd.commandID == CMD_SET_RELAY) {
        if (!maintenanceMode && acsAutoState != ACS_AUTO_IDLE) {
            Serial.println("[ACS] REFUSED: manual relay command ignored - automatic refill sequence in progress (enable Maintenance Mode to override).");
            return;
        }
        setACSRelay(cmd.subIndex, cmd.state == 1);
    } else if (cmd.commandID == CMD_SET_MAINTENANCE) {
        maintenanceMode = (cmd.state == 1);
        if (maintenanceMode) {
            // Clean handoff: stop whatever the automation currently has running so manual
            // control starts from a known, all-off state rather than fighting an active pump.
            for (uint8_t i = 0; i < 6; i++) {
                if (relayStates[i]) setACSRelay(i, false);
            }
            acsAutoState = ACS_AUTO_IDLE;
            Serial.println("[ACS] MAINTENANCE MODE ON - automatic refill paused, manual relay control unrestricted.");
        } else {
            Serial.println("[ACS] MAINTENANCE MODE OFF - automatic refill resumed.");
        }
    } else if (cmd.commandID == CMD_REQUEST_DELIVERY) {
        if (maintenanceMode) {
            Serial.println("[ACS] REFUSED: delivery request ignored - Maintenance Mode is active.");
            return;
        }
        deliveryPending = true;
        deliveryTargetML = cmd.param16;
        Serial.printf("[ACS] Delivery requested: %umL to Box 1 Humidifier.\n", deliveryTargetML);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE ACS: ALCOHOL CONTAINER SYSTEM (CORE 3.3.7)");
    Serial.println("=======================================================");

    // 1. Configure Relays (Default HIGH = Inactive/Locked)
    for (uint8_t i = 0; i < 6; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], RELAY_INACTIVE);
    }
    Serial.println("[ACS] RELAYS: Side Lock, Water/Scented/Alcohol/Mixer Pumps, Mixing Machine set to HIGH (Off/Locked).");

    // 2. Configure Ultrasonic Sensor Pins
    pinMode(US_WATER_TRIG, OUTPUT);   pinMode(US_WATER_ECHO, INPUT);
    pinMode(US_SCENTED_TRIG, OUTPUT); pinMode(US_SCENTED_ECHO, INPUT);
    pinMode(US_ALCOHOL_TRIG, OUTPUT); pinMode(US_ALCOHOL_ECHO, INPUT);
    pinMode(US_MIXER_TRIG, OUTPUT);   pinMode(US_MIXER_ECHO, INPUT);
    digitalWrite(US_WATER_TRIG, LOW);
    digitalWrite(US_SCENTED_TRIG, LOW);
    digitalWrite(US_ALCOHOL_TRIG, LOW);
    digitalWrite(US_MIXER_TRIG, LOW);
    Serial.println("[ACS] ULTRASONICS: Water/Scented/Alcohol/Mixer sensors initialized.");

    // 3. Force Wi-Fi Channel 1 for exact Master SoftAP Alignment
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.println("[ACS] WIFI INIT: STA Mode, Channel forced to 1 to match Master SoftAP.");

    // 4. Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ACS] ESP-NOW: Initialization Failed!");
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
    Serial.println("[ACS] ESP-NOW: Channel 1 Broadcast Peer registered.");
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Side Lock auto-relock timer (non-blocking)
    if (sideLockAutoTimerActive && currentMillis >= sideLockOffAtMillis) {
        setACSRelay(ACS_RELAY_SIDE_LOCK, false);
        Serial.println("[ACS] Side Lock auto-relocked after 5s.");
    }

    // 2. Sample all 4 ultrasonic sensors (median-of-5 each) every 1s, then run
    //    one tick of the automatic refill sequence against the fresh readings.
    if (currentMillis - lastSensorMillis >= SENSOR_INTERVAL_MS) {
        lastSensorMillis = currentMillis;
        waterDist   = measureDistanceMedian(US_WATER_TRIG, US_WATER_ECHO);
        scentedDist = measureDistanceMedian(US_SCENTED_TRIG, US_SCENTED_ECHO);
        alcoholDist = measureDistanceMedian(US_ALCOHOL_TRIG, US_ALCOHOL_ECHO);
        mixerDist   = measureDistanceMedian(US_MIXER_TRIG, US_MIXER_ECHO);

        Serial.printf("[ACS] SENSORS: Water=%.1fcm Scented=%.1fcm Alcohol=%.1fcm Mixer=%.1fcm\n",
                      waterDist, scentedDist, alcoholDist, mixerDist);

        runAutoRefillTick();
    }

    // 3. Telemetry to Master every 1s
    if (currentMillis - lastTelemetryMillis >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMillis = currentMillis;

        ACSTelemetryPacket packet;
        memset(&packet, 0, sizeof(ACSTelemetryPacket));
        packet.deviceID          = DEVICE_ACS;
        packet.usWaterDistance   = waterDist;
        packet.usScentedDistance = scentedDist;
        packet.usAlcoholDistance = alcoholDist;
        packet.usMixerDistance   = mixerDist;
        packet.waterLow          = isLow(waterDist);
        packet.scentedLow        = isLow(scentedDist);
        packet.alcoholLow        = isLow(alcoholDist);
        packet.mixerLow          = isLow(mixerDist);
        for (uint8_t i = 0; i < 6; i++) packet.relayStates[i] = relayStates[i];
        packet.acsBusy           = anyOtherRelayActive(255);
        packet.autoState         = (uint8_t)acsAutoState;
        packet.maintenanceMode   = maintenanceMode;

        esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(ACSTelemetryPacket));
    }
}
