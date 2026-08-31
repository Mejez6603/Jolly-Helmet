# Automated Helmet Cleaning Vending Machine

A coin-operated, multi-node IoT vending machine that sanitizes motorcycle helmets. Two independent cleaning units (a mist+UV "Box 1" and a heat-based "Box 2") share one central controller and web dashboard, backed by an autonomous alcohol-mixing subsystem (ACS) that keeps Box 1 supplied without manual refilling.

Built on ESP32 microcontrollers communicating over ESP-NOW, with the entire web dashboard served directly from flash memory — no SD card or external filesystem required.

---

## Features

- **Coin-operated cleaning cycles** — insert coins to add time; the machine walks the user through opening a chamber door, placing a helmet, and running an automated clean.
- **Two independent units on one controller:**
  - **Box 1** — alcohol mist + UV sterilization.
  - **Box 2** — heat + fan drying/sanitizing (no alcohol).
- **Automatic alcohol supply (ACS)** — a dedicated node mixes Water, Scented Liquid, and Alcohol at a fixed ratio and tops up Box 1's mist tank on its own, triggered automatically when the tank runs low.
- **Live web dashboard** — real-time telemetry, manual relay/sensor testing, and financial statistics, accessible from any phone or laptop over the machine's own WiFi access point (no internet required).
- **Statistics & revenue tracking** — sessions completed, total coins collected, average cycle duration, and a rolling revenue chart, persisted across reboots.
- **Maintenance Mode** — lets a technician manually flush ACS's tanks before transporting the machine, without fighting the automatic refill logic.
- **Safety interlocks** — one-relay-at-a-time enforcement (prevents overloading shared power), mid-cycle door/helmet monitoring with automatic pause-and-resume, and abort-with-confirmation during an active cycle.

---

## Technologies Used

| Layer | Technology |
|---|---|
| Microcontroller | ESP32-WROOM (ESP32-D0WD-V3), Arduino Core **3.3.7** (ESP-IDF 5.x) |
| Node-to-node networking | **ESP-NOW**, broadcast-only, fixed WiFi channel 1 |
| Web server | `ESPAsyncWebServer` + `AsyncTCP` (ESP32Async fork) |
| Real-time dashboard updates | `AsyncWebSocket` (`/ws`), 1 Hz JSON push |
| Captive portal | `DNSServer` |
| Persistent settings/stats | `Preferences` (ESP32 NVS) |
| JSON parsing/serialization | `ArduinoJson` |
| Display + touch | `TFT_eSPI` (ST7789 TFT, resistive touch) |
| Web assets | Raw HTML/CSS/JS embedded as `PROGMEM` strings — **no filesystem** |

---

## Getting Started

### 1. Install the Arduino IDE and ESP32 board support

- Arduino IDE (2.x recommended).
- Add the ESP32 board package (Boards Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-storage/package_esp32_index.json`) and install **ESP32 Arduino Core 3.3.7**.

### 2. Install required libraries (Library Manager)

- `ESP32Async/ESPAsyncWebServer`
- `ESP32Async/AsyncTCP`
- `ArduinoJson` (v6.x)
- `TFT_eSPI` (Bodmer) — requires manual pin configuration, see below
- `Preferences` and `DNSServer` ship with the ESP32 core.

### 3. Configure `TFT_eSPI` (for A1 and C1 only)

`TFT_eSPI` is configured via its own `User_Setup.h` (inside the library folder), not per-sketch. Set it up for an ST7789 driver with this pin mapping before compiling A1 or C1:

| Signal | Pin |
|---|---|
| CS | 15 |
| DC | 2 |
| RST | 4 |
| SCK | 14 |
| MOSI | 13 |
| Touch CS | 27 |
| Touch MISO | 12 |

### 4. Flash each node

Open the corresponding folder's `.ino` file, select the correct board/port, and upload. **Recommended boot order: power on A1/A2/C1/C2/ACS first, then Master last** — booting Master first has been observed to cause connectivity issues that are still being investigated (see [Troubleshooting](#troubleshooting)).

---

## Usage

### Operating the machine

1. Insert coins into either box's coin acceptor — each coin adds a fixed number of seconds (default 20s/coin, configurable from the dashboard).
2. Follow the on-screen prompts on that box's touchscreen: open the door, place the helmet, close the door.
3. The cleaning/heating cycle runs automatically, with a visible countdown and an **ABORT** option.
4. When finished, open the door to retrieve the helmet, then close it again to complete the cycle.

### Accessing the dashboard

1. Connect a phone or laptop to the WiFi network **`ESP32_LOCAL`** (password `12345678`).
2. Open a browser to `http://192.168.4.1` (or just open any URL — the captive portal will redirect you).
3. Use the ☰ menu to switch between:
   - **Statistics & Revenue** — sessions, revenue, average duration, and a recent-cycles chart.
   - **Box 1** — Node A1 (coin/touch terminal) and Node A2 (doors/locks/sensors) status and manual controls.
   - **Box 2 (Heater)** — the same, for Nodes C1/C2.
   - **Alcohol Container System** — tank levels, manual pump/lock testing, and Maintenance Mode.

---

## Project Structure

```
ESP32 Project/
├── ESP32_LOCALSRV/         Master/Server - orchestrates both boxes, hosts the web dashboard
│   ├── ESP32_LOCALSRV.ino
│   ├── Shared_Common.h      Wire protocol shared by every node (copy - see note below)
│   ├── index_html.h         Dashboard HTML (PROGMEM)
│   ├── style_css.h          Dashboard CSS (PROGMEM)
│   └── script_js.h          Dashboard JS (PROGMEM)
├── ESP32_A1/                Box 1 Terminal - coin acceptor, buzzer, TFT + touch
├── ESP32_A2/                Box 1 Actuator Hub - doors, locks, UV, mist, sensors
├── ESP32_ACS/               Alcohol Container System - autonomous tank mixing/refill
├── ESP32_C1/                Box 2 Terminal - clone of A1
├── ESP32_C2/                Box 2 Actuator Hub - doors, locks, Heater, Fan, helmet sensor
├── ESP32_C1_CALIBRATE/      Temporary touch-calibration utility for C1 (not part of the firmware)
└── README.md
```

> **Important:** `Shared_Common.h` must be byte-for-byte identical across `ESP32_LOCALSRV`, `ESP32_A1`, `ESP32_A2`, `ESP32_ACS`, `ESP32_C1`, and `ESP32_C2`. Arduino sketches can't share a header across folders, so any change to the wire protocol has to be manually copied into all six and re-flashed everywhere. Verify with:
> ```bash
> md5sum ESP32_*/Shared_Common.h
> ```

---

## Packaging for Distribution

To prepare firmware for deployment without needing the Arduino IDE on-site:

1. In Arduino IDE, open each node's `.ino` file and select **Sketch → Export Compiled Binary**. This produces a `.bin` in that sketch's folder.
2. Flash a `.bin` directly with `esptool.py`:
   ```bash
   esptool.py --chip esp32 --port COMx --baud 921600 write_flash 0x10000 ESP32_A1.ino.bin
   ```
   (Offset may vary by core version/partition scheme — check the `.map`/build output for the exact address if `0x10000` doesn't boot correctly.)
3. Keep a copy of `Shared_Common.h`'s current version alongside the binaries — if the protocol ever changes, every board's binary must come from the *same* version of that file, or telemetry parsing will silently break.
4. There is no separate "web asset" bundle to package — the dashboard is compiled directly into Master's `.bin`.

---

## Future Enhancements

- **Dynamic, dashboard-configurable thresholds** — Low/Full tank levels, helmet-detection distances, and coin timing are currently hardcoded constants; a settings panel would let these be tuned without reflashing.
- **Per-box statistics** — Box 1 and Box 2 currently feed into one shared revenue/session counter; splitting this out would show each box's performance independently.
- **EMI hardening** — relay/solenoid switching has caused observed interference (a TFT glitch on A1, garbled serial on ACS). A firmware-side cosmetic fix exists for the TFT case; the underlying fix (snubbers/flyback diodes, physical wiring separation, ferrite chokes) is a hardware task.
- **Confirm ACS's Mixing Machine relay pin** — currently inferred from a pinout reference sheet and flagged as unconfirmed in code.
- **Box 3** — a third cleaning unit appears in the original system architecture diagrams but has not been started.
- **Cloud/offline analytics** — the original architecture sketches show a secondary server and online analytics layer; not implemented.

---

## Changelog

- **Box 2 (Heater) added** — Nodes C1/C2 and a full parallel state machine on Master, mirroring Box 1's cycle (Instructions → Sensors → Heating → Retrieve → Finish), reusing the fixes already proven on Box 1.
- **AUTO_CALL delivery** — Box 1's humidifier tank level now automatically requests a refill from ACS (routed through Master so it's logged), instead of requiring a manual trigger.
- **ACS (Alcohol Container System) built** — autonomous Water/Scented/Alcohol mixing with dynamic, ratio-correct batch sizing when ingredients are partially available, plus a Maintenance Mode for pre-transport flushing.
- **Statistics & Revenue dashboard** — replaced an earlier, non-functional "Dynamic Timeline Editor" with session/revenue/duration tracking persisted to NVS.
- **Box 1 state machine rewrite** — hardcoded, diagram-driven flow (Welcome → Instructions → Checking → Refilling → Sensors → Cleaning → Retrieve → Finish) replacing a generic array-driven engine.
- **Core reliability fixes**: `strtok` reentrancy bug in screen parsing, real coin-slot debounce, WebSocket command misrouting, a packet-size collision between telemetry types that silently broke coin counting, and a two-phase retrieve step to close a "door left open" gap.
- **Dashboard polish** — merged per-node tabs into combined Box tabs, added a persistent low-tank refill banner, and reworked layout for desktop/exhibit display.

*(This is a summary. See project history/commit notes for full detail.)*

---

## System Requirements

### Hardware (per unit)

- ESP32-WROOM development board (one per node — 5 in the base system, 6 with Box 2)
- ST7789 TFT display with resistive touch (A1, C1 only)
- Allan-type coin acceptor, MED pulse mode (A1, C1)
- Active buzzer (A1, C1)
- HC-SR04-style ultrasonic distance sensors (A2, ACS, C2)
- Magnetic reed door switches ×3 per actuator node (A2, C2)
- Relay modules, active-LOW, for solenoid locks and loads (A2, ACS, C2)
- Solenoid door locks ×3 per box
- 12V DC power supplies (separate from any USB/logic supply where possible, to reduce switching noise)

### Software

- Arduino IDE 2.x
- ESP32 Arduino Core **3.3.7**
- Libraries: `ESPAsyncWebServer` (ESP32Async fork), `AsyncTCP` (ESP32Async fork), `ArduinoJson` 6.x, `TFT_eSPI`

### Client (for the dashboard)

- Any modern browser (Chrome, Edge, Safari, Firefox) capable of joining a WiFi network and rendering WebSockets — no app install needed.

---

## Troubleshooting

**TFT shows only the first line / partial screen**
`strtok()` is not reentrant — if this recurs after a code change, check for nested `strtok()` calls with different delimiters and switch to `strtok_r()` with separate save-pointers.

**Coins inserted but the machine doesn't progress**
Check Master's Serial Monitor for `[MASTER COIN RECEIPT]` when a coin is inserted. If it never appears, confirm `Shared_Common.h` is identical (byte-for-byte) across *all* node folders and that every board has been reflashed since the last protocol change — a stale build on one node can cause its packets to be misread as a different packet type entirely.

**TFT flashes white briefly after a relay/solenoid toggles**
This is switching noise (EMI/brownout) from the relay coil, not a logic bug — Master schedules an automatic repaint ~300ms after any A2/C2 relay command to paint over it. If it's still visually distracting, the real fix is hardware: a flyback diode across the solenoid coil, physical separation of TFT wiring from relay wiring, or a larger decoupling capacitor near the terminal's regulator.

**Machine behaves differently depending on power-on order**
Booting A1/A2/C1/C2/ACS *before* Master has been more reliable in testing than the reverse. This is unexpected for ESP-NOW (broadcast is connectionless — order shouldn't matter), and the root cause hasn't been confirmed. Until it is, treat "peripherals first, Master last" as the standard startup procedure.

**Touchscreen stops responding after changing `tft.setRotation()`**
Touch calibration data is tied to the rotation it was captured at. Changing rotation without recalibrating can cause `getTouch()` to compute coordinates outside the valid screen bounds and silently reject every touch (not just misplace it). Re-run `ESP32_C1_CALIBRATE.ino` (or an equivalent calibration sketch) **at the rotation actually used in production**, and copy its output `calData` array into the real firmware.

**A relay's actual behavior (LED/current) doesn't match what the code claims**
Relay modules vary between active-LOW and active-HIGH triggering. Check the board's silkscreen for "LOW LEVEL TRIGGER" / "HIGH LEVEL TRIGGER," or watch the Serial log line (e.g., `SOLENOID TRIGGERED: Pin 4 set to LOW=OPEN`) against the relay's LED at the same moment. If they disagree, `RELAY_ACTIVE`/`RELAY_INACTIVE` need to be swapped for that node — check whether it's isolated to one relay channel or the whole board before changing it.

**Ultrasonic sensor readings jump around by several cm**
This is a known characteristic of these sensors in confined tank/chamber geometries (wall-bounce). Never use a live reading as a repeated interrupt condition during an in-progress action — compute a target once from a stable snapshot and track progress by delta from that snapshot, with a time-based (not reading-based) timeout as the final safeguard.

---

## License

No license has been specified for this project yet — all rights reserved by default. If you intend to open-source this project, add a `LICENSE` file (e.g., MIT, Apache 2.0) and update this section accordingly.
