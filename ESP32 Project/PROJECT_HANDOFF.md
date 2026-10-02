# Project Handoff — ESP32 Helmet Vending Machine

Last updated: 2026-09-17. Written so a fresh Claude conversation (any machine) can pick up
exactly where this one left off. If you're Claude reading this cold: read this whole file
before touching any code, then ask the user what they want to work on next rather than
assuming.

## What this project is

A coin-operated helmet cleaning/drying vending system built on ESP32 boards talking over
ESP-NOW (no Wi-Fi router, no internet dependency). Team project — the user builds/debugs/
deploys the firmware and dashboard; a friend/teammate handles physical signage.

**Nodes:**
- `Master` / `ESP32_LOCALSRV` — the dashboard ("ESP32 CONTROL CENTER" web UI over WebSocket).
- `ESP32_S3A` + `ESP32_A1` + `ESP32_A2` — **Box 1**, the Helmet Cleaner (alcohol mist + UV).
  S3A owns and runs Box 1's entire state machine now; A1 is the coin/TFT/touch terminal; A2 is
  the relay/sensor actuator hub.
- `ESP32_S3C` + `ESP32_C1` + `ESP32_C2` — **Box 2**, the Helmet Dryer/Heater. Same split as
  Box 1: S3C runs the state machine, C1 is the terminal, C2 is the actuator hub (Heater + Fan
  instead of Humidifier, plus a 6th relay for the Fan that Box 1 doesn't have).
- `ESP32_ACS` (device D1) — Alcohol Container System: mixes and delivers disinfectant to Box 1
  on request. Untouched this arc.
- **Box 3** — a straight hardware/firmware clone of Box 1 (team decision). Finishing Box 1
  finishes Box 3 automatically.

All 8 node folders share one `Shared_Common.h` (identical byte-for-byte, checksum-verified
after every change — see "How to keep changing things" below).

## Current status by box

### Box 1 — Cleaner: Phase 1 shipped, flashed, confirmed working ✅
The customer has the unit now for real-world field testing. 16-step flow: Taps → Welcome →
Rate A → Time Allot → Insert Coin → Checking → Refilling → Instructions → Sensors → Cleaning →
Rate B → Retrieve → Finish (loops to Taps), plus Abort Confirm / Cancel Confirm / an unwired
Confirm placeholder.

Files: `ESP32_S3A/ESP32_S3A.ino`, `ESP32_A1/ESP32_A1.ino`, `Shared_Common.h` (all 8 copies),
`ESP32_LOCALSRV/{ESP32_LOCALSRV.ino,index_html.h,script_js.h}`. `ESP32_A2.ino` needed zero
changes.

**Not started:** Phase 2 (real tap-a-box rating grid + numeric keypad, replacing the current
-/+/OK button-row stand-ins) and Phase 3 (send the two ratings to LocalServer for dashboard
display). Both are deliberately deferred — don't start either without the user asking.

### Box 2 — Dryer: Phase 1 implemented, **not yet compiled/flashed/tested** ⚠️
Mirrors Box 1's Phase 1 almost line-for-line. 15-step flow: Taps → Welcome → Rate A (moisture)
→ Time Allot → Insert Coin → Instructions → Sensors → Heating → **Cool Down** → Rate B →
Retrieve → Finish (loops to Taps), plus Abort Confirm (reachable from Heating *or* Cool Down)
/ Cancel Confirm / an unwired Confirm placeholder.

**Genuinely new vs. Box 1 — Cool Down**: a Fan-only phase (Heater off) after Heating, so a
heated helmet doesn't come back too hot to wear. Its length is `coolDownRatio` × however long
Heating actually ran (`requiredCoinsForSession * secondsPerCoin`) — **not** a fixed value. This
went through two corrections this session:
1. First cut used a fixed 20s cooldown — wrong, because a 20-coin/10-minute Heating session
   needs proportionally longer cooling than a 1-coin/30-second one.
2. Then made it a straight 1:1 ratio of however long Heating ran — also not quite right, because
   the user wants to experiment with *shorter* cooldowns once real hardware shows the helmet is
   already cool enough, without making customers wait the full Heating time every time.

Landed on: `coolDownRatio` is its own **independent, dashboard-configurable knob**
(`Box2ConfigPacket.coolDownRatio`, NVS-persisted on S3C as `cd_ratio`), constrained on the
dashboard to 4 discrete choices — **Full (1.0)**, **Three Quarter (0.75)**, **Half (0.5)**,
**Quarter (0.25)** — via a `<select>` on the Dynamic Settings tab (`cfg-cooldown-ratio-c2`),
next to Box 2's existing Min/Max Coins fields. Default is Full (1.0), so behavior doesn't shift
until someone deliberately dials it down after seeing real hardware.

Files touched: `ESP32_S3C/ESP32_S3C.ino` (full rewrite), `ESP32_C1/ESP32_C1.ino` (ported A1's
real anchor/font-size support + any-tap-advances flag), `Shared_Common.h` (all 8 copies —
`Box2State` enum extended 8→15, `Box2ConfigPacket.coolDownRatio` added, packet grew 15→19
bytes), `ESP32_LOCALSRV/{ESP32_LOCALSRV.ino,index_html.h,script_js.h}` (state names, the
cooldown-ratio dropdown, save/render wiring). `ESP32_C2.ino` needed zero changes.

**To do before anything else on Box 2:** compile via Arduino IDE, reflash **S3C + C1** (C2
does NOT need reflashing), walk the full 15-step cycle once, and specifically check that Cool
Down actually feels proportional at a couple of different Heating durations.

**Judgment calls made without re-confirming — flag the user if any read wrong on hardware:**
- Rate A/B and Time Allot use plain -/+/OK button rows, not a real tap-grid or numpad (neither
  exists in real firmware yet — see [[esp32_firmware_vs_tool_gap]] equivalent notes in memory).
- The progress bar (PBAR) shown in the user's design for Heating/Cool Down isn't implemented —
  not a real firmware tag yet, deferred alongside the widgets above.
- FINISH's countdown was bumped from a previously-flashed 2s to 4s to match the new design.
- Abort/Cancel Confirm screens were reskinned to the new wording; Instructions/Sensors/Heating/
  Retrieve/Finish were deliberately left as-is (already reasonable, not "barebones").

## Architecture things worth knowing before you touch this again

- **ESP-NOW packets have no type tag.** `onDataRecv()` on every node dispatches purely by raw
  byte length (`sizeof()`). Every struct in `Shared_Common.h` must stay a pairwise-distinct
  size or packets silently get memcpy'd into the wrong struct. Current sizes: `CommandPacket`
  206, `TelemetryPacket` 31, `ACSTelemetryPacket` 30, `Box1ConfigPacket` 23, `Box2ConfigPacket`
  19, `BoxStatusPacket` 17, `StepAction` 223. Check this table before adding/growing any struct.
- **"Reuse a free scalar field" convention**: rather than adding new packet fields for every
  small value, this codebase piggybacks values on otherwise-unused fields of an existing command
  (e.g. `{MINCOINS}` rides `CMD_SYNC_VARS.subIndex`, `{REQUIREDCOIN}` rides `CMD_SYNC_VARS.state`
  on both boxes, the any-tap-advances flag rides `CMD_STEP_RENDER.state`). Look for this pattern
  before assuming a new command/struct is needed.
- **The Box Cycle Designer tool** (browser-only HTML/JS design tool, no real firmware
  connection — https://claude.ai/code/artifact/b96aae30-b511-4233-9019-785a165c95e6) is a
  *design sketchpad*, not source of truth. Its "Copy as Arduino" export uses a `StepAction[]`/
  `MAX_STEPS` model that is dead code in the real firmware (S3A/S3C are hand-written `switch`
  statements over `MachineState`/`Box2State`). Treat its output as content reference (screen
  text, relay masks) only, never paste it in literally.
- **`char payloadStr[200]`** on `CommandPacket` is a hard cap. Every new screen string has been
  manually byte-counted against this. If you add a new screen, count it again (`${#str}` in
  bash is the quick way) before assuming it fits.
- Both boxes' coin economy (seconds/coin, min/max coins, and now Box 2's cooldown ratio) is
  fully dashboard-configurable and actively being tuned — don't hardcode current values
  anywhere that isn't easy to update (this already burned once: the printed panel cards
  deliberately omit coin/time numbers for this exact reason).

## Other live things

- **Panel/sticker signage** — https://claude.ai/code/artifact/d22318e3-323c-43ca-bda9-efbf06811cef
  ("Jolly Helmet Panel Cards"), print-ready 5×8" instruction plates for both boxes, derived from
  the flows above. **Goes stale the same way** if the flow changes again — re-derive its 7-step
  summary from the real firmware before reprinting, same as it was built.
- **Coin session reset bug, Offline recovery spec, Node death failure mode** — older, still-open
  items from earlier work, not touched this arc. Ask the user before assuming any of these are
  still relevant/current.
- No git repository in the Arduino project folder — these `.ino`/`.h` files are the only copies.
  If you're reading this after a USB/cloud transfer to a new machine, double check the files
  actually came across intact (especially `Shared_Common.h` — it must still match byte-for-byte
  across all 8 node folders; `md5sum` them and compare) before making further changes.

## What's actually next

1. User compiles + reflashes Box 2 (S3C + C1) and walks the 15-step cycle on real hardware.
2. Based on what that surfaces: either fix whatever's wrong, or move on to Phase 2 (real
   Numpad/Rating widgets) for whichever box the user wants first — don't assume which.
3. Panel cards may need another revision once Phase 2 lands (the button-row stand-ins would be
   replaced by real widgets, which doesn't change what the customer sees on the sticker, but
   worth re-checking).
