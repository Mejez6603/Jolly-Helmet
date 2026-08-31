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

// Screen Elements Data Structures
struct StaticTextItem {
    int16_t  x;
    int16_t  y;
    uint16_t color;
    char     text[32];
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
uint32_t currentActiveTimer = 0;
uint32_t confirmedPulses    = 0;

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

uint16_t parseHexColor(const char* hexStr) {
    if (!hexStr || hexStr[0] != '#') return TFT_WHITE;
    long val = strtol(hexStr + 1, NULL, 16);
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b = val & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
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
        char segs[8][32];
        int sCount = 0;
        char* innerSave = nullptr;
        char* p = strtok_r(token, ",", &innerSave);
        while (p && sCount < 8) {
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

    // Static Text
    tft.setTextDatum(TL_DATUM);
    for (uint8_t i = 0; i < staticItemCount; i++) {
        tft.setTextColor(staticItems[i].color, currentBgColor);
        tft.drawString(staticItems[i].text, staticItems[i].x, staticItems[i].y, 2);
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
        if (cmd.commandID != CMD_SYNC_VARS) {
            // CMD_SYNC_VARS fires every 1s regardless of activity - logging it drowns out real events
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

    // Boot Layout (Step 0) - matches the Master's Box 2 SCREEN_WELCOME, so if this device
    // boots before the Master (and misses the one-shot initial broadcast) it shows
    // something correct instead of a stale/dead screen.
    parseDisplayPayload("#000080|ST,10,15,#FFFFFF,WELCOME|TT,10,60,#FFFF00,Please insert a COIN to Proceed,500");

    // Force Wi-Fi Channel 1 & ESP-NOW
    WiFi.mode(WIFI_STA);
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
            for (uint8_t i = 0; i < btnItemCount; i++) {
                if (tx >= btnItems[i].x && tx <= (btnItems[i].x + btnItems[i].w) &&
                    ty >= btnItems[i].y && ty <= (btnItems[i].y + btnItems[i].h)) {

                    lastTouchTime = currentMillis;
                    triggerBuzzerHardware(80);
                    Serial.printf("[C1] TOUCH MAPPED: Screen Button Hit -> Target Action: %s\n", btnItems[i].action);

                    CommandPacket pkt;
                    memset(&pkt, 0, sizeof(CommandPacket));
                    pkt.targetDeviceID = DEVICE_SERVER;
                    pkt.commandID      = CMD_TOUCH_ACTION;
                    strncpy(pkt.payloadStr, btnItems[i].action, sizeof(pkt.payloadStr) - 1);
                    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(CommandPacket));
                    break;
                }
            }
        }
    } else {
        isTouchPressed = false;
    }

    // 4. Repaint Screen
    if (screenNeedsRefresh) {
        screenNeedsRefresh = false;
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
            tft.setTextDatum(TL_DATUM);
            if (strlen(timedItems[i].lastRendered) > 0) {
                tft.setTextColor(currentBgColor, currentBgColor);
                tft.drawString(timedItems[i].lastRendered, timedItems[i].x, timedItems[i].y, 2);
            }
            if (timedItems[i].visible) {
                tft.setTextColor(timedItems[i].color, currentBgColor);
                tft.drawString(interpolated, timedItems[i].x, timedItems[i].y, 2);
            }
            strncpy(timedItems[i].lastRendered, interpolated, 31);
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
