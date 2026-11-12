#pragma once
// =====================================================================
//  UNIT IDENTITY for terminal B1 (Box 3)  -  the ONLY file that differs between ESP32_A1 / ESP32_B1 / ESP32_C1. (2026-11-12)
//  rotation 3; the touch numbers are still Box 1's (not calibrated yet: run ESP32_C1_CALIBRATE and put the 5 numbers here); coin on GPIO 32 since 2026-11-04
// =====================================================================
#define NODE_NAME        "B1"                  // log prefix
#define BOX_NAME         "Box 3"
#define MY_ID            DEVICE_B1             // this terminal
#define MY_S3            DEVICE_S3B            // the box's main controller it talks to
#define MY_SENS          DEVICE_B2            // the box's sensor board (its reports drive the OUT OF ORDER message and beep)
#define BOX_TAG          3                    // state tag of the main controller's CMD_SET_CONFIG broadcasts (Box 1 = 0, Box 2 = 2, Box 3 = 3)
#define BOX_NVS          "b1_nvs"           // flash namespace (the helmet cutoff it remembers)
#define PIN_COIN_SIGNAL  32                   // coin acceptor signal (A1: 23, B1 and C1: 32)
#define TFT_ROTATION     3                    // 1 or 3 (the screen is mounted one way or the other)
#define TFT_CAL_DATA     {327, 3421, 285, 3510, 7}       // touch calibration (5 numbers; the last is the rotation flag)
