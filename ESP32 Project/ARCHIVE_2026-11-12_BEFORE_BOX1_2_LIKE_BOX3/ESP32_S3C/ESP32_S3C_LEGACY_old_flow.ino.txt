#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "Shared_Common.h"

// -------------------------------------------------------------
// NODE S3C - BOX 2 INTERNAL SERVER MANAGEMENT (ESP32-S3)
//
// Owns and RUNS Box 2's entire 15-step cycle (taps -> welcome -> rate A ->
// time allot -> insert coin -> instructions -> sensors -> heating -> cool
// down -> rate B -> retrieve -> finish) directly against C1/C2, with zero
// dependency on Local Server being reachable. Mirrors ESP32_S3A.ino's role
// for Box 1, minus the alcohol check/refill steps and AUTO_CALL - Box 2
// (Heater) has no alcohol system to check or refill.
//
// Local Server's role narrows to: displaying S3C's broadcast status on the
// dashboard, pushing dashboard-driven config overrides down, and owning
// statistics (via completedSessionSeq).
// -------------------------------------------------------------

Preferences prefs;

// -------------------------------------------------------------
// Dynamic settings (NVS-owned here, not on Local Server - survives a Local
// Server replacement/reflash). Broadcast every 1s via Box2ConfigPacket.
// Independent of Box 1/S3A's own copies - Box 2 can be priced/gated
// differently from Box 1.
// -------------------------------------------------------------
float    helmetDetectCmC2 = 30.0f;
uint16_t secondsPerCoin   = 20;
// Minimum number of coins that must be inserted before B2_STATE_INSERT_COIN will let a session
// through - 0 (default) means any single coin works, matching the original behavior.
uint32_t minCoinsRequired = 0;
// Maximum coins Box 2 will credit toward cycle time - once activeTimer would exceed this many
// coins' worth of seconds, further coin pulses are discarded (not credited) rather than
// extending the session. 0 (default) means no cap. No physical way yet to stop the coin slot
// itself from accepting more (would need a relay on the Allan Coin Slot line) - this is the
// software-only interim.
uint32_t maxCoinsAllowed = 0;
// Added 2026-09-14, independent of secondsPerCoin/minCoinsRequired/maxCoinsAllowed above: fraction
// of Heating's actual duration that Cool Down runs for. 1.0 (default) matches Heating exactly;
// dashboard also offers 0.75/0.5/0.25 for experimenting with a shorter cooldown, since a long
// Heating session doesn't necessarily need equally long Fan time to be safely wearable again.
float coolDownRatio = 1.0f;

// -------------------------------------------------------------
// Redesigned customer flow state (Steps 1-5, 10 of the 15-step cycle) - added 2026-09-14,
// mirrors Box 1/S3A's identical 2026-09-13 addition.
// -------------------------------------------------------------
uint8_t  rateBeforeValue          = 5;   // Step 3 rating (1-10), captured before heating
uint8_t  rateAfterValue           = 5;   // Step 10 rating (1-10), captured after cooling down
uint32_t selectedTimeSec          = 0;   // Step 4: customer-picked heating duration
const uint32_t TIME_STEP_SEC      = 15;  // +/- increment on Step 4
const uint32_t TIME_SEC_MIN       = 15;
const uint32_t TIME_SEC_MAX       = 1800; // 30 min ceiling - a sanity bound independent of
                                           // whatever minCoinsRequired/maxCoinsAllowed happen
                                           // to be configured to
uint32_t requiredCoinsForSession  = 0;   // ceil(selectedTimeSec / secondsPerCoin), computed by
                                          // recomputeRequiredCoins() below
char     dynamicScreenBuf[200];          // scratch buffer for screens built with live numbers
                                          // baked in (Rate A/B, Time Allot) - see buildRateScreen()/
                                          // buildTimeAllotScreen()

// Step 9: post-Heating grace period before Rate B/Retrieve - NOT coin-gated (the customer only
// pays for Heating itself), but its LENGTH is coolDownRatio (dashboard-configurable, see its own
// declaration below) of however long Heating actually ran (requiredCoinsForSession * secondsPerCoin,
// unchanged since Insert Coin), not a fixed value - a 20-coin/10-minute Heating session leaves the
// helmet hotter than a 1-coin/30-second one, so a fixed cooldown would either hand back a
// still-too-hot helmet after a long session or make a short session wait pointlessly. Reuses
// activeTimer as its own countdown (same field Heating already uses and C1 already displays via
// {TIMER}), rather than a second timer variable.

// -------------------------------------------------------------
// Box 2 State Machine
// -------------------------------------------------------------
Box2State currentB2State     = B2_STATE_TAPS; // was B2_STATE_IDLE - see STATE_TAPS's comment in
                                                // Shared_Common.h for why
// Remembers which of Heating/Cool Down to resume to after Abort/Safety-Pause, since both phases
// can trigger them (both have a helmet mid-chamber and an on-screen ABORT button).
Box2State returnStateAfterInterrupt = B2_STATE_HEATING;
uint16_t  stepRemainingSec   = 0;
uint32_t  activeTimer        = 0;
uint32_t  lastKnownPulsesC1  = 0;

uint8_t  conditionHoldTicks = 0;
bool     sensorsWasClosed   = false;
uint8_t  safetyBreachTicks  = 0; // debounce for B2_STATE_HEATING's handshake check - a single
                                  // noisy sensor tick shouldn't be enough to pause a real cycle
                                  // (same fix already validated on Box 1/S3A's cleaning step)

// B2_STATE_SENSORS and B2_STATE_RETRIEVE's shared debounce state: enclosureUnlockedForRetry/
// doorOpenTicks track the enclosure lock separately from conditionHoldTicks (which both states
// reuse for debouncing the door-CLOSED check). Whenever the enclosure gets unlocked so the user
// can open it, this confirms the door has genuinely been opened (not just reed-switch bounce)
// for 2 ticks and then immediately releases the solenoid - it must never be left energized
// waiting on the user's own pace (a helmet sitting unretrieved, or the door just never being
// opened, for 10+ minutes overheated a solenoid badly enough to be a real fire-risk incident).
bool    enclosureUnlockedForRetry = false;
uint8_t doorOpenTicks      = 0;

// B2_STATE_PAUSED_SAFETY reuses enclosureUnlockedForRetry/doorOpenTicks (declared above) for
// the same reason Sensors/Retrieve do: while paused with the door closed, handshakeValid being
// false there means the helmet must be missing (otherwise handshakeValid would already be true
// and we'd be resuming instead) - it gets unlocked immediately once that's confirmed, released
// again once the door's confirmed open, and rechecked the next time it closes.

// The breach direction (handshake bad -> pause) was already debounced via safetyBreachTicks,
// but the restore direction (handshake good -> resume) fired on a single good tick with no
// debounce at all - a single noisy "helmet present" reading (real helmet sensors read noisy
// near their threshold) was enough to resume, immediately re-breach on the very next bad tick,
// and repeat indefinitely. restoreHoldTicks closes that gap with the same 2-tick pattern.
uint8_t restoreHoldTicks = 0;

unsigned long c1RedrawAtMillis = 0; // same cosmetic EMI-glitch fix Local Server/S3A used

TelemetryPacket nodeC1_Data;
TelemetryPacket nodeC2_Data;

// Session completion tracking, for Local Server's stats (see BoxStatusPacket).
uint32_t completedSessionSeq    = 0;
uint32_t sessionStartMillis     = 0;
uint32_t sessionCoinsAtStart    = 0;
uint16_t lastSessionDurationSec = 0;
uint16_t lastSessionCoins       = 0;

unsigned long lastOneSecTick   = 0;
unsigned long lastConfigBcastMillis = 0;
const unsigned long BCAST_INTERVAL_MS = 1000;
unsigned long lastRelayReassertMillis = 0;
const unsigned long RELAY_REASSERT_INTERVAL_MS = 2000; // self-heal a dropped relay packet within a couple seconds without flooding the mesh

// -------------------------------------------------------------
// Hardcoded Step Screens - identical to what Local Server used to own.
// -------------------------------------------------------------
// Steps 1-2 (added 2026-09-14): resting/intro screens - C1 advances on ANY tap on these two, not
// a specific button (see anyTapFlagForCurrentState() and CMD_STEP_RENDER's state field). Content
// matches the user's own design export, including Step 2's shared "JOLLY HELMET" branding.
const char* SCREEN_C1_TAPS =
    "#000000|ST,160,110,#FFFFFF,TAP! to,MC,2|TT,160,136,#FFFF00,CONTINUE,500,MC,4";

const char* SCREEN_C1_WELCOME_INTRO =
    "#000000|ST,160,60,#FFFFFF,Welcome to,BC,2|ST,160,60,#FFFFFF,JOLLY HELMET,TC,4|ST,162,120,#FFFFFF,Make your Helmet,BC,2|ST,160,120,#FFFFFF,Happy Again!,TC,4|TT,160,180,#FFFF00,Tap to START,500,TC,2";

// Step 5 (added 2026-09-14): coin accumulation against this session's own requiredCoinsForSession
// (computed on Step 4's Confirm), mirrors Box 1/S3A's identical SCREEN_INSERT_COIN.
const char* SCREEN_C1_INSERT_COIN =
    "#000000|ST,160,60,#FFFFFF,INSERT THE REQUIRED COIN,TC,2|TT,144,120,#FFFFFF,/ {REQUIREDCOIN} Coin,0,ML,2|TT,126,120,#FFFF00,{COINS},500,MR,2|BTN,20,190,90,32,#000000,CANCEL,CC";

// Step 5's "are you sure?" (added 2026-09-14) - YES resets exactly like the existing RESET touch
// action (back to Step 1/Taps), NO returns to Insert Coin unchanged.
const char* SCREEN_C1_CANCEL_CONFIRM =
    "#7f1d1d|ST,160,88,#FFFFFF,Cancel operation?,MC,2|BTN,12,154,140,52,#4A9E05,YES,CY|BTN,168,154,140,52,#8F0000,NO,CN";

// Step 13 (added 2026-09-14): defined per the user's own design export but deliberately wired to
// nothing - it was an empty "NONE" placeholder with no described trigger (same treatment as Box 1/
// S3A's SCREEN_CONFIRM_PLACEHOLDER). Kept as a plain constant so the screen exists to look at, not
// reachable by any real transition.
const char* SCREEN_C1_CONFIRM_PLACEHOLDER =
    "#000000|ST,10,15,#FFFFFF,NONE,TL,2";

const char* SCREEN_C1_INSTRUCTIONS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Proceed";

const char* SCREEN_C1_SENSORS =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Place the|ST,10,95,#FFFFFF,Headgear Inside the Chamber";

const char* SCREEN_C1_SENSORS_RETRY =
    "#800000|ST,10,15,#FFFFFF,No Headgear Detected!|ST,10,50,#FFFFFF,Please Open the Door|ST,10,75,#FFFFFF,and Insert your Headgear";

const char* SCREEN_C1_HEATING =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Heating in Progress...|TT,10,95,#00FF00,Time: {TIMER}s,0|BTN,215,8,95,32,#ef4444,ABORT,ABORT";

// Step 9 (added 2026-09-14): post-Heating grace period, same layout/pattern as Heating (Fan-only,
// no Heater) - {TIMER} shows activeTimer, reused as Cool Down's own countdown, set to the same
// duration Heating just ran for (see the B2_STATE_HEATING completion case below). The user's
// design also shows a progress bar (PBAR) here - not yet a real firmware tag (same Phase-1
// limitation as Box 1's NP/NR widgets), so this keeps the existing text countdown instead,
// deferred to Phase 2 alongside the other real widgets.
const char* SCREEN_C1_COOL_DOWN =
    "#000080|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Cooling in Progress...|TT,10,95,#00FF00,Time: {TIMER}s,0|BTN,215,8,95,32,#ef4444,ABORT,ABORT";

// Content reskinned 2026-09-14 per the user's new design - trigger/action strings unchanged
// (mirrors Box 1/S3A's identical 2026-09-13 reskin).
const char* SCREEN_C1_ABORT_CONFIRM =
    "#000000|ST,160,88,#FFFFFF,Abort operation?,MC,2|ST,160,120,#FFFFFF,COIN(s) won't be Refunded.,MC,2|BTN,12,154,140,52,#8E0101,ABORT,ABORT_YES|BTN,168,154,140,52,#32A800,RESUME,ABORT_NO";

const char* SCREEN_C1_SAFETY_PAUSE =
    "#800000|ST,10,20,#FFFFFF,SAFETY PAUSE|TT,10,60,#FFFF00,CLOSE DOOR / REPLACE HELMET,300";

const char* SCREEN_C1_RETRIEVE =
    "#006400|TT,10,8,#FFFFFF,COIN:{COINS} TIME:{TIMER}s,0|ST,10,70,#FFFFFF,Please Open the|ST,10,95,#FFFFFF,Enclosure Door to Retrieve";

const char* SCREEN_C1_FINISH =
    "#006400|ST,10,20,#FFFFFF,Thank you for your|ST,10,45,#FFFFFF,Patronage!|ST,10,80,#FFFF00,We Hope to See you Again! :D";

// -------------------------------------------------------------
// NVS
// -------------------------------------------------------------
void loadConfigFromNVS() {
    prefs.begin("s3c_nvs", false);
    helmetDetectCmC2  = prefs.getFloat("helm_c2_cm", 30.0f);
    secondsPerCoin    = prefs.getUShort("sec_coin", 20);
    minCoinsRequired  = prefs.getUInt("min_coins", 0);
    maxCoinsAllowed   = prefs.getUInt("max_coins", 0);
    coolDownRatio     = prefs.getFloat("cd_ratio", 1.0f);
    lastKnownPulsesC1 = prefs.getUInt("last_c1_pulses", 0); // survives an S3C reboot mid-session
    Serial.printf("[S3C NVS] Loaded: helmetC2=%.1f secPerCoin=%u minCoins=%u maxCoins=%u cdRatio=%.2f\n",
                  helmetDetectCmC2, secondsPerCoin, minCoinsRequired, maxCoinsAllowed, coolDownRatio);
}

void saveConfigToNVS() {
    prefs.putFloat("helm_c2_cm", helmetDetectCmC2);
    prefs.putUShort("sec_coin", secondsPerCoin);
    prefs.putUInt("min_coins", minCoinsRequired);
    prefs.putUInt("max_coins", maxCoinsAllowed);
    prefs.putFloat("cd_ratio", coolDownRatio);
}

// -------------------------------------------------------------
// ESP-NOW send/receive
// -------------------------------------------------------------
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (!tx_info) return;
}

void sendCmd(uint8_t targetDev, uint8_t cmdId, uint8_t subIdx, uint16_t param, uint8_t state, const char* str = nullptr) {
    CommandPacket cmd;
    memset(&cmd, 0, sizeof(CommandPacket));
    cmd.targetDeviceID = targetDev;
    cmd.commandID      = cmdId;
    cmd.subIndex       = subIdx;
    cmd.param16        = param;
    cmd.state          = state;
    if (str != nullptr) {
        strncpy(cmd.payloadStr, str, sizeof(cmd.payloadStr) - 1);
        cmd.payloadStr[sizeof(cmd.payloadStr) - 1] = '\0';
    }
    esp_now_send(BROADCAST_MAC, (uint8_t*)&cmd, sizeof(CommandPacket));
}

void broadcastConfig() {
    Box2ConfigPacket packet;
    memset(&packet, 0, sizeof(Box2ConfigPacket));
    packet.deviceID         = DEVICE_S3C;
    packet.helmetDetectCmC2 = helmetDetectCmC2;
    packet.secondsPerCoin   = secondsPerCoin;
    packet.minCoinsRequired = minCoinsRequired;
    packet.maxCoinsAllowed  = maxCoinsAllowed;
    packet.coolDownRatio    = coolDownRatio;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(Box2ConfigPacket));
}

// -------------------------------------------------------------
// Sensor / Relay Helpers
// -------------------------------------------------------------
bool helmetPresentFn() {
    return (nodeC2_Data.usHelmetDistance > 0.0f && nodeC2_Data.usHelmetDistance < helmetDetectCmC2);
}

bool enclosureClosedFn() {
    return !nodeC2_Data.doorEnclosure;
}

void broadcastStatus() {
    BoxStatusPacket packet;
    memset(&packet, 0, sizeof(BoxStatusPacket));
    packet.deviceID               = DEVICE_S3C;
    packet.machineState           = (uint8_t)currentB2State;
    packet.activeTimer            = activeTimer;
    packet.stepRemainingSec       = stepRemainingSec;
    packet.handshakeValid         = enclosureClosedFn() && helmetPresentFn();
    packet.completedSessionSeq    = completedSessionSeq;
    packet.lastSessionDurationSec = lastSessionDurationSec;
    packet.lastSessionCoins       = lastSessionCoins;
    esp_now_send(BROADCAST_MAC, (uint8_t*)&packet, sizeof(BoxStatusPacket));
}

bool c1HardRefreshPending = false; // set only when the pending redraw must also fully re-init
                                    // the ST7789 controller - see CMD_TFT_HARD_REFRESH's comment
                                    // in Shared_Common.h

void muteC1AndScheduleRedraw(bool hardRefresh) {
    sendCmd(DEVICE_C1, CMD_MUTE_COINS, 0, 250, 0);
    c1RedrawAtMillis = millis() + 300;
    if (hardRefresh) c1HardRefreshPending = true;
}

// Tracks the last commanded relay bitmask so it can be periodically re-sent (see loop()) -
// applyC2RelayBitmask() only fires once, at the moment of a state transition, so if that one
// burst of ESP-NOW packets gets dropped, C2 is left stuck in the previous state's relay
// configuration (e.g. door still locked, heater still on) until the next transition, which may
// be a long time away or may never come. Re-sending the current mask periodically means a
// single dropped packet self-heals within a few seconds instead of staying stuck.
uint8_t desiredC2Mask = 0;

void sendC2RelayMask(uint8_t mask) {
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t state = (mask >> i) & 0x01;
        sendCmd(DEVICE_C2, CMD_SET_RELAY, i, 0, state);
        delay(2); // firing 6 broadcasts back-to-back with zero gap can overflow ESP-NOW's send
                  // queue and silently drop some - a couple ms between each gives the radio time
                  // to actually clear the previous one first
    }
}

void applyC2RelayBitmask(uint8_t mask) {
    // Bit 0 is the enclosure lock relay (1=unlocked, 0=locked). The TFT-white-screen glitch is
    // specifically caused by the lock solenoid re-engaging (Unlocked->Locked), so only that exact
    // transition should force C1 to re-init its display controller - not every relay change.
    bool lockJustEngaged = (desiredC2Mask & 0x01) && !(mask & 0x01);
    desiredC2Mask = mask;
    muteC1AndScheduleRedraw(lockJustEngaged);
    sendC2RelayMask(mask);
}

// Zeroes C1's own live coin display and this node's pulse-tracking baseline together, so a
// customer never sees a leftover total from a previous completed cycle (C1's confirmedPulses
// otherwise only resets on its own reboot). Call whenever returning to B2_STATE_TAPS.
void resetCoinSession() {
    sendCmd(DEVICE_C1, CMD_RESET_COINS, 0, 0, 0);
    lastKnownPulsesC1 = 0;
    prefs.putUInt("last_c1_pulses", 0);
}

// Heater + Fan both stay on for the whole heating step - no blink cycle needed (unlike Box 1's
// UV toggle).
void applyC2HeatingRelays() {
    applyC2RelayBitmask(0b101000); // bit3=Heater ON, bit5=Fan ON
}

// Cool Down (Step 9, added 2026-09-14): Heater OFF, Fan stays on to actually cool the helmet.
void applyC2CoolDownRelays() {
    applyC2RelayBitmask(0b100000); // bit3=Heater OFF, bit5=Fan ON
}

// TAPS/WELCOME (Steps 1-2, added 2026-09-14) advance on ANY tap, not a specific button -
// piggybacked on CMD_STEP_RENDER's otherwise-unused state field (1 = any-tap-advances), same
// convention Box 1/S3A already established. Centralized here so every place that (re)sends a
// screen agrees on when it's set.
uint8_t anyTapFlagForCurrentState() {
    return (currentB2State == B2_STATE_TAPS || currentB2State == B2_STATE_WELCOME) ? 1 : 0;
}

// Step 4's cost math: whole coins only, always rounding up (a customer can't pay a fractional
// coin) - identical to Box 1/S3A's own recomputeRequiredCoins().
void recomputeRequiredCoins() {
    requiredCoinsForSession = (selectedTimeSec + secondsPerCoin - 1) / secondsPerCoin;
}

// Steps 3/10 share one screen layout (a live 1-10 value plus -/+/OK) - only the confirm action
// code and which variable is being edited differ between "before" and "after" heating. Mirrors
// Box 1/S3A's buildRateScreen(), just with Box 2's own "WET O' METER" moisture-rating copy.
void buildRateScreen(bool isAfter) {
    uint8_t val = isAfter ? rateAfterValue : rateBeforeValue;
    snprintf(dynamicScreenBuf, sizeof(dynamicScreenBuf),
        "#000000|ST,160,30,#FFFFFF,WET O' METER,MC,4|TT,160,100,#00FF00,%u / 10,0,MC,4|"
        "BTN,20,170,80,50,#334155,-,RM|BTN,120,170,80,50,#334155,+,RP|"
        "BTN,220,170,80,50,#22c55e,OK,%s",
        val, isAfter ? "RBC" : "RAC");
}
void sendRateScreen(bool isAfter) {
    buildRateScreen(isAfter);
    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, dynamicScreenBuf);
}

// Step 4's screen either shows the live time/cost readout (warning == nullptr/empty), or swaps
// the title line for a rejection message when Confirm's min/max check fails - drawn into the
// screen itself, not a popup, mirroring Box 1/S3A's identical buildTimeAllotScreen().
void buildTimeAllotScreen(const char* warning) {
    if (warning && warning[0]) {
        snprintf(dynamicScreenBuf, sizeof(dynamicScreenBuf),
            "#000000|ST,160,30,#FF6666,%s,MC,2|TT,160,90,#00FF00,%lus=%luCoin,0,MC,2|"
            "BTN,20,180,80,40,#334155,-,TM|BTN,120,180,80,40,#334155,+,TP|"
            "BTN,220,180,80,40,#22c55e,OK,TC",
            warning, (unsigned long)selectedTimeSec, (unsigned long)requiredCoinsForSession);
    } else {
        snprintf(dynamicScreenBuf, sizeof(dynamicScreenBuf),
            "#000000|ST,160,30,#FFFFFF,ALLOCATE HEAT TIME,MC,2|TT,160,90,#00FF00,%lus=%luCoin,0,MC,2|"
            "BTN,20,180,80,40,#334155,-,TM|BTN,120,180,80,40,#334155,+,TP|"
            "BTN,220,180,80,40,#22c55e,OK,TC",
            (unsigned long)selectedTimeSec, (unsigned long)requiredCoinsForSession);
    }
}
void sendTimeAllotScreen(const char* warning) {
    buildTimeAllotScreen(warning);
    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, dynamicScreenBuf);
}

const char* screenForCurrentB2State() {
    switch (currentB2State) {
        case B2_STATE_IDLE:          return SCREEN_C1_TAPS; // superseded - see Shared_Common.h
        case B2_STATE_INSTRUCTIONS:  return SCREEN_C1_INSTRUCTIONS;
        case B2_STATE_SENSORS:       return sensorsWasClosed ? SCREEN_C1_SENSORS_RETRY : SCREEN_C1_SENSORS;
        case B2_STATE_HEATING:       return SCREEN_C1_HEATING;
        case B2_STATE_COOL_DOWN:     return SCREEN_C1_COOL_DOWN;
        case B2_STATE_ABORT_CONFIRM: return SCREEN_C1_ABORT_CONFIRM;
        case B2_STATE_PAUSED_SAFETY: return SCREEN_C1_SAFETY_PAUSE;
        case B2_STATE_RETRIEVE:      return SCREEN_C1_RETRIEVE;
        case B2_STATE_FINISH:        return SCREEN_C1_FINISH;
        case B2_STATE_TAPS:          return SCREEN_C1_TAPS;
        case B2_STATE_WELCOME:       return SCREEN_C1_WELCOME_INTRO;
        case B2_STATE_RATE_A:        buildRateScreen(false); return dynamicScreenBuf;
        case B2_STATE_TIME_ALLOT:    buildTimeAllotScreen(nullptr); return dynamicScreenBuf;
        case B2_STATE_INSERT_COIN:   return SCREEN_C1_INSERT_COIN;
        case B2_STATE_RATE_B:        buildRateScreen(true); return dynamicScreenBuf;
        case B2_STATE_CANCEL_CONFIRM: return SCREEN_C1_CANCEL_CONFIRM;
        default:                     return SCREEN_C1_TAPS;
    }
}

// Shared by Heating and Cool Down, which are both entered with a helmet mid-chamber and need the
// same door/helmet monitoring. Returns true if a breach was detected and handled (state
// transitioned to B2_STATE_PAUSED_SAFETY), recording currentPhase in returnStateAfterInterrupt so
// the resume logic below knows which relay config/screen to restore.
bool checkSafetyBreach(bool handshakeValid, Box2State currentPhase) {
    if (!handshakeValid) {
        // Debounced: require 2 consecutive bad ticks before actually pausing, so a single noisy
        // ultrasonic/door-reed reading doesn't interrupt a real cycle (same fix already validated
        // on Box 1/S3A's cleaning step).
        safetyBreachTicks++;
        if (safetyBreachTicks >= 2) {
            Serial.println("[S3C] Safety Breach! Door opened or helmet removed.");
            returnStateAfterInterrupt = currentPhase;
            currentB2State = B2_STATE_PAUSED_SAFETY;
            restoreHoldTicks = 0;
            conditionHoldTicks = 0;
            enclosureUnlockedForRetry = false;
            doorOpenTicks = 0;
            applyC2RelayBitmask(0b000000);
            sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SAFETY_PAUSE);
            return true;
        }
    } else {
        safetyBreachTicks = 0;
    }
    return false;
}

void recordCompletedSession() {
    uint32_t durationSec = (millis() - sessionStartMillis) / 1000UL;
    uint32_t coinsUsed = (lastKnownPulsesC1 >= sessionCoinsAtStart) ? (lastKnownPulsesC1 - sessionCoinsAtStart) : 0;
    lastSessionDurationSec = (durationSec > 65535UL) ? 65535 : (uint16_t)durationSec;
    lastSessionCoins       = (coinsUsed  > 65535UL) ? 65535 : (uint16_t)coinsUsed;
    completedSessionSeq++;
    Serial.printf("[S3C STATS] Session #%u recorded: %ds, %d coins.\n", completedSessionSeq, lastSessionDurationSec, lastSessionCoins);
}

// -------------------------------------------------------------
// ESP-NOW Receive
// -------------------------------------------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (!info || !info->src_addr || !incomingData || len <= 0) return;

    if (len == sizeof(CommandPacket)) {
        CommandPacket cmd;
        memcpy(&cmd, incomingData, sizeof(CommandPacket));
        if (cmd.targetDeviceID != DEVICE_S3C) return; // broadcast traffic meant for another node

        if (cmd.commandID != CMD_SYNC_VARS) {
            Serial.printf("[S3C RX] Opcode %d\n", cmd.commandID);
        }

        if (cmd.commandID == CMD_SET_CONFIG) {
            // Accepts 3 fields (oldest dashboards, no Max Coins/Cool Down Ratio yet), 4 (with Max
            // Coins), or 5 (with Cool Down Ratio too) - same backward-compatible parsed==N pattern
            // Box 1/S3A uses for its own maxCoinsAllowed field.
            float helmCm, secPerCoinF, minCoinsF, maxCoinsF = 0, cdRatioF = 1.0f;
            int parsed = sscanf(cmd.payloadStr, "%f,%f,%f,%f,%f", &helmCm, &secPerCoinF, &minCoinsF, &maxCoinsF, &cdRatioF);
            if (parsed == 3 || parsed == 4 || parsed == 5) {
                // maxCoinsF/cdRatioF stay at their defaults (0/1.0) if an older dashboard/sender
                // only sent fewer values - graceful degradation rather than a rejected update.
                helmetDetectCmC2 = helmCm;
                secondsPerCoin   = (uint16_t)secPerCoinF;
                minCoinsRequired = (uint32_t)minCoinsF;
                maxCoinsAllowed  = (uint32_t)maxCoinsF;
                if (parsed == 5) coolDownRatio = cdRatioF;
                saveConfigToNVS();
                broadcastConfig();
                Serial.printf("[S3C CONFIG] Updated & saved: helmC2=%.1f secPerCoin=%u minCoins=%u maxCoins=%u cdRatio=%.2f\n",
                              helmetDetectCmC2, secondsPerCoin, minCoinsRequired, maxCoinsAllowed, coolDownRatio);
            } else {
                Serial.printf("[S3C CONFIG] REJECTED: malformed payload '%s' (parsed %d/5)\n", cmd.payloadStr, parsed);
            }
        } else if (cmd.commandID == CMD_TOUCH_ACTION) {
            Serial.printf("[S3C TOUCH] Action: '%s'\n", cmd.payloadStr);
            if (strcmp(cmd.payloadStr, "RESET") == 0) {
                activeTimer = 0;
                currentB2State = B2_STATE_TAPS;
                applyC2RelayBitmask(0b000000);
                resetCoinSession();
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), SCREEN_C1_TAPS);
            } else if (strcmp(cmd.payloadStr, "ABORT") == 0 &&
                       (currentB2State == B2_STATE_HEATING || currentB2State == B2_STATE_COOL_DOWN)) {
                returnStateAfterInterrupt = currentB2State;
                currentB2State = B2_STATE_ABORT_CONFIRM;
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_ABORT_CONFIRM);
            } else if (strcmp(cmd.payloadStr, "ABORT_YES") == 0 && currentB2State == B2_STATE_ABORT_CONFIRM) {
                Serial.println("[S3C] Cycle Cancelled by user (Abort Confirmed).");
                activeTimer = 0;
                currentB2State = B2_STATE_RETRIEVE;
                conditionHoldTicks = 0;
                enclosureUnlockedForRetry = true;
                doorOpenTicks = 0;
                applyC2RelayBitmask(0b000001); // unlock enclosure only
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE);
                sendCmd(DEVICE_C1, CMD_BUZZER, 0, 500, 0);
            } else if (strcmp(cmd.payloadStr, "ABORT_NO") == 0 && currentB2State == B2_STATE_ABORT_CONFIRM) {
                currentB2State = returnStateAfterInterrupt;
                if (returnStateAfterInterrupt == B2_STATE_HEATING) {
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                } else {
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_COOL_DOWN);
                }

            // --- Added 2026-09-14 for the redesigned Steps 1-5, 10 customer flow (mirrors
            // Box 1/S3A's identical 2026-09-13 addition) ---
            } else if (strcmp(cmd.payloadStr, "TAP_ADVANCE") == 0 && currentB2State == B2_STATE_TAPS) {
                currentB2State = B2_STATE_WELCOME;
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), SCREEN_C1_WELCOME_INTRO);
            } else if (strcmp(cmd.payloadStr, "TAP_ADVANCE") == 0 && currentB2State == B2_STATE_WELCOME) {
                currentB2State = B2_STATE_RATE_A;
                rateBeforeValue = 5;
                sendRateScreen(false);
            } else if (strcmp(cmd.payloadStr, "RM") == 0 && (currentB2State == B2_STATE_RATE_A || currentB2State == B2_STATE_RATE_B)) {
                uint8_t* val = (currentB2State == B2_STATE_RATE_A) ? &rateBeforeValue : &rateAfterValue;
                if (*val > 1) (*val)--;
                sendRateScreen(currentB2State == B2_STATE_RATE_B);
            } else if (strcmp(cmd.payloadStr, "RP") == 0 && (currentB2State == B2_STATE_RATE_A || currentB2State == B2_STATE_RATE_B)) {
                uint8_t* val = (currentB2State == B2_STATE_RATE_A) ? &rateBeforeValue : &rateAfterValue;
                if (*val < 10) (*val)++;
                sendRateScreen(currentB2State == B2_STATE_RATE_B);
            } else if (strcmp(cmd.payloadStr, "RAC") == 0 && currentB2State == B2_STATE_RATE_A) {
                Serial.printf("[S3C] Rate A confirmed: %u/10 -> Step 4 (Time Allot)\n", rateBeforeValue);
                currentB2State = B2_STATE_TIME_ALLOT;
                selectedTimeSec = TIME_SEC_MIN;
                recomputeRequiredCoins();
                sendTimeAllotScreen(nullptr);
            } else if (strcmp(cmd.payloadStr, "TM") == 0 && currentB2State == B2_STATE_TIME_ALLOT) {
                if (selectedTimeSec > TIME_SEC_MIN) selectedTimeSec -= TIME_STEP_SEC;
                recomputeRequiredCoins();
                sendTimeAllotScreen(nullptr);
            } else if (strcmp(cmd.payloadStr, "TP") == 0 && currentB2State == B2_STATE_TIME_ALLOT) {
                if (selectedTimeSec < TIME_SEC_MAX) selectedTimeSec += TIME_STEP_SEC;
                recomputeRequiredCoins();
                sendTimeAllotScreen(nullptr);
            } else if (strcmp(cmd.payloadStr, "TC") == 0 && currentB2State == B2_STATE_TIME_ALLOT) {
                if (requiredCoinsForSession < minCoinsRequired) {
                    char warn[48];
                    snprintf(warn, sizeof(warn), "Min %u Coins Required", (unsigned)minCoinsRequired);
                    Serial.printf("[S3C] Time rejected: %lu coins < min %u\n", (unsigned long)requiredCoinsForSession, (unsigned)minCoinsRequired);
                    sendTimeAllotScreen(warn);
                } else if (maxCoinsAllowed > 0 && requiredCoinsForSession > maxCoinsAllowed) {
                    char warn[48];
                    snprintf(warn, sizeof(warn), "Max %u Coins Allowed", (unsigned)maxCoinsAllowed);
                    Serial.printf("[S3C] Time rejected: %lu coins > max %u\n", (unsigned long)requiredCoinsForSession, (unsigned)maxCoinsAllowed);
                    sendTimeAllotScreen(warn);
                } else {
                    Serial.printf("[S3C] Time confirmed: %lus (%lu coins) -> Step 5 (Insert Coin)\n",
                                  (unsigned long)selectedTimeSec, (unsigned long)requiredCoinsForSession);
                    currentB2State = B2_STATE_INSERT_COIN;
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_INSERT_COIN);
                }
            } else if (strcmp(cmd.payloadStr, "CC") == 0 && currentB2State == B2_STATE_INSERT_COIN) {
                currentB2State = B2_STATE_CANCEL_CONFIRM;
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_CANCEL_CONFIRM);
            } else if (strcmp(cmd.payloadStr, "CY") == 0 && currentB2State == B2_STATE_CANCEL_CONFIRM) {
                Serial.println("[S3C] Cancelled by user -> Step 1 (Taps)");
                activeTimer = 0;
                currentB2State = B2_STATE_TAPS;
                applyC2RelayBitmask(0b000000);
                resetCoinSession();
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), SCREEN_C1_TAPS);
            } else if (strcmp(cmd.payloadStr, "CN") == 0 && currentB2State == B2_STATE_CANCEL_CONFIRM) {
                currentB2State = B2_STATE_INSERT_COIN;
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_INSERT_COIN);
            } else if (strcmp(cmd.payloadStr, "RBC") == 0 && currentB2State == B2_STATE_RATE_B) {
                Serial.printf("[S3C] Rate B confirmed: %u/10 -> Step 11 (Retrieve)\n", rateAfterValue);
                currentB2State = B2_STATE_RETRIEVE;
                conditionHoldTicks = 0;
                enclosureUnlockedForRetry = true;
                doorOpenTicks = 0;
                applyC2RelayBitmask(0b000001);
                sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE);
                sendCmd(DEVICE_C1, CMD_BUZZER, 0, 500, 0);
            }
        }
        return;
    }

    if (len != sizeof(TelemetryPacket)) return;

    TelemetryPacket packet;
    memcpy(&packet, incomingData, sizeof(TelemetryPacket));

    if (packet.deviceID == DEVICE_C1) {
        // lastKnownPulsesC1 is NVS-persisted (see loadConfigFromNVS/below) so an S3C reboot
        // mid-session doesn't forget which pulses it already credited and double-add time for
        // them - same class of bug already found and fixed on Local Server's/S3A's own copies.
        if (packet.pulseCount > lastKnownPulsesC1) {
            uint32_t newPulses = packet.pulseCount - lastKnownPulsesC1;
            uint32_t addedSeconds = newPulses * secondsPerCoin;
            // maxCoinsAllowed==0 means no cap. Otherwise clamp activeTimer at the cap and drop
            // the rest - no relay yet to physically stop the Allan Coin Slot from accepting more,
            // so this is the software-only "discard the excess" interim.
            uint32_t cappedSeconds = maxCoinsAllowed > 0 ? maxCoinsAllowed * secondsPerCoin : 0;
            if (cappedSeconds > 0 && activeTimer + addedSeconds > cappedSeconds) {
                uint32_t discarded = (activeTimer + addedSeconds) - cappedSeconds;
                activeTimer = cappedSeconds;
                Serial.printf("[S3C COIN] Capped at %u coins (%u sec) - discarded %u sec worth of extra coins.\n",
                              maxCoinsAllowed, cappedSeconds, discarded);
            } else {
                activeTimer += addedSeconds;
            }
            lastKnownPulsesC1 = packet.pulseCount;
            prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
            Serial.printf("[S3C COIN] +%d seconds from C1. activeTimer = %d sec\n", addedSeconds, activeTimer);
        } else if (packet.pulseCount < lastKnownPulsesC1) {
            // C1 itself rebooted or its counter was reset - resync the baseline down rather
            // than either double-crediting or freezing until C1's counter climbs back above
            // the old (now stale) baseline.
            Serial.printf("[S3C COIN] C1 pulse count dropped (%u -> %u) - resyncing baseline.\n", lastKnownPulsesC1, packet.pulseCount);
            lastKnownPulsesC1 = packet.pulseCount;
            prefs.putUInt("last_c1_pulses", lastKnownPulsesC1);
        }
        nodeC1_Data = packet;
    } else if (packet.deviceID == DEVICE_C2) {
        nodeC2_Data = packet;
    }
}

// -------------------------------------------------------------
// Setup / Loop
// -------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=======================================================");
    Serial.println("   NODE S3C: BOX 2 INTERNAL SERVER MANAGEMENT (ESP32-S3)");
    Serial.println("   Now running Box 2's full state machine directly.");
    Serial.println("=======================================================");

    loadConfigFromNVS();
    memset(&nodeC1_Data, 0, sizeof(TelemetryPacket));
    memset(&nodeC2_Data, 0, sizeof(TelemetryPacket));

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // modem sleep periodically powers down the receiver to save power,
                           // even with no AP joined - causes several-second gaps in ESP-NOW
                           // reception on a roughly-periodic cycle otherwise (Master already
                           // disables this; every other node was missing it)
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.println("[S3C] WiFi STA mode, channel forced to 1.");

    if (esp_now_init() != ESP_OK) {
        Serial.println("[S3C] ESP-NOW: Initialization FAILED!");
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
    Serial.println("[S3C] ESP-NOW: Broadcast peer registered.");

    broadcastConfig();
    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 1, SCREEN_C1_TAPS); // state=1: any-tap-advances
}

void loop() {
    unsigned long currentMillis = millis();

    // Post-solenoid TFT repaint - same EMI-glitch mitigation Local Server/S3A used.
    if (c1RedrawAtMillis != 0 && currentMillis >= c1RedrawAtMillis) {
        c1RedrawAtMillis = 0;
        if (c1HardRefreshPending) {
            c1HardRefreshPending = false;
            sendCmd(DEVICE_C1, CMD_TFT_HARD_REFRESH, 0, 0, anyTapFlagForCurrentState(), screenForCurrentB2State());
        } else {
            sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), screenForCurrentB2State());
        }
    }

    if (currentMillis - lastOneSecTick >= 1000) {
        lastOneSecTick = currentMillis;

        bool handshakeValid = enclosureClosedFn() && helmetPresentFn();

        switch (currentB2State) {
            // Old case (coin accumulation -> B2_STATE_INSTRUCTIONS) removed 2026-09-14 -
            // superseded by B2_STATE_INSERT_COIN below, which now does that job against the
            // customer's own chosen requiredCoinsForSession. See Shared_Common.h's B2_STATE_IDLE
            // comment - no longer reachable at all; kept as an explicit no-op case (rather than
            // omitted) just to avoid a -Wswitch warning (same treatment Box 1/S3A gave its own
            // STATE_IDLE).
            case B2_STATE_IDLE:
                break;

            // Steps 1-2 (TAPS/WELCOME) and 3-5 (Rate A/Time Allot/Insert Coin's button presses)
            // are entirely touch-driven - see CMD_TOUCH_ACTION above. B2_STATE_INSERT_COIN's own
            // coin-accumulation check (the only per-tick logic these five states need) is below,
            // in its usual position in the flow.
            case B2_STATE_TAPS:
            case B2_STATE_WELCOME:
            case B2_STATE_RATE_A:
            case B2_STATE_TIME_ALLOT:
            case B2_STATE_CANCEL_CONFIRM:
                break;

            case B2_STATE_INSERT_COIN:
                // Same accumulation idea B2_STATE_IDLE used to have, but against this session's
                // own requiredCoinsForSession (set on Step 4's Confirm) instead of a flat
                // minCoinsRequired - and once met, clamp activeTimer down to exactly the
                // paid-for duration, discarding any excess (same clamp-and-discard shape used
                // for maxCoinsAllowed elsewhere in this file).
                if (activeTimer >= requiredCoinsForSession * secondsPerCoin) {
                    activeTimer = requiredCoinsForSession * secondsPerCoin;
                    Serial.println("[S3C] Required coins met -> Step 6 (Instructions)");
                    currentB2State = B2_STATE_INSTRUCTIONS;
                    conditionHoldTicks = 0;
                    applyC2RelayBitmask(0b000001); // unlock enclosure
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_INSTRUCTIONS);
                    sendCmd(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case B2_STATE_INSTRUCTIONS:
                if (nodeC2_Data.doorEnclosure) conditionHoldTicks++; else conditionHoldTicks = 0;
                if (conditionHoldTicks >= 2) {
                    Serial.println("[S3C] Door opened -> Step 7 (Sensors)");
                    currentB2State = B2_STATE_SENSORS;
                    sensorsWasClosed = false;
                    conditionHoldTicks = 0;
                    enclosureUnlockedForRetry = false;
                    doorOpenTicks = 0;
                    applyC2RelayBitmask(0b000000); // re-lock enclosure
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SENSORS);
                    sendCmd(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                }
                break;

            case B2_STATE_SENSORS: {
                bool closedNow = enclosureClosedFn();

                if (closedNow) {
                    doorOpenTicks = 0;
                    conditionHoldTicks++;
                } else {
                    conditionHoldTicks = 0;
                    if (enclosureUnlockedForRetry) {
                        // Confirm the door's genuinely open (2 ticks, not just reed-switch
                        // bounce) before releasing the solenoid - it was unlocked below so the
                        // user could open it, and shouldn't stay energized any longer than that.
                        doorOpenTicks++;
                        if (doorOpenTicks >= 2) {
                            applyC2RelayBitmask(0b000000);
                            enclosureUnlockedForRetry = false;
                        }
                    }
                }

                // Debounced: only evaluates once per genuine door-close (conditionHoldTicks
                // hitting exactly 2), not on every tick a possibly-bouncing reed switch happens
                // to read closed - avoids repeatedly re-firing the retry unlock/message.
                if (conditionHoldTicks == 2) {
                    if (helmetPresentFn()) {
                        Serial.println("[S3C] Handshake verified -> Step 8 (Heating)");
                        currentB2State = B2_STATE_HEATING;
                        sessionStartMillis  = millis();
                        sessionCoinsAtStart = lastKnownPulsesC1;
                        applyC2HeatingRelays();
                        sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                        sendCmd(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                    } else {
                        Serial.println("[S3C] Door closed without helmet - unlocking enclosure so it can be reopened.");
                        // The screen says "open the door" - without this the enclosure is still
                        // locked from the earlier re-lock-on-entry and there's no way to comply.
                        applyC2RelayBitmask(0b000001);
                        enclosureUnlockedForRetry = true;
                        doorOpenTicks = 0;
                        sensorsWasClosed = true; // so the post-relay-toggle redraw shows the retry screen
                        sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_SENSORS_RETRY);
                        sendCmd(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                    }
                }
                break;
            }

            case B2_STATE_HEATING:
                if (!checkSafetyBreach(handshakeValid, B2_STATE_HEATING)) {
                    if (activeTimer > 0) {
                        activeTimer--;
                        if (activeTimer == 0) {
                            // Cool Down's length is coolDownRatio (dashboard-configurable, default
                            // 1.0/Full) of however long Heating actually ran - requiredCoinsForSession/
                            // secondsPerCoin are both unchanged since Insert Coin, so their product is
                            // that same duration. +0.5f rounds to the nearest second rather than
                            // always truncating down.
                            uint32_t coolDownSec = (uint32_t)((float)(requiredCoinsForSession * secondsPerCoin) * coolDownRatio + 0.5f);
                            Serial.printf("[S3C] Heating Complete! -> Step 9 (Cool Down, %us at %.2fx)\n", coolDownSec, coolDownRatio);
                            currentB2State = B2_STATE_COOL_DOWN;
                            activeTimer = coolDownSec; // reuses activeTimer/{TIMER} for Cool
                                                        // Down's own countdown
                            applyC2CoolDownRelays();
                            sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_COOL_DOWN);
                        }
                    }
                }
                break;

            case B2_STATE_COOL_DOWN:
                if (!checkSafetyBreach(handshakeValid, B2_STATE_COOL_DOWN)) {
                    if (activeTimer > 0) {
                        activeTimer--;
                        if (activeTimer == 0) {
                            Serial.println("[S3C] Cycle Complete! -> Step 10 (Rate B)");
                            recordCompletedSession();
                            currentB2State = B2_STATE_RATE_B;
                            rateAfterValue = 5;
                            sendRateScreen(true);
                        }
                    }
                }
                break;

            case B2_STATE_PAUSED_SAFETY:
                if (handshakeValid) {
                    // Debounced: require 2 consecutive good ticks before actually resuming, so
                    // a single noisy "helmet present" reading can't resume and then immediately
                    // re-breach on the next bad tick, oscillating indefinitely.
                    restoreHoldTicks++;
                    if (restoreHoldTicks >= 2) {
                        Serial.printf("[S3C] Safety Restored. Resuming %s.\n",
                                      returnStateAfterInterrupt == B2_STATE_HEATING ? "Heating" : "Cool Down");
                        currentB2State = returnStateAfterInterrupt;
                        restoreHoldTicks = 0;
                        enclosureUnlockedForRetry = false;
                        doorOpenTicks = 0;
                        if (returnStateAfterInterrupt == B2_STATE_HEATING) {
                            applyC2HeatingRelays();
                            sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_HEATING);
                        } else {
                            applyC2CoolDownRelays();
                            sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_COOL_DOWN);
                        }
                    }
                } else {
                    restoreHoldTicks = 0;
                    bool closedNow = enclosureClosedFn();

                    if (closedNow) {
                        doorOpenTicks = 0;
                        conditionHoldTicks++;
                    } else {
                        conditionHoldTicks = 0;
                        // Release the solenoid as soon as the door's confirmed genuinely open (2
                        // ticks, not reed-switch bounce) - same "don't hold it energized once
                        // it's already open" fix already applied to Sensors/Retrieve.
                        if (enclosureUnlockedForRetry) {
                            doorOpenTicks++;
                            if (doorOpenTicks >= 2) {
                                applyC2RelayBitmask(0b000000);
                                enclosureUnlockedForRetry = false;
                            }
                        }
                    }

                    // handshakeValid is false here, so a confirmed-closed door means the helmet
                    // must be missing (if it were present, handshakeValid would already be true
                    // and we'd be in the resume branch above instead) - unlock immediately, no
                    // arbitrary wait, so the user can get back in and fix it right away.
                    if (conditionHoldTicks == 2) {
                        Serial.println("[S3C] Door closed but headgear still missing - unlocking so it can be reopened.");
                        applyC2RelayBitmask(0b000001);
                        enclosureUnlockedForRetry = true;
                        doorOpenTicks = 0;
                    }
                }
                break;

            case B2_STATE_ABORT_CONFIRM:
            case B2_STATE_RATE_B: // touch-driven, same as B2_STATE_ABORT_CONFIRM - see RBC above
                break;

            case B2_STATE_RETRIEVE: {
                bool closedNow = enclosureClosedFn();

                if (!closedNow) {
                    conditionHoldTicks = 0;
                    // Release the solenoid as soon as the door's confirmed genuinely open (2
                    // ticks, not reed-switch bounce) - it doesn't need to stay energized for
                    // however long the user takes to actually lift the headgear out, and this
                    // is exactly the gap that let one sit energized for 10+ minutes unattended
                    // and overheat badly enough to be a real fire-risk incident.
                    if (enclosureUnlockedForRetry) {
                        doorOpenTicks++;
                        if (doorOpenTicks >= 2) {
                            applyC2RelayBitmask(0b000000);
                            enclosureUnlockedForRetry = false;
                        }
                    }
                } else {
                    doorOpenTicks = 0;
                    conditionHoldTicks++;
                }

                // Only ever check for the headgear once the door is confirmed closed again -
                // checking while it's open is meaningless (chamber's disturbed by the open
                // door) and was the other half of why the solenoid sat energized so long.
                if (conditionHoldTicks == 2) {
                    if (!helmetPresentFn()) {
                        Serial.println("[S3C] Door closed, headgear retrieved -> Step 12 (Finish)");
                        currentB2State = B2_STATE_FINISH;
                        stepRemainingSec = 4; // per the user's 2026-09-14 design export
                        applyC2RelayBitmask(0b000000); // lock enclosure
                        sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_FINISH);
                    } else {
                        Serial.println("[S3C] Door closed but headgear still inside - unlocking for retry.");
                        applyC2RelayBitmask(0b000001);
                        enclosureUnlockedForRetry = true;
                        doorOpenTicks = 0;
                        sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, 0, SCREEN_C1_RETRIEVE);
                        sendCmd(DEVICE_C1, CMD_BUZZER, 0, 250, 0);
                    }
                }
                break;
            }

            case B2_STATE_FINISH:
                if (stepRemainingSec > 0) {
                    stepRemainingSec--;
                } else {
                    activeTimer = 0;
                    currentB2State = B2_STATE_TAPS; // was B2_STATE_IDLE
                    resetCoinSession();
                    sendCmd(DEVICE_C1, CMD_STEP_RENDER, 0, 0, anyTapFlagForCurrentState(), SCREEN_C1_TAPS);
                }
                break;
        }

        // Small gaps between these periodic broadcasts (not the state-driven CMD_STEP_RENDER/
        // CMD_BUZZER calls above, which need to go out immediately) - firing several back-to-
        // back with zero gap can overflow ESP-NOW's send queue and silently drop some, which is
        // exactly what was causing C1 to flicker in and out of OUT OF ORDER even with S3C alive.
        // minCoinsRequired rides along on subIndex (otherwise unused for this command) so C1 can
        // show "{MINCOINS}" without a new command or CommandPacket field.
        // requiredCoinsForSession (added 2026-09-14) rides along on the state field (previously
        // maxCoinsAllowed's "{MAXCOINS}" piggyback - dropped since no Box 2 screen ever displayed
        // it) - same trick Box 1/S3A uses for its own "{REQUIREDCOIN}".
        sendCmd(DEVICE_C1, CMD_SYNC_VARS, (uint8_t)min(minCoinsRequired, (uint32_t)255), (uint16_t)activeTimer, (uint8_t)min(requiredCoinsForSession, (uint32_t)255));
        delay(2);

        // Broadcast a heartbeat every second so C1/C2 (and any other listening node) can tell
        // S3C is reachable, independent of Master's own heartbeat - see each node's OUT OF
        // ORDER detection.
        sendCmd(0, CMD_HEARTBEAT, DEVICE_S3C, 0, 0);
        delay(2);

        if (currentMillis - lastConfigBcastMillis >= BCAST_INTERVAL_MS) {
            lastConfigBcastMillis = currentMillis;
            broadcastConfig();
            delay(2);
        }

        // Periodic relay re-assertion (no mute/redraw side effects, unlike applyC2RelayBitmask
        // itself) - see desiredC2Mask's comment above for why this exists.
        if (currentMillis - lastRelayReassertMillis >= RELAY_REASSERT_INTERVAL_MS) {
            lastRelayReassertMillis = currentMillis;
            sendC2RelayMask(desiredC2Mask);
        }

        broadcastStatus();
    }
}
