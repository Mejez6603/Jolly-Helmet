// =====================================================================
//  ESP32_A3_BENCH_TEST - fake S3A (+ optional fake A2) for testing Node A3 and the real A2   (core 3.3.7)
//  Created 2026-10-02, extended the same day (OLD protocol mode + real-A2 monitor).
//
//  Flash this on a SPARE ESP32 (any DevKit - the freed old relay-A2 board will do), power ESP32_A3 and
//  ESP32_A2 next to it, connect a phone/PC to the Wi-Fi "A3_BENCH" (password 12345678) and open
//  http://192.168.4.1
//  Switch the real S3A, A1 and LocalServer OFF while testing (they would talk over this), and keep the
//  OLD relay A2 unplugged.
//
//  The page lets you:
//    - toggle each of A3's 12 relays (or a preset) -> sent as CMD_SET_RELAY_MASK, like the NEW S3A will
//    - switch to the OLD protocol: per-relay commands addressed to "A2" plus a state broadcast, exactly like
//      the S3A on the customer's box today -> tests A3's LEGACY BRIDGE (mist = 3 humidifiers, UV = 2 UVs,
//      coin power only in INSERT COIN / CLEANING)
//    - turn the fake S3A heartbeat and the 2-second re-send on/off (= "S3A died")
//    - pretend to be A2 (door open/closed, helmet distance) - leave it OFF when the real A2 is powered
//    - watch what the REAL A2 reports (distances, 4 doors, 2 water sensors) and what A3 reports back
//
//  SUGGESTED CHECKLIST (A3's own Serial log at 115200 narrates each step)
//   1. Power A3 alone: NO relay may click at power-up (all 12 stay OFF). Wait 15 s: with nobody
//      talking, A3 reports ORPHANED and (A2 unknown) holds the enclosure unlocked for ONE 15 s window.
//   2. NEW protocol, heartbeat ON + re-send ON: tap relays one by one; each LED/click follows within ~25 ms
//      (several at once switch ON one after another, OFF all together). Check Drain Pump 1 (GPIO33) too.
//   3. Press a preset (ALCOHOL / SCENTED / BALANCED): UV1+UV2 + the right humidifiers + HUMID 3.
//   4. Heartbeat OFF and re-send OFF with some relays ON: nothing changes for 15 s, then everything
//      switches OFF and A3 reports "orphaned". Turn heartbeat ON again: relays must STAY OFF until
//      you send a fresh mask (no stale mask re-applied).
//   5. Orphan + door CLOSED and helmet inside (use the real A2 with a hand/helmet in front of the helmet
//      sensor and the door shut, or the fake A2 at "door CLOSED, helmet 8 cm"; leave it like that for 2+ s
//      BEFORE the 15 s run out): the moment A3 goes orphaned the ENCLOSURE LOCK relay turns ON. Open the
//      door: it releases ~2 s later. Helmet 0 (nothing inside) + door closed: the lock must NOT unlock.
//   6. Orphan with NO A2 at all (real A2 off, fake A2 off): one unlock window of 15 s only, then it
//      relocks and rests.
//   7. OLD protocol: press "Protocol" to switch. Tap MIST: HUMIDIFIER 1, 2 and 3 all on. Tap UV LIGHT: UV 1
//      and UV 2 on. Locks follow their buttons. COIN POWER stays OFF until you pick step 14 INSERT COIN
//      or step 5 CLEANING; any other step (or "none") turns it off within ~1 s.
//   8. Real A2: the "Real A2" box should show distances that move when you put a hand in front of each
//      ultrasonic, each door switching OPEN/shut, and the two water sensors' raw values. The line about the
//      old-format packet should show the relay states mirrored from A3 and an alcohol distance of 4.0 cm.
// =====================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "Shared_Common.h"
#include "bench_page.h"   // const char PAGE[] - the web UI (kept out of the .ino, see the note in that file)

const char* AP_SSID     = "A3_BENCH";
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

// ---- what this tool is pretending to be ----
bool     fakeHeartbeatOn = true;   // S3A: CMD_HEARTBEAT every 1 s
bool     fakeMaskResend  = true;   // S3A: re-send the relay command(s) every 2 s
uint16_t fakeMask        = 0;      // NEW protocol: the 12-bit relay mask it wants
bool     fakeA2On        = false;  // fake A2: broadcast telemetry every 1 s (OFF by default - the real A2 is expected)
bool     fakeDoorOpen    = false;  // fake A2: enclosure door open?
float    fakeHelmetCm    = 0.0f;   // fake A2: helmet distance (0 = nothing detected)

bool     oldProtocol  = false;     // true = talk like the S3A on the box today
uint8_t  legacyMask   = 0;         // OLD protocol: bit0 enc, bit1 pan, bit2 bak, bit3 mist, bit4 UV
uint8_t  legacyState  = 255;       // OLD protocol: MachineState sent in the state broadcast (255 = send none)

uint32_t lastHbMs = 0, lastMaskMs = 0, lastA2Ms = 0, lastLegacyRelaysMs = 0, lastLegacyStatusMs = 0;

// ---- what the radio heard (written by the receive callback, read by the web handler) ----
static portMUX_TYPE a3Mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t a3Count  = 0;
volatile uint32_t a3LastMs = 0;
volatile uint16_t a3Mask   = 0;
volatile bool     a3Orph   = false;
volatile uint8_t  a3Wd     = 0;
A2SensorPacket    rA2;                 // last packet from the REAL A2 (guarded by a3Mux)
uint32_t          rA2Ms = 0, rA2Count = 0;
TelemetryPacket   rLeg;                // last OLD-format packet from A2 (the legacy bridge)
uint32_t          rLegMs = 0, rLegCount = 0;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !incomingData) return;

    if (len == (int)sizeof(A3StatusPacket)) {
        A3StatusPacket st;
        memcpy(&st, incomingData, sizeof(A3StatusPacket));
        if (st.deviceID != DEVICE_A3) return;
        portENTER_CRITICAL(&a3Mux);
        a3Mask   = st.relayMask;
        a3Orph   = st.orphaned;
        a3Wd     = st.watchdogState;
        a3LastMs = millis();
        a3Count  = a3Count + 1;
        portEXIT_CRITICAL(&a3Mux);
    } else if (len == (int)sizeof(A2SensorPacket)) {
        A2SensorPacket p;
        memcpy(&p, incomingData, sizeof(A2SensorPacket));
        if (p.deviceID != DEVICE_A2) return;
        portENTER_CRITICAL(&a3Mux);
        rA2      = p;
        rA2Ms    = millis();
        rA2Count = rA2Count + 1;
        portEXIT_CRITICAL(&a3Mux);
    } else if (len == (int)sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        memcpy(&t, incomingData, sizeof(TelemetryPacket));
        if (t.deviceID != DEVICE_A2) return;
        portENTER_CRITICAL(&a3Mux);
        rLeg      = t;
        rLegMs    = millis();
        rLegCount = rLegCount + 1;
        portEXIT_CRITICAL(&a3Mux);
    }
}

// Same argument order as S3A's sendCmd(target, opcode, subIndex, param16, state).
void sendCmd(uint8_t target, uint8_t opcode, uint8_t sub, uint16_t p16, uint8_t st) {
    CommandPacket c;
    memset(&c, 0, sizeof(c));
    c.targetDeviceID = target;
    c.commandID      = opcode;
    c.subIndex       = sub;
    c.param16        = p16;
    c.state          = st;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&c, sizeof(CommandPacket));
}

void sendMask() {
    sendCmd(DEVICE_A3, CMD_SET_RELAY_MASK, 0, fakeMask, 0);
    lastMaskMs = millis();
}

// OLD protocol, relays: what the S3A on the box does - one CMD_SET_RELAY per relay to DEVICE_A2, 2 ms apart.
void sendLegacyRelays() {
    for (uint8_t i = 0; i < 5; i++) {
        sendCmd(DEVICE_A2, CMD_SET_RELAY, i, 0, (uint8_t)((legacyMask >> i) & 1));
        delay(2);
    }
    lastLegacyRelaysMs = millis();
}

// OLD protocol, state: the S3A's BoxStatusPacket (A3 reads machineState from it).
void sendLegacyStatus() {
    BoxStatusPacket b;
    memset(&b, 0, sizeof(b));
    b.deviceID     = DEVICE_S3A;
    b.machineState = legacyState;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&b, sizeof(BoxStatusPacket));
    lastLegacyStatusMs = millis();
}

// The 12-bit mask this tool expects A3 to end up with, for highlighting mismatches on the page.
uint16_t expectedMask() {
    if (!oldProtocol) return fakeMask;
    uint16_t m = 0;
    if (legacyMask & 0x01) m |= A3_BIT(A3_RELAY_ENCLOSURE_LOCK);
    if (legacyMask & 0x02) m |= A3_BIT(A3_RELAY_PANEL_LOCK);
    if (legacyMask & 0x04) m |= A3_BIT(A3_RELAY_BACKDOOR_LOCK);
    if (legacyMask & 0x08) m |= A3_MASK_MIST;
    if (legacyMask & 0x10) m |= A3_MASK_UV;
    if (legacyState == STATE_INSERT_COIN || legacyState == STATE_CLEANING) m |= A3_BIT(A3_RELAY_COIN_POWER);
    return m;
}

void sendFakeA2() {
    static uint16_t seq = 0;
    A2SensorPacket t;
    memset(&t, 0, sizeof(t));
    t.deviceID      = DEVICE_A2;
    t.usHelmetCm    = fakeHelmetCm;
    t.doorEnclosure = fakeDoorOpen;
    t.seq           = seq++;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&t, sizeof(A2SensorPacket));
    lastA2Ms = millis();
}

void sendState() {
    uint32_t cnt, last; uint16_t m; bool o; uint8_t w;
    A2SensorPacket a2; uint32_t a2Ms, a2Cnt;
    TelemetryPacket lg; uint32_t lgMs, lgCnt;
    portENTER_CRITICAL(&a3Mux);
    cnt = a3Count; last = a3LastMs; m = a3Mask; o = a3Orph; w = a3Wd;
    a2 = rA2; a2Ms = rA2Ms; a2Cnt = rA2Count;
    lg = rLeg; lgMs = rLegMs; lgCnt = rLegCount;
    portEXIT_CRITICAL(&a3Mux);
    uint32_t now = millis();
    uint32_t age   = (cnt   == 0) ? 0 : (now - last);
    uint32_t a2Age = (a2Cnt == 0) ? 0 : (now - a2Ms);
    uint32_t lgAge = (lgCnt == 0) ? 0 : (now - lgMs);

    unsigned doors = (a2.doorEnclosure ? 1u : 0u) | (a2.doorPanel ? 2u : 0u) | (a2.doorBackdoor ? 4u : 0u) | (a2.doorAcsSide ? 8u : 0u);
    unsigned wet   = (a2.wsAlsWet ? 1u : 0u) | (a2.wsSlsWet ? 2u : 0u);
    unsigned lrs   = 0;
    for (uint8_t i = 0; i < 5; i++) if (lg.relayStates[i]) lrs |= (1u << i);

    char buf[1100];
    snprintf(buf, sizeof(buf),
             "{\"hb\":%d,\"auto\":%d,\"mask\":%u,\"a2\":%d,\"door\":%d,\"dist\":%.1f,"
             "\"a3ok\":%d,\"a3age\":%lu,\"a3mask\":%u,\"a3orph\":%d,\"a3wd\":%u,"
             "\"old\":%d,\"lmask\":%u,\"lstate\":%u,"
             "\"ra2ok\":%d,\"ra2age\":%lu,\"ra2h\":%.1f,\"ra2als\":%.1f,\"ra2sls\":%.1f,\"ra2doors\":%u,\"ra2wet\":%u,\"ra2raw0\":%u,\"ra2raw1\":%u,"
             "\"lgok\":%d,\"lgage\":%lu,\"lgalc\":%.1f,\"lgrs\":%u,"
             "\"up\":%lu,\"rst\":\"%s\",\"clients\":%d,\"joins\":%u,\"leaves\":%u,\"why\":%u}",
             fakeHeartbeatOn ? 1 : 0, fakeMaskResend ? 1 : 0, (unsigned)expectedMask(),
             fakeA2On ? 1 : 0, fakeDoorOpen ? 1 : 0, (double)fakeHelmetCm,
             cnt ? 1 : 0, (unsigned long)age, (unsigned)m, o ? 1 : 0, (unsigned)w,
             oldProtocol ? 1 : 0, (unsigned)legacyMask, (unsigned)legacyState,
             a2Cnt ? 1 : 0, (unsigned long)a2Age, (double)a2.usHelmetCm, (double)a2.usMcAlsCm, (double)a2.usMcSlsCm,
             doors, wet, (unsigned)a2.wsAlsRaw, (unsigned)a2.wsSlsRaw,
             lgCnt ? 1 : 0, (unsigned long)lgAge, (double)lg.usAlcoholDistance, lrs,
             (unsigned long)(millis() / 1000UL), resetReasonName(), (int)WiFi.softAPgetStationNum(), (unsigned)apJoins, (unsigned)apLeaves, (unsigned)apLastLeaveReason);
    server.send(200, "application/json", buf);
}

void handleSet() {
    bool maskChanged = false;
    if (server.hasArg("hb"))   fakeHeartbeatOn = server.arg("hb") == "1";
    if (server.hasArg("auto")) fakeMaskResend  = server.arg("auto") == "1";
    if (server.hasArg("a2"))   fakeA2On        = server.arg("a2") == "1";
    if (server.hasArg("door")) { fakeDoorOpen = server.arg("door") == "1"; if (fakeA2On) sendFakeA2(); }
    if (server.hasArg("dist")) { fakeHelmetCm = server.arg("dist").toFloat(); if (fakeA2On) sendFakeA2(); }

    if (server.hasArg("toggle") && !oldProtocol) {
        int i = server.arg("toggle").toInt();
        if (i >= 0 && i < A3_RELAY_COUNT) { fakeMask = (uint16_t)(fakeMask ^ A3_BIT(i)); maskChanged = true; }
    }
    if (server.hasArg("preset") && !oldProtocol) {
        String p = server.arg("preset");
        if      (p == "off") fakeMask = 0;
        else if (p == "alc") fakeMask = (uint16_t)(A3_MASK_UV | A3_BIT(A3_RELAY_HUMIDIFIER_1) | A3_BIT(A3_RELAY_HUMIDIFIER_3));
        else if (p == "scn") fakeMask = (uint16_t)(A3_MASK_UV | A3_BIT(A3_RELAY_HUMIDIFIER_2) | A3_BIT(A3_RELAY_HUMIDIFIER_3));
        else if (p == "bal") fakeMask = (uint16_t)(A3_MASK_UV | A3_MASK_MIST);
        else if (p == "all") fakeMask = A3_MASK_ALL;
        maskChanged = true;
    }

    // Protocol switch. Going to OLD first clears A3's relay mask with an empty NEW-style mask, so no
    // leftover bit from the NEW session survives, then sends the OLD commands (which flip A3 into its bridge).
    if (server.hasArg("proto")) {
        bool toOld = server.arg("proto") == "old";
        if (toOld && !oldProtocol) {
            oldProtocol = true;
            sendCmd(DEVICE_A3, CMD_SET_RELAY_MASK, 0, 0, 0);
            delay(5);
            sendLegacyRelays();
            if (legacyState != 255) sendLegacyStatus();
        } else if (!toOld && oldProtocol) {
            oldProtocol = false;
            maskChanged = true;   // sends the NEW mask below, which switches A3's bridge off again
        }
    }
    if (server.hasArg("lrelay")) {
        int i = server.arg("lrelay").toInt();
        if (i >= 0 && i < 5) {
            legacyMask = (uint8_t)(legacyMask ^ (1u << i));
            if (oldProtocol) sendLegacyRelays();
        }
    }
    if (server.hasArg("lstate")) {
        int v = server.arg("lstate").toInt();
        legacyState = (v >= 0 && v <= 255) ? (uint8_t)v : 255;
        if (oldProtocol && legacyState != 255) sendLegacyStatus();
    }

    // Any change goes out at once (even with "re-send" off), like S3A sending on every change.
    if (maskChanged && !oldProtocol) sendMask();
    sendState();
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== ESP32_A3_BENCH_TEST (fake S3A, optional fake A2, real-A2 monitor) ===");

    Serial.printf("[BENCH] Last reset: %s\n", resetReasonName());
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD, ESPNOW_WIFI_CHANNEL, 0, 4);
    WiFi.setTxPower(BENCH_TX_POWER);
    Serial.printf("[BENCH] AP '%s' / '%s' -> http://%s\n", AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[BENCH] ESP-NOW init failed - restarting in 3 s");
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
    server.on("/set", HTTP_GET, handleSet);
    server.begin();
    Serial.println("[BENCH] Web UI ready.");
}

void loop() {
    server.handleClient();
    uint32_t now = millis();

    if (fakeHeartbeatOn && now - lastHbMs >= 1000) {
        lastHbMs = now;
        sendCmd(0, CMD_HEARTBEAT, DEVICE_S3A, 0, 0); // exactly what S3A broadcasts
    }
    if (fakeMaskResend) {
        if (!oldProtocol && now - lastMaskMs >= 2000)          sendMask();
        if (oldProtocol && now - lastLegacyRelaysMs >= 2000)  sendLegacyRelays();
    }
    if (oldProtocol && legacyState != 255 && now - lastLegacyStatusMs >= 1000) {
        sendLegacyStatus();
    }
    if (fakeA2On && now - lastA2Ms >= 1000) {
        sendFakeA2();
    }
    delay(2);
}
