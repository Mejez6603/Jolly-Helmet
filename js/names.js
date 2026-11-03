"use strict";
/* Names, in the same order the machine firmware numbers them (copied from the Control Center Home page script).
   If a step is added to a machine's firmware, add its name at the same position here. */

/* Box 1 steps (MachineState) - Box 3 uses the same numbers, see ST3 below */
const ST13 = ["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1","AUTO DRY","START DRYING"];
/* Box 3 steps (the trimmed 36-step flow of 2026-11-03): the same numbers, no ratings / time picker / mist choice / free retry any more ("(old)" = a Box 1 step Box 3 does not use) */
const ST3 = ["IDLE","OPEN A","CHECKING","REFILLING (old)","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A (old)","TIME ALLOT (old)","INSERT COIN","RATE B (old)","CANCEL","METHOD (old)","CHECKING S (old)","REFILLING S (old)","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY (old)","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1 (old)","AUTO DRY","START DRYING"];
/* Box 2 steps (Box2State) */
const ST2  = ["IDLE","OPEN A","SENSORS (old)","HEATING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","COOL DOWN","RATE B","CANCEL","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","HELMET NOTICE A","HELMET NOTICE B","OUT OF ORDER"];

/* The 12 boards, in the order of the "n" flags: the 11 boards of the three boxes, then the Refilling Station (D1, replaces the old ACS 1 and ACS 2; the Online Server firmware of 2026-11-03 sends 12 flags,
   older firmware sent 13 with ACS 1 and ACS 2 at places 12 and 13: the page then does not know the station).
   2026-10-24: plain names instead of the board codes (A1 B1 C1 = TFT display, A2 B2 = sensors, A3 B3 = relay controller, C2 = sensors + relay controller,
   S3A S3B S3C = main controller); the codes stay in the firmware, the Control Center and the setup page. */
const NODES = ["Box 1 TFT Display","Box 1 Sensors","Box 1 Relay Controller","Box 1 Main Controller","Box 2 TFT Display","Box 2 Sensors / Relay Controller","Box 2 Main Controller","Box 3 TFT Display","Box 3 Sensors","Box 3 Relay Controller","Box 3 Main Controller","Refilling Station"];

/* The three boxes: their step names, doors, relay names (bit order of b[i][8]) and which "n" flags belong to them.
   The two drain pumps are gone from the design (manual valve instead); today's firmware still has them at bits 8 and 9, so those two places stay in the
   list as "(unused)" to keep every later bit where it is. When the firmware's relay order changes, change the list here in the same order. */
const STATION = 11;                                   // place of the Refilling Station in the "n" flags

const BOXES = [
  { name:'Box 1', role:'Cleaner + Dryer', st:ST13, doors:['Enclosure','Panel','Back','Side'],
    brain:3, term:0, sensors:1, relayBoard:2,
    relays:['Enclosure lock','Panel lock','Back lock','Humidifier 1','UV 1','Humidifier 2','Humidifier 3','UV 2','(unused)','(unused)','Side lock','Coin slot power'] },
  { name:'Box 2', role:'Cleaner + Dryer', st:ST2,  doors:['Enclosure','Panel','Back'],
    brain:6, term:4, sensors:5, relayBoard:5,
    relays:['Enclosure lock','Panel lock','Back lock','Heater','UV','Fan'] },
  { name:'Box 3', role:'Cleaner + Dryer', st:ST3,  doors:['Enclosure','Panel','Back','Side'],
    brain:10, term:7, sensors:8, relayBoard:9,
    relays:['Enclosure lock','Panel lock','Back lock','Humidifier 1','UV 1','Humidifier 2','Humidifier 3','UV 2','(unused)','(unused)','Side lock','Coin slot power','Heater'] }
];
/* Which steps count as "running", "needs attention" or "idle" (the rest are "customer is using it") */
const RUNNING = new Set(['CLEANING','HEATING','DRYING','AUTO DRY','COOL DOWN']);
const ATTN    = new Set(['SAFETY PAUSE','ABORT','CANCEL','SELECT DRY ONLY']);
const IDLE    = new Set(['IDLE','TAPS']);
