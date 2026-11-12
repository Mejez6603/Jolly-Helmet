#pragma once
// =====================================================================
//  BOARD IDENTITY for sensor board A2 (Box 1)  -  the ONLY file that differs between ESP32_A2 / ESP32_B2 / ESP32_C2. (2026-11-12)
//  Pins are the same on all three boards: helmet ultrasonic Echo 25 / Trig 26, door reeds Enclosure 27, Panel 14, Backdoor 19, Side door 18.
// =====================================================================
#define NODE_NAME   "A2"
#define BOX_NAME    "Box 1"
#define MY_ID       DEVICE_A2       // the deviceID this board puts into its A2SensorPacket
