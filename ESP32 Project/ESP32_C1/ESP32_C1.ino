#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>
#include "Shared_Common.h"

// -------------------------------------------------------------
// NODE C1 - BOX 2 (HEATER) TERMINAL
// Same role and identical pin mapping as Node A1 - only the
// device ID differs, so it can run as an independent second unit
// alongside Box 1 off the same Master.
// -------------------------------------------------------------

// Hardware Pin Mappings
#define PIN_COIN_SIGNAL 23
#define PIN_BUZZER      21

TFT_eSPI tft = TFT_eSPI();

uint16_t currentBgColor = TFT_NAVY;
bool screenNeedsRefresh = true;
bool pendingHardRefresh = false; // set by CMD_TFT_HARD_REFRESH; consumed by the next repaint

// Screen Elements Data Structures
struct StaticTextItem {
    int16_t  x;
    int16_t  y;
    uint16_t color;
    char     text[32];
    uint8_t  datum;    // Added 2026-09-14 (mirrors Box 1/A1's identical 2026-09-13 addition) -
                        // real TFT_eSPI text-datum constant (TL_DATUM etc.), parsed from an
                        // optional trailing 2-char anchor code. Defaults to TL_DATUM when absent,
                        // matching every screen written before this.
    uint8_t  fontSize;  // Added 2026-09-14 - real TFT_eSPI font number, from an optional trailing
                        // digit after the anchor code. Defaults to 2, matching every existing screen.
};
StaticTextItem staticItems[4];
uint8_t staticItemCount = 0;

struct TimedTextItem {
    int16_t  x;
    int16_t  y;
    uint16_t color;
    char     templateText[32];
    uint16_t blinkIntervalMs;
    unsigned long lastToggle;
    bool     visible;
    char     lastRendered[32];
    uint8_t  datum;    // Added 2026-09-14 - see StaticTextItem.datum
    uint8_t  fontSize; // Added 2026-09-14 - see StaticTextItem.fontSize
};
TimedTextItem timedItems[4];
uint8_t timedItemCount = 0;

struct TouchButtonItem {
    int16_t  x;
    int16_t  y;
    int16_t  w;
    int16_t  h;
    uint16_t color;
    char     label[16];
    char     action[16];
};
TouchButtonItem btnItems[3];
uint8_t btnItemCount = 0;

struct ShapeItem {
    int16_t  x;
    int16_t  y;
    int16_t  w;
    int16_t  h;
    uint16_t color;
    bool     filled;
};
ShapeItem shapeItems[4];
uint8_t shapeItemCount = 0;

// Dynamic Telemetry State
uint32_t currentActiveTimer  = 0;
uint32_t confirmedPulses     = 0;
uint8_t  minCoinsNeeded      = 0; // synced from S3C via CMD_SYNC_VARS.subIndex - for {MINCOINS}
uint8_t  requiredCoinsNeeded = 0; // Added 2026-09-14: synced via CMD_SYNC_VARS.state (previously
                                   // maxCoinsNeeded/{MAXCOINS} - dropped since no Box 2 screen
                                   // ever displayed it) - for Step 5's {REQUIREDCOIN}, mirrors
                                   // Box 1/A1's identical 2026-09-13 addition.

// Added 2026-09-14: Steps 1-2 (Taps/Welcome) advance on ANY tap, not a specific on-screen
// button - set from CMD_STEP_RENDER/CMD_TFT_HARD_REFRESH's state field (1 = any-tap-advances),
// checked in the touch-handling block in loop() ahead of the normal btnItems[] scan. Mirrors
// Box 1/A1's identical addition.
bool tapAnywhereAdvances = false;

// Touch Coordinates & Tracking
uint16_t currentTouchX = 0;
uint16_t currentTouchY = 0;
bool isTouchPressed = false;
unsigned long lastTouchTime = 0;

// Allan Coin Acceptor ISR State
volatile uint32_t isrPulseCount = 0;
volatile unsigned long fallTimeMicros = 0;
volatile bool pulseInProgress = false;
volatile unsigned long lastAcceptedPulseMicros = 0;
const unsigned long COIN_DEBOUNCE_US = 40000; // 40ms per spec - filters contact bounce on a single coin

// 250ms Wireless Lockout State
volatile bool isMuted = false;
volatile unsigned long muteUntilMillis = 0;

// Non-blocking Timers
unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 1000;

// -------------------------------------------------------------
// OUT OF ORDER - shown locally (not via a remote CMD_STEP_RENDER, since by definition S3C isn't
// reachable to send one) whenever this node hasn't heard from S3C for ORPHANED_TIMEOUT_MS. S3C
// owns Box 2's entire cycle now - Master being reachable doesn't matter, since Master no longer
// runs any of it (that's the whole point of the S3C migration - Box 2 survives Master going
// down). lastSeenMaster is still tracked for diagnostics, just not part of this decision.
// -------------------------------------------------------------
unsigned long lastSeenMaster = 0;
unsigned long lastSeenS3C    = 0;
// 15s, not 5s - a stretch of dropped ESP-NOW packets (RF interference, congestion) is a real,
// observed occurrence on this hardware, and 5s was tripping OUT OF ORDER on and off during
// completely normal operation. 15s still catches a genuinely dead S3C within a reasonable
// window, but stops flickering on brief connectivity blips.
const unsigned long ORPHANED_TIMEOUT_MS   = 15000;
const unsigned long OUT_OF_ORDER_BLINK_MS = 500;
bool outOfOrderActive  = false;
bool outOfOrderVisible = false;
unsigned long lastOutOfOrderToggle = 0;

uint16_t parseHexColor(const char* hexStr) {
    if (!hexStr || hexStr[0] != '#') return TFT_WHITE;
    long val = strtol(hexStr + 1, NULL, 16);
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b = val & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Added 2026-09-14 (mirrors Box 1/A1's identical 2026-09-13 addition): maps a 2-char anchor code
// (matching the design tool's own format exactly - vertical then horizontal, e.g. "MC" =
// middle-center) onto TFT_eSPI's real setTextDatum() constants. Anything missing/unrecognized
// falls back to TL_DATUM, so every screen written before this change (no anchor segment at all)
// keeps rendering exactly as it always has.
uint8_t anchorCodeToDatum(const char* code) {
    if (!code || strlen(code) < 2) return TL_DATUM;
    char v = code[0], h = code[1];
    if (v == 'T' && h == 'L') return TL_DATUM;
    if (v == 'T' && h == 'C') return TC_DATUM;
    if (v == 'T' && h == 'R') return TR_DATUM;
    if (v == 'M' && h == 'L') return ML_DATUM;
    if (v == 'M' && h == 'C') return MC_DATUM;
    if (v == 'M' && h == 'R') return MR_DATUM;
    if (v == 'B' && h == 'L') return BL_DATUM;
    if (v == 'B' && h == 'C') return BC_DATUM;
    if (v == 'B' && h == 'R') return BR_DATUM;
    return TL_DATUM;
}

void IRAM_ATTR onCoinChangeISR() {
    unsigned long nowMillis = millis();
    if (isMuted || (nowMillis < muteUntilMillis)) {
        pulseInProgress = false;
        ets_printf("[C1] COIN IGNORED: EMI Noise Lockout Active!\n");
        return;
    }

    unsigned long nowMicros = micros();
    if ((nowMicros - lastAcceptedPulseMicros) < COIN_DEBOUNCE_US) {
        return; // contact bounce within the debounce window - not a new coin
    }

    int pinVal = digitalRead(PIN_COIN_SIGNAL);

    if (pinVal == LOW) {
        lastAcceptedPulseMicros = nowMicros;
        fallTimeMicros = nowMicros;
        pulseInProgress = true;
        isrPulseCount++;
        ets_printf("[C1] ISR PULSE DETECTED! Raw count: %u\n", isrPulseCount);
    } else if (pinVal == HIGH && pulseInProgress) {
        pulseInProgress = false;
    }
}

// Active Buzzer Hardware Control (Immediate Pin Drive)
void triggerBuzzerHardware(uint32_t durationMs) {
    Serial.printf("[C1 ACTION] >>> SOUNDING BUZZER ON PIN 21 FOR %d ms!\n", durationMs);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durationMs);
    digitalWrite(PIN_BUZZER, LOW);
}

void parseDisplayPayload(const char* payload) {
    if (!payload || strlen(payload) < 3) return;

    char buf[200];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    staticItemCount = 0;
    timedItemCount  = 0;
    btnItemCount    = 0;
    shapeItemCount  = 0;

    char* outerSave = nullptr;
    char* token = strtok_r(buf, "|", &outerSave);
    if (token && token[0] == '#') {
        currentBgColor = parseHexColor(token);
        Serial.printf("[C1 ACTION] Setting TFT Screen Background: %s\n", token);
        token = strtok_r(NULL, "|", &outerSave);
    }

    while (token != NULL) {
        // Widened from 8 to 10 (2026-09-14, mirrors Box 1/A1's identical 2026-09-13 change) so
        // ST/TT can carry the two new optional trailing fields (anchor code, font size) on top of
        // what they already used - BTN was already at the old 8-segment ceiling with zero headroom.
        char segs[10][32];
        int sCount = 0;
        char* innerSave = nullptr;
        char* p = strtok_r(token, ",", &innerSave);
        while (p && sCount < 10) {
            strncpy(segs[sCount++], p, 31);
            p = strtok_r(NULL, ",", &innerSave);
        }

        if (sCount > 0) {
            const char* tag = segs[0];
            if (strcmp(tag, "ST") == 0 && sCount >= 5 && staticItemCount < 4) {
                staticItems[staticItemCount].x = atoi(segs[1]);
                staticItems[staticItemCount].y = atoi(segs[2]);
                staticItems[staticItemCount].color = parseHexColor(segs[3]);
                strncpy(staticItems[staticItemCount].text, segs[4], 31);
                staticItems[staticItemCount].datum    = (sCount >= 6) ? anchorCodeToDatum(segs[5]) : TL_DATUM;
                staticItems[staticItemCount].fontSize = (sCount >= 7) ? (uint8_t)atoi(segs[6]) : 2;
                staticItemCount++;
            } else if (strcmp(tag, "TT") == 0 && sCount >= 6 && timedItemCount < 4) {
                timedItems[timedItemCount].x = atoi(segs[1]);
                timedItems[timedItemCount].y = atoi(segs[2]);
                timedItems[timedItemCount].color = parseHexColor(segs[3]);
                strncpy(timedItems[timedItemCount].templateText, segs[4], 31);
                timedItems[timedItemCount].blinkIntervalMs = atoi(segs[5]);
                timedItems[timedItemCount].lastToggle = millis();
                timedItems[timedItemCount].visible = true;
                timedItems[timedItemCount].lastRendered[0] = '\0';
                timedItems[timedItemCount].datum    = (sCount >= 7) ? anchorCodeToDatum(segs[6]) : TL_DATUM;
                timedItems[timedItemCount].fontSize = (sCount >= 8) ? (uint8_t)atoi(segs[7]) : 2;
                timedItemCount++;
            } else if (strcmp(tag, "BTN") == 0 && sCount >= 8 && btnItemCount < 3) {
                btnItems[btnItemCount].x = atoi(segs[1]);
                btnItems[btnItemCount].y = atoi(segs[2]);
                btnItems[btnItemCount].w = atoi(segs[3]);
                btnItems[btnItemCount].h = atoi(segs[4]);
                btnItems[btnItemCount].color = parseHexColor(segs[5]);
                strncpy(btnItems[btnItemCount].label, segs[6], 15);
                strncpy(btnItems[btnItemCount].action, segs[7], 15);
                btnItemCount++;
            } else if (strcmp(tag, "SHP") == 0 && sCount >= 7 && shapeItemCount < 4) {
                shapeItems[shapeItemCount].x = atoi(segs[1]);
                shapeItems[shapeItemCount].y = atoi(segs[2]);
                shapeItems[shapeItemCount].w = atoi(segs[3]);
                shapeItems[shapeItemCount].h = atoi(segs[4]);
                shapeItems[shapeItemCount].color = parseHexColor(segs[5]);
                shapeItems[shapeItemCount].filled = (atoi(segs[6]) == 1);
                shapeItemCount++;
            }
        }
        token = strtok_r(NULL, "|", &outerSave);
    }
    screenNeedsRefresh = true;
}

void interpolateTemplate(const char* tmpl, char* out, size_t maxLen) {
    String s = String(tmpl);
    s.replace("{TIMER}", String(currentActiveTimer));
    s.replace("{COINS}", String(confirmedPulses));
    s.replace("{MINCOINS}", String(minCoinsNeeded));
    s.replace("{REQUIREDCOIN}", String(requiredCoinsNeeded)); // Added 2026-09-14, Step 5
    strncpy(out, s.c_str(), maxLen - 1);
    out[maxLen - 1] = '\0';
}

void drawScreenLayout() {
    tft.fillScreen(currentBgColor);

    // Shapes
    for (uint8_t i = 0; i < shapeItemCount; i++) {
        if (shapeItems[i].filled) {
            tft.fillRect(shapeItems[i].x, shapeItems[i].y, shapeItems[i].w, shapeItems[i].h, shapeItems[i].color);
        } else {
            tft.drawRect(shapeItems[i].x, shapeItems[i].y, shapeItems[i].w, shapeItems[i].h, shapeItems[i].color);
        }
    }

    // Static Text - per-item datum/font (2026-09-14, mirrors Box 1/A1's identical 2026-09-13
    // addition); both default to TL_DATUM/2 at parse time when a screen doesn't specify them, so
    // this is unchanged for every pre-existing screen.
    for (uint8_t i = 0; i < staticItemCount; i++) {
        tft.setTextDatum(staticItems[i].datum);
        tft.setTextColor(staticItems[i].color, currentBgColor);
        tft.drawString(staticItems[i].text, staticItems[i].x, staticItems[i].y, staticItems[i].fontSize);
    }

    // Interactive Buttons
    for (uint8_t i = 0; i < btnItemCount; i++) {
        tft.fillRoundRect(btnItems[i].x, btnItems[i].y, btnItems[i].w, btnItems[i].h, 6, btnItems[i].color);
        tft.drawRoundRect(btnItems[i].x, btnItems[i].y, btnItems[i].w, btnItems[i].h, 6, TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, btnItems[i].color);
        tft.drawString(btnItems[i].label, btnItems[i].x + (btnItems[i].w / 2), btnItems[i].y + (btnItems[i].h / 2), 2);
    }

    for (uint8_t i = 0; i < timedItemCount; i++) {
        timedItems[i].lastRendered[0] = '\0';
    }
}

void drawOutOfOrderText(bool visible) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(visible ? TFT_RED : TFT_BLACK, TFT_BLACK);
    tft.drawString("OUT OF", 160, 95, 4);
    tft.drawString("ORDER", 160, 145, 4);
}

// Call every loop() iteration. Enters/exits OUT OF ORDER and drives its blink independent of
// the normal WYSIWYG screen pipeline (drawScreenLayout()/parseDisplayPayload()) - S3C is the
// only thing that ever sends a real CMD_STEP_RENDER, so if it's unreachable nobody else can
// send one either (Master doesn't drive Box 2's screen at all anymore), and this renders itself.
void tickOutOfOrder(unsigned long currentMillis) {
    // lastSeenS3C is written from the ESP-NOW receive callback, which runs on a different
    // task/core than loop() - if a packet lands and updates it to a timestamp just ahead of
    // this loop() iteration's own currentMillis snapshot, the plain subtraction below would
    // underflow (both unsigned) into a huge number and read as "orphaned" for one tick, then
    // self-correct immediately - exactly the instant flip-then-recover seen in testing, with
    // zero relation to real connectivity. Guarding against currentMillis < lastSeenS3C avoids it.
    bool orphaned = (currentMillis >= lastSeenS3C) && (currentMillis - lastSeenS3C > ORPHANED_TIMEOUT_MS);

    if (orphaned && !outOfOrderActive) {
        outOfOrderActive  = true;
        outOfOrderVisible = true;
        lastOutOfOrderToggle = currentMillis;
        tft.fillScreen(TFT_BLACK);
        drawOutOfOrderText(true);
        Serial.println("[C1] OUT OF ORDER: lost contact with S3C.");
    } else if (!orphaned && outOfOrderActive) {
        outOfOrderActive = false;
        screenNeedsRefresh = true; // let the normal pipeline repaint whatever screen is current
        Serial.println("[C1] Reconnected - clearing OUT OF ORDER.");
    }

    if (outOfOrderActive && (currentMillis - lastOutOfOrderToggle >= OUT_OF_ORDER_BLINK_MS)) {
        lastOutOfOrderToggle = currentMillis;
        outOfOrderVisible = !outOfOrderVisible;
        // Re-clearing the whole background on every blink (not just once on entry) self-heals a
        // torn/interrupted fillScreen() within one blink cycle instead of leaving stale garbage
        // stuck on screen for as long as OUT OF ORDER stays active.
        tft.fillScreen(TFT_BLACK);
        drawOutOfOrderText(outOfOrderVisible);
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

    if (len == sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));
        if (cmd.commandID != CMD_SYNC_VARS && cmd.commandID != CMD_HEARTBEAT) {
            // CMD_SYNC_VARS/CMD_HEARTBEAT fire every 1s regardless of activity - logging them
            // drowns out real events
            Serial.printf("\n[C1 RX] >>> INCOMING COMMAND PACKET! Target: %d | Opcode: %d | Param: %d\n",
                          cmd.targetDeviceID, cmd.commandID, cmd.param16);
        }

        if (cmd.targetDeviceID == DEVICE_C1 || cmd.targetDeviceID == 0) {
            if (cmd.commandID == CMD_BUZZER) {
                triggerBuzzerHardware(cmd.param16 > 0 ? cmd.param16 : 250);
            } else if (cmd.commandID == CMD_SET_COLOR) {
                currentBgColor = cmd.param16;
                screenNeedsRefresh = true;
                Serial.printf("[C1 ACTION] Updated background color to 0x%04X\n", currentBgColor);
            } else if (cmd.commandID == CMD_STEP_RENDER) {
                tapAnywhereAdvances = (cmd.state == 1); // Added 2026-09-14 - see its own declaration
                parseDisplayPayload(cmd.payloadStr);
            } else if (cmd.commandID == CMD_TFT_HARD_REFRESH) {
                pendingHardRefresh = true; // consumed by the repaint parseDisplayPayload() below triggers
                tapAnywhereAdvances = (cmd.state == 1);
                parseDisplayPayload(cmd.payloadStr);
            } else if (cmd.commandID == CMD_RESET_COINS) {
                noInterrupts();
                isrPulseCount = 0;
                interrupts();
                confirmedPulses = 0;
                screenNeedsRefresh = true;
                Serial.println("[C1 ACTION] Coin pulses reset to 0.");
            } else if (cmd.commandID == CMD_MUTE_COINS) {
                isMuted = true;
                muteUntilMillis = millis() + (cmd.param16 > 0 ? cmd.param16 : 250);
                pulseInProgress = false;
                Serial.printf("[C1 LOCKOUT] Muting coin slot for %u ms (Relay EMI protection)\n", cmd.param16 > 0 ? cmd.param16 : 250);
            } else if (cmd.commandID == CMD_SYNC_VARS) {
                currentActiveTimer = cmd.param16;
                minCoinsNeeded = cmd.subIndex; // piggybacked on the otherwise-unused subIndex
                                                // field of this same packet - no new command or
                                                // struct field needed for {MINCOINS}
                requiredCoinsNeeded = cmd.state; // Added 2026-09-14 - same trick, on the
                                                  // otherwise-unused state field, for {REQUIREDCOIN}
                lastSeenS3C = millis(); // CMD_SYNC_VARS is exclusively S3C's traffic to C1, same
                                         // as CMD_HEARTBEAT - counting both makes a single dropped
                                         // packet harmless instead of flickering OUT OF ORDER
            } else if (cmd.commandID == CMD_HEARTBEAT) {
                if (cmd.subIndex == DEVICE_SERVER) lastSeenMaster = millis();
                else if (cmd.subIndex == DEVICE_S3C) lastSeenS3C = millis();
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE C1: BOX 2 (HEATER) TERMINAL INITIALIZING (CORE 3.3.7)");
    Serial.println("=======================================================");

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    pinMode(PIN_COIN_SIGNAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_COIN_SIGNAL), onCoinChangeISR, FALLING);
    Serial.println("[C1] COIN SLOT: Pin 23 configured with INPUT_PULLUP and FALLING ISR.");

    // Initialize ST7789 TFT on SPI (CS=15, DC=2, RST=4, SCK=14, MOSI=13)
    tft.init();
    tft.setRotation(3); // 320x240 Landscape, flipped 180 - this unit's TFT is mounted upside down
    // Real calibration captured at rotation 3 via ESP32_C1_CALIBRATE.ino's touch_calibrate routine.
    uint16_t calData[5] = {408, 3325, 535, 3051, 7};
    tft.setTouch(calData);
    Serial.println("[C1] DISPLAY: ST7789 initialized with touch calibration matrix.");

    // Boot Layout (Step 1/TAPS) - matches S3C's own SCREEN_C1_TAPS exactly (updated 2026-09-14,
    // was the old coin-prompt Welcome screen), so if this device boots before S3C (and misses the
    // one-shot initial broadcast) it shows something correct instead of a stale/wrong screen.
    tapAnywhereAdvances = true;
    parseDisplayPayload("#000000|ST,160,110,#FFFFFF,TAP! to,MC,2|TT,160,136,#FFFF00,CONTINUE,500,MC,4");

    // Force Wi-Fi Channel 1 & ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep periodically powers down the receiver to save power,
                           // even with no AP joined - causes several-second gaps in ESP-NOW
                           // reception on a roughly-periodic cycle otherwise (Master already
                           // disables this; every other node was missing it)
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.println("[C1] WIFI: Forced STA mode on Channel 1.");

    if (esp_now_init() != ESP_OK) {
        Serial.println("[C1] ESP-NOW: Initialization Failed!");
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
    Serial.println("[C1] ESP-NOW: Broadcast peer registered on Channel 1.");

    triggerBuzzerHardware(100);
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Lockout Timer Expiration
    if (isMuted && (currentMillis >= muteUntilMillis)) {
        isMuted = false;
    }

    // 2. Process Debounced Coin Pulses
    noInterrupts();
    uint32_t pulses = isrPulseCount;
    interrupts();

    if (pulses != confirmedPulses) {
        confirmedPulses = pulses;
        Serial.printf("[C1] COIN VALIDATED: +1 Pulse. Total Pulses sent to Master: %u\n", confirmedPulses);
        triggerBuzzerHardware(60);
    }

    // 3. Screen Touch Events & Crosshair Output
    uint16_t tx = 0, ty = 0;
    bool touched = tft.getTouch(&tx, &ty);

    if (touched) {
        currentTouchX = tx;
        currentTouchY = ty;
        isTouchPressed = true;

        Serial.printf("[C1] TOUCH EVENT: Screen Tapped at Raw X: %d, Y: %d\n", tx, ty);
        tft.fillCircle(tx, ty, 2, TFT_RED);
        tft.drawCircle(tx, ty, 6, TFT_WHITE);

        if (currentMillis - lastTouchTime > 350) {
            if (tapAnywhereAdvances) {
                // Added 2026-09-14 (mirrors Box 1/A1's identical addition): Taps/Welcome advance
                // on ANY touch, not a specific button - same CMD_TOUCH_ACTION path as a button
                // hit, just with a fixed action string and no btnItems[] hit-test at all.
                lastTouchTime = currentMillis;
                triggerBuzzerHardware(80);
                Serial.println("[C1] TOUCH MAPPED: Any-tap advance -> Target Action: TAP_ADVANCE");

                CommandPacket pkt;
                memset(&pkt, 0, sizeof(CommandPacket));
                pkt.targetDeviceID = DEVICE_S3C;
                pkt.commandID      = CMD_TOUCH_ACTION;
                strncpy(pkt.payloadStr, "TAP_ADVANCE", sizeof(pkt.payloadStr) - 1);
                esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(CommandPacket));
            } else {
                for (uint8_t i = 0; i < btnItemCount; i++) {
                    if (tx >= btnItems[i].x && tx <= (btnItems[i].x + btnItems[i].w) &&
                        ty >= btnItems[i].y && ty <= (btnItems[i].y + btnItems[i].h)) {

                        lastTouchTime = currentMillis;
                        triggerBuzzerHardware(80);
                        Serial.printf("[C1] TOUCH MAPPED: Screen Button Hit -> Target Action: %s\n", btnItems[i].action);

                        CommandPacket pkt;
                        memset(&pkt, 0, sizeof(CommandPacket));
                        pkt.targetDeviceID = DEVICE_S3C;
                        pkt.commandID      = CMD_TOUCH_ACTION;
                        strncpy(pkt.payloadStr, btnItems[i].action, sizeof(pkt.payloadStr) - 1);
                        esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(CommandPacket));
                        break;
                    }
                }
            }
        }
    } else {
        isTouchPressed = false;
    }

    tickOutOfOrder(currentMillis);

    if (!outOfOrderActive) {
        // 4. Repaint Screen
        if (screenNeedsRefresh) {
            screenNeedsRefresh = false;
            if (pendingHardRefresh) {
                // Only reached when S3C explicitly sent CMD_TFT_HARD_REFRESH, i.e. right after the
                // enclosure lock solenoid engaged (Unlocked->Locked) - that's the one transition
                // observed to sag the shared supply enough to brown out the ST7789 controller
                // itself (powers back up blank/white until re-initialized, not just a corrupted
                // frame). Re-initing on every repaint was tried first and fixed the glitch too,
                // but also flashed the screen on every unrelated action (touches, timers, coin
                // resets) - real fix is decoupling capacitors at the TFT's power pins; this is
                // the narrowly-scoped software stopgap until then.
                pendingHardRefresh = false;
                tft.init();
                tft.setRotation(3); // tft.init() resets rotation to its own default - must
                                     // reapply every time (this unit's TFT is mounted upside
                                     // down, hence 3), or this repaint flips the screen instead
                                     // of fixing it.
            }
            drawScreenLayout();
        }

        // 5. Dynamic Text Interpolation & Blinking Engine
        for (uint8_t i = 0; i < timedItemCount; i++) {
            bool toggleBlink = false;
            if (timedItems[i].blinkIntervalMs > 0) {
                if (currentMillis - timedItems[i].lastToggle >= timedItems[i].blinkIntervalMs) {
                    timedItems[i].lastToggle = currentMillis;
                    timedItems[i].visible = !timedItems[i].visible;
                    toggleBlink = true;
                }
            } else {
                timedItems[i].visible = true;
            }

            char interpolated[32];
            interpolateTemplate(timedItems[i].templateText, interpolated, sizeof(interpolated));

            if (strcmp(interpolated, timedItems[i].lastRendered) != 0 || toggleBlink) {
                // Per-item datum/font (2026-09-14, mirrors Box 1/A1's identical 2026-09-13
                // addition) - erase and redraw both use the same anchor/position so they always
                // cover the same rectangle when text length is stable.
                tft.setTextDatum(timedItems[i].datum);
                if (strlen(timedItems[i].lastRendered) > 0) {
                    tft.setTextColor(currentBgColor, currentBgColor);
                    tft.drawString(timedItems[i].lastRendered, timedItems[i].x, timedItems[i].y, timedItems[i].fontSize);
                }
                if (timedItems[i].visible) {
                    tft.setTextColor(timedItems[i].color, currentBgColor);
                    tft.drawString(interpolated, timedItems[i].x, timedItems[i].y, timedItems[i].fontSize);
                }
                strncpy(timedItems[i].lastRendered, interpolated, 31);
            }
        }
    }

    // 6. 1-Second Telemetry Ping to Master
    if (currentMillis - lastTelemetryMillis >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMillis = currentMillis;

        TelemetryPacket packet;
        memset(&packet, 0, sizeof(TelemetryPacket));
        packet.deviceID     = DEVICE_C1;
        packet.pulseCount   = confirmedPulses;
        packet.touchX       = currentTouchX;
        packet.touchY       = currentTouchY;
        packet.touchPressed = isTouchPressed;
        esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(TelemetryPacket));
    }
}
