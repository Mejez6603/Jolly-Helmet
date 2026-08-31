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
    DEVICE_C2     = 5  // Box 2 (Heater) Actuator Hub - same role/pins as A2, Heater instead of Humidifier, +Fan
};

enum MachineState : uint8_t {
    STATE_IDLE           = 0, // Step 0: Welcome, waiting for coin
    STATE_INSTRUCTIONS   = 1, // Step 1: waiting for Enclosure door OPEN
    STATE_CHECKING       = 2, // Step 2: checking alcohol level
    STATE_REFILLING      = 3, // Step 3: waiting for alcohol refill to reach HIGH threshold
    STATE_SENSORS        = 4, // Step 4: waiting for door CLOSED + helmet detected
    STATE_CLEANING       = 5, // Step 5: UV + Mist active
    STATE_ABORT_CONFIRM  = 6, // Step 5b: user-initiated abort confirmation dialog
    STATE_RETRIEVE       = 7, // Step 6: waiting for door OPEN + helmet removed
    STATE_FINISH         = 8, // Step 7: thank-you screen, then loops back to Step 0
    STATE_PAUSED_SAFETY  = 9  // Mid-cleaning safety breach (unexpected door/helmet violation)
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
    CMD_REQUEST_DELIVERY  = 10  // Master -> ACS: deliver param16 mL of mixed product to Box 1's Humidifier
};

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

#pragma pack(push, 1)

// Unified Telemetry Structure Payload - shared by A1/A2 and C1/C2 (same shape, each
// device populates only the fields relevant to its own role and leaves the rest zeroed).
struct TelemetryPacket {
    uint8_t  deviceID;          // 1=A1, 2=A2, 4=C1, 5=C2
    uint32_t pulseCount;        // Allan Coin Slot pulse counter (A1/C1)
    uint16_t touchX;            // Calibrated touch X coordinate (A1/C1)
    uint16_t touchY;            // Calibrated touch Y coordinate (A1/C1)
    bool     touchPressed;      // Touch active flag (A1/C1)
    float    usAlcoholDistance; // Alcohol Tank Distance in cm (A2 only - unused/zero on C2)
    float    usHelmetDistance;  // Helmet Detection Distance in cm (A2/C2)
    bool     doorEnclosure;     // true = OPEN, false = CLOSED (A2/C2 Pin 27)
    bool     doorPanel;         // true = OPEN, false = CLOSED (A2/C2 Pin 14)
    bool     doorBackdoor;      // true = OPEN, false = CLOSED (A2/C2 Pin 19)
    bool     relayStates[6];    // Current state of all relays (A2 uses [0..4], C2 uses [0..5] incl. Fan)
    uint32_t activeTimer;       // Active cycle seconds remaining
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

#endif // SHARED_COMMON_H