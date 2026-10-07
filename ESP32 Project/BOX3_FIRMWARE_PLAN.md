# Box 3 (Cleaning + Drying) firmware (2026-10-03)

Source flow: the user's `BOX 3 STEP.txt` (54 steps) with the agreed changes, saved as `BOX3_STEPS_2026-10-03_v3_proposal.json` (55 steps).
Review notes: memory "Box 3 flow draft". Same four-node split as Box 1 (`BOX1_FIRMWARE_PLAN.md`), one more relay (the heater).

## What was built

| Node | File | What it is |
|---|---|---|
| **B1** (screen / touch / coins / buzzer) | `ESP32_B1/ESP32_B1.ino` | The finished Box 1 terminal (A1) under its own device ID, talking to S3B. **Touch calibration and rotation are A1's** - Box 3's screen has not been calibrated (see the header of the file). |
| **B2** (sensors only) | `ESP32_B2/ESP32_B2.ino` | Box 1's sensing board (A2) under device ID B2, without the legacy bridge. Same pins: helmet US 25/26, MC alcohol US 33/32, MC scented US 22/23, doors 27/14/19/18, analog water sensors 34/35. |
| **B3** (relays) | `ESP32_B3/ESP32_B3.ino` | Box 1's relay controller (A3) + the **HEATER on GPIO26** = 13 channels; one `CMD_SET_RELAY_MASK`, boot-safe pins, 25 ms ON stagger (heater always last), 15 s "S3B is gone" shutdown (heater included) with the trapped-helmet release. |
| **S3B** (brain) | `ESP32_S3B/ESP32_S3B.ino` + `S3B_Screens.h` | Box 1's S3A extended with the mode choice, the DRYING procedure, the heater phases and the new rating/free-retry rules. 42 screens generated from the Box 3 flow. |
| Shared header | `Shared_Common.h` (all folders) | `DEVICE_B1..B3 = 9..11`, `DEVICE_S3B = 12`, `MachineState` 29-33 (SELECT_MODE, SELECT_DRY_ONLY, DRYING, COOL_DOWN, RATE_B1), `B3_RELAY_COUNT/HEATER/MASK_ALL`. |

## Flow as built (S3B)

TAPS -> CHECKING A / S (+ REFILLING) -> WELCOME -> **SELECT MODE** (CLEANING / DRYING) -> ...

**CLEANING:** RATE A (odor) -> METHOD (mist) -> TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> CLEANING -> OPEN B -> SENSORS B -> CLOSE DOOR B -> **RATE B1 (dryness)** -> **RATE B (odor)** -> (FREE RETRY) -> FINISH.
**DRYING:** RATE A (dryness) -> TIME ALLOT -> INSERT COIN -> OPEN A -> ... -> **DRYING** (Heater + UV 1 + UV 2) -> **COOL DOWN** (nothing runs, 60 s, no fan on Box 3) -> OPEN B -> ... -> RATE B (dryness) -> (FREE RETRY) -> FINISH.

- **Containers low:** when a container is low and the refill gave up, the box is NOT out of order: WELCOME opens **SELECT DRY ONLY** (CLEANING drawn as a grey outline, not pressable) and DRYING works. A later passing check clears it by itself. With `checks` OFF (the default until the containers are measured) cleaning is always available.
- **Heater in a cleaning:** only in the END part (default: the last minute), together with the UV; the humidifiers are off by then. If phasing is switched off for a cleaning the heater stays OFF - it never runs together with the mist.
  - PAID: UV the first and last minute, humidifiers in the middle, heater + UV the last minute.
  - FREE (2 min): humidifiers the first minute, heater the last minute, UV the whole time.
  - Paid and free have their own part lengths and relay patterns (6 relays each incl. the heater). Coins added mid-clean keep the END part "the last minute"; a safety pause freezes the clock.
- **Free retry:** offered once per session when EITHER rating in the free range qualifies (after a cleaning the odor OR the dryness; after a drying the dryness). With the default "from rating 1" everybody qualifies. The retry is another cleaning (or another drying after a drying), 2 minutes, not phased differently than the settings say, no coins.
- **ABORT:** during a cleaning -> OPEN B; during a drying -> COOL DOWN first.
- **Safety pause:** door opened for 2 s during a cleaning or drying -> everything off; resumes when the door is shut with the helmet in. Enclosure lock never energized more than 60 s in a row (5 s rest).
- **Coins:** acceptor power only in INSERT COIN and a PAID cleaning / drying (up to the maximum), like Box 1. A stray coin elsewhere is ignored and logged.
- **Maintenance:** hand-set relays from a dashboard (13 bits); a hand-set heater switches itself off after 5 minutes.
- **OUT OF ORDER:** only the "a node is not answering" case (B2 or B3 silent at the start); B1 draws its own when S3B is silent for 15 s.

## Settings (NVS in S3B; defaults = the Box 3 export + a copy of Box 1's price rules)

Rules: bands 1-5 = 5 min, 6-7 = 10, 8-9 = 15, 10 = 20; minimum 5 min; 15 coins = 5 min; maximum 90 coins (30 min); free retry from rating 1 for 2 min. Levels: checks OFF, helmet < 15 cm. Drying: cool down 60 s.
USB console (115200): `help`, `cfg`, `reset`, `bypass 0/1`, `set cooldown 90`, `set freerating 7`, `set pstart 1 min`, `set ppat 2 2 2 5 5 4` (hum1 hum2 hum3 uv1 uv2 heater; 1 start, 2 middle, 4 end)...
Over the air: `CMD_SET_CONFIG` to S3B (subIndex 1 RULES, 2 LEVELS, 3 PHASES = 22 numbers, 4 DRYING = coolDownSec); S3B re-broadcasts them every 2 s with target 0 / `state = 3` (Box 1 sends 0, Box 2 sends 2 - each settings page only listens to its own).

## Flash order / bench checklist

1. **B3** first, nothing connected: the serial monitor shows 13 relays HIGH at boot and no click.
2. **B2** (check the readings on its serial monitor; calibrate the two water sensors with the real liquids later).
3. **B1** - then check touch: if a tap lands in the wrong place or the picture is upside down, calibrate (header of the file).
4. **S3B**: `cfg` prints the settings; B1 shows TAPS. Walk both procedures once with the loads (heater, UV) unplugged; watch the B3 log for the relay changes.
5. Then with the loads: a short CLEANING (check the order UV -> mist -> UV + heater), a DRYING, an abort in each, a door opening mid-run, and pulling S3B's power mid-run (B3 must switch everything off, heater first, after 15 s).

## Not done / open

- ~~A settings page for Box 3~~ and ~~LocalServer support~~: DONE 2026-10-03 - the LocalServer's `/box3` page edits LEVELS, RULES, PHASES (paid + free, heater), DRYING and runs the 13-relay maintenance; Home shows Box 3 and counts its revenue (see `LOCALSERVER_PLAN.md`). The USB console still works too.
- ACS requests are OFF (`set acs 1` when the ACS is finished); the container lines and water-sensor numbers must be measured before turning `checks` on.
- Ratings and the session mode are logged to Serial only (not in the stats packet).
- "One minute of heat" is the user's boss's call - the part lengths are settings.
- If only the dryness rating qualifies for the free retry, the retry is still a cleaning (the Designer export has no other path); a free DRYING in that case is a possible improvement.
- B1's touch calibration; B3's pins as per the pin sheet (heater GPIO26) - the 12 shared channels use A3's re-wired pins (UV 1 = 27, drain pump 2 = 32).
