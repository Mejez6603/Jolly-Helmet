"use strict";
/* Names, in the same order the machine firmware numbers them (copied from the Control Center Home page script).
   If a step is added to a machine's firmware, add its name at the same position here. */

/* The steps of ALL THREE boxes (since 2026-11-12 Box 1 and Box 2 run the same 36-step flow as Box 3: one program, the same numbers, no ratings / time picker /
   mist choice / free retry; "(unused)" = a number the flow no longer uses). Before 2026-11-12 Box 1 had the older 36-step list and Box 2 its own 25-step list. */
const STEPS = ["IDLE","OPEN A","CHECKING","REFILLING (unused)","SENSORS (unused)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A (unused)","TIME ALLOT (unused)","INSERT COIN","RATE B (unused)","CANCEL","METHOD (unused)","CHECKING S (unused)","REFILLING S (unused)","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY (unused)","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1 (unused)","AUTO DRY","START DRYING"];

/* The 13 boards, in the order of the "n" flags: the boards of the three boxes (Box 1 = places 0-3, Box 2 = 4-6, Box 3 = 7-10), then the Refilling Station (D1, place 11, replaced the old ACS 1 and ACS 2),
   then Box 2's relay board C3 (place 13 / index 12, ADDED 2026-11-12 at the END so no earlier place moved). Firmware older than 2026-11-12 sends 12 flags: the page then
   treats the missing 13th as "not heard" (see nodeFlags in render.js) - update the Online Server together with this page. Older than 2026-11-03 it sent 13 with ACS 1 and ACS 2 at places 12 and 13: the page then does not know the station.
   2026-10-24: plain names instead of the board codes (A1 B1 C1 = TFT display, A2 B2 C2 = sensors, A3 B3 C3 = relay controller, S3A S3B S3C = main controller);
   the codes stay in the firmware, the Control Center and the setup page. */
const NODES = ["Box 1 TFT Display","Box 1 Sensors","Box 1 Relay Controller","Box 1 Main Controller","Box 2 TFT Display","Box 2 Sensors","Box 2 Main Controller","Box 3 TFT Display","Box 3 Sensors","Box 3 Relay Controller","Box 3 Main Controller","Refilling Station","Box 2 Relay Controller"];

/* The three boxes: their step names, doors, relay names (bit order of b[i][8]) and which "n" flags belong to them.
   The two drain pumps are gone from the design (manual valve instead); the relay boards still have their two places, so bits 8 and 9 stay in the
   list as "(unused)" to keep every later bit where it is. When the firmware's relay order changes, change the list here in the same order.
   All three boxes have the same 13 relays (bit 12 = the heater, a hair dryer aimed at the helmet) and the same four door reeds. */
const STATION = 11;                                   // place of the Refilling Station in the "n" flags
const RELAYS = ['Enclosure lock','Panel lock','Back lock','Humidifier 1','UV 1','Humidifier 2','Humidifier 3','UV 2','(unused)','(unused)','Side lock','Coin slot power','Heater'];
const DOORS  = ['Enclosure','Panel','Back','Side'];

const BOXES = [
  { name:'Box 1', role:'Cleaner + Dryer', st:STEPS, doors:DOORS, brain:3,  term:0, sensors:1, relayBoard:2,  relays:RELAYS },
  { name:'Box 2', role:'Cleaner + Dryer', st:STEPS, doors:DOORS, brain:6,  term:4, sensors:5, relayBoard:12, relays:RELAYS },
  { name:'Box 3', role:'Cleaner + Dryer', st:STEPS, doors:DOORS, brain:10, term:7, sensors:8, relayBoard:9,  relays:RELAYS }
];
/* Which steps count as "running", "needs attention" or "idle" (the rest are "customer is using it") */
const RUNNING = new Set(['CLEANING','DRYING','AUTO DRY','COOL DOWN']);
const ATTN    = new Set(['SAFETY PAUSE','ABORT','CANCEL','SELECT DRY ONLY']);
const IDLE    = new Set(['IDLE','TAPS']);
