#pragma once
// =====================================================================
//  BOARD IDENTITY for sensor board C2 (Box 2)  -  the ONLY file that differs between ESP32_A2 / ESP32_B2 / ESP32_C2. (2026-11-12)
//  Pins are the same on all three boards: helmet ultrasonic Echo 25 / Trig 26, door reeds Enclosure 27, Panel 14, Backdoor 19, Side door 18.
// =====================================================================
#define NODE_NAME   "C2"
#define BOX_NAME    "Box 2"
#define MY_ID       DEVICE_C2       // the deviceID this board puts into its A2SensorPacket
