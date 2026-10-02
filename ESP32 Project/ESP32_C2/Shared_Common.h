#ifndef SHARED_COMMON_H
#define SHARED_COMMON_H

#include <Arduino.h>

#define ESPNOW_WIFI_CHANNEL 1
#define MAX_STEPS 8

// Universal Broadcast MAC for zero-configuration pairing
static const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum DeviceID : uint8_t {
    DEVICE_SERVER = 0,
    DEVICE_A1     = 1,
    DEVICE_A2     = 2,
    DEVICE_ACS    = 3,
    DEVICE_C1     = 4, // Box 2 (Heater) Terminal - same role/pins as A1
    DEVICE_C2     = 5, // Box 2 (Heater) Actuator Hub - same role/pins as A2, Heater instead of Humidifier, +Fan
    DEVICE_S3A    = 6, // Box 1 Internal Server Management (ESP32-S3) - owns Box 1's dynamic
                        // settings so they survive a Local Server replacement/reflash
    DEVICE_S3C    = 7, // Box 2 Internal Server Management (ESP32-S3) - same role as S3A,
                        // for Box 2 (C1/C2) instead of Box 1
    DEVICE_A3     = 8  // Box 1 relay controller (added 2026-10-02): every relay-driven output, 12 channels.
                        // A2 is sensing only once A3 exists. Box 3's B3 will get its own ID later.
};

// Box 2's state machine - shared here (not Master-local) so both Node S3C (which runs it)
// and Local Server (which just displays it, via BoxStatusPacket) can name its states.
enum Box2State : uint8_t {
    B2_STATE_IDLE           = 0, // SUPERSEDED 2026-09-14 by the TAPS/WELCOME/.../INSERT_COIN intro
                                  // sequence below - nothing transitions into this anymore (kept
                                  // defined, not renumbered, so old numeric values stay stable).
    B2_STATE_INSTRUCTIONS   = 1, // Step 6: waiting for Enclosure door OPEN (now reached from
                                  // Insert Coin instead of directly from Idle)
    B2_STATE_SENSORS        = 2, // Step 7: waiting for door CLOSED + helmet detected
    B2_STATE_HEATING        = 3, // Step 8: Heater + Fan active
    B2_STATE_ABORT_CONFIRM  = 4, // user-initiated abort confirmation dialog (from Heating or Cool
                                  // Down - see returnStateAfterInterrupt in ESP32_S3C.ino)
    B2_STATE_RETRIEVE       = 5, // Step 11: waiting for door OPEN + helmet removed, then CLOSED again
    B2_STATE_FINISH         = 6, // Step 12: thank-you screen, then loops back to B2_STATE_TAPS
    B2_STATE_PAUSED_SAFETY  = 7, // Mid-heating/cool-down safety breach (unexpected door/helmet violation)
    // --- Added 2026-09-14 for the redesigned Box 2 customer flow (mirrors Box 1's own
    // STATE_TAPS..STATE_CANCEL_CONFIRM addition on 2026-09-13). Appended rather than renumbering
    // 0-7 above so every existing reference/log/dashboard value stays valid. ---
    B2_STATE_TAPS           = 8,  // Step 1: resting/attract screen - any tap advances
    B2_STATE_WELCOME        = 9,  // Step 2: intro screen - any tap advances
    B2_STATE_RATE_A         = 10, // Step 3: moisture/satisfaction rating, before heating
    B2_STATE_TIME_ALLOT     = 11, // Step 4: customer picks heat duration; validated against
                                   // min/max coins before proceeding
    B2_STATE_INSERT_COIN    = 12, // Step 5: coin accumulation against the chosen duration's cost
                                   // (was B2_STATE_IDLE's role) - has its own Cancel confirmation
    B2_STATE_COOL_DOWN      = 13, // Step 9: Fan-only cool-down after Heating, before Rate B - not
                                   // coin-gated, but runs exactly as long as Heating just did (a
                                   // longer-paid Heating session leaves the helmet hotter)
    B2_STATE_RATE_B         = 14, // Step 10: moisture/satisfaction rating, after cooling down
    B2_STATE_CANCEL_CONFIRM = 15  // Step 5's "are you sure you want to cancel?" dialog
};

enum MachineState : uint8_t {
    STATE_IDLE           = 0, // SUPERSEDED 2026-09-13 by the TAPS/WELCOME/.../INSERT_COIN intro
                               // sequence below - nothing transitions into this anymore (kept
                               // defined, not renumbered, so old numeric values stay stable).
    STATE_INSTRUCTIONS   = 1, // waiting for Enclosure door OPEN (now reached from Refilling/
                               // Checking instead of directly from Idle - see STATE_INSERT_COIN)
    STATE_CHECKING       = 2, // checking alcohol level
    STATE_REFILLING      = 3, // waiting for alcohol refill to reach HIGH threshold
    STATE_SENSORS        = 4, // waiting for door CLOSED + helmet detected
    STATE_CLEANING       = 5, // UV + Mist active
    STATE_ABORT_CONFIRM  = 6, // user-initiated abort confirmation dialog (from Cleaning)
    STATE_RETRIEVE       = 7, // waiting for door OPEN + helmet removed
    STATE_FINISH         = 8, // thank-you screen, then loops back to STATE_TAPS
    STATE_PAUSED_SAFETY  = 9, // Mid-cleaning safety breach (unexpected door/helmet violation)
    // --- Added 2026-09-13 for the redesigned customer flow. Appended rather than renumbering
    // 0-9 above so every existing reference/log/dashboard value stays valid. ---
    STATE_TAPS           = 10, // Step 1: resting/attract screen - any tap advances
    STATE_WELCOME        = 11, // Step 2: intro screen - any tap advances
    STATE_RATE_A         = 12, // Step 3: satisfaction rating, taken before cleaning
    STATE_TIME_ALLOT     = 13, // Step 4: customer picks cleaning duration; validated against
                                // min/max coins before proceeding
    STATE_INSERT_COIN    = 14, // Step 5: coin accumulation against the chosen duration's cost
                                // (was STATE_IDLE's role) - has its own Cancel confirmation
    STATE_RATE_B         = 15, // Step 11: satisfaction rating, taken after cleaning
    STATE_CANCEL_CONFIRM = 16, // Step 5's "are you sure you want to cancel?" dialog
    // --- Added 2026-10-02 for the new Box 1 procedure (Designer export BOX1_STEPS_FINAL_2026-10-02.json).
    // Appended, never renumbered. Mapping from the Designer's steps:
    //   TAPS/WELCOME/RATE_A/TIME_ALLOT/INSERT_COIN/RATE_B/FINISH/ABORT_CONFIRM/CANCEL_CONFIRM keep their states;
    //   STATE_CHECKING / STATE_REFILLING are now the ALCOHOL check/refill (CHECKING A / REFILLING A);
    //   STATE_INSTRUCTIONS = OPEN A (waiting for the door to open); STATE_RETRIEVE = OPEN B;
    //   STATE_SENSORS (4) is superseded by STATE_PLACE_HELMET + STATE_CLOSE_DOOR_A below;
    //   the Designer's six CLEANING steps are ONE state (STATE_CLEANING) plus session flags kept in S3A
    //   (mist mode, free retry); START CLEANING is only a routing decision, not a state;
    //   OUT OF ORDER drawn by A1 when S3A is silent (15 s) is A1's own screen and is not a state. ---
    STATE_METHOD         = 17, // mist choice: BALANCED / MORE ALCOHOL / MORE SCENTED
    STATE_CHECKING_SCN   = 18, // scented-container level check (CHECKING S)
    STATE_REFILLING_SCN  = 19, // waiting for the scented refill (10 tries, 10 s apart) (REFILLING S)
    STATE_PLACE_HELMET   = 20, // "SENSORS A": door open, waiting for the helmet to be placed inside
    STATE_CLOSE_DOOR_A   = 21, // helmet placed, waiting for the door to be closed
    STATE_TAKE_HELMET    = 22, // "SENSORS B": door open, waiting for the helmet to be taken out
    STATE_CLOSE_DOOR_B   = 23, // helmet out, waiting for the door to be closed
    STATE_FREE_RETRY     = 24, // YES / NO prompt after RATE B when the helmet still smells (once per session)
    STATE_MAINTENANCE    = 25, // entered from the Local Server dashboard only
    STATE_OUT_OF_ORDER   = 26, // S3A-driven: a container is below LOW and the refill gave up (S3A is alive -
                               // different from A1's own screen for a dead S3A)
    // Added later the same day, from the user's second Designer export (steps 31-32):
    STATE_HELMET_NOTICE_A = 27, // 2-second "HELMET NO DETECTED" notice, then back to OPEN A
    STATE_HELMET_NOTICE_B = 28  // 2-second "HELMET DETECTED" notice (still inside), then back to OPEN B
};

enum CommandOpcode : uint8_t {
    CMD_NONE            = 0,
    CMD_SET_COLOR       = 1,
    CMD_BUZZER          = 2, // Trigger active buzzer on Pin 21
    CMD_RESET_COINS     = 3, // Reset coin counter
    CMD_SET_RELAY       = 4, // Toggle Solenoids / Relays on Node A2
    CMD_MUTE_COINS      = 5, // Wireless EMI lockout broadcast
    CMD_STEP_RENDER     = 6, // Render Step Screen Layout
    CMD_TOUCH_ACTION    = 7, // Node A1 touch button trigger to Master
    CMD_SYNC_VARS         = 8,  // Real-time {TIMER} and {COINS} telemetry push
    CMD_SET_MAINTENANCE   = 9,  // Pause/resume ACS automatic refill (Node ACS only) - state 1=ON, 0=OFF
    CMD_REQUEST_DELIVERY  = 10, // Master -> ACS: deliver param16 mL of mixed product to Box 1's Humidifier
    CMD_SET_CONFIG        = 11, // Dynamic config push, payloadStr CSV, shape depends on target:
                                 // -> ACS:  "lowDistCm,fullDistCm,mixAlcohol,mixWater,mixScented"
                                 // -> S3A:  "helmCm,lowPct,highPct,secPerCoin,minCoinsRequired"
                                 // -> S3C:  "helmCm,secPerCoin,minCoinsRequired[,maxCoinsAllowed[,coolDownRatio]]"
    CMD_HEARTBEAT         = 12, // Broadcast every ~1s by Master AND by each S3 host (S3A/S3C) so
                                 // every node can independently tell whether it's lost contact
                                 // with EITHER one - subIndex carries the sender's own DeviceID
                                 // (DEVICE_SERVER/DEVICE_S3A/DEVICE_S3C) since CommandPacket has
                                 // no sender-identity field otherwise. targetDeviceID is 0
                                 // (broadcast) - every node listens for this regardless of who
                                 // it's normally addressed to.
    CMD_TFT_HARD_REFRESH  = 13, // Same payload/handling as CMD_STEP_RENDER (redraws the given
                                 // screen), but tells A1/C1 to also fully re-init the ST7789
                                 // controller (tft.init()+setRotation()) first. S3A/S3C send this
                                 // instead of plain CMD_STEP_RENDER ONLY at the exact moment an
                                 // enclosure lock relay goes Unlocked->Locked (solenoid engaging),
                                 // since that's the specific transition observed to brown out the
                                 // TFT controller into a blank white screen. Every other screen
                                 // update (touches, timers, unlock, coin resets, blinking) keeps
                                 // using plain CMD_STEP_RENDER so it stays fast and glitch-free -
                                 // re-initing the controller on every repaint was tried first and
                                 // caused a visible flash on every single screen change instead.
    CMD_SET_RELAY_MASK    = 14, // S3A -> A3 (added 2026-10-02): param16 = the WHOLE 12-bit relay mask at
                                 // once (A3RelayIndex bits, 1 = ON/energized). One packet replaces five
                                 // per-relay CMD_SET_RELAY commands, so a single dropped packet cannot
                                 // leave the board half-switched. S3A re-sends the current mask every
                                 // couple of seconds, and A3 counts each one as proof S3A is alive.
    CMD_STATUS_LINE       = 15, // S3A -> A1 (added 2026-10-02): one line of the cleaning "kill feed" for
                                 // the {STATUS} / {STATUS2} / {STATUS3} tokens. subIndex = line number
                                 // (0 = newest), payloadStr = the text, at most 31 characters (all A1 can
                                 // hold per line of text). An empty payloadStr clears that line.
    CMD_SET_TOKEN         = 16  // S3A -> A1 (added 2026-10-02): a plain number for a screen token.
                                 // subIndex = TokenId (below), param16 = the value.
};

// Token ids for CMD_SET_TOKEN (A1's TT text interpolates them).
enum TokenId : uint8_t {
    TOKEN_TRY      = 0, // {TRY}      refill attempt number, "Refilling... Try {TRY}/{TRIES}"
    TOKEN_TRIES    = 1, // {TRIES}    refill attempts allowed (10)
    TOKEN_MAXCOINS = 2  // {MAXCOINS} most coins one session can take
};

// CMD_TOUCH_ACTION from A1 (added 2026-10-02) now also carries param16: the rating (1-10, 0 = none picked) when
// the screen holds a rating grid (NR2), or the chosen seconds with the numpad's "NP_OK". Every other press leaves
// it 0, exactly as before. Numpad actions: "NP_OK" (param16 = seconds), "NP_BACK", "NP_CANCEL".

enum RelayIndex : uint8_t {
    RELAY_ENCLOSURE_LOCK = 0, // Pin 4  (Active LOW: LOW = Open,   HIGH = Locked)
    RELAY_PANEL_LOCK     = 1, // Pin 16 (Active LOW: LOW = Open,   HIGH = Locked)
    RELAY_BACKDOOR_LOCK  = 2, // Pin 17 (Active LOW: LOW = Open,   HIGH = Locked)
    RELAY_HUMIDIFIER     = 3, // Pin 18 (Active LOW: LOW = ON,     HIGH = OFF)
    RELAY_UV_LIGHT       = 4  // Pin 5  (Active LOW: LOW = ON,     HIGH = OFF)
};

// Node C2 (Box 2 / Heater) relay map - same pins/positions as RelayIndex above except
// index 3 is a Heater instead of a Humidifier, plus a 6th relay (Fan) that A2 doesn't have.
enum C2RelayIndex : uint8_t {
    C2_RELAY_ENCLOSURE_LOCK = 0, // Pin 4  (Active LOW: LOW = Open,   HIGH = Locked)
    C2_RELAY_PANEL_LOCK     = 1, // Pin 16 (Active LOW: LOW = Open,   HIGH = Locked)
    C2_RELAY_BACKDOOR_LOCK  = 2, // Pin 17 (Active LOW: LOW = Open,   HIGH = Locked)
    C2_RELAY_HEATER         = 3, // Pin 18 (Active LOW: LOW = ON,     HIGH = OFF)
    C2_RELAY_UV_LIGHT       = 4, // Pin 5  (Active LOW: LOW = ON,     HIGH = OFF)
    C2_RELAY_FAN            = 5  // Pin 23 (Active LOW: LOW = ON,     HIGH = OFF)
};

// Node ACS (Alcohol Container System / D1) relay map - separate namespace from RelayIndex above.
enum ACSRelayIndex : uint8_t {
    ACS_RELAY_SIDE_LOCK      = 0, // Pin 23 (Active LOW: LOW = Open,   HIGH = Locked)
    ACS_RELAY_WATER_PUMP     = 1, // Pin 21 (Active LOW: LOW = ON,     HIGH = OFF)
    ACS_RELAY_SCENTED_PUMP   = 2, // Pin 19 (Active LOW: LOW = ON,     HIGH = OFF)
    ACS_RELAY_ALCOHOL_PUMP   = 3, // Pin 18 (Active LOW: LOW = ON,     HIGH = OFF)
    ACS_RELAY_MIXER_PUMP     = 4, // Pin 5  (Active LOW: LOW = ON,     HIGH = OFF)
    ACS_RELAY_MIXING_MACHINE = 5  // Pin 25 (Active LOW: LOW = ON,     HIGH = OFF) - unconfirmed, see comment in ESP32_ACS.ino
};

// Node A3 (Box 1 "Controller", added 2026-10-02) - every relay-driven output, 12 channels. The index
// is also the bit position in CMD_SET_RELAY_MASK's param16 and in A3StatusPacket::relayMask
// (1 = ON/energized). Every relay is Active-LOW on its pin. GPIOs follow the 2026-10-01 pin sheet:
// UV 1 and Drain Pump 2 were moved off GPIO5/GPIO14, which pulse at power-up and could click a relay.
enum A3RelayIndex : uint8_t {
    A3_RELAY_ENCLOSURE_LOCK = 0,  // GPIO4   (ON = unlocked)
    A3_RELAY_PANEL_LOCK     = 1,  // GPIO16  (ON = unlocked)
    A3_RELAY_BACKDOOR_LOCK  = 2,  // GPIO17  (ON = unlocked)
    A3_RELAY_HUMIDIFIER_1   = 3,  // GPIO18  Alcohol
    A3_RELAY_UV_1           = 4,  // GPIO27
    A3_RELAY_HUMIDIFIER_2   = 5,  // GPIO19  Scented
    A3_RELAY_HUMIDIFIER_3   = 6,  // GPIO25  Both - draws from BOTH containers, on in every mist mode
    A3_RELAY_UV_2           = 7,  // GPIO21  always switched together with UV 1
    A3_RELAY_DRAIN_PUMP_1   = 8,  // GPIO33  Alcohol - maintenance only (moved from GPIO13 on 2026-10-02: it stayed ON with no command)
    A3_RELAY_DRAIN_PUMP_2   = 9,  // GPIO32  Scented - maintenance only
    A3_RELAY_ACS_DOOR_LOCK  = 10, // GPIO23  (ON = unlocked)
    A3_RELAY_COIN_POWER     = 11  // GPIO22  (ON = the coin acceptor's 12V line is live)
};
#define A3_RELAY_COUNT 12
#define A3_BIT(i) (1u << (i))
static const uint16_t A3_MASK_ALL  = 0x0FFF;
static const uint16_t A3_MASK_MIST = A3_BIT(A3_RELAY_HUMIDIFIER_1) | A3_BIT(A3_RELAY_HUMIDIFIER_2) | A3_BIT(A3_RELAY_HUMIDIFIER_3);
static const uint16_t A3_MASK_UV   = A3_BIT(A3_RELAY_UV_1) | A3_BIT(A3_RELAY_UV_2);

#pragma pack(push, 1)

// Unified Telemetry Structure Payload - shared by A1/A2 and C1/C2 (same shape, each
// device populates only the fields relevant to its own role and leaves the rest zeroed).
struct TelemetryPacket {
    uint8_t  deviceID;          // 1=A1, 4=C1, 5=C2 (A2 moved to A2SensorPacket on 2026-10-02 - the legacy
                                // relay-carrying A2 firmware still sends 2 here)
    uint32_t pulseCount;        // Allan Coin Slot pulse counter (A1/C1)
    uint16_t touchX;            // Calibrated touch X coordinate (A1/C1)
    uint16_t touchY;            // Calibrated touch Y coordinate (A1/C1)
    bool     touchPressed;      // Touch active flag (A1/C1)
    float    usAlcoholDistance; // Alcohol Tank Distance in cm (A2 only - unused/zero on C2).
                                // A1 DIAGNOSTIC (2026-10-02): shortest gap in ms between two raw coin-wire edges
                                // (>= 2 ms), 0 = no pair of edges seen yet
    float    usHelmetDistance;  // Helmet Detection Distance in cm (A2/C2)
    bool     doorEnclosure;     // true = OPEN, false = CLOSED (A2/C2 Pin 27)
    bool     doorPanel;         // true = OPEN, false = CLOSED (A2/C2 Pin 14)
    bool     doorBackdoor;      // true = OPEN, false = CLOSED (A2/C2 Pin 19)
    bool     relayStates[6];    // Current state of all relays (A2 uses [0..4], C2 uses [0..5] incl. Fan)
    uint32_t activeTimer;       // Active cycle seconds remaining.
                                // A1 DIAGNOSTIC (2026-10-02): raw coin-wire falling edges since A1 booted, counted
                                // before the mute/debounce filters (pulseCount above is what survived them)
} __attribute__((packed));

// Node ACS (D1) Telemetry Structure Payload
struct ACSTelemetryPacket {
    uint8_t  deviceID;             // DEVICE_ACS
    float    usWaterDistance;      // cm
    float    usScentedDistance;    // cm
    float    usAlcoholDistance;    // cm
    float    usMixerDistance;      // cm
    bool     waterLow;
    bool     scentedLow;
    bool     alcoholLow;
    bool     mixerLow;
    bool     relayStates[6];       // ACSRelayIndex order
    bool     acsBusy;              // true while any relay-driven action is in progress
    uint8_t  autoState;            // ACSAutoState - current phase of the automatic Mixer refill
    bool     maintenanceMode;      // true = automatic refill paused, manual relay control unrestricted
} __attribute__((packed));

// Node ACS automatic Mixer-refill sequence phases (Steps 1d->4)
enum ACSAutoState : uint8_t {
    ACS_AUTO_IDLE            = 0,
    ACS_AUTO_PRECHECK        = 1,
    ACS_AUTO_REFILL_WATER    = 2,
    ACS_AUTO_REFILL_SCENTED  = 3,
    ACS_AUTO_REFILL_ALCOHOL  = 4,
    ACS_AUTO_MIXING          = 5,
    ACS_AUTO_FINISHING       = 6,
    ACS_AUTO_DELIVERING      = 7  // Pumping mixed product from the Mixer to Box 1's Humidifier
};

// Box 1's dynamic settings, owned and NVS-persisted by Node S3A (not Local Server) - broadcast
// once a second so Local Server's decision logic (helmetPresent(), alcohol check) always has
// a fresh cached copy, and both sides resync automatically within a second of either rebooting.
// secondsPerCoin/minCoinsRequired moved here (from Local Server) once S3A took over Box 1's
// actual coin-processing/state-machine logic - S3A needs its own authoritative copy to keep
// running correctly with zero dependency on Local Server being reachable.
struct Box1ConfigPacket {
    uint8_t  deviceID;          // DEVICE_S3A
    float    helmetDetectCmA2;  // Node A2's helmet-detection distance cutoff
    float    alcLowPct;         // Alcohol tank: at/below this -> go refill
    float    alcHighPct;        // Alcohol tank: at/above this -> refill complete
    uint16_t secondsPerCoin;    // seconds of cycle time added per coin pulse
    uint32_t minCoinsRequired;  // coins that must be inserted before STATE_IDLE -> STATE_INSTRUCTIONS
    uint32_t maxCoinsAllowed;   // Added 2026-09-13, mirrors Box2ConfigPacket's field - coins beyond
                                // this stop adding cycle time on STATE_INSERT_COIN (0 = no cap)
} __attribute__((packed));

// Box 2's dynamic settings, owned and NVS-persisted by Node S3C (mirrors Box1ConfigPacket for
// Box 2's own coin economy/hardware geometry) - no alcohol fields since Box 2 (Heater) has no
// alcohol system to check/refill.
struct Box2ConfigPacket {
    uint8_t  deviceID;          // DEVICE_S3C
    float    helmetDetectCmC2;  // Node C2's helmet-detection distance cutoff
    uint16_t secondsPerCoin;    // seconds of cycle time added per coin pulse - Box 2's own copy,
                                 // independent of Box 1's (S3A's) value
    uint32_t minCoinsRequired;  // coins that must be inserted before B2_STATE_IDLE -> B2_STATE_INSTRUCTIONS
    uint32_t maxCoinsAllowed;   // Box 2 only - coins beyond this stop adding cycle time (0 = no cap)
    float    coolDownRatio;     // Added 2026-09-14, Box 2 only - fraction of Heating's actual
                                 // duration that B2_STATE_COOL_DOWN runs for (1.0 Full / 0.75 /
                                 // 0.5 Half / 0.25 Quarter - dashboard constrains it to these 4).
                                 // Independent knob from secondsPerCoin/minCoinsRequired since a
                                 // long Heating session doesn't necessarily need equally long
                                 // cooling to be safely wearable - default 1.0 preserves the
                                 // originally-shipped "matches Heating exactly" behavior.
} __attribute__((packed));

// Live state-machine status for either Box 1 (from Node S3A) or Box 2 (from Node S3C),
// broadcast once a second so Local Server's dashboard can display current step/timer without
// needing to run either state machine itself - deviceID says which box this is (same
// disambiguate-by-deviceID pattern as TelemetryPacket, shared by A1/A2 and C1/C2). Also
// carries session-completion info for stats: completedSessionSeq increments every time a
// session finishes: Local Server compares it against the last value it saw and records a
// new stat entry (using lastSessionDurationSec/lastSessionCoins) whenever it's changed. Since
// this whole packet repeats every second, a single dropped packet doesn't lose a session -
// the next second's broadcast still carries the same "latest completed session" info.
struct BoxStatusPacket {
    uint8_t  deviceID;               // DEVICE_S3A or DEVICE_S3C
    uint8_t  machineState;           // MachineState (from S3A) or Box2State (from S3C)
    uint32_t activeTimer;            // seconds remaining in the current cycle
    uint16_t stepRemainingSec;       // finish-step countdown
    bool     handshakeValid;         // door closed + helmet present, right now
    uint32_t completedSessionSeq;    // increments once per completed session
    uint16_t lastSessionDurationSec; // duration of the session completedSessionSeq refers to
    uint16_t lastSessionCoins;       // coins used in that same session
} __attribute__((packed));

// Node A3 status (added 2026-10-02) - broadcast about once a second so S3A and Local Server can show
// what the relay board is really doing and whether its own safety watchdog has taken over.
struct A3StatusPacket {
    uint8_t  deviceID;       // DEVICE_A3
    uint16_t relayMask;      // what is actually switched ON right now (A3RelayIndex bits)
    bool     orphaned;       // true = no sign of S3A for 15 s, the watchdog is forcing everything safe
    uint8_t  watchdogState;  // 0 = normal, 1 = safe-off applied, 2 = enclosure held unlocked for a trapped helmet
} __attribute__((packed));   // 5 bytes - must stay different from every other packet's size

// Node A2 sensor report (added 2026-10-02) - Box 1's SENSING-ONLY board. Once A3 owns every relay, A2 no
// longer sends TelemetryPacket (that one carries relay states and a single alcohol distance); it sends
// this instead, about once a second AND immediately whenever a door or water sensor changes. Raw values
// only - thresholds (what counts as "low", "helmet inside") stay in S3A.
struct A2SensorPacket {
    uint8_t  deviceID;       // DEVICE_A2
    float    usHelmetCm;     // helmet ultrasonic (Echo 25 / Trig 26); 0 = no echo
    float    usMcAlsCm;      // Main Container, ALCOHOL ultrasonic (Echo 33 / Trig 32); 0 = no echo
    float    usMcSlsCm;      // Main Container, SCENTED ultrasonic (Echo 22 / Trig 23); 0 = no echo
    bool     doorEnclosure;  // true = OPEN, false = CLOSED (reed on GPIO27)
    bool     doorPanel;      // true = OPEN (GPIO14)
    bool     doorBackdoor;   // true = OPEN (GPIO19)
    bool     doorAcsSide;    // true = OPEN (GPIO18) - the ACS side door
    bool     wsAlsWet;       // water sensor at the ALCOHOL container: true = in liquid. The sensor is ANALOG
                             // (GPIO34, ADC1); A2 judges wet/dry from the reading with hysteresis + debounce
    bool     wsSlsWet;       // water sensor at the SCENTED container (GPIO35): same
    uint16_t wsAlsRaw;       // raw 12-bit reading (0-4095) behind wsAlsWet - for the dashboard / calibration
    uint16_t wsSlsRaw;       // raw 12-bit reading behind wsSlsWet
    uint16_t seq;            // rolling counter - lets a listener see dropped packets / tell fresh from repeated
} __attribute__((packed));   // 25 bytes - must stay different from every other packet's size

// Universal Command Packet
struct CommandPacket {
    uint8_t  targetDeviceID;    // 1 for A1, 2 for A2, 0 for Broadcast
    uint8_t  commandID;         // CommandOpcode
    uint8_t  subIndex;          // RelayIndex or Component ID
    uint16_t param16;           // Duration / RGB565 / Lockout ms
    uint8_t  state;             // 0 = OFF, 1 = ON
    char     payloadStr[200];   // Tokenized WYSIWYG screen layout string
} __attribute__((packed));

// In-Memory Step Configuration
struct StepAction {
    char     name[20];
    uint16_t durationSec;
    uint8_t  relayBitmask;       // bit0: EncLock, bit1: PanLock, bit2: BakLock, bit3: Mist, bit4: UV
    char     displayTokens[200]; // Tokenized screen string: BG|ST,...|TT,...|BTN,...|SHP,...
} __attribute__((packed));

#pragma pack(pop)

// Master's onDataRecv() dispatches an incoming ESP-NOW packet by matching its raw byte
// length against sizeof() of each struct below - there is no explicit packet-type tag.
// If two structs are ever the same size, a packet gets silently memcpy'd into the wrong
// struct instead of erroring (this already happened once: TelemetryPacket and
// ACSTelemetryPacket briefly collided at 30 bytes each right after ACS was added, which
// silently broke Box 1's coin counting until relayStates[5]->[6] shifted TelemetryPacket
// to 31 bytes). These asserts turn any future collision into a compile error instead of
// a silent runtime bug - if one of these fires, dispatch-by-length is no longer safe and
// either the colliding struct needs padding or onDataRecv() needs an explicit type tag.
static_assert(sizeof(CommandPacket) != sizeof(TelemetryPacket),
              "CommandPacket and TelemetryPacket are now the same size - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(CommandPacket) != sizeof(ACSTelemetryPacket),
              "CommandPacket and ACSTelemetryPacket are now the same size - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(TelemetryPacket) != sizeof(ACSTelemetryPacket),
              "TelemetryPacket and ACSTelemetryPacket are now the same size - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(Box1ConfigPacket) != sizeof(CommandPacket) &&
              sizeof(Box1ConfigPacket) != sizeof(TelemetryPacket) &&
              sizeof(Box1ConfigPacket) != sizeof(ACSTelemetryPacket),
              "Box1ConfigPacket collides in size with another packet type - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(Box2ConfigPacket) != sizeof(CommandPacket) &&
              sizeof(Box2ConfigPacket) != sizeof(TelemetryPacket) &&
              sizeof(Box2ConfigPacket) != sizeof(ACSTelemetryPacket) &&
              sizeof(Box2ConfigPacket) != sizeof(Box1ConfigPacket),
              "Box2ConfigPacket collides in size with another packet type - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(BoxStatusPacket) != sizeof(CommandPacket) &&
              sizeof(BoxStatusPacket) != sizeof(TelemetryPacket) &&
              sizeof(BoxStatusPacket) != sizeof(ACSTelemetryPacket) &&
              sizeof(BoxStatusPacket) != sizeof(Box1ConfigPacket) &&
              sizeof(BoxStatusPacket) != sizeof(Box2ConfigPacket),
              "BoxStatusPacket collides in size with another packet type - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(A3StatusPacket) != sizeof(CommandPacket) &&
              sizeof(A3StatusPacket) != sizeof(TelemetryPacket) &&
              sizeof(A3StatusPacket) != sizeof(ACSTelemetryPacket) &&
              sizeof(A3StatusPacket) != sizeof(Box1ConfigPacket) &&
              sizeof(A3StatusPacket) != sizeof(Box2ConfigPacket) &&
              sizeof(A3StatusPacket) != sizeof(BoxStatusPacket),
              "A3StatusPacket collides in size with another packet type - onDataRecv() dispatch-by-length will misroute packets");
static_assert(sizeof(A2SensorPacket) != sizeof(CommandPacket) &&
              sizeof(A2SensorPacket) != sizeof(TelemetryPacket) &&
              sizeof(A2SensorPacket) != sizeof(ACSTelemetryPacket) &&
              sizeof(A2SensorPacket) != sizeof(Box1ConfigPacket) &&
              sizeof(A2SensorPacket) != sizeof(Box2ConfigPacket) &&
              sizeof(A2SensorPacket) != sizeof(BoxStatusPacket) &&
              sizeof(A2SensorPacket) != sizeof(A3StatusPacket),
              "A2SensorPacket collides in size with another packet type - onDataRecv() dispatch-by-length will misroute packets");

#endif // SHARED_COMMON_H