"use strict";
/* Names, in the same order the machine firmware numbers them (copied from the Control Center Home page script).
   If a step is added to a machine's firmware, add its name at the same position here. */

/* Box 1 and Box 3 steps (MachineState) */
const ST13 = ["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1"];
/* Box 2 steps (Box2State) */
const ST2  = ["IDLE","OPEN A","SENSORS (old)","HEATING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","COOL DOWN","RATE B","CANCEL","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","HELMET NOTICE A","HELMET NOTICE B","OUT OF ORDER"];

/* Refill station (ACS) steps, pumps and warning bits */
const ACSSTEP = ["IDLE","ALCOHOL MIXER: WATER IN","ALCOHOL MIXER: ALCOHOL IN","SCENTED MIXER: WATER IN","SCENTED MIXER: SCENTED IN","DELIVERING ALCOHOL","DELIVERING SCENTED","WATER EMPTY","ALCOHOL EMPTY","SCENTED EMPTY","MAINTENANCE"];
const PUMPS = ["alcohol pump","water pump (alcohol)","water pump (scented)","scented pump","mixer alcohol pump","mixer scented pump"];
const ACSWARN = [[1,'no echo from the alcohol mixer ultrasonic'],[2,'no echo from the scented mixer ultrasonic'],[4,'alcohol mixer: water sensor wet but the level reads far from full'],[8,'scented mixer: water sensor wet but the level reads far from full'],[16,'alcohol mixer holds a half-made mix'],[32,'scented mixer holds a half-made mix'],[64,'a pump hit its time limit']];

/* The 13 boards, in the order of the "n" flags */
const NODES = ["A1 Terminal (Box 1)","A2 Sensors (Box 1)","A3 Relays (Box 1)","S3A Brain (Box 1)","C1 Terminal (Box 2)","C2 Sensors + relays (Box 2)","S3C Brain (Box 2)","B1 Terminal (Box 3)","B2 Sensors (Box 3)","B3 Relays (Box 3)","S3B Brain (Box 3)","ACS 1 (Box 1)","ACS 2 (Box 3)"];

/* The three boxes: their step names, doors, relay names (bit order of b[i][8]) and which "n" flags belong to them */
const BOXES = [
  { name:'Box 1', role:'Cleaner',         st:ST13, tanks:true,  doors:['Enclosure','Panel','Back','ACS side'],
    brain:3, term:0, sensors:1, relayBoard:2,
    relays:['Enclosure lock','Panel lock','Back lock','Humidifier 1 (alcohol)','UV 1','Humidifier 2 (scented)','Humidifier 3 (both)','UV 2','Drain pump 1','Drain pump 2','ACS door lock','Coin slot power'] },
  { name:'Box 2', role:'Dryer',           st:ST2,  tanks:false, doors:['Enclosure','Panel','Back'],
    brain:6, term:4, sensors:5, relayBoard:5,
    relays:['Enclosure lock','Panel lock','Back lock','Heater','UV','Fan'] },
  { name:'Box 3', role:'Cleaner + Dryer', st:ST13, tanks:true,  doors:['Enclosure','Panel','Back','ACS side'],
    brain:10, term:7, sensors:8, relayBoard:9,
    relays:['Enclosure lock','Panel lock','Back lock','Humidifier 1 (alcohol)','UV 1','Humidifier 2 (scented)','Humidifier 3 (both)','UV 2','Drain pump 1','Drain pump 2','ACS door lock','Coin slot power','Heater'] }
];

/* Which steps count as "running", "needs attention" or "idle" (the rest are "customer is using it") */
const RUNNING = new Set(['CLEANING','HEATING','DRYING','COOL DOWN']);
const ATTN    = new Set(['SAFETY PAUSE','ABORT','CANCEL','SELECT DRY ONLY']);
const IDLE    = new Set(['IDLE','TAPS']);
