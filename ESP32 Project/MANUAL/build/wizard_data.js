"use strict";
/* GENERATED from MANUAL\build\flowgen.html (the same decision trees as the manual). Do not edit by hand. */
const WIZARD_TREES = [
 {
  "id": "tree1",
  "title": "A box shows OUT OF ORDER",
  "nodes": {
   "S": {
    "k": "start",
    "t": "A box shows OUT OF ORDER"
   },
   "Q1": {
    "k": "dec",
    "t": "Dashboard: is a board of that box OFFLINE?"
   },
   "A1": {
    "k": "act",
    "t": "Fix that board first: see \"A board shows OFFLINE\""
   },
   "Q2": {
    "k": "dec",
    "t": "Door shut with a helmet inside?"
   },
   "A2": {
    "k": "act",
    "t": "The lock opens by itself and the buzzer beeps: take the helmet out, close the door"
   },
   "Q3": {
    "k": "dec",
    "t": "After a power cut?"
   },
   "A3": {
    "k": "act",
    "t": "The box S3 restarts by itself. Still stuck: power-cycle the S3, check the UPS"
   },
   "E": {
    "k": "end",
    "t": "Still stuck? Read the Serial log of the box S3"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "yes"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "no"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "E",
    "l": "no"
   }
  ]
 },
 {
  "id": "tree2",
  "title": "A board shows OFFLINE (Control Center Home)",
  "nodes": {
   "S": {
    "k": "start",
    "t": "A board shows OFFLINE (Control Center Home)"
   },
   "Q1": {
    "k": "dec",
    "t": "Is the board powered (5 V)?"
   },
   "A1": {
    "k": "act",
    "t": "No: check the supply, fuse, cable and the DC-DC module"
   },
   "Q2": {
    "k": "dec",
    "t": "All boards offline at once?"
   },
   "A2": {
    "k": "act",
    "t": "Yes: power-cycle the LocalServer, then the brains (S3)"
   },
   "Q3": {
    "k": "dec",
    "t": "Just flashed or replaced?"
   },
   "A3": {
    "k": "act",
    "t": "Right sketch folder? Same Shared_Common.h as ESP32_S3A? Reflash"
   },
   "Q4": {
    "k": "dec",
    "t": "A bench or test sketch running?"
   },
   "A4": {
    "k": "act",
    "t": "Switch it off: it fights the real board"
   },
   "E": {
    "k": "end",
    "t": "Reset it, read the Serial log (115200): brown-out?"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "no"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "no"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "Q4",
    "l": "no"
   },
   {
    "a": "Q4",
    "b": "A4",
    "l": "yes"
   },
   {
    "a": "Q4",
    "b": "E",
    "l": "no"
   }
  ]
 },
 {
  "id": "tree3",
  "title": "Coin inserted, the count does not move",
  "nodes": {
   "S": {
    "k": "start",
    "t": "Coin inserted, the count does not move"
   },
   "Q1": {
    "k": "dec",
    "t": "Is the screen on INSERT COIN?"
   },
   "A1": {
    "k": "act",
    "t": "No: normal. The coin slot only has power while paying"
   },
   "Q2": {
    "k": "dec",
    "t": "12 V at the slot during INSERT COIN?"
   },
   "A2": {
    "k": "act",
    "t": "No: COIN POWER relay (A3, B3), 12 V supply or its wiring"
   },
   "Q3": {
    "k": "dec",
    "t": "Does the COIN wire go LOW as a coin passes?"
   },
   "A3": {
    "k": "act",
    "t": "No: acceptor speed switch, coin path, COIN wire, 10 k resistor"
   },
   "E": {
    "k": "end",
    "t": "Pulses arrive: check the coin pin (GPIO 23, C1: 32) and the coins-per-pulse setting"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "no"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "no"
   },
   {
    "a": "Q3",
    "b": "E",
    "l": "yes"
   }
  ]
 },
 {
  "id": "tree4",
  "title": "The door will not open (helmet stuck)",
  "nodes": {
   "S": {
    "k": "start",
    "t": "The door will not open (helmet stuck)"
   },
   "Q1": {
    "k": "dec",
    "t": "Is a run still going?"
   },
   "A1": {
    "k": "act",
    "t": "Yes: wait, or CANCEL, CONFIRM. After the auto-dry or cool down the door unlocks"
   },
   "Q2": {
    "k": "dec",
    "t": "Is the machine switched on?"
   },
   "A2": {
    "k": "act",
    "t": "No: locks need power to unlock. Restore the power, then Maintenance > ENCLOSURE LOCK"
   },
   "Q3": {
    "k": "dec",
    "t": "Does the lock click in Maintenance?"
   },
   "A3": {
    "k": "act",
    "t": "Yes: mechanical. Push the door in a little, check the latch"
   },
   "Q4": {
    "k": "dec",
    "t": "12 V at the solenoid when the relay is ON?"
   },
   "A4": {
    "k": "act",
    "t": "No: relay, 12 V supply or wiring (A3, B3, C2)"
   },
   "E": {
    "k": "end",
    "t": "The solenoid or its diode is faulty: replace it"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "yes"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "Q4",
    "l": "no"
   },
   {
    "a": "Q4",
    "b": "A4",
    "l": "no"
   },
   {
    "a": "Q4",
    "b": "E",
    "l": "yes"
   }
  ]
 },
 {
  "id": "tree5",
  "title": "A container stays EMPTY or LOW",
  "nodes": {
   "S": {
    "k": "start",
    "t": "A container stays EMPTY or LOW"
   },
   "Q1": {
    "k": "dec",
    "t": "Is the Refilling Station ONLINE on the Dashboard?"
   },
   "A1": {
    "k": "act",
    "t": "No: see \"A board shows OFFLINE\""
   },
   "Q2": {
    "k": "dec",
    "t": "Is the water supply on and connected?"
   },
   "A2": {
    "k": "act",
    "t": "No: connect it, open the tap. The station tries again every 60 s"
   },
   "Q3": {
    "k": "dec",
    "t": "Test valve on the station page: does water flow?"
   },
   "A3": {
    "k": "act",
    "t": "No: valve, relay LED clicks?, valve supply, blocked filter or tube"
   },
   "Q4": {
    "k": "dec",
    "t": "Does the level rise while it fills?"
   },
   "A4": {
    "k": "act",
    "t": "No: sensor in the wrong place or the edges are wrong. Watch the raw value"
   },
   "E": {
    "k": "end",
    "t": "Check \"Waits for a rise\" and \"Valve open at most\" of that box"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "no"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "no"
   },
   {
    "a": "Q3",
    "b": "Q4",
    "l": "yes"
   },
   {
    "a": "Q4",
    "b": "A4",
    "l": "no"
   },
   {
    "a": "Q4",
    "b": "E",
    "l": "yes"
   }
  ]
 },
 {
  "id": "tree6",
  "title": "The web page says OLD DATA or NO LINK",
  "nodes": {
   "S": {
    "k": "start",
    "t": "The web page says OLD DATA or NO LINK"
   },
   "Q1": {
    "k": "dec",
    "t": "Does the Online Server setup page say ONLINE?"
   },
   "A1": {
    "k": "act",
    "t": "Yes: the page token is wrong or the browser is offline. Check Settings"
   },
   "Q2": {
    "k": "dec",
    "t": "Does it say the Wi-Fi was joined?"
   },
   "A2": {
    "k": "act",
    "t": "No: wrong password or not found. Use 2.4 GHz and check the hotspot"
   },
   "Q3": {
    "k": "dec",
    "t": "What does Blynk upload say?"
   },
   "A3": {
    "k": "act",
    "t": "datastream missing: make V0 invalid token: copy it again read timeout: check the internet"
   },
   "E": {
    "k": "end",
    "t": "OK but no data: is the LocalServer on? Power- cycle the Online Server"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "yes"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "Q3",
    "l": "yes"
   },
   {
    "a": "Q3",
    "b": "A3",
    "l": "error"
   },
   {
    "a": "Q3",
    "b": "E",
    "l": "OK"
   }
  ]
 },
 {
  "id": "tree7",
  "title": "Screen white, blank or the touch is wrong",
  "nodes": {
   "S": {
    "k": "start",
    "t": "Screen white, blank or the touch is wrong"
   },
   "Q1": {
    "k": "dec",
    "t": "Does Force refresh bring it back?"
   },
   "A1": {
    "k": "act",
    "t": "No: is the terminal ONLINE? If not, see \"A board shows OFFLINE\""
   },
   "Q2": {
    "k": "dec",
    "t": "Does it keep coming back?"
   },
   "A2": {
    "k": "act",
    "t": "No: fine. Watch it"
   },
   "A3": {
    "k": "act",
    "t": "Tick the relay that causes it (Screen section), lower the auto refresh seconds"
   },
   "A4": {
    "k": "act",
    "t": "Add a 470 to 1000 uF capacitor at the TFT supply, short thick 5 V wires"
   },
   "E": {
    "k": "end",
    "t": "Touch wrong: calibrate with ESP32_C1_CALIBRATE. Always white: check VCC, GND, CS, DC, RST"
   }
  },
  "edges": [
   {
    "a": "S",
    "b": "Q1",
    "l": ""
   },
   {
    "a": "Q1",
    "b": "A1",
    "l": "no"
   },
   {
    "a": "Q1",
    "b": "Q2",
    "l": "yes"
   },
   {
    "a": "Q2",
    "b": "A2",
    "l": "no"
   },
   {
    "a": "Q2",
    "b": "A3",
    "l": "yes"
   },
   {
    "a": "A3",
    "b": "E",
    "l": ""
   },
   {
    "a": "A4",
    "b": "E",
    "l": ""
   }
  ]
 }
];
