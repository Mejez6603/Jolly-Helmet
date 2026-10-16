# Online Server -> Blynk -> index.html : the data contract

`index.html` only READS one Blynk datastream. The Online Server (`ESP32_ONLINESRV`, an ESP32-S3) writes it in exactly this shape.
Written 2026-10-09 (updated the same day when the firmware was written). The firmware compiles but has NOT been run on the real boards yet.

## 1. Blynk setup (once)
1. Blynk Console > Templates > New template ("Jolly Helmet Online Server", hardware ESP32, connection Wi-Fi).
2. Datastreams:
   * **V0** - Virtual Pin, name `live`, data type **String** (a String value may be up to 1024 bytes; the real payload is about 650). Required.
   * V1 `revenue` (Double), V2 `customers` (Integer), V3 `alerts` (Integer), V4 `running` (Integer) - optional, only for Blynk's own dashboards.
     Switch them on in the Online Server's setup page ("Also send single numbers V1 to V4").
3. Optional: Events > New event, code **`alert`** (type Warning or Critical, notify by push / email). Switch on "Send a Blynk event" in the setup page.
4. Devices > New device from that template. Copy its **Auth Token** (Device info). Paste it into the Online Server's setup page and into the monitor page.

Note: the board writes with Blynk's HTTPS API, not with the Blynk library, so Blynk Console shows the device as "offline" most of the time even though
its values keep arriving. That is normal; the monitor page judges "alive" by the `seq` and `ts` fields instead.

## 2. How often it writes
Every `iv` seconds (set in the setup page: 30 s, 1 min, 2 min or 5 min; default 1 min). Each write is one `update` call to V0 (plus one for V1-V4 and one
per new alert event when those are switched on). A burst takes about 5 to 10 seconds, during which the board is not listening to the machines,
so do not go below 30 s. One write a minute is about 43,200 a month. Put your plan's monthly message limit in the setup page ("More settings") and the
monitor page shows a quota bar; leave it at 0 to hide the bar.

## 3. The V0 value (one JSON text)

```json
{"seq":5,"ts":1791540182,"iv":60,"n":"1111111111111",
 "b":[[12,0,0,0,0,73,62,0,0],[3,25,6,0,8,-1,-1,0,24],[31,40,8,0,6,65,42,1,4240]],
 "a":[[1,0,0,0,0,85,67,0,0],[1,1,0,0,0,68,70,0,0]],
 "st":{"cv":1.00,"ss":41,"cn":331,"rv":331.00,"ad":412,
       "sys":{"up":18025,"rst":"power-on","boots":3,"rssi":-58,"heap":162000,"mq":1180,"mqmax":0},
       "h":[[686,15],[625,30]], "dy":[[22,3],[31,4],[18,2]]}}
```

| Key | Meaning |
|---|---|
| `seq` | Counter, +1 on every write (starts at a random number after a restart). The page treats a changed `seq` as "new data". |
| `ts` | Unix time in seconds (set by NTP at the first successful upload). 0 until then. |
| `iv` | Upload interval in seconds. The page uses it to decide LIVE / DELAYED / OLD DATA. |
| `n` | 13 characters, `1` = heard in the last 8 seconds, `0` = not heard. Order: A1, A2, A3, S3A, C1, C2, S3C, B1, B2, B3, S3B, ACS 1, ACS 2 (same as the Control Center Home page). |
| `b` | Three boxes: Box 1, Box 2, Box 3. Each is a list of 9 numbers (below). |
| `a` | Two refill stations, ACS 1 then ACS 2. Each is a list of 9 numbers (below). |
| `st` | Analytics and Online Server health (below). |

### Box list `b[i]`
| Index | Meaning |
|---|---|
| 0 | Step number: `MachineState` for Box 1 and Box 3, `Box2State` for Box 2 (the numbers the S3 broadcasts; `js/names.js` has the names). 0 if the brain is not heard. |
| 1 | Seconds left in the current step. |
| 2 | Coins counted (the terminal's pulse counter, as the Control Center Home page shows). 0 if the terminal is not heard. |
| 3 | Doors open, bit mask: 1 enclosure, 2 panel, 4 back, 8 ACS side. Box 2 has no ACS side door. 0 if the sensor board is not heard. |
| 4 | Helmet ultrasonic distance in cm, 0 = no echo, **-1 = sensor board not heard**. The page says "inside" when 0 < cm < 15. |
| 5 | Alcohol container level 0-100 %, **-1 = unknown or not applicable** (Box 2 has no containers). |
| 6 | Scented container level, same rule. |
| 7 | Water sensors, bit mask: 1 = alcohol sensor wet, 2 = scented sensor wet. |
| 8 | Relays that are ON now, bit mask in firmware relay order. Box 1 (A3): 0 enclosure lock, 1 panel lock, 2 back lock, 3 humidifier 1, 4 UV 1, 5 humidifier 2, 6 humidifier 3, 7 UV 2, 8 drain pump 1, 9 drain pump 2, 10 ACS door lock, 11 coin power. Box 3 (B3): the same plus 12 heater. Box 2 (C2): 0 enclosure lock, 1 panel lock, 2 back lock, 3 heater, 4 UV, 5 fan. |

Container percent: the S3 holds the LOW line (`alcLowCm`, `sctLowCm`) and the FULL line (`alcHighCm`, `sctHighCm`) of each box and re-broadcasts them
(CMD_SET_CONFIG, LEVELS). The Online Server computes `percent = clamp(100 * (lowCm - cm) / (lowCm - highCm), 0, 100)`: 0 % = at the refill line, 100 % = full.

### ACS list `a[i]` (what `ACSStatusPacket` carries, the same list the Control Center Home page uses)
`[ok, step, flags, empty, warn, alcoholMixerPercent, scentedMixerPercent, pumpMask, stepSeconds]`. `ok` 1 = heard. `step` 0 IDLE ... 10 MAINTENANCE.

### `st`
| Key | Meaning |
|---|---|
| `cv`, `ss`, `cn`, `rv`, `ad` | Coin value (pesos), customers served, coins counted, revenue (pesos), average session seconds. Straight from the LocalServer's totals. Missing until the LocalServer has been heard. |
| `h` | Last 10 finished sessions, oldest first, each `[seconds, pesos]`. |
| `dy` | Last 14 days, oldest first, each `[coins, customers]`; the last entry is today. Counted by the Online Server from the change of the LocalServer totals, per local day (time zone set in the setup page). Missing until the clock is set. |
| `sn` | Unix time when the Online Server started counting the three lists below (the moment the clock became known; reset from the setup page). |
| `bx` | Per box, `[finished sessions, coins]` for Box 1, Box 2, Box 3, counted by the Online Server from each brain's `completedSessionSeq` (BoxStatusPacket). Revenue = coins x `cv`. |
| `rs` | Restated old data, only present after the spare ESP32_RESTATE board pressed **Update** (2026-10-11): `[update no., captured at (unix), customers at the capture, coins collected at the capture, those coins restated at the new price list, coins sold since the capture (-1 = the LocalServer statistics were reset), coin value x100 at the capture]`. The web page shows restated revenue = restated coins x old coin value + new coins x `cv`, always next to the collected figure. Stored in the Online Server flash; the spare board can be unplugged. |
| `hr` | 24 numbers: finished sessions per local hour of the day (0 = midnight hour), since `sn`. |
| `wd` | 7 numbers: finished sessions per weekday, Sunday first, since `sn`. |
| `sys.up`, `sys.rst`, `sys.boots` | Online Server uptime (s), last restart reason, number of starts. |
| `sys.rssi`, `sys.heap` | Wi-Fi signal (dBm) at the last upload, free memory (bytes). |
| `sys.mq`, `sys.mqmax` | Uploads counted this calendar month, and the plan limit (only sent if you entered one). |

## 4. How the Online Server gets its data
* **Live data**: it listens (receive only) to the ESP-NOW broadcasts of the machine boards on channel 1, exactly what the LocalServer hears. It never sends a command.
* **Revenue totals**: the LocalServer broadcasts a `HubStatsPacket` (58 bytes, defined in `Shared_Common.h`) every 10 seconds with its customers / coins / duration totals, the coin value and the last 10 sessions. **The LocalServer must be on and flashed with this version** for the analytics to appear; the live data does not need it.
* **One radio, two channels**: the machines are on channel 1, the user's router is on some other channel, and one radio can only be on one channel. The board listens on channel 1 almost all the time, takes a snapshot every `iv` seconds, joins the user's Wi-Fi for a few seconds, uploads, and goes back to channel 1. So **any Wi-Fi works** (no need to move the router to channel 1).
* **Security**: the TLS certificate is checked against ISRG Root X1 (blynk.cloud validates against it alone; checked with openssl on 2026-10-09). Everything Blynk could send back is ignored. A Blynk token can also write values through the web API: do not publish it.
* **Setup Wi-Fi**: the board's own Wi-Fi `JollyHelmet-Online` (password in the firmware, default `12345678`) opens a captive portal where the user picks the Wi-Fi, pastes the Blynk token and sees whether the board is connected.

## 5. The public status device (added 2026-10-10) - for `status.html`, the page customers see
A Blynk token can also WRITE, so the private token of section 1 must never be published. For a customer page a **second Blynk device** is used:
1. Blynk Console > Devices > New device, from the SAME template (it already has V0, String). Copy its Auth Token. This token is meant to be public.
2. Online Server setup page (JollyHelmet-Online) > "Public status device token": paste it, Save. From then on every upload also writes V0 of that device.
3. Put the same token in `js/config.js` (`publicToken`) or give it in the address: `status.html#t=THE_PUBLIC_TOKEN` (the QR button of the monitor does that).

What it holds (about 100 bytes, nothing else) in V0:
```json
{"seq":5,"ts":1791540182,"iv":60,"b":[[10,0,1,52],[3,41,1,20],[31,95,1,71]]}
```
`b` = Box 1, Box 2, Box 3, each `[step number, seconds left, brain heard 0/1, sessions]`; the step numbers are the same as in section 3 (`js/names.js`).
`sessions` (4th number, added 2026-10-14, needs the Online Server firmware of that date) is the brain's completed-sessions counter: +1 when a customer's cycle ends
(finished or aborted); 0 when the brain is not heard, and it starts again at 0 after the brain restarts. The page works without it (it ignores a missing 4th number).
The page shows FREE for TAPS/IDLE, IN USE for every other step (with the minutes left while it is running), OUT OF ORDER, NOT AVAILABLE for MAINTENANCE, and NO INFORMATION when
the brain is not heard or the data is older than about 4 minutes.
Worst case if somebody misuses the public token: they can write a wrong status into that one device (the next upload, within a minute, overwrites it) and use up
some of your Blynk messages. They cannot reach the private device, its data or the machines.
Cost: one more Blynk message per upload (a minute: about 43,200 more a month).

### 5b. Optional customer names (added 2026-10-14) - the one thing the customer page WRITES
A customer can type a name or nickname (12 characters at most) on the card of a machine that is running his or her cycle. Everybody sees it on that card
("Maria's helmet"), the customer's own phone shows "your helmet is done" when the cycle ends, and the name clears by itself. It is optional: no name, nothing shown.
* **One extra datastream, once:** Blynk Console > Templates > the template of the public device > Datastreams > New > Virtual Pin **V5**, name `names`, data type **String**.
  (The Blynk free plan allows only a few datastreams per template. If you are at the limit, delete V1-V4 (`revenue`, `customers`, `alerts`, `running`: they only feed Blynk's own
  dashboards and are off by default in the Online Server) or put another unused String pin in `js/config.js` > `namesPin`.) Without this datastream the page works exactly
  as before; a customer who tries to add a name is told that names are not switched on yet.
* **What it holds** (written by the customers' phones with the public token, never by the Online Server):
```json
[["Maria",52,1791540500],0,["Jun",71,1791540700]]
```
  One slot per box (Box 1, Box 2, Box 3): `[name, the box's sessions counter when the name was typed, server time in seconds]`, or `0` for no name.
* **When a name is shown:** only while the box is in a step where a helmet is being looked after (OPEN A, SENSORS A, CLOSE DOOR A, CLEANING / HEATING / DRYING, SAFETY PAUSE, ABORT)
  AND its `sessions` number is still the one stored with the name. So it disappears the moment the cycle ends, even if nobody has the page open; the next page that looks
  also deletes the stale slot. Without the `sessions` number (old firmware) a name is hidden when the box leaves those steps and after 45 minutes at most.
* **A name can be added only after the coins are in** (from OPEN A on), so a payment that is cancelled never leaves a name behind. The first name on a machine stays until the
  cycle ends: the phone that typed it can edit or remove it, other phones can only see it.
* **Safety:** whatever sits in V5 is treated as untrusted text (anybody who holds the public token can write to it): the page parses it defensively, strips `<` `>` and control
  characters, and only ever shows it as plain text. The worst a vandal can do is show a silly name for a cycle (or delete the names) and use some Blynk messages. Names are public
  to everybody who opens the page, so the page tells the customer to use a nickname.
* **Cost:** a few Blynk messages per customer who adds a name (one to save, one to verify, one to clear), nothing for the others.
* `js/config.js`: `names: false` hides the feature; `namesPin` chooses the datastream.
