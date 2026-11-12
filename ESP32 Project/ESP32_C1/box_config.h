#pragma once
// =====================================================================
//  UNIT IDENTITY for terminal C1 (Box 2)  -  the ONLY file that differs between ESP32_A1 / ESP32_B1 / ESP32_C1. (2026-11-12)
//  this unit is mounted upside down (rotation 3) and has its own touch calibration; coin on GPIO 32 since 2026-10-16
// =====================================================================
#define NODE_NAME        "C1"                  // log prefix
#define BOX_NAME         "Box 2"
#define MY_ID            DEVICE_C1             // this terminal
#define MY_S3            DEVICE_S3C            // the box's main controller it talks to
#define MY_SENS          DEVICE_C2            // the box's sensor board (its reports drive the OUT OF ORDER message and beep)
#define BOX_TAG          2                    // state tag of the main controller's CMD_SET_CONFIG broadcasts (Box 1 = 0, Box 2 = 2, Box 3 = 3)
#define BOX_NVS          "c1_nvs"           // flash namespace (the helmet cutoff it remembers)
#define PIN_COIN_SIGNAL  32                   // coin acceptor signal (A1: 23, B1 and C1: 32)
#define TFT_ROTATION     3                    // 1 or 3 (the screen is mounted one way or the other)
#define TFT_CAL_DATA     {408, 3325, 535, 3051, 7}       // touch calibration (5 numbers; the last is the rotation flag)
