#pragma once
// =====================================================================
//  BOX IDENTITY for Box 3 (S3B)  -  the ONLY file that differs between ESP32_S3A / ESP32_S3B / ESP32_S3C. (2026-11-12)
//  Box 1 = S3A + A1 (terminal) + A2 (sensors) + A3 (relays);  Box 2 = S3C + C1 + C2 + C3;  Box 3 = S3B + B1 + B2 + B3.
// =====================================================================
#define BOX_NUMBER       3
#define BOX_NAME         "Box 3"
#define NODE_NAME        "S3B"                // log prefix
#define MY_S3            DEVICE_S3B                 // this main controller
#define MY_TERM          DEVICE_B1                 // TFT touch + coin + buzzer terminal
#define MY_SENS          DEVICE_B2                 // sensor board (helmet ultrasonic + door reeds)
#define MY_REL           DEVICE_B3                 // relay board (13 channels incl. the heater)
#define TERM_NAME        "B1"
#define SENS_NAME        "B2"
#define REL_NAME         "B3"
#define BOX_TAG          3                         // state tag of this box's CMD_SET_CONFIG broadcasts: Box 1 = 0, Box 2 = 2, Box 3 = 3 (4 = the Refilling Station)
#define BOX_STATION_IDX  2                         // this box's slot in the RefillStatusPacket arrays: Box 1 = 0, Box 2 = 1, Box 3 = 2
#define BOX_NVS          "s3b_nvs"             // flash namespace (kept as before so a reflashed unit keeps its rules and its booked-coin total)
#define BOX_PULSE_KEY    "last_b1_pulses"      // flash key of the last coin-pulse count seen from the terminal (kept as before)
