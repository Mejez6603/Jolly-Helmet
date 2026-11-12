# Jolly Helmet - coin-operated helmet cleaning and drying machines

Three coin-operated boxes that clean (UV + mist) and dry (UV + heater) motorcycle helmets, one **Refilling Station** that keeps their liquid containers full, a **LocalServer** (the technician's Control Center) and an **Online Server** that sends a snapshot to the cloud (Blynk) for a monitoring web page. Everything inside the machine talks over **ESP-NOW** (Wi-Fi channel 1, broadcast, no router, no internet); the web pages are served from flash memory.

*Rewritten 2026-11-03 for the Refilling Station design. The old two-box / ACS / "Master" description is gone: the ACS (alcohol container system) was replaced by one Refilling Station.*

---

## The system

| Part | Boards | What it does |
|---|---|---|
| **Box 1** (cleaner) | S3A main controller, A1 TFT display + coin slot, A2 sensors, A3 relay controller | the customer flow of Box 1. Still the OLDER flow (ratings, time picker). To be rebuilt like Box 3. |
| **Box 2** (dryer) | S3C main controller, C1 TFT display + coin slot, C2 sensors + relays | the customer flow of Box 2 (24 steps). To be rebuilt like Box 3. |
| **Box 3** (cleaner + dryer) | S3B main controller, B1 TFT display + coin slot, B2 sensors (helmet, doors), B3 relay controller (+ heater) | the trimmed 36-step flow: tap, checking, mode, coins, door, helmet, cleaning, auto-dry, door, thank you. |
| **Refilling Station** | ESP32-WROOM `ESP32_REFILL`: 3 water-level sensors, 3 normally-closed valves | works alone: one valve at a time, EMPTY first then LOW. The boxes only read its levels. |
| **LocalServer** | ESP32-S3 `ESP32_LOCALSRV` | Wi-Fi `JollyHelmet-Local` (192.168.4.1): Dashboard, session log, revenue, one settings page per box and one for the Refilling Station, maintenance. |
| **Online Server** | ESP32-S3 `ESP32_ONLINESRV` | listens only, uploads a JSON snapshot to Blynk every minute, router-style setup page. |
| **Online web page** | `ONLINE_WEB\` (static site) | Monitor (boxes drawn as machines, level dials, network map), Alerts, Reports; `status.html` for customers. |
| **ENTRY board** | `ESP32_ENTRY` | form to type the previous customers into the LocalServer. |

Customer rule for the container: on the first tap the box shows CHECKING (2 s), then WELCOME; on the next tap CLEANING is offered only when the Refilling Station was heard in the last 15 s **and** the box's container reads MID, HIGH or MAX. Otherwise the customer gets SELECT DRY ONLY.

The Refilling Station: levels per box EMPTY / LOW / MID / HIGH / MAX from a raw sensor value (defaults 0 / 500 / 1000 / 1200, editable per box); EMPTY or LOW joins a queue; EMPTY first, then LOW, first come first served; one valve open at a time; MAX stops the refill; no rise in the level (30 s when EMPTY, 5 s when LOW) or 3 minutes open = FAILED, retry every 60 s for ever.

**Charts** (Mermaid code + PNG + PDF): `FLOWCHART_SYSTEM\`. **Customer screens** (TFT): `FLOWCHART_BOX3\`. **Design notes and decisions**: `PROJECT_HANDOFF.md`.

---

## Folders

```
ESP32 Project/
├── ESP32_REFILL/            Refilling Station firmware (+ tools\refill_simulator.html: the queue logic with 17 self-tests)
├── ESP32_S3B/  B1  B2  B3   Box 3: main controller (+ tools\s3b_flow_test.html: 31 scenario tests), TFT display, sensors, relays
├── ESP32_S3A/  A1  A2  A3   Box 1 (older flow)
├── ESP32_S3C/  C1  C2       Box 2
├── ESP32_LOCALSRV/          LocalServer (settings_pages.h = the settings pages, settings_hub.h = their API, index_html.h/script_js.h = Dashboard)
├── ESP32_ONLINESRV/         Online Server (+ the master copy of Shared_Common.h)
├── ESP32_ENTRY/             ENTRY board
├── ONLINE_WEB/              the monitoring web page (upload to any web host) + DATA_CONTRACT.md
├── FLOWCHART_SYSTEM/  FLOWCHART_BOX3/   charts
├── MANUAL/                  the user manual (Word is the master; build tools in MANUAL\build)
├── BOX 3 STEP corrected.txt the Box 3 flow (Box Cycle Designer export); BOX 1 STEP.txt / BOX 2 STEP.txt the other two
├── ARCHIVE_2026-11-03_BEFORE_REFILLING_STATION/   the ACS firmware and the files that were replaced
└── ...bench / test / helper sketches (A1_WIDGET_TEST, A3_BENCH_TEST, B3_RELAY_TEST, C1_CALIBRATE, *_CONFIG_BENCH, RESTATE)
```

### The shared header

`Shared_Common.h` (device ids, packet layouts, commands, the Refilling Station's rules) must be **byte-for-byte identical in every node folder** (Arduino sketches cannot share a header). The master copy is `ESP32_ONLINESRV\Shared_Common.h`; after any change copy it to all the others, then check:

```powershell
Get-ChildItem -Recurse -Filter Shared_Common.h | ForEach-Object { (Get-FileHash $_.FullName).Hash.Substring(0,8) } | Group-Object
```
(one group = all identical; `ESP32_S3A_COMMTEST` is an old test sketch with its own copy). Packets are told apart by their SIZE, so every packet has a `static_assert` against all the others.

---

## Build and flash

- Arduino core **ESP32 3.3.7**. Libraries: ESPAsyncWebServer + AsyncTCP (ESP32Async fork), ArduinoJson 7, TFT_eSPI (B1, A1, C1 only: configure its `User_Setup.h` for the ST7789 + touch pins of the pin sheet).
- ESP32-S3 boards (S3A, S3B, S3C, LocalServer, Online Server): board `esp32s3`, options `CDCOnBoot=cdc, PSRAM=opi, FlashSize=16M, PartitionScheme=app3M_fat9M_16MB`. Everything else: plain `esp32` (WROOM).
- Command line (what the project was compiled with): `arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB <folder>`.
- Flash order and bench checks for the new design: see `PROJECT_HANDOFF.md`.

---

## Troubleshooting (kept from the early builds)

**Touchscreen stops responding after changing `tft.setRotation()`** - the touch calibration belongs to the rotation it was captured at. Re-run `ESP32_C1_CALIBRATE` at the rotation used in production and copy its `calData` into the firmware (B1 still has A1's numbers: calibrate it).

**Coins inserted but nothing happens** - check that `Shared_Common.h` is identical everywhere and every board was reflashed after the last protocol change (a stale board misreads packets as another type). Watch the S3's serial log for `[S3B COIN]` lines.

**TFT flashes white after a relay switches** - switching noise. The S3 repaints B1 after relay changes and has per-relay "refresh" settings on the LocalServer page; the real cure is hardware (flyback diode across every solenoid and valve coil, separate wiring from the TFT, bigger decoupling capacitor).

**Random coin pulses** - the same switching noise; the drain pumps that caused it were removed from the design. The Refilling Station stands apart from Boxes 1-3 and has no coin signal, so it needs no extra suppression (decided 2026-11-03); if the station itself ever resets when a valve switches, add a flyback diode (DC valve) or RC snubber (AC valve) across that coil.

**A relay does the opposite of what the log says** - relay boards are active-LOW or active-HIGH; check `RELAY_ACTIVE` in that node (on the Refilling Station: `VALVE_ACTIVE_LOW`, 1 = active-LOW).

**Ultrasonic reading jumps** - normal in a metal box (wall bounce); the S3 uses a median of 5 readings and a 2-tick confirmation for the helmet and the doors.

---

*No license has been specified: all rights reserved.*
