// =====================================================================
//  RESTATE PACKET (2026-10-11) - shared by ESP32_RESTATE (sender), ESP32_LOCALSRV and ESP32_ONLINESRV (receivers).
//  Kept in its own file so Shared_Common.h (identical in every sketch folder) is not touched.
//
//  The spare ESP32_RESTATE board broadcasts it (ESP-NOW, channel 1) when the user presses "Update" on its page. It carries the OLD data
//  frozen at the capture, restated at the new price list. A receiver stores it (flash), so the spare board can be unplugged afterwards, and
//  works out "since the capture" from its OWN running totals:
//      restated coins   = oldRestCoins + (its total coins - baseCoins)
//      restated revenue = oldRestCoins x cv + (its total coins - baseCoins) x its coin value
//  The coins really collected are never touched anywhere: the receivers show them beside the restated figure.
// =====================================================================
#pragma once
#include "Shared_Common.h"

#define RESTATE_MAGIC 0xB7

struct RestatePacket {
    uint8_t  deviceID;          // DEVICE_SERVER (it speaks about the LocalServer's totals)
    uint8_t  magic;             // RESTATE_MAGIC
    uint8_t  active;            // 1 = restated data, 0 = withdrawn (receivers forget it)
    uint8_t  mode;              // 0 = show the restated figures BESIDE the collected ones; 1 = APPLY: replace the receiver's coin total with the corrected one (the original is archived and can be restored)
    uint16_t rev;               // +1 on every press of Update: a receiver stores a packet only when this (or "active") changed
    uint16_t cvX100;            // coin value at the capture, pesos x 100
    uint32_t capturedAt;        // unix time of the capture (clock of the device that pressed Capture), 0 = unknown
    int16_t  tzMin;             // that device's JS getTimezoneOffset() (UTC+8 = -480)
    uint16_t cpmNewX100;        // blended NEW coins per minute x 100 (information only)
    uint32_t baseSessions;      // the LocalServer's totals at the capture ...
    uint32_t baseCoins;
    uint32_t baseDurSec;
    uint32_t oldRestCoins;      // ... and the old data restated at the new price list
} __attribute__((packed));      // 32 bytes - must stay different from every other packet's size

static_assert(sizeof(RestatePacket) != sizeof(CommandPacket) && sizeof(RestatePacket) != sizeof(TelemetryPacket) &&
              sizeof(RestatePacket) != sizeof(ACSTelemetryPacket) && sizeof(RestatePacket) != sizeof(Box1ConfigPacket) &&
              sizeof(RestatePacket) != sizeof(Box2ConfigPacket) && sizeof(RestatePacket) != sizeof(BoxStatusPacket) &&
              sizeof(RestatePacket) != sizeof(A3StatusPacket) && sizeof(RestatePacket) != sizeof(A2SensorPacket) &&
              sizeof(RestatePacket) != sizeof(ACSStatusPacket) && sizeof(RestatePacket) != sizeof(HubStatsPacket),
              "RestatePacket must have a size no other packet has");

// What the LocalServer keeps (flash) after it applied a mode-1 update: enough to show a one-line disclosure and to put the original figures back.
struct RestateAdj {
    uint8_t  on;
    uint8_t  pad;
    uint16_t rev;               // the update that was applied
    int16_t  tzMin;
    uint16_t cvX100;            // coin value at the capture
    uint32_t capturedAt;
    uint32_t baseSessions;      // the totals AT THE CAPTURE (the original figures) ...
    uint32_t baseCoins;
    uint32_t restCoins;         // ... and what the old coins were restated to
};
