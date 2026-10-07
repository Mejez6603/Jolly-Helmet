# ACS 1 (Box 1) and ACS 2 (Box 3) - refill system firmware (2026-10-03)

One design, one sketch, two boards ("E1" pin sheet). Replaces the retired 4-tank `ESP32_ACS.ino` (archived as
`ESP32_ACS1/ESP32_ACS_LEGACY_4tank.ino.txt`; the old `ESP32_ACS` folder is gone so it cannot be flashed by habit - its pins were different).
Flow source: the Designer's ACS flow (`ACS1_STEPS_2026-10-03_draft_v1.json` / `ACS2_...`) with the refinements listed below.

## What was built

| Item | File | Notes |
|---|---|---|
| **ACS 1** (serves Box 1) | `ESP32_ACS1/ESP32_ACS1.ino` | `DEVICE_ACS` (3). S3A asks it. |
| **ACS 2** (serves Box 3) | `ESP32_ACS2/ESP32_ACS2.ino` | `DEVICE_ACS2` (13, new). S3B asks it. **Identical to ACS 1 except the one line `#define ACS_UNIT`.** |
| Settings page (spare ESP32) | `ESP32_ACS_CONFIG_BENCH/` | Wi-Fi `ACS_CONFIG` / `12345678` -> `http://192.168.4.1`; pick ACS 1 or ACS 2 at the top. |
| Shared header | `Shared_Common.h` (all folders, md5 starts `9E0A81B5`) | `DEVICE_ACS2`, `ACSPump`, `ACSStep`, `ACSStatusPacket` (22 bytes, size-asserted), flag/empty/warn bits, new meaning of `CMD_REQUEST_DELIVERY` / `CMD_SET_MAINTENANCE` / `CMD_SET_CONFIG` for the ACS. The old `ACSTelemetryPacket` stays only so the old LocalServer still compiles. |
| Box side | `ESP32_S3A.ino`, `ESP32_S3B.ino` | S3A -> ACS 1, S3B -> ACS 2; "stop, it is full" message; quiet re-ask while CLEANING is greyed out / Box 1 is out of order (see below). |

## Pins (both boards, from the E1 sheet) - relays are Active-LOW

| Pump (bit in the mask) | GPIO | | Sensor | Pins |
|---|---|---|---|---|
| 0 ALCOHOL pump (supply -> alcohol mixer) | 18 | | US alcohol mixer | trigger 4 / echo 14 |
| 1 WATER pump ALC (water -> alcohol mixer) | 21 | | US scented mixer | trigger 26 / echo 33 |
| 2 WATER pump SCN (water -> scented mixer) | 23 | | WL alcohol mixer | GPIO34 (analog, input-only) |
| 3 SCENTED pump (supply -> scented mixer) | 19 | | WL scented mixer | GPIO35 (analog, input-only) |
| 4 MIXER ALCOHOL pump (mixer -> box) | 22 | | | |
| 5 MIXER SCENTED pump (mixer -> box) | 25 | | | |

All six pump pins are driven HIGH (off) before they become outputs, so nothing clicks at boot. Only ONE pump ever runs at a time.

## How it behaves

- **Top-up recipe** (per mixer container), started when the liquid has dropped to `topup` (or it is LOW): **water in** until the water share of what still fits is reached (`waterA` 30 % = 70/30; `waterS` 90 % placeholder), then **alcohol (or scented) in** until FULL. Because the share is taken of what still fits, a top-up on top of the old mix keeps the ratio right (the simulation gave 30.6 % water from empty and 31.3 % after a top-up from mid-level).
- **The boss's rule:** a fill pump runs `noRise` seconds (12) and the level does not rise -> **that supply is EMPTY** (or its pump is dead): the pump stops, WATER / ALCOHOL / SCENTED EMPTY is reported, the *other* supplies keep working, and that supply is tried again after `emptyHold` (300 s). A rise counts if the US sees it **or** the WL sees it (on two readings in a row); "no rise" needs both flat. The box then runs out of refill tries and greys CLEANING out (Box 3) / goes out of order (Box 1).
- **A half-made mix is never delivered.** The recipe's progress (0 none, 1 water going in, 2 water done) is kept in flash: an EMPTY alcohol supply leaves "water only" in the container - the box must not get that. An interrupted recipe (empty supply, power loss) resumes at the right pump, without adding the water twice.
- **Delivery:** the box asks (`CMD_REQUEST_DELIVERY`, alcohol / scented, repeated every refill interval while its container is low). The ACS runs the mixer pump for **one shot** (`deliver` = 6 s) or until that mixer container is LOW; the box's "stop" message ends it early. A LOW or half-made container delivers nothing.
- **WL + US:** a WET WL means FULL and wins over the US. A US that says "full" while the WL is dry is still believed for *stopping* a pump (stopping early is the safe side) - this is a deliberate difference from the Designer's "WL wins" wording. A container whose level cannot be read at all (no echo, WL dry) is never pumped into or from automatically; a warning shows.
- **Limits:** a pump never runs longer than `phaseMax` (120 s); after such a timeout all fills pause `holdoff` (60 s).
- **Maintenance** (bench / dashboard / console): automatic work paused, pumps by hand one at a time, each hand-set pump switches itself off after 60 s, maintenance ends by itself after 30 min without a command.
- Reports an `ACSStatusPacket` once a second; settings live in the ACS's flash and are re-broadcast every few seconds (`CMD_SET_CONFIG`, target 0, state 4 = ACS 1 / 5 = ACS 2).

## Settings (USB console `help`, or the bench page) - the defaults are PLACEHOLDERS until the containers are measured

| Setting | Default | Meaning |
|---|---|---|
| `emptyA/S` | 14.0 | sensor -> bottom (cm); only for the % read-out |
| `topupA/S` | 8.0 | refill recipe starts when the distance reaches this |
| `lowA/S` | 11.0 | delivery stops / refill forced (distance) |
| `fullA/S` | 3.3 | FULL line (distance). Smaller distance = fuller. Needs empty > low >= topup > full >= 1 |
| `waterA / waterS` | 30 / 90 % | water share of each top-up (the scented ratio is the boss's call) |
| `norise` / `risecm` / `wlrise` | 12 s / 0.5 cm / 150 | the no-rise detector |
| `phasemax` / `deliver` / `emptyhold` / `reqhold` / `holdoff` | 120 / 6 / 300 / 30 / 60 s | |
| `wlenter` / `wlleave` / `wlhigher` | 600 / 400 / 1 | WL wet / dry thresholds (the bench works them out from a DRY and a WET reading) |

## Bench checklist (do this order)

1. **Flash `ESP32_ACS1` (Box 1) or `ESP32_ACS2` (Box 3) with the pumps' power unplugged.** Serial monitor 115200: "all 6 pumps OFF"; every 5 s a status line with both US distances and WL raws.
2. Flash `ESP32_ACS_CONFIG_BENCH` on the spare ESP32, join `ACS_CONFIG`, pick the ACS. The page should show it online with both containers.
3. **Sensors:** with each mixer container at its real levels use "use live" for `low`, `full`, `topup`, `empty`. **Water-level sensors:** dip in the real liquid (WET) and out (DRY), "Work out the thresholds". *Alcohol conducts poorly - a WL sensor may read much lower in the alcohol mix than in water.* Send.
4. **Pump map (Maintenance):** with water in a supply, run each pump by hand ~3 s (one at a time) and check it moves liquid where the tubing says (pump 0 alcohol -> alcohol mixer, 1 water -> alcohol mixer, 2 water -> scented mixer, 3 scented -> scented mixer, 4 / 5 mixers -> the box).
5. **Automatic run:** leave Maintenance. Use plain water in all three supplies for the first test. Watch the step names and the level. Then lift a supply's tube out of its liquid: the bench must show `... EMPTY` after ~12 s and the other container's recipe must carry on.
6. **Delivery:** "Ask: alcohol" -> one shot from the alcohol mixer; tune `deliver` so one shot does not overfill the box's container.
7. **Link the box:** when ACS 1 / ACS 2 is verified, on S3A / S3B run `set acs 1` (and `checks on` once that box's container lines are measured). Until then the box only counts its tries (ACS requests default OFF).

## Box-side changes made for this

- S3A asks `DEVICE_ACS` (ACS 1), S3B asks `DEVICE_ACS2` (ACS 2) - the two ACS boards can no longer answer each other's box.
- When a container is refilled the box sends the "stop, it is full" message (state 1).
- **Quiet re-ask:** once the box has given up, nothing used to ask the ACS again, so a supply refilled by hand was never used. Now Box 1 (while OUT OF ORDER) and Box 3 (on the TAPS screen, while CLEANING is greyed out) ask once every 30 s for the container that is not refilled, and lift the bypass as soon as both containers read "not low" - no customer needed. (Needs `acs` ON.)

## Not done / open

- **LocalServer** (updated later the same day, see `LOCALSERVER_PLAN.md`): its `/acs` page now does everything the ACS bench does, for ACS 1 and ACS 2, and the Home page shows both. The bench is a spare.
- **Designer** (published v56) still shows ACS 1's pins as "pin TBD" and a fixed WATER LINE; the firmware uses a water *share* and a top-up line (see above). The pin labels are already fixed in my working copy (ACS 1 = the same E1 pins as ACS 2) but NOT published yet; the water-share / top-up wording is not changed.
- The scented ratio (2 % scent cannot be metered by level), the shot size (`deliver`), the WL thresholds in alcohol and every distance are measurements/decisions for the real machine.
- Nothing here has run on hardware; the control logic was run against a software model of the tanks (9 scenarios) and every sketch compiles.
