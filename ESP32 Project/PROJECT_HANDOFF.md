# PROJECT HANDOFF - state of the Jolly Helmet system, 2026-11-03 (updated 2026-11-12: Box 1 and Box 2 rebuilt like Box 3 - see the first section)

Read `README.md` first for what the system is. This file says **what is done, what is not, how to bring it up on the bench, and what is still open.**

---

## 2026-11-12 - BOX 1 AND BOX 2 REBUILT LIKE BOX 3 (read this first: it replaces every "Box 1 / Box 2 later" note below)

**Nothing of this has been flashed or uploaded.** Everything compiles; the old firmware is untouched in `ARCHIVE_2026-11-12_BEFORE_BOX1_2_LIKE_BOX3\` (the whole project as it was, including the Box 1 / Box 2 builds that were flashed and field-tested) - that is the rollback.

**The idea: ONE design, three boxes.** Box 1, Box 2 and Box 3 are now the same machine with different names. Each node type is ONE program, copied into three folders; only a small `box_config.h` in each folder differs, so a bug is fixed once and the file copied.

| Node | Folders | What differs (`box_config.h`) |
|---|---|---|
| Main controller (ESP32-S3) | `ESP32_S3A` (Box 1), `ESP32_S3C` (Box 2), `ESP32_S3B` (Box 3) | the box's device IDs, the settings state tag (0 / 2 / 3), its Refilling Station slot (0 / 1 / 2), the flash namespace (kept as before, so a reflashed unit keeps its booked-coin total and its rate rules). `BoxScreens.h` = the 36-step screens, identical. |
| Terminal (TFT + coin + buzzer) | `ESP32_A1`, `ESP32_C1`, `ESP32_B1` | per unit: screen rotation, touch calibration, coin GPIO, its S3 / sensor IDs. **A1: rotation 1, touch {327,3421,285,3510,1}, coin GPIO 23. C1: rotation 3, touch {408,3325,535,3051,7}, coin GPIO 32. B1: rotation 3, touch still A1's numbers (calibrate), coin GPIO 32.** |
| Sensor board | `ESP32_A2`, `ESP32_C2`, `ESP32_B2` | only its device ID. Helmet ultrasonic Echo 25 / Trig 26; door reeds Enclosure 27, Panel 14, Back 19, Side 18. **C2 no longer carries relays** (it used to be sensors + relays). |
| Relay board | `ESP32_A3`, `ESP32_C3` (**NEW**), `ESP32_B3` | its device IDs, state tag, flash namespace. 13 channels (12 + the heater on GPIO 26), the same pin map as B3, S3-loss watchdog, trapped-helmet release. |

- New device ID `DEVICE_C3 = 15` (Shared_Common.h, all copies identical). Box 2's step numbers are now the normal `MachineState` list (the old `Box2State` list is superseded and nothing uses it).
- **The three S3 programs are byte-identical** (`cmp`); same for the three terminals, the three sensor boards and the three relay boards.
- **Refilling Station:** `WATCH_BOX = { true, true, true }` (was Box 3 only). Its sensors / valves for Box 1 and Box 2 must be wired, otherwise those boxes show EMPTY / SENSOR FAULT and offer DRYING only. `ESP32_REFILL_FAKE` reports all three as watched.
- **LocalServer:** the three box pages are ONE page (`PAGE_BOX`, it reads its box from the address /box1 /box2 /box3) and one set of handlers (`hubBoxHandler`); box index = box number - 1. Same settings for every box (rules, sensors, auto-dry, drying, refresh masks, auto refresh, beep). **13 devices** on the Dashboard (C3 added). Box 2's revenue is now booked from S3C's booked coins like Box 1 / Box 3 (the first packet only sets the starting point, nothing is counted twice). The old Box 1 / Box 2 pages and the legacy C2 `TelemetryPacket` handling are gone.
- **Online Server:** one box structure; the `n` string has **13 characters** - the 12 places of before did not move, Box 2's relay board C3 was added at the END (place 13); a new alert id 18 = "Box 2 Relay Controller is not heard". The "levels" settings list and the old Box 2 state numbers are gone.
- **Online web page (needs a small update, not a rewrite):** `js/names.js` (one list of steps for all boxes, 13 boards, Box 2's relay board = place 13, every box has 13 relays and 4 doors), `js/demo.js`, two texts in `js/gauges.js`, and `DATA_CONTRACT.md`. The JSON shape did not change. Upload the whole folder; if the page is NOT updated but the Online Server is, Box 2's step names and relay names come out wrong.

**Flash order for the new design (loads unplugged first):** Refilling Station -> LocalServer -> relay boards (A3, C3, B3) -> sensor boards (A2, C2, B2) -> terminals (A1, C1, B1) -> main controllers (S3A, S3C, S3B) -> Online Server -> upload ONLINE_WEB. After flashing S3A / S3C open `/box1` and `/box2` once and check every number. **What a reflashed S3A / S3C keeps and what it loses (compared key by key against the archived firmware, 2026-11-12):** it keeps the rate rules (`rate_c`, `rate_v`, `rate_u`, `max_c`), the auto-dry / refresh / beep settings that Box 1 already had, and Box 1's booked-coin total and last coin-pulse count (same flash namespace and keys). It LOSES the helmet cutoff: Box 1 saved it as `helm_a2_cm` and Box 2 as `helm_c2_cm`, the shared program reads `helm_cm` (Box 3's key), so both go back to the default 15 cm until you type the value in the "Sensors" box of the page and press Send. Box 2 never had a booked-coin counter, so its total starts at 0 (the LocalServer takes the first packet as the starting point, so nothing is counted twice or lost). The old settings of the removed features (ratings, free retry, time bands, scented/alcohol levels...) are simply never read again. A1 / C1 / A3 / C3 start with empty flash (their helmet cutoff arrives from the S3, which re-broadcasts all its settings every 2 seconds).

**Needs the hardware team before the new firmware can run on Box 2 (and partly Box 1):**
1. **C3 does not exist yet.** Box 2's relays were on C2 (6 relays: locks GPIO 4 / 16 / 17, heater 18, UV 5, fan 23). They must move to a 13-channel relay board like B3's (pin list at the top of `ESP32_C3.ino`; the heater goes to GPIO 26, UV 1 / UV 2 / humidifiers / coin power as on B3) and C2 is then a plain sensor board like B2. Check the C3 pin map against the real wiring before powering any load.
2. **Box 1's A3 has no heater relay** (GPIO 26 drives nothing yet) and the new flow dries with the heater (AUTO DRY): without it Box 1 only has UV during the dry. Box 2 has a heater but on the old board.
3. **Refilling Station:** water-level sensors and valves for Box 1 and Box 2 (GPIO 34 / 35 sensors, 21 / 19 valves as decided 2026-11-03) - or set that box to `false` in `WATCH_BOX` until they are wired.
4. A2 / C2 no longer read the two container ultrasonics or the water sensors (the station does); those pins are free.
5. B1 touch calibration is still A1's numbers; A1 and C1 keep their own.
6. The SIDE door reed (GPIO 18) must be wired (or the input stays HIGH = "open" in the monitor; the flow itself only looks at the enclosure door and the helmet).

**What the field-tested Box 1 / Box 2 had that the new firmware does not (decided, same as Box 3):** ratings, the time picker, the mist choice, the free retry, the ACS refill steps. Same coin rule as Box 3: INSERT COIN asks for ONE rate step (default 15 coins = 5 min), more coins can be added while it runs up to the maximum, a coin that arrives late is counted (COIN SETTLE 1.5 s, at most 6 s).

**Tests (all re-run on 2026-11-12, after the rebuild):** every sketch compiles with 0 warnings: S3A / S3C / S3B 28 %, LocalServer 40 %, Online Server 37 % of the real 3 MB app partition (board options from README: `esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`); the plain ESP32 boards on the default partition: sensors A2 / B2 / C2 68 %, relays A3 / B3 / C3 69 %, TFT terminals A1 / B1 / C1 73 %. `ESP32_S3B\tools\s3b_flow_test.html` (a JavaScript port of the shared S3 logic, which is the same in all three boxes) = 47 of 47 pass; `ESP32_REFILL\tools\refill_simulator.html` = 24 of 24 pass. The S3 logic itself was not changed by the rebuild, only its names and settings moved into `box_config.h`. The tests run the PORT, not the .ino, and nothing was run on hardware. **Second pass, same evening ("verify everything"):** all 22 sketches that use the shared header were built from clean (0 errors, 0 warnings; this includes the bench / test sketches A1_WIDGET_TEST, A3_BENCH_TEST, S3A_COMMTEST, S3A_CONFIG_BENCH, S3C_CONFIG_BENCH, RESTATE, REFILL_FAKE - they compile, but they were written for the old per-box protocols and were NOT checked functionally; the two CONFIG_BENCH sketches in particular stand in for the old LocalServer pages). The ONLINE_WEB pages (Monitor with `?demo`, `?demo=alerts`, `status.html?demo`) were loaded over a local web server: no console errors, a Box 2 relay-board-offline case shows "relay board inactive" on Box 2 only. The LocalServer pages were run against a small mock server built from the real page text in the `.h` files: /box1 /box2 /box3 fill in the right board names (S3A A1 A2 A3 / S3C C1 C2 C3 / S3B B1 B2 B3), the seven Send buttons reach "has these values", maintenance + the 13 relay buttons (heater = bit 12) work, /refill and the Dashboard (13 devices, one offline) are right. The C++ handlers themselves (`settings_hub.h`) were read line by line against that mock's answers but not executed. Each box's flash keys, pin tables and `box_config.h` values were compared with the archived firmware (see the table above and the keys note in the flash order).

---
## 1. Where things stand

**Nothing from 2026-11-03 has been flashed or uploaded.** Everything below was compiled (all firmware projects build) and, where there is no hardware, tested in the browser with simulators that are line-by-line ports of the firmware logic. Treat every board as "compiled, not yet run on hardware" unless you flashed it yourself.

| Part | State | How it was checked |
|---|---|---|
| Refilling Station `ESP32_REFILL` | written, compiles (69% of the default partition) | `ESP32_REFILL\tools\refill_simulator.html`: 17 rule tests pass (queue order, one valve at a time, stall / time limit / retry, test refusal ...) |
| Box 3 main controller `ESP32_S3B` | rewritten for the 36-step flow, compiles (28%) | `ESP32_S3B\tools\s3b_flow_test.html`: 31 scenario tests pass (every branch of `BOX 3 STEP corrected.txt`, relays per step, coins, abort, safety pause, station offer) |
| Box 3 `B1` (TFT) / `B2` (sensors) / `B3` (relays) | `B1`: touch debug off; `B2`: helmet + doors only; `B3`: drain relay bits never driven, Side lock. All compile. | read-through only |
| LocalServer | Refilling Station page, Box 3 page rewritten, Dashboard card, 12 devices; compiles (41%) | pages tested on a mock server in the browser (forms, send/echo, queue chips, test button, phone width) |
| Online Server + `ONLINE_WEB` | station support, queue flag; compiles (37%) | `?demo=cycle` of the web page |
| Box 1 (S3A, A1, A2, A3), Box 2 (S3C, C1, C2) | **REBUILT 2026-11-12 like Box 3** (see the section above; Box 2 now has C3 for its relays) | compile only |
| ACS 1 / ACS 2 | **removed** (archived) | - |

Older reports (2026-10-03): the user flashed the Box 1/2/3 + ACS + LocalServer builds of that day at the school; builds after that (booked coins / Finished-Cancelled / OPEN beep of 10-13, the Online Server, the ENTRY board) were never confirmed flashed.

---

## 2. What changed on 2026-11-03

1. **The ACS (two alcohol-mixing boards) is gone.** One **Refilling Station** (ESP32-WROOM, 3 water-level sensors, 3 normally-closed valves) keeps the three main containers full from one shared supply. It never waits for a box: it reads the levels, queues, fills, and broadcasts a `RefillStatusPacket` every second.
2. **The boxes only read the station.** `CHECKING` (2 s) -> `WELCOME`; on the tap CLEANING is offered only when the station was heard within 15 s AND the box's container is MID/HIGH/MAX; otherwise SELECT DRY ONLY. A silent station = dry only. Nothing asks the station for liquid.
3. **Box 3 flow trimmed** (panelists): no ratings, no time picker, no mist choice, no free retry, no touch debug, no container sensing, no drain pumps. Coins: INSERT COIN asks for one rate step (default 15 coins = 5 min); more coins can be added while cleaning / drying up to the maximum (90). A cleaning always ends with a 3-minute AUTO DRY (heater off for the last 30 s); CANCEL during a cleaning also goes to AUTO DRY, CANCEL during a drying goes to COOL DOWN (60 s).
4. **Names**: the web page and alerts use plain names (TFT Display, Sensors, Relay Controller, Main Controller, Refilling Station); ACS door lock = Side lock.
5. **Alerts**: EMPTY / LOW alone is not an alert (the station refills). Alerts = Refilling Station offline, refill failed, board not heard, out of order.

### The contract (`Shared_Common.h`, same file in 22 folders)

- `DEVICE_REFILL = 14`; `RefillStatusPacket` (35 bytes, magic `0xD1`): raw[3], state[3] (0 EMPTY .. 4 MAX, 255 fault), valveMask, queueMask, failMask, testMask, midEdge/highEdge/maxEdge[3].
- `CMD_REFILL_TEST = 17` (LocalServer -> station, subIndex = box 0..2, state 1 start / 0 stop).
- Station settings: `CMD_SET_CONFIG` to `DEVICE_REFILL`; subIndex 1/2/3 = Box 1/2/3 `emptyMax,midEdge,highEdge,maxEdge,stallEmptyS,stallLowS,maxRefillS,retryS,minRise` (defaults 0,500,1000,1200,30,5,180,60,10), subIndex 4 = `testSeconds` (2). The station echoes them as `CMD_SET_CONFIG` target 0, state tag 4.
- Box 3 settings (S3B, state tag 3): 1 RULES `rateCoins,rateValue,rateUnitMin,maxCoins`; 2 SENSORS `helmetCm,refillOfflineSec`; 3 AUTO-DRY `autoDrySec,heaterOffTailSec`; 4 DRYING `coolDownSec`; 5 refresh masks; 6 auto refresh; 7 beep.
- New machine states: `STATE_AUTO_DRY = 34`, `STATE_START_DRY = 35` (appended, nothing renumbered).

---

## 3. Bring-up on the bench (suggested order)

Loads (UV, humidifiers, heater, valves) **unplugged** for the first power-up of each board.

**A0. Stand-in for the station (while the real one is not finished, 2026-11-03):** flash `ESP32_REFILL_FAKE` on a spare ESP32 (WROOM or S3). It has no sensors / valves and broadcasts the real `RefillStatusPacket` once a second with levels you choose (default all HIGH). Serial 115200: `3 high` / `3 low` / `all mid` / `3 fault`, `off` / `on` (offline), `queue 3` / `valve 3` / `fail 3` (flags for the LocalServer / web page); BOOT button = Box 3 level up one step, hold 1 s = on / off. Box 3 check: `3 high|mid|max` -> tap at WELCOME -> SELECT MODE; `3 low|empty|fault` -> SELECT DRY ONLY; `off` + 15 s -> SELECT DRY ONLY. **From a phone:** the board makes its own Wi-Fi `JollyHelmet-FakeStation` (password `12345678`, channel 1, captive portal): join it and the control page opens by itself (or browse to `http://192.168.4.1/`): level buttons per box, QUEUE / VALVE / FAIL flags, big BROADCASTING / OFFLINE button, and a line saying whether Box 3 will offer CLEANING + DRYING or DRYING only. The page follows the BOOT button and serial changes too. The phone is on the fake's Wi-Fi (no internet, not on `JollyHelmet-Local`), so rejoin the LocalServer afterwards. Never run it together with the real station. Remove it when `ESP32_REFILL` goes in.

**A. Refilling Station** (nothing else needed)
1. Pins (**DECIDED 2026-11-03**, also in `RevisedJollyHelmet_Checked.xlsx` as block F1, rows 171-201): water-level sensors (signal wire only) Box 1 = GPIO 34, Box 2 = GPIO 35, Box 3 = GPIO 32 (ADC1 only); valve relay inputs Box 1 = GPIO 21, Box 2 = GPIO 19, Box 3 = GPIO 18 (relay IN1 / IN2 / IN3). Only `SENSOR_PIN` / `VALVE_PIN` change if a pin is ever moved. **Relay level:** `VALVE_ACTIVE_LOW 1` = the project's active-LOW relay modules (LOW = relay on = valve open). If the modules you fit are active-HIGH set it to 0. At the first power-up (valves unplugged) the three relay LEDs must be OFF; the serial banner says which level the sketch assumes. Power the sensors from 3.3 V (expansion board voltage jumper), never 5 V into the ADC. The valve coil voltage (12 V DC / 220 V AC) is still undecided (workbook cell K200 shows "?").
2. Flash. Serial 115200: `help`. The three valves must be CLOSED at power-up. `raw` streams the readings; `test 1`/`test 2`/`test 3` click each relay for 2 s.
3. Put each sensor dry, then in the liquid: write down the raw numbers. Set the edges per box on the LocalServer Refilling Station page (EMPTY up to / MID starts / HIGH starts / MAX starts) or with `set <box> <name> <value>`.
4. With the valves connected and a supply: lower a container below LOW; the valve must open, close at MAX, and only ONE valve may ever be open. Pull the supply: the refill must fail after 30 s (EMPTY) / 5 s (LOW) and retry every 60 s.
5. No flyback diode / snubber asked for (decided 2026-11-03: the station is isolated from Boxes 1-3, no coin signal). Only if the station resets when a valve switches, add one across that coil.
6. **Sensor health (added 2026-11-04):** the station reports level UNKNOWN (page: SENSOR FAULT, the box offers drying only, no refill, web alert) when a sensor reads on the 3.3 V rail (>= 4090) or its reading jumps by more than 1500 within a second while its valve is closed, for 5 s; it clears after 10 s of good readings (constants `HEALTH_*` in `ESP32_REFILL.ino`). Console command `sensors` prints average / min / max / spread. A dry sensor reading 0 cannot be told from a dead one by the value (that case = a refill that does not rise = FAILED). **Bench test:** with the sensor in place type `sensors`, then pull the SIG wire and type it again; if a disconnected wire reads like a dry one, tell the programmer so the limits (or a pull-up on SIG) can be changed. Needs the station reflashed; the web alert / dial text needs the `ONLINE_WEB` upload; the new page note needs the LocalServer reflashed (optional).

**B. LocalServer**: flash, join `JollyHelmet-Local`, open `/refill`: the station must show online with its levels; send a settings change and see the green tick; the Dashboard must say "12 of 12" when everything runs.

**C. Box 3** (loads unplugged): flash **B3, then B2, then B1, then S3B last**.
1. B3: all relays off at boot; from `/box3` Maintenance each relay tile clicks (bits 8, 9 hidden and never driven).
2. B2: `[B2] helmet=... doors ...` once a second; open each door reed and watch it change. Set the helmet distance on `/box3` (use live).
3. B1: calibrate the touch (it still has A1's numbers: run `ESP32_C1_CALIBRATE` at the rotation used). `TOUCH_DEBUG` is 0.
4. S3B: walk the whole flow with the loads unplugged: TAPS -> tap -> CHECKING 2 s -> WELCOME -> tap -> SELECT MODE (station running and the container MID or more) / SELECT DRY ONLY (unplug the station). Pay 15 coins, open the door, helmet in, close: cleaning (UV 1+2, 3 humidifiers, coin slot ON), then AUTO DRY (heater off for the last 30 s), then OPEN B ... FINISH. Repeat for DRYING (START DRYING "Please Wait", heater, COOL DOWN 60 s). Try CANCEL in both, the safety pause (open the door during a run), and a silent B2 (unplug it at WELCOME: OUT OF ORDER).
   **Helmet cutoff (2026-11-03 ~10 AM):** "helmet inside" = B2's distance is above 0 and under the LocalServer value (`/box3` -> Sensors -> "Helmet counts as inside under", 1-200 cm, "use live" reads the current distance). S3B, B3 and B1 all use that ONE value: S3B broadcasts it every 2 s and B3 / B1 keep the last one in flash (default 40 cm until they have heard it), so it still works after a power cut. Before this change B3 and B1 had a fixed 15 cm. After flashing B3 and B1, open `/box3` once and check the number (set it a little above the helmet's real reading); B3 / B1 log `Helmet cutoff is now ... cm` when they take it over.
   **Trapped helmet (added 2026-11-03 ~6 AM):** put a helmet inside, shut the door, then unplug B3 (or B2) at WELCOME -> OUT OF ORDER; after 2 s the enclosure lock must release and the buzzer must beep 100 ms / 500 ms until the door is opened (or 180 s pass); an empty box stays locked and silent. With B2 unplugged the lock gives ONE 15 s blind window, no beep. Then unplug **S3B** itself with a helmet inside and the door shut: after about 15 s B3 releases the lock by itself (watchdog, up to three 60 s windows) and B1 shows OUT OF ORDER and beeps the same way (200 s limit) - if B2 is still powered.
   **UPS design (user, 2026-11-03):** B1, B2, B3 (+ the enclosure-lock solenoid's supply) on a UPS; the S3s, LocalServer and Online Server on mains only. A power cut silences the S3 -> from ANY step Box 3 goes OUT OF ORDER (B3 turns its outputs off after 15 s, unlocks a trapped helmet; B1 beeps and shows "Your helmet is still inside. Take it out, then close the door." / "Please close the door."). A paid session lost to a power cut is not restored or refunded (decided: the grid is not our fault). **Power back:** the S3 reboots; 10-18 s after boot, if nobody has paid yet and the door is shut with a helmet inside for 3 s, S3B goes to OPEN B first (unlock + beep, take the helmet, close, FINISH) instead of locking it in. Bench test: helmet inside, door shut, unplug and replug S3B only -> after ~12 s the lock releases and the buzzer beeps.
5. Connect the loads one at a time.

**D. Online Server + web**: flash `ESP32_ONLINESRV`, upload the whole `ONLINE_WEB` folder to the web host (the page needs the new `rf` data and the 12-board flags; older firmware shows the percent dials). First check that the Blynk channel-1 return after an upload burst works.

**Boxes 1 and 2 (changed 2026-11-12):** they are rebuilt like Box 3 - bring them up exactly like Box 3 below (relay board, sensor board, terminal, main controller; on Box 2 the relays now come from the new C3). See the 2026-11-12 section at the top.

### Rollback
- The ACS firmware, its plan and its LocalServer page are in `ARCHIVE_2026-11-03_BEFORE_REFILLING_STATION\`. The pre-change LocalServer files and the old S3B sketch (and its screens) are in the same folder.
- Old `B2` = `ESP32_A2` with `DEVICE_B2` as the device id (it was a copy).
- Keep the `.bin` of whatever is running on a unit before reflashing it.

---

## 4. Open items

**Needs the hardware team:** station pins are DECIDED (see 3.A.1); confirm the relay modules' active level (firmware assumes active-LOW) and the valve coil voltage; sensor type and its raw range; Box 2's three-board split is now in the firmware (C1 terminal, C2 sensors, C3 relays) but the C3 board has to be built (see the 2026-11-12 section). B1 touch: same as Box 1's numbers for now; flash a calibration only if it proves inaccurate on the test day. Flyback diodes: not needed (station is isolated, no coin signal).

**Decisions made but not built:** (done 2026-11-12: Box 1 and Box 2 got the Box 3 flow, one common program per node type with a `box_config.h`.) The station's GPIO pins are decided (section 3.A.1).

**Manual: v10 DONE 2026-11-04** (`MANUAL\JollyHelmet_UserManual_v10.docx` + `.pdf`, 111 pages; v9 + Refilling Station chapter, Box 3 36-step customer pages, UPS / OUT OF ORDER, wiring, charts, settings reference, glossary, Rev 10; Box 1 and Box 2 text unchanged; details in `MANUAL\README.txt`). If the flow or a chart changes after the bench tests, v11 must follow the same change list; the Word file is the master. The original change list (kept for reference) was: the next version must: replace the ACS chapters with a Refilling Station chapter (levels, queue, test button, failure and what to do); rewrite the Box 3 flow and screens to the 36 steps (CHECKING, dry-only, required coin, AUTO DRY, cancel paths); new LocalServer page screenshots (Box 3 page, Refilling Station page, Dashboard card); the plain board names; the web page's dials / alerts / network map; the 12 devices; remove drain pumps, ratings, time picker. Charts for it: `FLOWCHART_SYSTEM\png`.

**Older open items still valid:** alcohol-vapour + heater safety review; UV tube geometry notes; "add little minutes" for coins mid-cycle (seconds per coin); the coin-noise / booked-coins builds were never confirmed flashed; LocalServer heap-crash hunt (check Home -> "last restart", BROWNOUT = supply); `CONCEPTUAL_FRAMEWORK` pictures still show the ACS (superseded by `FLOWCHART_SYSTEM`).

**Housekeeping:** `ESP32_LOCALSRV\tools\build_hub_pages.ps1.obsolete` generated the old settings pages and must not be run: `settings_pages.h` is edited by hand now. The simulators must be kept in step with their sketches when the rules change.


## 2026-11-04 - the Refilling Station watches and refills BOX 3 ONLY (for now)
Why: Box 1 and Box 2 are still being worked on. How: `const bool WATCH_BOX[3] = { false, false, true };` in ESP32_REFILL.ino. An unwatched box is never fault-checked, never queued, its valve never opens
(also not by the manual test), and the station reports it as `REFILL_OFF` (254, new in Shared_Common.h, all 20 identical copies updated; the checksum of the file is now 16217b04) with raw 0.
Readers updated: LocalServer (Dashboard card and Refilling Station page say NOT WATCHED, test button greyed), Online Server (`rf` state -2), ONLINE_WEB (grey dial "NOT WATCHED", no alert, demo data),
ESP32_REFILL_FAKE (reports Box 1/2 as not watched like the real station), refill simulator (24 tests pass). To switch Box 1 or Box 2 on later: set its entry to true in WATCH_BOX and reflash the station.
NOTE: Box 1 / Box 2 firmware (S3A, S3C) is not rebuilt to the Box 3 flow yet; once it is, switch them on in WATCH_BOX or they would offer DRYING only.
Reflash needed: ESP32_REFILL, ESP32_LOCALSRV, ESP32_ONLINESRV (the boxes do not need it; the new constant only matters to the readers). All compile (REFILL 69 %, FAKE 73 %, LocalServer 41 %, Online Server 37 %, S3B 28 %, B1 73 %, S3A 29 %).
Manual v11 (100 pages) already says this (admin Refilling Station chapter, technician board table and pinout note, settings reference, glossary NOT WATCHED, version page).


## 2026-11-04 - COIN WAIT in INSERT COIN (a 20-peso coin for 15 required used to buy only 5 minutes)
Cause: B1 reports its pulse count to S3B once a second, and S3B moved on the moment the required coins were reached and clamped the time to the required coins; the rest of a big coin (the other pulses) arrived
after the state change and was ignored. Fix (ESP32_S3B.ino, `coinSettleCheck()`): after the required coins are reached INSERT COIN keeps the coin power on and counts until no new pulse has been seen for
COIN_SETTLE_MS = 1500 ms (longer than B1's 1 s report), at most COIN_SETTLE_MAX_MS = 6000 ms; then ALL coins counted become time (up to maxCoins) and revenue (20 coins = 400 s, 20 booked). Only S3B changes
(B1 untouched, no new packet). s3b_flow_test.html: 47 of 47 pass (4 new tests). Compiled (S3B 28 %), NOT flashed. Box 1 / Box 2 (S3A, S3C) still have the old clamp until they are rebuilt.
