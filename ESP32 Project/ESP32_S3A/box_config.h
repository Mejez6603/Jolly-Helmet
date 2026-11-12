#pragma once
// =====================================================================
//  BOX IDENTITY for Box 1 (S3A)  -  the ONLY file that differs between ESP32_S3A / ESP32_S3B / ESP32_S3C. (2026-11-12)
//  Box 1 = S3A + A1 (terminal) + A2 (sensors) + A3 (relays);  Box 2 = S3C + C1 + C2 + C3;  Box 3 = S3B + B1 + B2 + B3.
// =====================================================================
#define BOX_NUMBER       1
#define BOX_NAME         "Box 1"
#define NODE_NAME        "S3A"                // log prefix
#define MY_S3            DEVICE_S3A                 // this main controller
#define MY_TERM          DEVICE_A1                 // TFT touch + coin + buzzer terminal
#define MY_SENS          DEVICE_A2                 // sensor board (helmet ultrasonic + door reeds)
#define MY_REL           DEVICE_A3                 // relay board (13 channels incl. the heater)
#define TERM_NAME        "A1"
#define SENS_NAME        "A2"
#define REL_NAME         "A3"
#define BOX_TAG          0                         // state tag of this box's CMD_SET_CONFIG broadcasts: Box 1 = 0, Box 2 = 2, Box 3 = 3 (4 = the Refilling Station)
#define BOX_STATION_IDX  0                         // this box's slot in the RefillStatusPacket arrays: Box 1 = 0, Box 2 = 1, Box 3 = 2
#define BOX_NVS          "s3a_nvs"             // flash namespace (kept as before so a reflashed unit keeps its rules and its booked-coin total)
#define BOX_PULSE_KEY    "last_a1_pulses"      // flash key of the last coin-pulse count seen from the terminal (kept as before)
