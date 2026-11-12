#pragma once
// =====================================================================
//  UNIT IDENTITY for terminal A1 (Box 1)  -  the ONLY file that differs between ESP32_A1 / ESP32_B1 / ESP32_C1. (2026-11-12)
//  the original Box 1 unit: rotation 1, coin on GPIO 23
// =====================================================================
#define NODE_NAME        "A1"                  // log prefix
#define BOX_NAME         "Box 1"
#define MY_ID            DEVICE_A1             // this terminal
#define MY_S3            DEVICE_S3A            // the box's main controller it talks to
#define MY_SENS          DEVICE_A2            // the box's sensor board (its reports drive the OUT OF ORDER message and beep)
#define BOX_TAG          0                    // state tag of the main controller's CMD_SET_CONFIG broadcasts (Box 1 = 0, Box 2 = 2, Box 3 = 3)
#define BOX_NVS          "a1_nvs"           // flash namespace (the helmet cutoff it remembers)
#define PIN_COIN_SIGNAL  23                   // coin acceptor signal (A1: 23, B1 and C1: 32)
#define TFT_ROTATION     1                    // 1 or 3 (the screen is mounted one way or the other)
#define TFT_CAL_DATA     {327, 3421, 285, 3510, 1}       // touch calibration (5 numbers; the last is the rotation flag)
