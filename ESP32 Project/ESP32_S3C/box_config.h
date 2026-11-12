#pragma once
// =====================================================================
//  BOX IDENTITY for Box 2 (S3C)  -  the ONLY file that differs between ESP32_S3A / ESP32_S3B / ESP32_S3C. (2026-11-12)
//  Box 1 = S3A + A1 (terminal) + A2 (sensors) + A3 (relays);  Box 2 = S3C + C1 + C2 + C3;  Box 3 = S3B + B1 + B2 + B3.
// =====================================================================
#define BOX_NUMBER       2
#define BOX_NAME         "Box 2"
#define NODE_NAME        "S3C"                // log prefix
#define MY_S3            DEVICE_S3C                 // this main controller
#define MY_TERM          DEVICE_C1                 // TFT touch + coin + buzzer terminal
#define MY_SENS          DEVICE_C2                 // sensor board (helmet ultrasonic + door reeds)
#define MY_REL           DEVICE_C3                 // relay board (13 channels incl. the heater)
#define TERM_NAME        "C1"
#define SENS_NAME        "C2"
#define REL_NAME         "C3"
#define BOX_TAG          2                         // state tag of this box's CMD_SET_CONFIG broadcasts: Box 1 = 0, Box 2 = 2, Box 3 = 3 (4 = the Refilling Station)
#define BOX_STATION_IDX  1                         // this box's slot in the RefillStatusPacket arrays: Box 1 = 0, Box 2 = 1, Box 3 = 2
#define BOX_NVS          "s3c_nvs"             // flash namespace (kept as before so a reflashed unit keeps its rules and its booked-coin total)
#define BOX_PULSE_KEY    "last_c1_pulses"      // flash key of the last coin-pulse count seen from the terminal (kept as before)
