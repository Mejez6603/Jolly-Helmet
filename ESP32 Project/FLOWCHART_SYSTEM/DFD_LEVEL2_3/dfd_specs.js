/* Jolly Helmet - the Level 2 and Level 3 data flow diagrams (see dfd_engine.js for the drawing helpers).
   Every diagram is "balanced" with the one above it: the flows that cross its edge are the flows of the parent process in Level 1 / Level 2. */
const SPECS = {}, ORDER = [];
const NOTE_L2 = 'Level 2 splits one Level 1 process into its sub-processes. The data flows that enter and leave the dashed boxes are the same as in Level 1 (the diagram is balanced). Box 1, Box 2 and Box 3 are built alike (target design).';
const C = [360, 800, 1240];          /* the three process columns */
const W = 330;                       /* process width */
const H = 160;                       /* process height */
function def(id, meta, build) { SPECS[id] = { meta, build }; ORDER.push(id); }

/* ------------------------------------------------------------------ Level 2, process 1.0 */
def('L2-1', { title: 'Data flow diagram (Level 2)', sub: 'Process 1.0 CUSTOMER TRANSACTION AND CYCLE CONTROL (the box main controller)', note: NOTE_L2 }, d => {
  const R1 = 250, R2 = 650;
  d.ent('sens', 470, 60, 1100, 72, 'BOX SENSORS AND ACTUATORS', 'helmet and door sensors (in)  -  UV-C lamps, fogging, heater, fan, door lock (out)');
  d.proc('p2', 50, 250, 230, 120, '2.0', 'CONTAINER REFILLING', 'Refilling Station', { out: 1 });
  d.ent('cust', 40, 650, 170, 160, 'CUSTOMER', 'touch screen, coin acceptor');
  d.proc('p3', 1740, 620, 190, 130, '3.0', 'LOCAL RECORDING AND CONTROL', 'Local Server', { out: 1 });
  d.proc('p4', 1740, 790, 190, 130, '4.0', 'ONLINE DATA UPLOAD', 'Online Server', { out: 1 });

  d.proc('a1', C[0], R1, W, H, '1.1', 'CHECK THE BOX AND THE CONTAINER', 'Reads the helmet and door sensors, the relay board and the container level. Station silent for 15 s, or container EMPTY or LOW: DRYING only. A silent sensor or relay board: OUT OF ORDER.');
  d.proc('a4', C[1], R1, W, H, '1.4', 'CONTROL THE STEPS AND RUN THE CYCLE', 'Start, mode, coins, door, helmet, cleaning (fogging and UV-C 254 nm), drying, cooling down, cancel, safety pause, thank you.  (Level 3)');
  d.proc('a5', C[2], R1, W, H, '1.5', 'SWITCH THE OUTPUTS', 'The relay controller switches the whole relay mask and reports what is really ON. Everything goes OFF if the main controller is silent for 15 s.');
  d.proc('a2', C[0], R2, W, H, '1.2', 'RUN THE CUSTOMER INTERFACE', 'The display board draws the screen it is told to draw, reads the touch buttons and the coin pulses, and beeps.');
  d.proc('a3', C[1], R2, W, H, '1.3', 'BOOK THE PAYMENT', 'Counts the coins with the coin rate, keeps to the most coins allowed, books the accepted coins and works out the time bought and the run plan.');
  d.proc('a6', C[2], R2, W, H, '1.6', 'REPORT THE STATUS AND KEEP THE SETTINGS', 'Broadcasts the box status every second. Applies and saves the settings and carries out the maintenance commands of the Local Server.');
  d.store('d3', 360, 1020, 900, 60, 'D3', 'BOX SETTINGS', 'kept in the box main controller own flash: coin rate, most coins, helmet distance, station timeout, auto-dry, drying seconds');

  d.flow('sens.b:540', 'a1.t:540', 'Helmet and door<br>sensor data');
  d.flow('sens.b:960', 'a4.t:960', 'Door and helmet<br>state', { lo: .3 });
  d.flow('a5.t:1480', 'sens.b:1480', 'Relay commands: UV-C, fogging,<br>heater, fan, door locks');
  d.flow('p2.r:330', 'a1.l:330', 'Container levels<br>(EMPTY to MAX)');
  d.flow('cust.r:710', 'a2.l:710', 'Touch choices,<br>coin pulses');
  d.flow('a2.l:765', 'cust.r:765', 'Screens,<br>beeps', { below: true });
  d.flow('a1.r:330', 'a4.l:330', 'Ready, dry only,<br>out of order');
  d.flow('a4.r:330', 'a5.l:330', 'Relay mask<br>(outputs ON)');
  d.flow('a5.t:1330', 'a1.t:640', 'Relay status (what is ON)', { m: 222, lp: [1130, 218, 'bc'] });
  d.flow('a4.b:880', 'a2.t:580', 'Screen to draw<br>(step, time left)', { m: 500, lp: [730, 496, 'bc'] });
  d.flow('a2.t:640', 'a4.b:920', 'Touch choices<br>(mode, start, cancel)', { m: 565, lp: [790, 561, 'bc'] });
  d.flow('a2.r:710', 'a3.l:710', 'Coin pulses');
  d.flow('a3.l:765', 'a2.r:765', 'Coins counted', { below: true });
  d.flow('a3.t:1060', 'a4.b:1060', 'Coins paid, time bought,<br>run plan', { lp: [1051, 600, 'rm'] });
  d.flow('a3.r:710', 'a6.l:710', 'Booked coins');
  d.flow('a4.b:1100', 'a6.t:1380', 'Step, time left,<br>session counter', { m: 520, lp: [1240, 516, 'bc'] });
  d.flow('a6.t:1500', 'a5.b:1500', 'Maintenance: relays<br>by hand (2-s safety)');
  d.flow('a6.r:690', 'p3.l:690', 'Box status,<br>booked coins');
  d.flow('p3.l:735', 'a6.r:735', 'Settings, commands,<br>maintenance', { below: true });
  d.flow('a6.r:785', 'p4.l:855', 'Box status<br>(broadcast)', { m: 1655, lp: [1698, 851, 'bc'] });
  d.flow('a6.b:1380', 'd3.t:1180', 'Settings to keep', { m: 900, lp: [1280, 896, 'bc'] });
  d.flow('d3.t:900', 'a3.b:900', 'Coin rate, most coins,<br>auto-dry, drying seconds', { left: true });
  d.flow('d3.l:1050', 'a1.l:385', { l: 'Helmet distance,<br>station timeout', via: [[338, 1050], [338, 385]], lp: [330, 540, 'rm'] });
});

/* ------------------------------------------------------------------ Level 2, process 2.0 */
def('L2-2', { title: 'Data flow diagram (Level 2)', sub: 'Process 2.0 CONTAINER REFILLING (the Refilling Station)', note: NOTE_L2 }, d => {
  const R1 = 250;
  d.ent('cont', 470, 60, 1100, 72, 'MAIN CONTAINERS AND WATER SUPPLY', '3 water-level sensors (in)  -  3 valves (out)');
  d.proc('p1', 1740, 580, 190, 140, '1.0', 'CUSTOMER TRANSACTION AND CYCLE CONTROL', 'box main controllers', { out: 1 });
  d.proc('p4', 1740, 740, 190, 130, '4.0', 'ONLINE DATA UPLOAD', 'Online Server', { out: 1 });
  d.proc('p3', 1740, 890, 190, 130, '3.0', 'LOCAL RECORDING AND CONTROL', 'Local Server', { out: 1 });

  d.proc('b1', C[0], R1, W, H, '2.1', 'READ THE SENSORS AND WORK OUT THE LEVELS', '5 times a second, a 1 s average of the 3 sensors: EMPTY, LOW, MID, HIGH or MAX from the level edges. A stuck or jumping sensor = SENSOR FAULT: level UNKNOWN, no refill.');
  d.proc('b2', C[1], R1, W, H, '2.2', 'MANAGE THE REFILL QUEUE', 'A container that is EMPTY or LOW joins the queue. EMPTY ones first, then LOW; the one that waited longest, then Box 1, 2, 3. A refill that is running is never interrupted.');
  d.proc('b3', C[2], R1, W, H, '2.3', 'CONTROL THE VALVES', 'Opens the valve of the next box (only ONE valve can be open), closes it at MAX or when the refill fails, waits 60 s before a retry, runs the manual valve test.  (Level 3)');
  d.proc('b5', C[1], 600, W, H, '2.5', 'BROADCAST THE LEVELS AND THE STATUS', 'Every second, and at once when something changes: levels and raw values of the 3 containers, the open valve, who is waiting, the failed refills. Listen only for the others.');
  d.proc('b4', C[2], 900, W, H, '2.4', 'KEEP THE STATION SETTINGS', 'Receives the level edges, waiting times, retry time and the valve test from the Local Server and saves them in the station flash.');
  d.store('d4', 360, 950, 770, 60, 'D4', 'STATION SETTINGS', 'kept in the Refilling Station own flash: level edges, waiting times, retry time, test seconds');

  d.flow('cont.b:540', 'b1.t:540', 'Water-level readings<br>(5 times a second)');
  d.flow('b3.t:1480', 'cont.b:1480', 'Valve commands<br>(open, close)');
  d.flow('d4.t:520', 'b1.b:520', 'Level edges,<br>sensor fault rule', { lp: [511, 800, 'rm'] });
  d.flow('b1.r:300', 'b2.l:300', 'Levels of the<br>3 containers');
  d.flow('b2.r:300', 'b3.l:300', 'Next box<br>to refill');
  d.flow('b3.l:360', 'b2.r:360', 'Refill worked<br>or failed', { below: true });
  d.flow('b1.b:600', 'b3.b:1450', 'Levels (rise, MAX reached)', { m: 470, lp: [1180, 466, 'bc'] });
  d.flow('b1.b:460', 'b5.l:680', 'Levels and<br>raw values');
  d.flow('b2.b:960', 'b5.t:960', 'Who is waiting<br>(the queue)', { left: true });
  d.flow('b3.b:1280', 'b5.t:1060', 'Valve open,<br>failed refill', { m: 530, lp: [1170, 526, 'bc'] });
  d.flow('b5.r:650', 'p1.l:650', 'Container levels (broadcast)', { lp: [1450, 646, 'bc'] });
  d.flow('b5.r:700', 'p4.l:790', { l: 'Container levels (broadcast)', via: [[1700, 700], [1700, 790]], lp: [1450, 696, 'bc'] });
  d.flow('b5.r:745', 'p3.l:1000', { l: 'Levels, valve state, failures', via: [[1650, 745], [1650, 1000]], lp: [1450, 741, 'bc'] });
  d.flow('p3.l:965', 'b4.r:965', 'Station<br>settings,<br>valve test', { lp: [1696, 961, 'bc'] });
  d.flow('b4.l:965', 'd4.r:965', 'Settings to keep');
  d.flow('d4.r:995', 'b4.l:995', 'Saved settings', { below: true });
  d.flow('b4.t:1330', 'b3.b:1330', 'Stall times, retry,<br>valve test request', { lp: [1321, 790, 'rm'] });
});

/* ------------------------------------------------------------------ Level 2, process 3.0 */
def('L2-3', { title: 'Data flow diagram (Level 2)', sub: 'Process 3.0 LOCAL RECORDING AND CONTROL (the Local Server)', note: NOTE_L2 }, d => {
  const R1 = 220, R2 = 650;
  d.proc('p4', 50, 225, 170, 140, '4.0', 'ONLINE DATA UPLOAD', 'Online Server', { out: 1 });
  d.ent('owner', 40, 640, 170, 170, 'OWNER / TECHNICIAN', 'authorized administrator');
  d.ent('team', 40, 850, 170, 110, 'TEAM', 'ENTRY board (spare ESP32)');
  d.proc('p1', 1740, 215, 190, 150, '1.0', 'CUSTOMER TRANSACTION AND CYCLE CONTROL', 'box main controllers', { out: 1 });
  d.proc('p2', 1740, 650, 190, 160, '2.0', 'CONTAINER REFILLING', 'Refilling Station', { out: 1 });

  d.proc('c5', C[0], R1, W, H, '3.5', 'SEND THE TOTALS', 'Every 10 s the totals, the coin value and the last 10 sessions go to the Online Server by radio (and a heartbeat every second).');
  d.proc('c2', C[1], R1, W, H, '3.2', 'RECORD THE SESSIONS AND THE TOTALS', 'A finished or cancelled session is recorded: date, time, box, Finished or Cancelled, coins, minutes. Totals and revenue (booked coins) are updated.');
  d.proc('c1', C[2], R1, W, H, '3.1', 'RECEIVE THE RADIO DATA', 'Listens on ESP-NOW channel 1: box status and booked coins of each box, sensor and relay status, Refilling Station levels, valves, queue and failures.');
  d.store('d12', 330, 470, 600, 56, 'D1.2', 'SESSION LOG AND TOTALS', 'kept in the log storage (FFat); a backup CSV can be downloaded and restored');
  d.proc('c3', C[1], R2, W, H, '3.3', 'SERVE THE CONTROL CENTER', 'Wi-Fi JollyHelmet-Local: Dashboard every second, Box and Refilling Station pages, day viewer, settings, CSV download and restore, history file, crash reports.');
  d.proc('c4', C[2], R2, W, H, '3.4', 'KEEP AND SEND THE SETTINGS', 'Rules, sensors, auto-dry, drying seconds, level edges, maintenance and valve test: saved in flash and sent to the box main controllers and the station.');
  d.store('d13', 780, 930, 300, 56, 'D1.3', 'HISTORY AND CRASHES', 'history.json, last 3 crash reports');
  d.store('d11', 1230, 930, 330, 56, 'D1.1', 'SETTINGS', 'kept in flash');

  d.flow('c5.l:290', 'p4.r:290', 'Totals, last<br>10 sessions<br>(every 10 s)');
  d.flow('c1.l:300', 'c2.r:300', 'New session,<br>booked coins');
  d.flow('c1.b:1280', 'c3.t:1060', 'Live status of the boxes<br>and the station', { m: 455, lp: [1170, 451, 'bc'] });
  d.flow('c2.b:850', 'd12.t:850', 'Session record,<br>totals');
  d.flow('d12.t:430', 'c5.b:430', 'Totals, last<br>10 sessions');
  d.flow('d12.b:900', 'c3.t:900', 'Session log,<br>totals');
  d.flow('c3.r:730', 'c4.l:730', 'Settings, rules,<br>maintenance,<br>valve test');
  d.flow('c3.b:830', 'd13.t:830', 'History file,<br>restored backup', { left: true });
  d.flow('d13.t:960', 'c3.b:960', 'History rows,<br>crash reports');
  d.flow('c4.b:1300', 'd11.t:1300', 'Settings to keep', { left: true });
  d.flow('d11.t:1480', 'c4.b:1480', 'Saved settings');
  d.flow('d11.l:958', 'c3.b:1090', 'Settings<br>(to show)');
  d.flow('owner.r:690', 'c3.l:690', 'Settings, rules, maintenance,<br>backup to restore');
  d.flow('c3.l:750', 'owner.r:750', 'Control Center pages,<br>session log, totals, CSV', { below: true });
  d.flow('team.r:905', 'c3.l:795', 'Previous customers<br>(history.json)', { via: [[700, 905], [700, 795]], lp: [450, 901, 'bc'] });
  d.flow('p1.l:275', 'c1.r:275', 'Box status,<br>booked coins');
  d.flow('p2.l:705', 'c1.r:340', 'Levels, valve state,<br>failures', { via: [[1625, 705], [1625, 340]], lp: [1616, 500, 'rm'] });
  d.flow('c4.r:695', 'p1.l:330', 'Settings, commands,<br>maintenance', { via: [[1690, 695], [1690, 330]], lp: [1699, 500, 'lm'] });
  d.flow('c4.r:775', 'p2.l:775', 'Station settings,<br>valve test', { below: true });
});

/* ------------------------------------------------------------------ Level 2, process 4.0 */
def('L2-4', { title: 'Data flow diagram (Level 2)', sub: 'Process 4.0 ONLINE DATA UPLOAD (the Online Server)', note: NOTE_L2 }, d => {
  d.proc('p1', 40, 215, 170, 110, '1.0', 'CUSTOMER TRANSACTION AND CYCLE CONTROL', 'box main controllers', { out: 1 });
  d.proc('p2', 40, 350, 170, 110, '2.0', 'CONTAINER REFILLING', 'Refilling Station', { out: 1 });
  d.proc('p3', 40, 485, 170, 110, '3.0', 'LOCAL RECORDING AND CONTROL', 'Local Server', { out: 1 });
  d.ent('owner', 40, 790, 170, 160, 'OWNER / TECHNICIAN', 'authorized administrator');

  d.proc('e1', C[0], 215, W, 360, '4.1', 'LISTEN TO THE RADIO', 'Only listens, never sends. Hears the box status of every box board, the Refilling Station levels and the totals of the Local Server on ESP-NOW channel 1. Takes a snapshot every second.');
  d.proc('e2', C[1], 215, W, H, '4.2', 'COUNT THE STATISTICS', 'Counts the sessions per box, per hour, per weekday and per day, and takes the totals and the revenue from the Local Server.');
  d.proc('e3', C[2], 215, W, H, '4.3', 'WORK OUT THE ALERTS', 'Every second looks at the snapshot: out of order, Refilling Station offline, refill failed, board not heard.');
  d.store('d5', 360, 650, 1210, 60, 'D5', 'SNAPSHOT, STATISTICS AND ALERTS', 'kept in the Online Server memory: the last 1 s snapshot, the sessions per box, hour, weekday and day, the alerts');
  d.proc('e5', C[0], 790, W, H, '4.5', 'KEEP THE SETUP', 'Router-style setup page on the own Wi-Fi of the Online Server: Wi-Fi name, Blynk token, interval and time zone. The Wi-Fi is tested first, then Blynk.');
  d.proc('e4', C[1], 790, W, 260, '4.4', 'UPLOAD TO THE BLYNK CLOUD', 'Every interval (30 s to 1 h, default 1 min) a burst of 5 to 10 s: leaves the radio, joins the Wi-Fi, writes the live data, the box steps and any alert, returns to channel 1.  (Level 3)');
  d.store('d6', 360, 1110, 770, 60, 'D6', 'ONLINE SETUP', 'kept in flash: Wi-Fi name and password, Blynk tokens, interval, time zone');
  d.store('d21', 1240, 820, 330, 60, 'D2.1', 'PRIVATE DEVICE V0', 'live data (JSON)');
  d.store('d22', 1240, 910, 330, 60, 'D2.2', 'PUBLIC DEVICE V0', 'box steps only');
  d.store('d23', 1240, 1000, 330, 60, 'D2.3', 'ALERT EVENT', 'push to the Blynk app');

  d.flow('p1.r:270', 'e1.l:270', 'Box status<br>(broadcast)');
  d.flow('p2.r:405', 'e1.l:405', 'Container levels<br>(broadcast)');
  d.flow('p3.r:540', 'e1.l:540', 'Totals, last 10<br>sessions (10 s)');
  d.flow('e1.r:290', 'e2.l:290', 'Session counters,<br>totals');
  d.flow('e1.b:430', 'd5.t:430', 'Snapshot<br>(every 1 s)', { left: true });
  d.flow('e2.b:860', 'd5.t:860', 'Statistics', { left: true });
  d.flow('d5.t:1300', 'e3.b:1300', 'Snapshot', { left: true });
  d.flow('e3.b:1450', 'd5.t:1450', 'Alerts');
  d.flow('d5.b:900', 'e4.t:900', 'Snapshot, statistics,<br>alerts');
  d.flow('owner.r:850', 'e5.l:850', 'Wi-Fi, Blynk token,<br>interval, time zone');
  d.flow('e5.l:920', 'owner.r:920', 'Setup page,<br>Wi-Fi test result', { below: true });
  d.flow('e5.b:430', 'd6.t:430', 'Setup to keep', { left: true });
  d.flow('d6.t:900', 'e4.b:900', 'Wi-Fi, token, interval,<br>time zone');
  d.flow('e4.r:850', 'd21.l:850', 'Live data<br>(HTTPS)');
  d.flow('e4.r:940', 'd22.l:940', 'Box steps<br>(HTTPS)');
  d.flow('e4.r:1030', 'd23.l:1030', 'Alert<br>event');
});

/* ------------------------------------------------------------------ Level 2, process 5.0 */
def('L2-5', { title: 'Data flow diagram (Level 2)', sub: 'Process 5.0 ONLINE MONITORING (the website)', note: NOTE_L2 }, d => {
  d.ent('owner', 470, 60, 1100, 72, 'OWNER / TECHNICIAN', 'authorized administrator: Monitor, Alerts and Reports pages (the Blynk token is typed once and kept in that browser)');
  d.ent('cust', 470, 1100, 1100, 72, 'CUSTOMER (PHONE)', 'scans the QR sticker: status page');

  d.proc('f2', C[0], 250, W, H, '5.2', 'SHOW THE MONITOR AND THE REPORTS', 'Live, delayed or old data, each box drawn as a machine, the three level dials of the Refilling Station, the network map and the reports.');
  d.proc('f1', C[1], 250, W, H, '5.1', 'READ THE LIVE DATA', 'The Monitor page asks Blynk every 10 s, only while the tab is open and visible, with the token typed once and kept in that browser.');
  d.proc('f3', C[2], 250, W, H, '5.3', 'SHOW THE ALERTS', 'Alerts in four levels (critical, warning, notice, info) with sounds and a log. The page also says when the data is not yet updated.');
  d.store('d21', 800, 470, 330, 60, 'D2.1', 'PRIVATE DEVICE V0', 'live data (JSON)');
  d.store('d22', 1240, 640, 330, 60, 'D2.2', 'PUBLIC DEVICE V0', 'box steps only');
  d.proc('f5', C[0], 790, W, 170, '5.5', 'SEND THE NICKNAME', 'The customer may type a nickname after the coins are in. It is the only value the website writes (V5, 12 characters at most) and it clears when the cycle of the box ends.');
  d.store('d23', 800, 855, 330, 60, 'D2.3', 'NICKNAME V5', 'in the public device');
  d.proc('f4', C[2], 790, W, 170, '5.4', 'READ THE BOX STEPS', 'The status page (QR sticker) reads the public device every 30 s: START screen = FREE, other steps = IN USE with the minutes left, OUT OF ORDER, NO INFORMATION. Shows the nickname on the card.');

  d.flow('f2.t:520', 'owner.b:520', 'Monitor, reports');
  d.flow('f3.t:1400', 'owner.b:1400', 'Alerts with sounds');
  d.flow('d21.t:965', 'f1.b:965', 'Live data (private V0)<br>every 10 s');
  d.flow('f1.l:330', 'f2.r:330', 'Live data');
  d.flow('f1.r:330', 'f3.l:330', 'Live data');
  d.flow('d22.b:1405', 'f4.t:1405', 'Box steps (public V0)<br>every 30 s');
  d.flow('f5.r:885', 'd23.l:885', 'Nickname<br>(V5)');
  d.flow('d23.r:885', 'f4.l:885', 'Nickname<br>(on the card)');
  d.flow('f4.b:1400', 'cust.t:1400', 'Status page: FREE, IN USE,<br>OUT OF ORDER');
  d.flow('cust.t:520', 'f5.b:520', 'Nickname (optional)');
});

/* ================================================================== LEVEL 3 */
const NOTE_L3 = 'Level 3 splits one Level 2 process into its sub-processes. The dashed boxes and the entity around the edge are the neighbours of that process in Level 2, and the data flows that cross the edge are the same as in Level 2 (the diagram is balanced). Box 1, Box 2 and Box 3 are built alike (target design).';

/* ------------------------------------------------------------------ Level 3, process 1.4 */
def('L3-1', { title: 'Data flow diagram (Level 3)', sub: 'Process 1.4 CONTROL THE STEPS AND RUN THE CYCLE (inside the box main controller)', note: NOTE_L3 }, d => {
  const K = [400, 850, 1300], W3 = 310;
  d.ent('sens', 470, 60, 1100, 72, 'BOX SENSORS AND ACTUATORS', 'helmet and door sensors');
  d.proc('p11', 40, 230, 200, 100, '1.1', 'CHECK THE BOX AND THE CONTAINER', '', { out: 1 });
  d.proc('p13', 40, 360, 200, 110, '1.3', 'BOOK THE PAYMENT', '', { out: 1 });
  d.proc('p12', 40, 740, 200, 140, '1.2', 'RUN THE CUSTOMER INTERFACE', '', { out: 1 });
  d.proc('p15', 1750, 740, 180, 140, '1.5', 'SWITCH THE OUTPUTS', '', { out: 1 });
  d.proc('p16', 850, 1020, 310, 120, '1.6', 'REPORT THE STATUS AND KEEP THE SETTINGS', '', { out: 1 });

  d.proc('u1', K[0], 230, W3, 250, '1.4.1', 'READ THE INPUTS', 'Collects what the step machine needs: ready, dry only or out of order; the touch choices; the coins paid and the run plan; the door and helmet state (a door that opens during a run is noticed at once).');
  d.proc('u2', K[1], 230, W3, 250, '1.4.2', 'DECIDE THE NEXT STEP', 'The step machine. From the inputs and the timer it picks the step. A CANCEL asks CONFIRM or RESUME. A door that opens during a run gives the SAFETY PAUSE until the door is closed. Everything is kept in the session state.');
  d.proc('u3', K[2], 230, W3, 250, '1.4.3', 'TIME THE STEP', 'CHECKING 2 s, NO HELMET and HELMET STILL INSIDE 2 s, cleaning or drying = the coins bought, DRYING after cleaning 3:00, COOLING DOWN 60 s, THANK YOU 5 s. It stops while the box is in the safety pause.');
  d.store('d31', 400, 570, 1210, 60, 'D3.1', 'SESSION STATE', 'in the memory of the main controller: step, mode, coins paid, run plan, time left, pause flag, session counter');
  d.proc('u5', K[0], 740, W3, 170, '1.4.5', 'WORK OUT THE SCREEN', 'Picks the screen of the step (text, buttons, coin slot on or off, beep) and the time left to show.');
  d.proc('u6', K[1], 740, W3, 170, '1.4.6', 'REPORT THE STEP', 'Puts the step, the time left and the session counter into the box status that 1.6 broadcasts every second.');
  d.proc('u4', K[2], 740, W3, 170, '1.4.4', 'WORK OUT THE RELAY MASK', 'CLEANING = UV-C 1 + 2 and fogging, DRYING = UV-C and heater, COOLING DOWN and safety pause = all off, door lock released at the door steps.');

  d.flow('sens.b:560', 'u1.t:560', 'Door and helmet<br>state');
  d.flow('p11.r:285', 'u1.l:285', 'Ready, dry only,<br>out of order');
  d.flow('p13.r:420', 'u1.l:420', 'Coins paid, time bought,<br>run plan');
  d.flow('p12.r:775', 'u1.l:455', 'Touch choices (mode,<br>start, cancel, resume)', { via: [[320, 775], [320, 455]], lp: [311, 620, 'rm'] });
  d.flow('u1.r:340', 'u2.l:340', 'Inputs (touch,<br>coins, doors,<br>ready)');
  d.flow('u2.r:310', 'u3.l:310', 'Start the timer<br>(seconds)');
  d.flow('u3.l:400', 'u2.r:400', 'Time up', { below: true });
  d.flow('u2.b:930', 'd31.t:930', 'Step, mode,<br>run plan', { left: true });
  d.flow('d31.t:1060', 'u2.b:1060', 'Step, time left,<br>pause flag');
  d.flow('u3.b:1455', 'd31.t:1455', 'Time left');
  d.flow('d31.b:560', 'u5.t:560', 'Step, time left,<br>mode');
  d.flow('d31.b:1005', 'u6.t:1005', 'Step, time left,<br>session counter');
  d.flow('d31.b:1455', 'u4.t:1455', 'Step, run plan,<br>pause flag');
  d.flow('u5.l:830', 'p12.r:830', 'Screen to draw<br>(step, time left)', { below: true });
  d.flow('u4.r:825', 'p15.l:825', 'Relay mask<br>(outputs ON)');
  d.flow('u6.b:1005', 'p16.t:1005', 'Step, time left,<br>session counter');

  d.text(1260, 985, '<div style="width:670px"><b>THE STEPS KEPT IN THE SESSION STATE (D3.1)</b><br>START, CHECKING (2 s), WELCOME, SELECT MODE, INSERT COIN (15 coins = 5 min), OPEN THE DOOR, PLACE THE HELMET, CLOSE THE DOOR (NO HELMET after 2 s), CLEANING (UV-C 1 + 2 and 3 humidifiers), DRYING (UV-C and heater), DRYING after cleaning (3:00, heater off the last 30 s), COOLING DOWN (60 s), OPEN THE DOOR, TAKE THE HELMET (HELMET STILL INSIDE after 2 s), THANK YOU (5 s). A CANCEL asks CONFIRM or RESUME; a door that opens during a run gives the SAFETY PAUSE (UV-C, mist and heater off).</div>');
});

/* ------------------------------------------------------------------ Level 3, process 2.3 */
def('L3-2', { title: 'Data flow diagram (Level 3)', sub: 'Process 2.3 CONTROL THE VALVES (inside the Refilling Station)', note: NOTE_L3 }, d => {
  d.ent('cont', 470, 60, 1100, 72, 'MAIN CONTAINERS AND WATER SUPPLY', '3 valves (out)');
  d.proc('p22', 40, 240, 190, 540, '2.2', 'MANAGE THE REFILL QUEUE', '', { out: 1 });
  d.proc('p24', 1740, 240, 190, 150, '2.4', 'KEEP THE STATION SETTINGS', '', { out: 1 });
  d.proc('p21', 1740, 690, 190, 130, '2.1', 'READ THE SENSORS AND WORK OUT THE LEVELS', '', { out: 1 });
  d.proc('p25', 360, 930, 770, 110, '2.5', 'BROADCAST THE LEVELS AND THE STATUS', '', { out: 1 });

  d.proc('g1', 360, 240, W, 170, '2.3.1', 'CHECK THAT THE VALVE IS FREE', 'Takes the next box from the queue only when no valve is open (a refill or a test) and 1 s has passed since the last valve closed.');
  d.proc('g2', 800, 240, W, 170, '2.3.2', 'OPEN AND CLOSE THE VALVE', 'The only process that drives the valves. Opens the valve of the box (only ONE valve can ever be open), closes it on order, keeps which valve is open and since when. All 3 valves are closed at power on.');
  d.proc('g5', 1240, 240, W, 170, '2.3.5', 'RUN THE MANUAL VALVE TEST', 'Test valve button of the Refilling Station page: if no valve is open and none closed in the last second, that valve opens for 2 s (a setting) and closes. Otherwise: Busy. The queue is not touched.');
  d.proc('g4', 360, 600, W, 190, '2.3.4', 'REPORT THE RESULT AND RETRY', 'MAX reached: the refill WORKED, the failure flag is cleared. Stalled: the refill FAILED, the flag is set (alert on the Online web page and the Local Server), wait 60 s, then the box joins the queue again, until a refill works.');
  d.proc('g3', 1240, 600, W, 190, '2.3.3', 'WATCH THE LEVEL RISE', 'Waits for the container to reach MAX. No rise in 30 s while EMPTY, in 5 s while LOW, or the valve open for 3 minutes: the refill has FAILED. Then it orders the valve closed.');

  d.flow('p22.r:300', 'g1.l:300', 'Next box<br>to refill');
  d.flow('g1.r:300', 'g2.l:300', 'Open the valve<br>of this box');
  d.flow('g2.l:370', 'g1.r:370', 'Valve state', { below: true });
  d.flow('g5.l:300', 'g2.r:300', 'Open for 2 s,<br>then close');
  d.flow('g2.r:370', 'g5.l:370', 'Valve state', { below: true });
  d.flow('p24.l:300', 'g5.r:300', 'Valve test<br>request');
  d.flow('g2.t:960', 'cont.b:960', 'Valve commands<br>(open, close)');
  d.flow('g2.b:1080', 'g3.l:625', 'Box being<br>refilled', { lp: [1160, 621, 'bc'] });
  d.flow('g3.l:680', 'g2.b:1020', 'Close the<br>valve now', { lp: [1138, 676, 'bc'] });
  d.flow('p21.l:725', 'g3.r:725', 'Levels (rise,<br>MAX reached)');
  d.flow('p24.b:1835', 'g3.r:640', 'Stall times,<br>retry time');
  d.flow('g3.l:740', 'g4.r:740', 'Refill worked or stalled,<br>retry time', { lp: [1130, 736, 'bc'] });
  d.flow('g4.l:650', 'p22.r:650', 'Refill worked<br>or failed');
  d.flow('g4.b:430', 'p25.t:430', 'Failed refill');
  d.flow('g2.b:900', 'p25.t:900', 'Valve open<br>(which box)', { lp: [909, 520, 'lm'] });
});
