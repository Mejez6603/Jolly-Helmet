#pragma once
// =====================================================================
//  BOARD IDENTITY for relay board C3 (Box 2)  -  the ONLY file that differs between ESP32_A3 / ESP32_B3 / ESP32_C3. (2026-11-12)
// =====================================================================
#define NODE_NAME   "C3"
#define BOX_NAME    "Box 2"
#define MY_ID       DEVICE_C3         // the deviceID this board puts into its A3StatusPacket / answers to
#define MY_S3       DEVICE_S3C        // the box's main controller (its heartbeat is this board's proof of life)
#define S3_NAME     "S3C"
#define MY_SENS     DEVICE_C2        // the box's sensor board (its door + helmet reports feed the trapped-helmet watchdog)
#define SENS_NAME   "C2"
#define BOX_TAG     2                // state tag of the main controller's CMD_SET_CONFIG broadcasts (Box 1 = 0, Box 2 = 2, Box 3 = 3): carries the helmet cutoff
#define BOX_NVS     "c3_nvs"      // flash namespace (remembers the helmet cutoff through a power cut)
