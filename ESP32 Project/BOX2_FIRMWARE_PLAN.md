# Box 2 (Heater) firmware - new procedure (2026-10-03)

Source flow: `BOX 2 STEP.txt` (the user's live Designer export), frozen as `BOX2_STEPS_2026-10-03_v2.json` (24 steps).
Review notes and decisions: see the memory note "Box 2 new flow review". Box 1's equivalent is `BOX1_FIRMWARE_PLAN.md`.

## What was built

| Node | File | What changed |
|---|---|---|
| **S3C** (sequencer) | `ESP32_S3C/ESP32_S3C.ino` + `S3C_Screens.h` | Full rewrite, same shape as Box 1's S3A. Old flow archived as `ESP32_S3C_LEGACY_old_flow.ino.txt`. |
| **C1** (screen / touch / coins / buzzer) | `ESP32_C1/ESP32_C1.ino` | Rebuilt from the finished Box 1 terminal (A1): rating grid (NR2), numpad (NP), progress bar (PBAR), `{STATUS}` lines, `{MINUTE}/{SECOND}` tokens, coin diagnostics. Only its device ID, upside-down TFT (rotation 3) and touch calibration differ from A1. Old one: `ESP32_C1_LEGACY_pre_widgets.ino.txt`. |
| **C2** (relays + sensors) | `ESP32_C2/ESP32_C2.ino` | Takes ONE `CMD_SET_RELAY_MASK` (6 bits) re-sent every 2 s instead of six per-relay commands; relay pins are driven HIGH before they become outputs; relays that turn ON are staggered 25 ms; the 15 s "S3C is gone" shutdown now switches the UV off too; door changes are reported at once. Old one: `ESP32_C2_LEGACY_pre_mask.ino.txt`. |
| Shared header | `Shared_Common.h` (all folders) | `Box2State` 16-24 added (PLACE_HELMET, CLOSE_DOOR_A, TAKE_HELMET, CLOSE_DOOR_B, FREE_RETRY, MAINTENANCE, HELMET_NOTICE_A/B, OUT_OF_ORDER); `Box2ConfigPacket.coolDownRatio` (float) -> `coolDownSec` (uint32, same 4 bytes so the packet is still 19 bytes); `C2_BIT()/C2_MASK_ALL`. |
| LocalServer | `ESP32_LOCALSRV/*` | Cool-down setting is now a number of SECONDS (default 60) instead of the Full/3-4/half/quarter dropdown; the 9 new Box 2 state names. Nothing else touched (its real Box 2 page is a later job). |
| Box 1 settings bench | `ESP32_S3A_CONFIG_BENCH.ino` | Ignores the lists S3C broadcasts (`state == 2`), so the two boxes' settings never mix. |
| **Box 2 settings bench (new)** | `ESP32_S3C_CONFIG_BENCH/` | Wi-Fi settings page for a SPARE ESP32 (see below). |

## Box 2 settings bench (spare ESP32)

Flash `ESP32_S3C_CONFIG_BENCH` on the spare ESP32 (WROOM or S3). Power Box 2 as usual (S3C, C1, C2 on, the real LocalServer off), join the Wi-Fi **`S3C_CONFIG`** (password `12345678`) and open **http://192.168.4.1**.
It shows S3C's step and timer, C2's helmet distance / doors / which relays are on, and C1's coin counts (pulses counted, raw wire edges, shortest gap); edits **HEAT** (helmet cut-off with a "use live" button, cool-down seconds) and **RULES** (rating bands, minimum, coin rate, maximum with a "= N min, P pesos" read-out, free retry) and says "S3C has these values ✓" once S3C echoes them back; and has a **Maintenance** switch with six hand relays (heater is cut after 5 minutes by S3C itself; the enclosure lock rests after 60 s).
It sends no heartbeat, so it can stay on next to the real box. It only listens to Box 2's lists (`state == 2`), and Box 1's bench (`S3A_CONFIG`) only listens to Box 1's, so both can run at once if you ever have two spare boards.

## Flow as built (S3C)

TAPS -> WELCOME -> RATE A -> TIME ALLOT -> INSERT COIN -> OPEN A -> SENSORS A -> CLOSE DOOR A -> **HEATING** (Heater + UV + Fan) ->
**COOL DOWN** (Fan only, `cooldown` seconds, default 60) -> OPEN B -> SENSORS B -> CLOSE DOOR B -> RATE B -> (FREE RETRY ->
OPEN A -> ... HEATING FREE, 2 min) -> FINISH (5 s) -> TAPS.

- ABORT (button on the heating screen) -> confirm -> **COOL DOWN**, so a hot helmet gets the fan first. The cool-down screen has no abort.
- A free retry is offered after RATE B to every rating (rule `freeMinRating` = 1, a deployment-demo decision); the free heating is not phased, takes no coins and does not count as a session.
- Door opened during HEATING or COOL DOWN (2 s) -> SAFETY PAUSE, everything off; resumes when the door is shut with the helmet in. If the helmet is already out during the cool down, the rest of it is skipped (-> CLOSE DOOR B).
- Door/helmet gaps of the export are covered the same way as on Box 1 (a helmet placed and the door shut quickly, a helmet taken before the door opened, ...).
- Enclosure lock: never energized more than 60 s in a row (5 s rest) - the solenoid fire-risk rule.
- Coins: Box 2 has no coin-power relay, so the acceptor is always live; only INSERT COIN and a PAID heating (up to the maximum) credit coins, a coin at any other moment is ignored and logged.
- OUT OF ORDER: C1 draws its own when S3C is silent for 15 s (unchanged); S3C also shows one when C2 is not answering at the TAPS screen.
- C2 silent while a cycle runs: S3C assumes door OPEN / no helmet, so a heating pauses with heater, UV and fan off.

## Settings (NVS in S3C; defaults = the Designer's Box 2 rules)

Bands 1-3 = 3 min, 4-6 = 5, 7-8 = 8, 9-10 = 10 | minimum 3 min | 10 coins = 3 min (18 s per coin) | maximum 100 coins (30 min) | free retry
from rating 1 for 2 min | helmet detected under 30 cm | **cool down 60 s**.
Change from the USB console (115200): `help`, `cfg`, `reset`, `set cooldown 90`, `set maxcoins 100`, `set freerating 7`, `set band1 1 3 3` ...
The dashboard path is `CMD_SET_CONFIG` to S3C: subIndex 0 = old line `helmCm,secPerCoin,minCoins,maxCoins,coolDownSec`, 1 = RULES (20 numbers, same
as S3A), 2 = HEAT (`helmCm,coolDownSec`). S3C re-broadcasts RULES and HEAT every 2 s (target 0, `state = 2`).

## Flash order / bench checklist

1. **C2 first** (relay board): with nothing connected to the loads, the serial monitor should show the six relays HIGH at boot and no click.
2. **C1**: shows the OUT OF ORDER blink until S3C is up.
3. **S3C**: type `cfg` - rules and `cool down 60 s` should print; C1 goes to TAPS.
4. Walk the whole flow once WITHOUT heating loads connected (or the heater fuse out): rating -> time -> coins -> door -> helmet -> heating (check the three relays in the C2 log) -> cool down -> retrieve -> rating B -> free retry -> finish.
5. Then with the loads: heating for the minimum 3 minutes, abort once (cool down must follow), open the door during heating (pause), pull S3C's power during a heating (C2 must switch heater/UV/fan off after 15 s).

## Not done / known

- LocalServer's Box 2 page (coin economy fields still show S3C's old-style numbers; rules/heat lists are only editable from the console until its new page exists).
- Ratings and session extras (not in the stats packet yet, same as Box 1).
- C2's UV relay is on GPIO5, which pulses at power-up (Box 1's A3 moved its UV to GPIO27 for this reason): the UV relay may click once at boot. A pin change would be a wiring change.
- A safety pause waits for the door forever (same as Box 1).
