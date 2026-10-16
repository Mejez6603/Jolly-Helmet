ESP32_RESTATE - price recalibration board (2026-10-11)
=======================================================
Purpose: keep the OLD data (collected under the old price list: 5 min = 30 or 45 coins) and restate it at the NEW price list
(Box 1 and Box 3: 15 coins = 5 min, Box 2: 10 coins = 3 min) for the Analytics Document Team. Nothing is reset or overwritten.

What it is: a SPARE ESP32 (any ESP32 or ESP32-S3). It LISTENS to the LocalServer's ESP-NOW totals (the same 10-second broadcast the
Online Server uses). It sends nothing until you press Update (step 7); the boxes are never touched. The LocalServer and the Online Server need the 2026-10-11 firmware only to SHOW the restated figures.
Delete it afterwards: nothing depends on it.

Flash:  open ESP32_RESTATE.ino in the Arduino IDE, board "ESP32 Dev Module" (or your S3), Upload. Libraries: ESP Async WebServer + Async TCP.

Use (in this order):
 1. Set the NEW rates on all three boxes (Control Center /box1 /box2 /box3 > Rules: 15 coins = 5 min, 10 coins = 3 min, 15 coins = 5 min).
 2. Power the spare board close to the LocalServer. Join Wi-Fi  JollyHelmet-Restate  (password 12345678).
    Open  http://192.168.4.1  in a normal browser (Chrome/Safari, not the "sign in to network" pop-up).
 3. The top pill must say "LocalServer heard ... s ago". If it does not: the LocalServer is off, or it was not flashed with the 2026-10-09 firmware.
 4. Check "Price lists" (defaults: new 15/5, 10/3, 15/5; old 30/5, 30/5, 45/5). The old list is only a check. Set the "share of minutes" if the three
    boxes served very different minutes. Save.
 5. Press "Capture the old data now". The totals are copied into this board's flash (survives power-off). It cannot be overwritten until "Clear the capture".
 6. "Result" shows, side by side: as collected / restated at the new list, for the old data, for what is sold since the capture, and the total.
    "Download report (CSV)" gives the same for the document. The CSV lists the method and the assumptions.

Method: restated coins = minutes served x new coins per second (blended by the share of minutes per box). Customers and minutes do not depend on the
price. The coins really collected are never changed and are always shown beside the restated figure. Coins that the minutes served cannot explain
(test/bench pulses, aborted runs, refunds, free-retry minutes) stay in "as collected" only. Write the restated figure as "restated", never as
"revenue collected".

Limits: the LocalServer keeps only totals and the last 10 sessions (no record per sale or per box), so the restatement is an estimate by design.
If the LocalServer's statistics are reset after the capture, the page warns and the "since the capture" figures are not valid; the captured copy stays.
The capture time comes from the clock of the phone/PC that pressed Capture (this board has no clock).

UPDATE BUTTON (2026-10-11) - show the restated figures on the LocalServer and on the Online page
 7. After the capture press "Update now" (card 3). The board broadcasts one small packet (Restate_Common.h, 32 bytes) on the machine radio: five
    quick repeats, then every 10 s while it stays powered. The LocalServer and the Online Server store it in their own flash, so the spare board can be unplugged afterwards.
      - LocalServer Control Center, Home page: a new block "Restated at the new price list" under Statistics & revenue (as collected | restated, for the old data, since the capture, total).
        The statistics above it are NOT changed. "Remove" on that block forgets the restated figures.
      - Online page, Monitor tab: two extra tiles (restated revenue, restated coins, each with the collected figure under it) and one line saying what was restated.
      - Online Server setup page shows "Restated old data: stored: update #N".
    Press Update again after changing the price lists (the page says when they changed). "Withdraw" (or clearing the capture) makes both forget it.
    REFLASH NEEDED for this: ESP32_LOCALSRV and ESP32_ONLINESRV (both now include Restate_Common.h) and this ESP32_RESTATE board. The boxes and ACS boards: no.
    The restated figures keep following new sales by themselves: restated coins = old restated coins + coins sold since the capture (from each receiver's own totals).
