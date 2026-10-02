// =====================================================================
//  NODE A2 - BOX 1 SENSING BOARD (sensors only)           (ESP32, core 3.3.7)
//  Rewritten 2026-10-02 for the A1/A2/A3 split. The previous relay-carrying A2 is archived next to this
//  file as ESP32_A2_LEGACY_pre_A3.ino.txt (it is still what is flashed on the customer's box today).
//
//  This board no longer drives any relay: the relays are on A3. The S3A / LocalServer currently flashed on the
//  customer's box only understand the OLD A2 packet, so a TEMPORARY LEGACY BRIDGE (LEGACY_S3A_BRIDGE, just
//  below) makes this board ALSO send that old packet. It is removed once the new S3A (Phase 5) exists.
//  A2 and A3 are meant to be installed TOGETHER, and the OLD relay A2 must be unplugged (two boards
//  claiming to be A2 would confuse everything).
//
//  What it does: measures, debounces and reports. No outputs except the three ultrasonic TRIG pins,
//  no commands accepted, no watchdog (it has nothing to switch off). Every ~1 s, and immediately when a
//  door or water sensor changes, it broadcasts one A2SensorPacket (S3A decides what is "low" or
//  "helmet inside"; the only judgement made here is wet/dry for the water sensors, see below).
//
//  Sensors (pins from the 2026-10-01 pin sheet, A2 block - water sensors MOVED 2026-10-02, see below):
//    Helmet US          Echo 25 / Trig 26
//    US MC ALS (alcohol container)   Echo 33 / Trig 32
//    US MC SLS (scented container)   Echo 22 / Trig 23
//    Door reeds (INPUT_PULLUP, HIGH = open): Enclosure 27, Panel 14, Backdoor 19, ACS side door 18
//    Water sensors (ANALOG): WS ALS GPIO34, WS SLS GPIO35
//
//  Ultrasonics are read round-robin (helmet, ALS, SLS, helmet, ...), one 20 ms apart, so no sensor hears
//  another's ping and each sensor is re-pinged only every ~60 ms. The reported value is the MEDIAN of
//  that sensor's last 5 readings (filters wall-bounce spikes, same idea as the ACS). 0 = no echo.
//  A wet ultrasonic reads unreliably - that is exactly why the water sensors exist; S3A cross-checks.
//
//  WATER SENSORS ARE ANALOG (user, 2026-10-02). The old sheet had them on GPIO5 / GPIO17, which have NO
//  ADC, so they moved to GPIO34 / GPIO35 (input-only ADC1 pins - ADC1 keeps working while Wi-Fi/ESP-NOW
//  is on; ADC2 pins would not). WIRING NOTES:
//   - Power the sensor from 3.3 V, NOT 5 V: a 5 V-powered analog sensor can put more than 3.3 V on the
//     pin and damage the ESP32 (its inputs are not 5 V tolerant).
//   - GPIO34/35 have no internal pull resistor, so an unplugged signal wire floats. A 100 k resistor from
//     the pin to GND makes a loose wire read dry (the safe side) - optional but recommended.
//   - Resistive "water" sensors sense CONDUCTIVITY. Water-based scented liquid should work; strong
//     isopropyl alcohol conducts far less and may read low. THEY MUST BE CALIBRATED WITH THE REAL LIQUIDS.
//  CALIBRATE: open the Serial Monitor (115200). Every second a line shows "WS ALS raw=.. (dry|WET)".
//  Note the reading with the sensor dry, and in the actual liquid at the LOW line; set WS_WET_ENTER_RAW
//  (reading that means "now wet") and WS_WET_LEAVE_RAW (reading that means "dry again") between them -
//  the gap between the two is the hysteresis that stops flicker. If wet reads LOWER than dry on your
//  sensor, set WS_WET_IS_HIGHER to 0 and swap the logic of the two numbers (ENTER < LEAVE then).
// =====================================================================
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"

// ---------------------------------------------------------------------
// TEMPORARY LEGACY BRIDGE (added 2026-10-02 so A2+A3 can go straight into the customer's box)
// The S3A and LocalServer flashed on the box today only understand the OLD A2 packet (TelemetryPacket).
// With the bridge ON, every report is sent twice: the new A2SensorPacket AND an old-style
// TelemetryPacket with deviceID = DEVICE_A2 carrying:
//   usHelmetDistance   = helmet ultrasonic
//   doorEnclosure/Panel/Backdoor
//   relayStates[0..4]  = what A3 reports it is really doing (heard via A3StatusPacket), so the
//                        LocalServer dashboard's relay buttons show the true state
//   usAlcoholDistance  = see LEGACY_ALC_MODE
// The old S3A judges alcohol with the OLD humidifier tank's geometry (2 cm = full ... 9 cm = empty), which the
// new main-container sensor will not match, so by default the bridge reports a fixed "plenty" reading:
//   LEGACY_ALC_MODE 0 = pass the real MC ALS reading through (only if it behaves like the old 2..9 cm tank)
//   LEGACY_ALC_MODE 1 = report LEGACY_ALC_FIXED_CM instead (default). The LocalServer dashboard then
//                       shows ~71 % alcohol - NOT real. Watch the container yourself during this test;
//                       the real MC ALS reading is on this board's Serial Monitor.
// Phase 5 (new S3A) makes the whole bridge unnecessary: set LEGACY_S3A_BRIDGE to 0 then.
// ---------------------------------------------------------------------
#define LEGACY_S3A_BRIDGE   1
#define LEGACY_ALC_MODE     1
#define LEGACY_ALC_FIXED_CM 4.0f

// ---------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------
#define US_HELMET_TRIG 26
#define US_HELMET_ECHO 25
#define US_ALS_TRIG    32
#define US_ALS_ECHO    33
#define US_SLS_TRIG    23
#define US_SLS_ECHO    22

#define PIN_DOOR_ENCLOSURE 27
#define PIN_DOOR_PANEL     14
#define PIN_DOOR_BACKDOOR  19
#define PIN_DOOR_ACS_SIDE  18
#define DOOR_OPEN_LEVEL    HIGH   // reed switch + INPUT_PULLUP: contact closed (door shut) = LOW, open = HIGH

#define PIN_WS_ALS 34             // analog, ADC1_CH6, input-only
#define PIN_WS_SLS 35             // analog, ADC1_CH7, input-only

// ---------------------------------------------------------------------
// Water-sensor calibration - PLACEHOLDER VALUES, CALIBRATE WITH THE REAL LIQUIDS (see the top)
// ---------------------------------------------------------------------
#define WS_WET_IS_HIGHER 1        // 1 = the reading goes UP when the sensor is in liquid, 0 = it goes DOWN
const uint16_t WS_WET_ENTER_RAW = 600;  // reading that means "now wet"      (12-bit: 0..4095)
const uint16_t WS_WET_LEAVE_RAW = 400;  // reading that means "dry again"
const uint8_t  WS_AVG_SAMPLES   = 8;    // analogRead() is noisy on the ESP32 - average this many per reading

// ---------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------
const uint32_t US_STEP_MS         = 20;    // one ultrasonic ping every 20 ms, rotating through the 3 sensors
const uint32_t US_ECHO_TIMEOUT_US = 25000; // ~4.3 m; pulseIn returns 0 after this = no echo
const uint32_t DOOR_DEBOUNCE_MS   = 50;    // reed switch must hold a new level this long
const uint32_t WS_READ_MS         = 50;    // water sensors are read this often
const uint32_t WS_DEBOUNCE_MS     = 500;   // liquid sloshes - a new wet/dry verdict must hold this long
const uint32_t REPORT_INTERVAL_MS = 1000;  // periodic broadcast
const uint32_t REPORT_MIN_GAP_MS  = 100;   // never faster than this, even if inputs chatter
const uint32_t SERIAL_SUMMARY_MS  = 1000;

// ---------------------------------------------------------------------
// Ultrasonics
// ---------------------------------------------------------------------
#define US_COUNT 3                       // 0 = helmet, 1 = MC ALS, 2 = MC SLS
const uint8_t US_TRIG[US_COUNT] = { US_HELMET_TRIG, US_ALS_TRIG, US_SLS_TRIG };
const uint8_t US_ECHO[US_COUNT] = { US_HELMET_ECHO, US_ALS_ECHO, US_SLS_ECHO };

float   usRing[US_COUNT][5];             // last 5 readings of each sensor
uint8_t usRingCount[US_COUNT] = {0, 0, 0};
uint8_t usRingHead[US_COUNT]  = {0, 0, 0};
float   usCm[US_COUNT]        = {0.0f, 0.0f, 0.0f};  // current median (0 until 5 readings exist)
uint8_t usTurn                = 0;       // whose turn it is
uint32_t lastUsStepMs         = 0;

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
void tickUltrasonics(uint32_t now) {
    if (now - lastUsStepMs < US_STEP_MS) return;
    lastUsStepMs = now;

    uint8_t s = usTurn;
    usTurn = (uint8_t)((usTurn + 1) % US_COUNT);

    usRing[s][usRingHead[s]] = pingOnce(US_TRIG[s], US_ECHO[s]);
    usRingHead[s] = (uint8_t)((usRingHead[s] + 1) % 5);
    if (usRingCount[s] < 5) usRingCount[s]++;
    if (usRingCount[s] >= 5) usCm[s] = medianOf5(usRing[s]);
}

// ---------------------------------------------------------------------
// Doors: 0 enclosure, 1 panel, 2 backdoor, 3 ACS side door (debounced digital inputs)
// ---------------------------------------------------------------------
#define DOOR_COUNT 4
const uint8_t DOOR_PIN[DOOR_COUNT] = { PIN_DOOR_ENCLOSURE, PIN_DOOR_PANEL, PIN_DOOR_BACKDOOR, PIN_DOOR_ACS_SIDE };
const char* const DOOR_NAME[DOOR_COUNT] = { "ENCLOSURE DOOR", "PANEL DOOR", "BACKDOOR", "ACS SIDE DOOR" };

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
// Water sensors (analog): 0 = ALS (alcohol container), 1 = SLS (scented container)
// ---------------------------------------------------------------------
#define WS_COUNT 2
const uint8_t WS_PIN[WS_COUNT] = { PIN_WS_ALS, PIN_WS_SLS };
const char* const WS_NAME[WS_COUNT] = { "WS ALS", "WS SLS" };

uint16_t wsRaw[WS_COUNT]       = {0, 0};      // latest averaged reading
bool     wsWet[WS_COUNT]       = {false, false};  // current (debounced) verdict
uint32_t wsPendingSinceMs[WS_COUNT] = {0, 0};
uint32_t lastWsReadMs          = 0;

uint16_t readWaterAveraged(uint8_t k) {
    uint32_t sum = 0;
    for (uint8_t n = 0; n < WS_AVG_SAMPLES; n++) sum += analogRead(WS_PIN[k]);
    return (uint16_t)(sum / WS_AVG_SAMPLES);
}

bool rawMeansWet(uint16_t raw) {
#if WS_WET_IS_HIGHER
    return raw >= WS_WET_ENTER_RAW;
#else
    return raw <= WS_WET_ENTER_RAW;
#endif
}

bool rawMeansDry(uint16_t raw) {
#if WS_WET_IS_HIGHER
    return raw <= WS_WET_LEAVE_RAW;
#else
    return raw >= WS_WET_LEAVE_RAW;
#endif
}

// Reads the sensor (rate-limited by the caller) and updates the verdict. Returns true when the
// verdict changes. The reading must sit beyond the ENTER (or LEAVE) number for WS_DEBOUNCE_MS in a row;
// between the two numbers nothing changes (hysteresis).
bool updateWater(uint8_t k, uint32_t now) {
    wsRaw[k] = readWaterAveraged(k);

    bool want = wsWet[k];
    if (!wsWet[k] && rawMeansWet(wsRaw[k])) want = true;
    else if (wsWet[k] && rawMeansDry(wsRaw[k])) want = false;

    if (want == wsWet[k]) {            // nothing pending
        wsPendingSinceMs[k] = now;
        return false;
    }
    if (now - wsPendingSinceMs[k] >= WS_DEBOUNCE_MS) {
        wsWet[k] = want;
        wsPendingSinceMs[k] = now;
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

#if LEGACY_S3A_BRIDGE
volatile uint16_t a3MaskSeen = 0;      // A3's applied relay mask, as last broadcast by A3

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData || len != (int)sizeof(A3StatusPacket)) return;
    A3StatusPacket st;
    memcpy(&st, incomingData, sizeof(A3StatusPacket));
    if (st.deviceID != DEVICE_A3) return;
    a3MaskSeen = st.relayMask;
}

// Old-format report for the S3A / LocalServer that predate A2SensorPacket.
void sendLegacyTelemetry() {
    TelemetryPacket t;
    memset(&t, 0, sizeof(TelemetryPacket));
    t.deviceID          = DEVICE_A2;
    t.usAlcoholDistance = (LEGACY_ALC_MODE == 0) ? usCm[1] : LEGACY_ALC_FIXED_CM;
    t.usHelmetDistance  = usCm[0];
    t.doorEnclosure     = doorIsOpen(0);
    t.doorPanel         = doorIsOpen(1);
    t.doorBackdoor      = doorIsOpen(2);
    uint16_t m = a3MaskSeen;
    t.relayStates[RELAY_ENCLOSURE_LOCK] = (m & A3_BIT(A3_RELAY_ENCLOSURE_LOCK)) != 0;
    t.relayStates[RELAY_PANEL_LOCK]     = (m & A3_BIT(A3_RELAY_PANEL_LOCK)) != 0;
    t.relayStates[RELAY_BACKDOOR_LOCK]  = (m & A3_BIT(A3_RELAY_BACKDOOR_LOCK)) != 0;
    t.relayStates[RELAY_HUMIDIFIER]     = (m & A3_MASK_MIST) != 0;
    t.relayStates[RELAY_UV_LIGHT]       = (m & A3_MASK_UV) != 0;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&t, sizeof(TelemetryPacket));
}
#endif

void sendReport(uint32_t now) {
    A2SensorPacket p;
    memset(&p, 0, sizeof(p));
    p.deviceID      = DEVICE_A2;
    p.usHelmetCm    = usCm[0];
    p.usMcAlsCm     = usCm[1];
    p.usMcSlsCm     = usCm[2];
    p.doorEnclosure = doorIsOpen(0);
    p.doorPanel     = doorIsOpen(1);
    p.doorBackdoor  = doorIsOpen(2);
    p.doorAcsSide   = doorIsOpen(3);
    p.wsAlsWet      = wsWet[0];
    p.wsSlsWet      = wsWet[1];
    p.wsAlsRaw      = wsRaw[0];
    p.wsSlsRaw      = wsRaw[1];
    p.seq           = txSeq++;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(A2SensorPacket));
#if LEGACY_S3A_BRIDGE
    delay(2);   // a couple of ms between back-to-back broadcasts keeps ESP-NOW's send queue from dropping one
    sendLegacyTelemetry();
#endif
    lastReportMs = now;
}

void setup() {
    // Only the TRIG pins are outputs - make them quiet first.
    for (uint8_t s = 0; s < US_COUNT; s++) {
        pinMode(US_TRIG[s], OUTPUT);
        digitalWrite(US_TRIG[s], LOW);
        pinMode(US_ECHO[s], INPUT);
    }
    for (uint8_t i = 0; i < DOOR_COUNT; i++) pinMode(DOOR_PIN[i], INPUT_PULLUP);

    analogReadResolution(12);
    for (uint8_t k = 0; k < WS_COUNT; k++) {
        pinMode(WS_PIN[k], INPUT);
        analogSetPinAttenuation(WS_PIN[k], ADC_11db);  // full 0..~3.3 V range
    }

    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE A2: BOX 1 SENSING BOARD - sensors only (CORE 3.3.7)");
    Serial.println("=======================================================");

    delay(20); // let the pulls settle, then take the starting levels as already-debounced
    uint32_t now = millis();
    for (uint8_t i = 0; i < DOOR_COUNT; i++) {
        doorStable[i]  = digitalRead(DOOR_PIN[i]);
        doorLastRaw[i] = doorStable[i];
        doorSinceMs[i] = now;
    }
    for (uint8_t k = 0; k < WS_COUNT; k++) {   // starting verdict straight from the first reading
        wsRaw[k] = readWaterAveraged(k);
        wsWet[k] = rawMeansWet(wsRaw[k]);
        wsPendingSinceMs[k] = now;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep makes ESP-NOW drop out for seconds at a time
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[A2] ESP-NOW: init failed - restarting in 3 s.");
        delay(3000);
        ESP.restart();
    }
    esp_now_register_send_cb(onDataSent);
#if LEGACY_S3A_BRIDGE
    esp_now_register_recv_cb(onDataRecv);   // only to hear A3's status for the old-style relayStates
#endif

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("[A2] ESP-NOW: channel 1 broadcast peer registered. Reporting every 1 s.");
    Serial.printf("[A2] Water sensors are ANALOG (GPIO%d / GPIO%d): wet when raw %s %u, dry again when raw %s %u.\n",
                  PIN_WS_ALS, PIN_WS_SLS,
                  WS_WET_IS_HIGHER ? ">=" : "<=", (unsigned)WS_WET_ENTER_RAW,
                  WS_WET_IS_HIGHER ? "<=" : ">=", (unsigned)WS_WET_LEAVE_RAW);
    Serial.println("[A2] The thresholds are PLACEHOLDERS - calibrate with the real liquids (see the top of this file).");
#if LEGACY_S3A_BRIDGE
    Serial.println("[A2] LEGACY S3A BRIDGE ON: also sending old-style TelemetryPacket for the current S3A / LocalServer.");
    if (LEGACY_ALC_MODE == 0) Serial.println("[A2] Legacy alcohol distance = the REAL MC ALS reading.");
    else Serial.printf("[A2] Legacy alcohol distance is FIXED at %.1f cm (the dashboard's alcohol level is NOT real) - real MC ALS is in the status line below.\n", (double)LEGACY_ALC_FIXED_CM);
#endif
}

void loop() {
    uint32_t now = millis();

    tickUltrasonics(now);

    bool changed = false;
    for (uint8_t i = 0; i < DOOR_COUNT; i++) {
        if (updateDoor(i, now)) {
            changed = true;
            Serial.printf("[A2] %-14s -> %s\n", DOOR_NAME[i], doorIsOpen(i) ? "OPEN" : "CLOSED");
        }
    }
    if (now - lastWsReadMs >= WS_READ_MS) {
        lastWsReadMs = now;
        for (uint8_t k = 0; k < WS_COUNT; k++) {
            if (updateWater(k, now)) {
                changed = true;
                Serial.printf("[A2] %s raw=%u -> %s\n", WS_NAME[k], (unsigned)wsRaw[k], wsWet[k] ? "WET" : "DRY");
            }
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
        Serial.printf("[A2] helmet=%.1f cm | MC ALS=%.1f cm | MC SLS=%.1f cm | doors E:%s P:%s B:%s ACS:%s | WS ALS raw=%u (%s) SLS raw=%u (%s) | tx fail=%lu\n",
                      (double)usCm[0], (double)usCm[1], (double)usCm[2],
                      doorIsOpen(0) ? "OPEN" : "shut", doorIsOpen(1) ? "OPEN" : "shut",
                      doorIsOpen(2) ? "OPEN" : "shut", doorIsOpen(3) ? "OPEN" : "shut",
                      (unsigned)wsRaw[0], wsWet[0] ? "WET" : "dry",
                      (unsigned)wsRaw[1], wsWet[1] ? "WET" : "dry",
                      (unsigned long)sendFailCount);
    }

    delay(1); // yield
}
