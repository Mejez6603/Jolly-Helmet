# Online Server (2026-10-09)

A new ESP32-S3 (`ESP32_ONLINESRV`) that listens to the machines and puts a snapshot into Blynk. The read-only page `ONLINE_WEB/index.html` shows it.
**Compiled only (35 % of the 3 MB app slot) - never run on a board yet.**

## What changed
| Where | Change |
|---|---|
| `Shared_Common.h` (all folders, identical again, 20 copies; COMMTEST untouched as before) | New `HubStatsPacket` (58 bytes, unique size, static_assert added). Nothing else changed, other nodes ignore the unknown size. S3A and A1 were recompiled to check. |
| `ESP32_LOCALSRV.ino` | `broadcastHubStats()`: every 10 s it broadcasts its customers / coins / duration totals, the coin value and the last 10 sessions. **Reflash the LocalServer.** No other node needs a reflash. |
| `ESP32_ONLINESRV/` (new) | `ESP32_ONLINESRV.ino` (Wi-Fi portal, burst upload, Blynk calls), `online_hub.h` (radio listener, alerts, JSON), `portal_page.h` (setup page), `isrg_root.h` (certificate). |
| `ONLINE_WEB/` | The monitor page (split into html / css / js), `DATA_CONTRACT.md`, `README.txt`. |

## How it works
* Listens (receive only) on ESP-NOW channel 1 like the LocalServer, keeps the latest packet of every kind.
* Own Wi-Fi `JollyHelmet-Online` / `12345678` with a captive portal: choose ANY Wi-Fi, paste the Blynk token, see at once whether it is connected
  (Wi-Fi joined? Blynk write OK? how long ago? why not?). Everything else (machines heard, LocalServer totals heard, clock, next upload) is shown too.
* Every `iv` seconds (default 60) it takes the snapshot, joins the user's Wi-Fi (the radio leaves channel 1 for a few seconds), syncs the clock (NTP),
  writes V0 with Blynk's HTTPS API (TLS checked against ISRG Root X1), optionally V1-V4 and an `alert` event, then returns to channel 1.
  Bursts are held back while somebody is using the setup page (90 s after the last click), so the page does not drop while you type.
* Revenue per day (14 days) is counted from the change of the LocalServer totals, per local day, saved in NVS.

## Bench test (do in this order)
1. Flash the LocalServer (new) and the Online Server. Serial 115200 on the Online Server shows `[ONLINE] ...` lines.
2. Phone: join `JollyHelmet-Online`, the setup page opens. Badge says NOT SET UP. "Machines heard" should show 13 of 13 after a few seconds with all boards on.
3. Search for networks, pick one, enter the password and the Blynk token, Save. The page drops for a few seconds (normal), comes back, shows TESTING then ONLINE or the reason.
4. **Most important check**: after the first burst the "machines heard" count must come back to 13 of 13 within about 10 seconds (the radio returned to channel 1).
   If it stays low, tell me the serial log: the return-to-channel-1 step needs another approach.
5. In Blynk the V0 value is a JSON text. Open `ONLINE_WEB/index.html`, paste the token: LIVE badge, the cards match the real machines.
6. Revenue totals appear after the LocalServer has been heard; the 14-day chart and "today" appear once the clock is set (first successful upload).
7. Switch on "single numbers" and "event" in the setup page only after you made V1-V4 and the `alert` event in the Blynk template (otherwise Blynk answers an error, shown in the status).

## Not verified yet
* Everything on real hardware: channel return after a burst, TLS to blynk.cloud (certificate check, `setCACert`), NTP, the captive-portal pop-up on Android / iPhone, scan during AP+STA.
* The state numbers in the page are the ones in the Control Center script; a new firmware step needs its name added in `ONLINE_WEB/js/names.js`.
* Blynk plan limits (the user has a paid plan; the monthly message limit can be typed into the setup page to get the quota bar).
* No OTA for this board yet (it does not use the Blynk library, so Blynk.Air OTA does not apply).
