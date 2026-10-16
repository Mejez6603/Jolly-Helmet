// =====================================================================
//  ADJUST PACKET (2026-10-11) - "refunds / corrections" the owner enters on the LocalServer Home page.
//  The LocalServer counts every coin pulse as revenue. When money was given back (for example customers refunded after the early pricing error),
//  the net revenue is the coins counted MINUS those refunds. The owner types the refunded amount and a short reason on the Control Center; the
//  LocalServer shows   gross - corrections = net   and broadcasts this packet every 10 s so the Online Server shows the same.
//  The coin counters are never changed: the gross figure and the reason stay visible beside the net figure.
//  Own file so Shared_Common.h (identical in every sketch folder) is not touched.
// =====================================================================
#pragma once
#include "Shared_Common.h"
#include "Restate_Common.h"

#define ADJUST_MAGIC 0xA9

struct AdjustPacket {
    uint8_t  deviceID;          // DEVICE_SERVER
    uint8_t  magic;             // ADJUST_MAGIC
    uint16_t rev;               // +1 every time the owner saves; a receiver stores a packet only when this changed
    uint32_t pesosX100;         // refunds / corrections in pesos x 100 (0 = none)
    char     note[44];          // the reason, plain text without quotes
} __attribute__((packed));      // 52 bytes - must stay different from every other packet's size

static_assert(sizeof(AdjustPacket) != sizeof(CommandPacket) && sizeof(AdjustPacket) != sizeof(TelemetryPacket) &&
              sizeof(AdjustPacket) != sizeof(ACSTelemetryPacket) && sizeof(AdjustPacket) != sizeof(Box1ConfigPacket) &&
              sizeof(AdjustPacket) != sizeof(Box2ConfigPacket) && sizeof(AdjustPacket) != sizeof(BoxStatusPacket) &&
              sizeof(AdjustPacket) != sizeof(A3StatusPacket) && sizeof(AdjustPacket) != sizeof(A2SensorPacket) &&
              sizeof(AdjustPacket) != sizeof(ACSStatusPacket) && sizeof(AdjustPacket) != sizeof(HubStatsPacket) &&
              sizeof(AdjustPacket) != sizeof(RestatePacket),
              "AdjustPacket must have a size no other packet has");
