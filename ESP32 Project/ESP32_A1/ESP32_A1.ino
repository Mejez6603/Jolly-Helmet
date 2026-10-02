#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>
#include "Shared_Common.h"

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
    uint8_t  datum;    // Added 2026-09-13 - real TFT_eSPI text-datum constant (TL_DATUM etc.),
                        // parsed from an optional trailing 2-char anchor code. Defaults to
                        // TL_DATUM when absent, matching every screen written before this.
    uint8_t  fontSize;  // Added 2026-09-13 - real TFT_eSPI font number, from an optional trailing
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
    uint8_t  datum;    // Added 2026-09-13 - see StaticTextItem.datum
    uint8_t  fontSize; // Added 2026-09-13 - see StaticTextItem.fontSize
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

// -------------------------------------------------------------
// Widgets added 2026-10-02 for the new Box 1 procedure (designed in the Box Cycle Designer). At most ONE of
// each per screen, kept apart from the 4/4/3/4 item arrays above. A screen without these tags behaves
// exactly as before.
//   NR2,x,y,w,h,value,textColor,selectedColor,top;bottom   rating grid, 1-5 over 6-10 (value 0 = none picked)
//   NP,x,y,w,h,color,secPerCoin,label[,initialSeconds]      numpad: type MM:SS locally, OK sends the seconds
//   PBAR,x,y,w,h,trackColor,fillColor[,totalSeconds]        fills as the countdown runs
// -------------------------------------------------------------
struct RatingGrid {
    bool     active;
    int16_t  x, y, w, h;
    uint8_t  value;            // 0 = nothing picked yet, otherwise 1..10
    uint16_t textColor, selColor;
    char     topLabel[16], bottomLabel[16];
};
RatingGrid nr2;
int16_t nr2BoxX[10], nr2BoxY[10];   // top-left of each box, worked out once per screen
int16_t nr2CellW = 0, nr2CellH = 0, nr2Gap = 0, nr2CapH = 0;

struct NumpadPanel {
    bool     active;
    int16_t  x, y, w, h;
    uint16_t color;
    uint16_t secPerCoin;
    char     label[32];
    char     digits[5];        // up to 4 typed digits (MMSS); "" = nothing typed yet
    bool     fresh;            // the digits are only a suggestion: the next digit starts a new entry
};
NumpadPanel np;

struct ProgressBar {
    bool     active;
    int16_t  x, y, w, h;
    uint16_t track, fill;
    bool     explicitTotal;    // the screen gave the total seconds; otherwise the biggest remaining time seen
    uint32_t totalSec;
    int16_t  drawnW;           // how many pixels of fill are on screen (-1 = nothing drawn yet)
};
ProgressBar pbar;
uint32_t pbarSeenMax = 0;

char     statusLines[3][32] = {"", "", ""};   // {STATUS}, {STATUS2}, {STATUS3} - newest first, from CMD_STATUS_LINE
uint16_t tokenTry = 0, tokenTries = 0, tokenMaxCoins = 0;   // {TRY}, {TRIES}, {MAXCOINS} from CMD_SET_TOKEN

bool prevTouched = false;                 // for press-edge detection on the widgets
unsigned long lastWidgetTouch = 0;
const unsigned long WIDGET_TOUCH_GAP_MS = 180;

// Dynamic Telemetry State
uint32_t currentActiveTimer   = 0;
uint32_t confirmedPulses      = 0;
uint8_t  minCoinsNeeded       = 0; // synced from S3A via CMD_SYNC_VARS.subIndex - for {MINCOINS}
uint8_t  requiredCoinsNeeded  = 0; // Added 2026-09-13: synced via CMD_SYNC_VARS.state (otherwise
                                    // unused on that command) - for Step 5's {REQUIREDCOIN}

// Added 2026-09-13: Steps 1-2 (Taps/Welcome) advance on ANY tap, not a specific on-screen
// button - set from CMD_STEP_RENDER/CMD_TFT_HARD_REFRESH's state field (1 = any-tap-advances),
// checked in the touch-handling block in loop() ahead of the normal btnItems[] scan.
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
volatile uint32_t isrRawEdges = 0;                   // diagnostic: raw falling edges, before any filter
volatile unsigned long lastRawEdgeMicros = 0;
volatile unsigned long minRawGapMicros = 0xFFFFFFFFUL; // shortest gap (>= 2 ms) between two raw edges

// 250ms Wireless Lockout State
volatile bool isMuted = false;
volatile unsigned long muteUntilMillis = 0;

// Non-blocking Timers
unsigned long lastTelemetryMillis = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 1000;

// -------------------------------------------------------------
// OUT OF ORDER - shown locally (not via a remote CMD_STEP_RENDER, since by definition S3A isn't
// reachable to send one) whenever this node hasn't heard from S3A for ORPHANED_TIMEOUT_MS. S3A
// owns Box 1's entire cycle now - Master being reachable doesn't matter, since Master no longer
// runs any of it (that's the whole point of the S3A migration - Box 1 survives Master going
// down). lastSeenMaster is still tracked for diagnostics, just not part of this decision.
// -------------------------------------------------------------
unsigned long lastSeenMaster = 0;
unsigned long lastSeenS3A    = 0;
// 15s, not 5s - a stretch of dropped ESP-NOW packets (RF interference, congestion) is a real,
// observed occurrence on this hardware, and 5s was tripping OUT OF ORDER on and off during
// completely normal operation. 15s still catches a genuinely dead S3A within a reasonable
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

// Added 2026-09-13: maps a 2-char anchor code (matching the design tool's own format exactly -
// vertical then horizontal, e.g. "MC" = middle-center) onto TFT_eSPI's real setTextDatum()
// constants. Anything missing/unrecognized falls back to TL_DATUM, so every screen written
// before this change (no anchor segment at all) keeps rendering exactly as it always has.
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
    // Diagnostic (2026-10-02): every raw falling edge on the coin wire is counted BEFORE the mute and debounce
    // filters below, with the shortest real gap between two edges (>= 2 ms, to ignore contact bounce). Reported
    // in the telemetry so a bench page can show "edges the wire produced" next to "pulses A1 accepted".
    unsigned long rawNow = micros();
    isrRawEdges = isrRawEdges + 1;
    unsigned long rawGap = rawNow - lastRawEdgeMicros;
    if (isrRawEdges > 1 && rawGap >= 2000UL && rawGap < minRawGapMicros) minRawGapMicros = rawGap;
    lastRawEdgeMicros = rawNow;

    unsigned long nowMillis = millis();
    if (isMuted || (nowMillis < muteUntilMillis)) {
        pulseInProgress = false;
        ets_printf("[A1] COIN IGNORED: EMI Noise Lockout Active!\n");
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
        ets_printf("[A1] ISR PULSE DETECTED! Raw count: %u\n", isrPulseCount);
    } else if (pinVal == HIGH && pulseInProgress) {
        pulseInProgress = false;
    }
}

// Active Buzzer Hardware Control (Immediate Pin Drive)
void triggerBuzzerHardware(uint32_t durationMs) {
    Serial.printf("[A1 ACTION] >>> SOUNDING BUZZER ON PIN 21 FOR %d ms!\n", durationMs);
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
    nr2.active  = false;
    np.active   = false;
    pbar.active = false;
    pbarSeenMax = currentActiveTimer; // a bar that follows the countdown starts from whatever is left right now

    char* outerSave = nullptr;
    char* token = strtok_r(buf, "|", &outerSave);
    if (token && token[0] == '#') {
        currentBgColor = parseHexColor(token);
        Serial.printf("[A1 ACTION] Setting TFT Screen Background: %s\n", token);
        token = strtok_r(NULL, "|", &outerSave);
    }

    while (token != NULL) {
        // Widened from 8 to 10 (2026-09-13) so ST/TT can carry the two new optional trailing
        // fields (anchor code, font size) on top of what they already used - BTN was already
        // at the old 8-segment ceiling with zero headroom before this.
        char segs[10][32];
        int sCount = 0;
        char* innerSave = nullptr;
        char* p = strtok_r(token, ",", &innerSave);
        while (p && sCount < 10) {
            strncpy(segs[sCount], p, 31);
            segs[sCount][31] = '\0';   // strncpy leaves a 31+ character field unterminated
            sCount++;
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
            } else if (strcmp(tag, "NR2") == 0 && sCount >= 8 && !nr2.active) {
                nr2.x = atoi(segs[1]);
                nr2.y = atoi(segs[2]);
                nr2.w = atoi(segs[3]);
                nr2.h = atoi(segs[4]);
                int v = atoi(segs[5]);
                nr2.value     = (v < 0) ? 0 : (v > 10 ? 10 : (uint8_t)v);
                nr2.textColor = parseHexColor(segs[6]);
                nr2.selColor  = parseHexColor(segs[7]);
                nr2.topLabel[0] = '\0';
                nr2.bottomLabel[0] = '\0';
                if (sCount >= 9) {   // "top;bottom" - the two captions travel as one comma-free field
                    const char* semi = strchr(segs[8], ';');
                    if (semi) {
                        size_t n = (size_t)(semi - segs[8]);
                        if (n > 15) n = 15;
                        memcpy(nr2.topLabel, segs[8], n);
                        nr2.topLabel[n] = '\0';
                        strncpy(nr2.bottomLabel, semi + 1, 15);
                        nr2.bottomLabel[15] = '\0';
                    } else {
                        strncpy(nr2.topLabel, segs[8], 15);
                        nr2.topLabel[15] = '\0';
                    }
                }
                computeNr2Layout();
                nr2.active = true;
            } else if (strcmp(tag, "NP") == 0 && sCount >= 7 && !np.active) {
                np.x = atoi(segs[1]);
                np.y = atoi(segs[2]);
                np.w = atoi(segs[3]);
                np.h = atoi(segs[4]);
                np.color = parseHexColor(segs[5]);
                int spc = atoi(segs[6]);
                np.secPerCoin = (spc < 1) ? 1 : (uint16_t)spc;
                strncpy(np.label, (sCount >= 8) ? segs[7] : "Pick Your Time", 31);
                np.label[31] = '\0';
                np.digits[0] = '\0';
                np.fresh = false;
                if (sCount >= 9) {   // optional suggested time in seconds, shown until the customer types
                    long init = atol(segs[8]);
                    if (init > 5999) init = 5999;
                    if (init > 0) {
                        snprintf(np.digits, sizeof(np.digits), "%02ld%02ld", init / 60, init % 60);
                        np.fresh = true;
                    }
                }
                np.active = true;
                computeNpLayout();
            } else if (strcmp(tag, "PBAR") == 0 && sCount >= 7 && !pbar.active) {
                pbar.x = atoi(segs[1]);
                pbar.y = atoi(segs[2]);
                pbar.w = atoi(segs[3]);
                pbar.h = atoi(segs[4]);
                pbar.track = parseHexColor(segs[5]);
                pbar.fill  = parseHexColor(segs[6]);
                pbar.totalSec = (sCount >= 8) ? (uint32_t)atol(segs[7]) : 0;
                pbar.explicitTotal = (pbar.totalSec > 0);
                pbar.drawnW = -1;
                pbar.active = true;
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
    s.replace("{REQUIREDCOIN}", String(requiredCoinsNeeded)); // Added 2026-09-13, Step 5

    // Added 2026-10-02 for the new Box 1 procedure. {MINUTE}:{SECOND} is the remaining time as 05:00.
    char two[12];
    snprintf(two, sizeof(two), "%02lu", (unsigned long)(currentActiveTimer / 60));
    s.replace("{MINUTE}", two);
    snprintf(two, sizeof(two), "%02lu", (unsigned long)(currentActiveTimer % 60));
    s.replace("{SECOND}", two);
    s.replace("{MAXCOINS}", String(tokenMaxCoins));
    s.replace("{TRIES}", String(tokenTries));
    s.replace("{TRY}", String(tokenTry));
    s.replace("{STATUS2}", statusLines[1]);
    s.replace("{STATUS3}", statusLines[2]);
    s.replace("{STATUS}", statusLines[0]);

    strncpy(out, s.c_str(), maxLen - 1);
    out[maxLen - 1] = '\0';
}

// A touch result for S3A. value rides in param16: the rating (NR2 screens), the seconds ("NP_OK"), else 0.
void sendTouchAction(const char* action, uint16_t value) {
    CommandPacket pkt;
    memset(&pkt, 0, sizeof(CommandPacket));
    pkt.targetDeviceID = DEVICE_S3A;
    pkt.commandID      = CMD_TOUCH_ACTION;
    pkt.param16        = value;
    strncpy(pkt.payloadStr, action, sizeof(pkt.payloadStr) - 1);
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(CommandPacket));
}

// -------------------------------------------------------------
// Rating grid (NR2): two rows of five boxes, 1-5 over 6-10. Same arithmetic as the Box Cycle Designer's
// preview, so what is drawn there is what shows up here. A tap only selects a box; the screen's own button
// (CONFIRM) is what sends the rating, via sendTouchAction()'s value.
// -------------------------------------------------------------
void computeNr2Layout() {
    int capH = (nr2.h * 9) / 100;
    if (capH < 10) capH = 10;
    if (capH > 18) capH = 18;
    int topH = nr2.topLabel[0] ? capH : 0;
    int botH = nr2.bottomLabel[0] ? capH : 0;
    int gap = (nr2.w * 25) / 1000;
    if (gap < 3) gap = 3;
    if (gap > 8) gap = 8;
    int rowH = (nr2.h - topH - botH - gap) / 2;
    if (rowH < 8) rowH = 8;
    int cellW = (nr2.w - gap * 4) / 5;
    if (cellW < 6) cellW = 6;
    nr2CapH = capH;
    nr2Gap = gap;
    nr2CellW = cellW;
    nr2CellH = rowH;
    int gridTop = nr2.y + topH;
    for (uint8_t i = 0; i < 10; i++) {
        nr2BoxX[i] = nr2.x + (i % 5) * (cellW + gap);
        nr2BoxY[i] = gridTop + (i / 5) * (rowH + gap);
    }
}

void drawNr2Box(uint8_t i) {
    bool sel = (nr2.value == (uint8_t)(i + 1));
    uint16_t fillC = sel ? nr2.selColor : tft.color565(18, 18, 18);
    uint16_t edgeC = sel ? nr2.selColor : tft.color565(89, 89, 89);
    tft.fillRoundRect(nr2BoxX[i], nr2BoxY[i], nr2CellW, nr2CellH, 2, fillC);
    tft.drawRoundRect(nr2BoxX[i], nr2BoxY[i], nr2CellW, nr2CellH, 2, edgeC);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(sel ? nr2.textColor : tft.color565(204, 204, 204), fillC);
    uint8_t font = (nr2CellH >= 30) ? 4 : ((nr2CellH >= 18) ? 2 : 1);
    tft.drawNumber(i + 1, nr2BoxX[i] + nr2CellW / 2, nr2BoxY[i] + nr2CellH / 2, font);
}

void drawNr2All() {
    for (uint8_t i = 0; i < 10; i++) drawNr2Box(i);
    uint8_t capFont = (nr2CapH >= 14) ? 2 : 1;
    tft.setTextColor(tft.color565(180, 180, 180), currentBgColor);
    if (nr2.topLabel[0]) {
        tft.setTextDatum(ML_DATUM);
        tft.drawString(nr2.topLabel, nr2.x, nr2.y + nr2CapH / 2, capFont);
    }
    if (nr2.bottomLabel[0]) {
        tft.setTextDatum(MR_DATUM);
        tft.drawString(nr2.bottomLabel, nr2.x + nr2.w, nr2BoxY[9] + nr2CellH + nr2CapH / 2, capFont);
    }
}

// Which box (0-9) a touch lands on, or -1. The gaps between boxes count for the nearer box so no tap is lost.
int8_t nr2HitTest(uint16_t tx, uint16_t ty) {
    int pad = nr2Gap / 2;
    for (uint8_t i = 0; i < 10; i++) {
        if ((int)tx + pad >= nr2BoxX[i] && (int)tx <= nr2BoxX[i] + nr2CellW + pad &&
            (int)ty + pad >= nr2BoxY[i] && (int)ty <= nr2BoxY[i] + nr2CellH + pad) return (int8_t)i;
    }
    return -1;
}

// -------------------------------------------------------------
// Numpad (NP): MM:SS entry with a live "N coin(s) needed" line. All typing happens here; only the result
// leaves the box (NP_OK with the seconds, NP_BACK, NP_CANCEL).
// -------------------------------------------------------------
const int NP_HEADER_H = 20, NP_READOUT_H = 26, NP_HINT_H = 16, NP_CANCEL_H = 22, NP_GAP = 4;
int npGridTop = 0, npCellW = 0, npCellH = 0, npBarY = 0, npHalfW = 0, npBackX = 0, npCancelX = 0;

void computeNpLayout() {
    npGridTop = np.y + NP_HEADER_H + NP_READOUT_H + NP_HINT_H + NP_GAP;
    int gridH = (np.y + np.h - NP_CANCEL_H - NP_GAP) - npGridTop;
    if (gridH < 30) gridH = 30;
    npCellW = np.w / 3;
    npCellH = gridH / 4;
    npBarY = np.y + np.h - NP_CANCEL_H - 2;
    npHalfW = (np.w - 12 - 4) / 2;
    npBackX = np.x + 6;
    npCancelX = npBackX + npHalfW + 4;
}

// The typed digits (right-aligned into MMSS) as a total in seconds, capped at 99:59.
uint16_t npTotalSeconds() {
    char padded[5] = {'0', '0', '0', '0', '\0'};
    size_t n = strlen(np.digits);
    if (n > 4) n = 4;
    for (size_t k = 0; k < n; k++) padded[4 - n + k] = np.digits[k];
    long mm = (padded[0] - '0') * 10 + (padded[1] - '0');
    long ss = (padded[2] - '0') * 10 + (padded[3] - '0');
    long total = mm * 60 + ss;   // 90 typed seconds roll over into minutes, like a microwave
    if (total > 5999) total = 5999;
    return (uint16_t)total;
}

void drawNumpadReadout() {
    uint16_t panelBg = tft.color565(10, 10, 14);
    uint16_t total = npTotalSeconds();
    int ry = np.y + NP_HEADER_H;
    tft.fillRect(np.x + 3, ry, np.w - 6, NP_READOUT_H + NP_HINT_H, panelBg);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(total / 60), (unsigned)(total % 60));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(np.color, panelBg);
    tft.drawString(buf, np.x + np.w / 2, ry + NP_READOUT_H / 2 + 2, 4);
    char hint[32];
    if (total > 0) {
        unsigned coins = ((unsigned)total + np.secPerCoin - 1) / np.secPerCoin;
        snprintf(hint, sizeof(hint), "%u coin(s) needed", coins);
    } else {
        snprintf(hint, sizeof(hint), "Enter MM:SS then OK");
    }
    tft.setTextColor(tft.color565(204, 204, 204), panelBg);
    tft.drawString(hint, np.x + np.w / 2, ry + NP_READOUT_H + NP_HINT_H / 2 + 1, 2);
}

void drawNumpad() {
    uint16_t panelBg = tft.color565(10, 10, 14);
    tft.fillRoundRect(np.x, np.y, np.w, np.h, 8, panelBg);
    tft.drawRect(np.x + 1, np.y + 1, np.w - 2, np.h - 2, np.color);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, panelBg);
    tft.drawString(np.label, np.x + np.w / 2, np.y + NP_HEADER_H / 2 + 2, 2);
    drawNumpadReadout();

    static const char* const keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "OK"};
    uint8_t keyFont = (npCellH >= 26) ? 4 : 2;
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < 12; i++) {
        int kx = np.x + (i % 3) * npCellW;
        int ky = npGridTop + (i / 3) * npCellH;
        uint16_t kc = (i == 11) ? tft.color565(17, 62, 36) : ((i == 9) ? tft.color565(60, 23, 26) : tft.color565(27, 27, 30));
        tft.fillRect(kx + 2, ky + 2, npCellW - 4, npCellH - 4, kc);
        tft.setTextColor(TFT_WHITE, kc);
        tft.drawString(keys[i], kx + npCellW / 2, ky + npCellH / 2, keyFont);
    }

    uint16_t barC = tft.color565(27, 27, 30);
    tft.fillRect(npBackX, npBarY, npHalfW, NP_CANCEL_H, barC);
    tft.fillRect(npCancelX, npBarY, npHalfW, NP_CANCEL_H, barC);
    tft.setTextColor(TFT_WHITE, barC);
    tft.drawString("BACK", npBackX + npHalfW / 2, npBarY + NP_CANCEL_H / 2, 2);
    tft.drawString("CANCEL", npCancelX + npHalfW / 2, npBarY + NP_CANCEL_H / 2, 2);
}

// 0-11 = keypad key (digits as "1".."9", 9 = DEL, 10 = "0", 11 = OK), 20 = BACK, 21 = CANCEL, -1 = a miss.
int npHitTest(uint16_t tx, uint16_t ty) {
    int x = tx, y = ty;
    if (y >= npBarY && y <= npBarY + NP_CANCEL_H) {
        if (x >= npBackX && x <= npBackX + npHalfW) return 20;
        if (x >= npCancelX && x <= npCancelX + npHalfW) return 21;
        return -1;
    }
    if (y >= npGridTop && y < npGridTop + npCellH * 4 && x >= np.x && x < np.x + npCellW * 3) {
        return ((y - npGridTop) / npCellH) * 3 + ((x - np.x) / npCellW);
    }
    return -1;
}

// -------------------------------------------------------------
// Progress bar (PBAR). Fills as the countdown runs. The total is the screen's own 8th field if it gave one,
// otherwise the biggest remaining time seen since this screen appeared (coins added mid-cycle make it grow,
// and the bar eases back a little - there is more time to go).
// -------------------------------------------------------------
int16_t progressFillWidth() {
    uint32_t total = pbar.explicitTotal ? pbar.totalSec : pbarSeenMax;
    if (total == 0) return 0;
    uint32_t rem = currentActiveTimer;
    if (rem > total) rem = total;
    return (int16_t)(((uint32_t)pbar.w * (total - rem)) / total);
}

void drawProgressBarFull() {
    tft.fillRect(pbar.x, pbar.y, pbar.w, pbar.h, pbar.track);
    int16_t fw = progressFillWidth();
    if (fw > 0) tft.fillRect(pbar.x, pbar.y, fw, pbar.h, pbar.fill);
    pbar.drawnW = fw;
}

// Call every loop(): paints only the NEW slice of fill, so the bar never flickers.
void updateProgressBar() {
    if (!pbar.active) return;
    if (!pbar.explicitTotal && currentActiveTimer > pbarSeenMax) pbarSeenMax = currentActiveTimer;
    int16_t fw = progressFillWidth();
    if (fw == pbar.drawnW) return;
    if (pbar.drawnW >= 0 && fw > pbar.drawnW) {
        tft.fillRect(pbar.x + pbar.drawnW, pbar.y, fw - pbar.drawnW, pbar.h, pbar.fill);
        pbar.drawnW = fw;
    } else {
        drawProgressBarFull();
    }
}

// A press on one of the widgets. True if the widget used it (so the button scan must skip it).
bool handleWidgetTouch(uint16_t tx, uint16_t ty) {
    if (nr2.active) {
        int8_t hit = nr2HitTest(tx, ty);
        if (hit >= 0) {
            nr2.value = (uint8_t)(hit + 1);
            drawNr2All();
            triggerBuzzerHardware(30);
            Serial.printf("[A1] RATING GRID: picked %u\n", nr2.value);
            return true;
        }
    }
    if (np.active) {
        int code = npHitTest(tx, ty);
        if (code < 0) return false;
        if (code == 9) {   // DEL: edits the suggestion itself too
            size_t n = strlen(np.digits);
            if (n > 0) np.digits[n - 1] = '\0';
            np.fresh = false;
            drawNumpadReadout();
        } else if (code <= 10) {   // a digit
            char d = (code == 10) ? '0' : (char)('1' + code);
            if (np.fresh) { np.digits[0] = '\0'; np.fresh = false; }   // typing replaces a suggestion
            size_t n = strlen(np.digits);
            if (n >= 4) { memmove(np.digits, np.digits + 1, 3); n = 3; }   // phone-keypad rollover
            np.digits[n] = d;
            np.digits[n + 1] = '\0';
            drawNumpadReadout();
        } else if (code == 11) {   // OK
            uint16_t total = npTotalSeconds();
            if (total == 0) { triggerBuzzerHardware(20); return true; }   // nothing typed yet
            Serial.printf("[A1] NUMPAD: OK with %u seconds\n", (unsigned)total);
            sendTouchAction("NP_OK", total);
        } else if (code == 20) {
            sendTouchAction("NP_BACK", 0);
        } else if (code == 21) {
            sendTouchAction("NP_CANCEL", 0);
        }
        triggerBuzzerHardware(30);
        return true;
    }
    return false;
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

    // Static Text - per-item datum/font (2026-09-13); both default to TL_DATUM/2 at parse time
    // when a screen doesn't specify them, so this is unchanged for every pre-existing screen.
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

    // Widgets (2026-10-02) go on top. The numpad paints its own panel; the bar starts from nothing drawn.
    if (nr2.active) drawNr2All();
    if (np.active)  drawNumpad();
    if (pbar.active) drawProgressBarFull();

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
// the normal WYSIWYG screen pipeline (drawScreenLayout()/parseDisplayPayload()) - S3A is the
// only thing that ever sends a real CMD_STEP_RENDER, so if it's unreachable nobody else can
// send one either (Master doesn't drive Box 1's screen at all anymore), and this renders itself.
void tickOutOfOrder(unsigned long currentMillis) {
    // lastSeenS3A is written from the ESP-NOW receive callback, which runs on a different
    // task/core than loop() - if a packet lands and updates it to a timestamp just ahead of
    // this loop() iteration's own currentMillis snapshot, the plain subtraction below would
    // underflow (both unsigned) into a huge number and read as "orphaned" for one tick, then
    // self-correct immediately - exactly the instant flip-then-recover seen in testing on Box 2,
    // with zero relation to real connectivity. Guarding against currentMillis < lastSeenS3A
    // avoids it (same fix applied to C1).
    bool orphaned = (currentMillis >= lastSeenS3A) && (currentMillis - lastSeenS3A > ORPHANED_TIMEOUT_MS);

    if (orphaned && !outOfOrderActive) {
        outOfOrderActive  = true;
        outOfOrderVisible = true;
        lastOutOfOrderToggle = currentMillis;
        tft.fillScreen(TFT_BLACK);
        drawOutOfOrderText(true);
        Serial.println("[A1] OUT OF ORDER: lost contact with S3A.");
    } else if (!orphaned && outOfOrderActive) {
        outOfOrderActive = false;
        screenNeedsRefresh = true; // let the normal pipeline repaint whatever screen is current
        Serial.println("[A1] Reconnected - clearing OUT OF ORDER.");
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
            Serial.printf("\n[A1 RX] >>> INCOMING COMMAND PACKET! Target: %d | Opcode: %d | Param: %d\n",
                          cmd.targetDeviceID, cmd.commandID, cmd.param16);
        }

        if (cmd.targetDeviceID == DEVICE_A1 || cmd.targetDeviceID == 0) {
            if (cmd.commandID == CMD_BUZZER) {
                triggerBuzzerHardware(cmd.param16 > 0 ? cmd.param16 : 250);
            } else if (cmd.commandID == CMD_SET_COLOR) {
                currentBgColor = cmd.param16;
                screenNeedsRefresh = true;
                Serial.printf("[A1 ACTION] Updated background color to 0x%04X\n", currentBgColor);
            } else if (cmd.commandID == CMD_STEP_RENDER) {
                tapAnywhereAdvances = (cmd.state == 1); // Added 2026-09-13 - see its own declaration
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
                Serial.println("[A1 ACTION] Coin pulses reset to 0.");
            } else if (cmd.commandID == CMD_MUTE_COINS) {
                isMuted = true;
                muteUntilMillis = millis() + (cmd.param16 > 0 ? cmd.param16 : 250);
                pulseInProgress = false;
                Serial.printf("[A1 LOCKOUT] Muting coin slot for %u ms (Relay EMI protection)\n", cmd.param16 > 0 ? cmd.param16 : 250);
            } else if (cmd.commandID == CMD_SYNC_VARS) {
                currentActiveTimer = cmd.param16;
                minCoinsNeeded = cmd.subIndex; // piggybacked on the otherwise-unused subIndex
                                                // field of this same packet - no new command
                                                // or struct field needed for {MINCOINS}
                requiredCoinsNeeded = cmd.state; // Added 2026-09-13 - same trick, on the
                                                  // otherwise-unused state field, for {REQUIREDCOIN}
                lastSeenS3A = millis(); // CMD_SYNC_VARS is exclusively S3A's traffic to A1, same
                                         // as CMD_HEARTBEAT - counting both makes a single dropped
                                         // packet harmless instead of flickering OUT OF ORDER
            } else if (cmd.commandID == CMD_HEARTBEAT) {
                if (cmd.subIndex == DEVICE_SERVER) lastSeenMaster = millis();
                else if (cmd.subIndex == DEVICE_S3A) lastSeenS3A = millis();
            } else if (cmd.commandID == CMD_STATUS_LINE) {
                // Added 2026-10-02: one line of the cleaning "kill feed" for {STATUS}/{STATUS2}/{STATUS3}.
                if (cmd.subIndex < 3) {
                    cmd.payloadStr[31] = '\0';   // A1 keeps 31 characters per line, whatever arrived
                    strncpy(statusLines[cmd.subIndex], cmd.payloadStr, 31);
                    statusLines[cmd.subIndex][31] = '\0';
                }
                lastSeenS3A = millis();   // S3A-only traffic, like CMD_SYNC_VARS
            } else if (cmd.commandID == CMD_SET_TOKEN) {
                // Added 2026-10-02: plain numbers for {TRY}, {TRIES}, {MAXCOINS}.
                if (cmd.subIndex == TOKEN_TRY)           tokenTry = cmd.param16;
                else if (cmd.subIndex == TOKEN_TRIES)    tokenTries = cmd.param16;
                else if (cmd.subIndex == TOKEN_MAXCOINS) tokenMaxCoins = cmd.param16;
                lastSeenS3A = millis();
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=======================================================");
    Serial.println("   NODE A1: USER TERMINAL INITIALIZING (CORE 3.3.7)");
    Serial.println("=======================================================");

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    pinMode(PIN_COIN_SIGNAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_COIN_SIGNAL), onCoinChangeISR, FALLING);
    Serial.println("[A1] COIN SLOT: Pin 23 configured with INPUT_PULLUP and FALLING ISR.");

    // Initialize ST7789 TFT on SPI (CS=15, DC=2, RST=4, SCK=14, MOSI=13)
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    uint16_t calData[5] = {327, 3421, 285, 3510, 1};
    tft.setTouch(calData);
    Serial.println("[A1] DISPLAY: ST7789 initialized with touch calibration matrix.");
    
    // Boot Layout (Step 1/TAPS) - matches S3A's own SCREEN_TAPS exactly (updated 2026-09-13, was
    // the old coin-prompt Welcome screen), so if this device boots before S3A (and misses the
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
    Serial.println("[A1] WIFI: Forced STA mode on Channel 1.");

    if (esp_now_init() != ESP_OK) {
        Serial.println("[A1] ESP-NOW: Initialization Failed!");
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
    Serial.println("[A1] ESP-NOW: Broadcast peer registered on Channel 1.");

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
        Serial.printf("[A1] COIN VALIDATED: +1 Pulse. Total Pulses sent to Master: %u\n", confirmedPulses);
        triggerBuzzerHardware(60);
    }

    // 3. Screen Touch Events & Crosshair Output
    uint16_t tx = 0, ty = 0;
    bool touched = tft.getTouch(&tx, &ty);

    if (touched) {
        currentTouchX = tx;
        currentTouchY = ty;
        isTouchPressed = true;

        Serial.printf("[A1] TOUCH EVENT: Screen Tapped at Raw X: %d, Y: %d\n", tx, ty);
        // The red crosshair is a debug aid that stays until the next full repaint; the rating grid and
        // numpad redraw themselves on every tap, so they would just collect crosshairs.
        if (!nr2.active && !np.active) {
            tft.fillCircle(tx, ty, 2, TFT_RED);
            tft.drawCircle(tx, ty, 6, TFT_WHITE);
        }

        // Widgets (2026-10-02) act on a fresh press only, with a short gap, so holding a finger down does
        // not type the same digit again. A press they use is not also offered to the buttons.
        bool widgetUsedTouch = false;
        if (!prevTouched && !outOfOrderActive && (nr2.active || np.active) &&
            (currentMillis - lastWidgetTouch > WIDGET_TOUCH_GAP_MS)) {
            if (handleWidgetTouch(tx, ty)) {
                widgetUsedTouch = true;
                lastWidgetTouch = currentMillis;
                lastTouchTime = currentMillis;
            }
        }

        if (!widgetUsedTouch && currentMillis - lastTouchTime > 350) {
            if (tapAnywhereAdvances) {
                // Added 2026-09-13: Taps/Welcome advance on ANY touch, not a specific button -
                // same CMD_TOUCH_ACTION path as a button hit, just with a fixed action string
                // and no btnItems[] hit-test at all.
                lastTouchTime = currentMillis;
                triggerBuzzerHardware(80);
                Serial.println("[A1] TOUCH MAPPED: Any-tap advance -> Target Action: TAP_ADVANCE");

                CommandPacket pkt;
                memset(&pkt, 0, sizeof(CommandPacket));
                pkt.targetDeviceID = DEVICE_S3A;
                pkt.commandID      = CMD_TOUCH_ACTION;
                strncpy(pkt.payloadStr, "TAP_ADVANCE", sizeof(pkt.payloadStr) - 1);
                esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(CommandPacket));
            } else {
                for (uint8_t i = 0; i < btnItemCount; i++) {
                    if (tx >= btnItems[i].x && tx <= (btnItems[i].x + btnItems[i].w) &&
                        ty >= btnItems[i].y && ty <= (btnItems[i].y + btnItems[i].h)) {

                        lastTouchTime = currentMillis;
                        triggerBuzzerHardware(80);
                        Serial.printf("[A1] TOUCH MAPPED: Screen Button Hit -> Target Action: %s\n", btnItems[i].action);

                        CommandPacket pkt;
                        memset(&pkt, 0, sizeof(CommandPacket));
                        pkt.targetDeviceID = DEVICE_S3A; // S3A owns Box 1's state machine now, not Local Server
                        pkt.commandID      = CMD_TOUCH_ACTION;
                        pkt.param16        = nr2.active ? nr2.value : 0; // rating picked on a rating-grid screen
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
    prevTouched = touched;

    tickOutOfOrder(currentMillis);

    if (!outOfOrderActive) {
        // 4. Repaint Screen
        if (screenNeedsRefresh) {
            screenNeedsRefresh = false;
            if (pendingHardRefresh) {
                // Only reached when S3A explicitly sent CMD_TFT_HARD_REFRESH, i.e. right after the
                // enclosure lock solenoid engaged (Unlocked->Locked) - that's the one transition
                // observed to sag the shared supply enough to brown out the ST7789 controller
                // itself (powers back up blank/white until re-initialized, not just a corrupted
                // frame). Re-initing on every repaint was tried first and fixed the glitch too,
                // but also flashed the screen on every unrelated action (touches, timers, coin
                // resets) - real fix is decoupling capacitors at the TFT's power pins; this is
                // the narrowly-scoped software stopgap until then.
                pendingHardRefresh = false;
                tft.init();
                tft.setRotation(1); // tft.init() resets rotation to its own default - must
                                     // reapply every time, or this repaint flips the screen
                                     // instead of fixing it.
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
                // Per-item datum/font (2026-09-13, see StaticTextItem.datum's comment) - erase
                // and redraw both use the same anchor/position so they always cover the same
                // rectangle when text length is stable. A live value that both shrinks in width
                // AND uses a Middle/Right anchor could in theory leave a thin unerased sliver
                // until the next full screen redraw - not a concern for any current live value,
                // since {COINS}/{TIMER} only ever count in one direction during a single screen.
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

        updateProgressBar(); // 2026-10-02: fills as the countdown runs (does nothing without a PBAR)
    }

    // 6. 1-Second Telemetry Ping to Master
    if (currentMillis - lastTelemetryMillis >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMillis = currentMillis;

        TelemetryPacket packet;
        memset(&packet, 0, sizeof(TelemetryPacket));
        packet.deviceID     = DEVICE_A1;
        packet.pulseCount   = confirmedPulses;
        packet.touchX       = currentTouchX;
        packet.touchY       = currentTouchY;
        packet.touchPressed = isTouchPressed;
        // Coin-wire diagnostic riding on two fields A1 never uses (see Shared_Common.h's TelemetryPacket comments)
        packet.activeTimer      = isrRawEdges;
        packet.usAlcoholDistance = (minRawGapMicros == 0xFFFFFFFFUL) ? 0.0f : (float)minRawGapMicros / 1000.0f;
        esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(TelemetryPacket));
    }
}