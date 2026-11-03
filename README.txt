JOLLY HELMET ONLINE MONITOR
===========================
A read-only web page. It shows what the Online Server (the ESP32-S3 in ESP32_ONLINESRV) puts into Blynk. It has no button that can change
or start anything.

FILES (open index.html; the rest is split so each part is easy to read)
  index.html        the markup (what is on the page)
  css/style.css     the look (colors are variables at the top, light and dark)
  img/logo.png      the logo
  js/config.js      token, server, refresh time, currency  <- the file you may want to edit
  js/util.js        small helpers
  js/names.js       names of the machine steps, boards, relays (same order as the firmware numbers them)
  js/api.js         reads Blynk (HTTPS GET only)
  js/render.js      draws the cards, alerts, charts
  js/demo.js        made-up data for the demo
  js/app.js         refresh loop, status badge, settings window, light/dark
  js/tabs.js, js/alerts.js, js/sound.js, js/gauges.js, js/reports.js, js/qr.js, js/share.js, js/status.js   the tabs and the customer page (see below)
  status.html, css/status.css   the customer page
  DATA_CONTRACT.md  exactly what the Online Server writes to Blynk

TRY IT NOW (no hardware, no Blynk)
  Open index.html and add  ?demo  to the address:  index.html?demo   (or  ?demo=alerts  to see the warnings)

USE IT FOR REAL
  1. Blynk Console: template with datastream V0 (String), a device, copy its Auth Token (DATA_CONTRACT.md, section 1).
  2. Flash ESP32_ONLINESRV on the ESP32-S3, join its Wi-Fi "JollyHelmet-Online" (password 12345678), pick the Wi-Fi and paste the token there.
  3. Put this folder on any web host (GitHub Pages, Netlify, Cloudflare Pages, a school server) - or just open index.html from your PC.
  4. Open it, paste the same token once (stays in that browser). Or open it once as  index.html#token=THE_TOKEN  on a phone.

KEEP IN MIND
  * A Blynk token can also WRITE. The page never does, and the firmware ignores writes, but do not put the token inside js/config.js on a
    public web site. Leave CONFIG.token empty (the default) so each viewer types it.
  * The page reads Blynk every 10 seconds only while its tab is open and visible (Settings can change that).
  * "LIVE / DELAYED / OLD DATA" and "updated x ago" come from the numbers inside the data, not from Blynk's own online/offline label
    (the board uses Blynk's HTTPS API, so Blynk Console may show the device as offline; that is normal).
  * The analytics (customers, revenue, last cycles) need the LocalServer to be on, flashed with the version that broadcasts its totals.

REMOVED ON 2026-10-23: the Checklist, Fix it and Manual tabs, and the "Fix it" links on the alerts (and with them the "maintenance due" alert).
  Their code is kept in ..\ONLINE_WEB_REMOVED (not part of this folder, not uploaded). Old links such as index.html#fix just open the Monitor.
  The Word manual and its PDF are made in ..\MANUAL as before; they are no longer put on the web page.

THE TABS  (Monitor, Alerts, Reports; Checklist, Fix it and Manual were taken out on 2026-10-23)
  Monitor    live machines (each box is DRAWN as a little machine: the touch screen with the step and time left, the coin slot and coin count, the helmet window with
             the UV tubes, mist nozzles 1 to 3, heater and fan lighting up when their relay is ON, the doors Enclosure / Panel / Back / Side open or closed with a padlock
             for each lock relay, and the main container beside it with its level; grey dashes = not known because that board is offline) + LEVEL GAUGES
             (3 dials, centred: the main container of Box 1, of Box 2 and of Box 3, read by the Refilling Station: red = EMPTY or LOW (the box offers drying only), yellow = MID, yellow to green = HIGH, green = MAX;
             a valve symbol above each container lights blue while the station is refilling it. Firmware without the station shows the old percent dials with a "falling,
             about 5 h to the refill line" estimate from the readings this browser has seen). Set the early warning line in the Alerts tab (default 30 %).
             Alerts also warn when the Refilling Station is offline (every box then offers drying only) and when a refill failed.
             Further down: the NETWORK MAP (a drawing like the Online Server's own setup page): Internet/Blynk - Wi-Fi at the site - Online Server, then Box 1, Box 2, Box 3, the Refilling Station
             and the LocalServer, each with a coloured dot (green = all heard, amber = some boards missing with their names, red = broken, grey = not known) and the Online Server's own figures.
             Boards have plain names on this page (TFT Display, Sensors, Relay Controller, Main Controller); the codes A1, B1, C1, A2, B2, A3, B3, C2, S3A, S3B, S3C
             are only used in the firmware, the Control Center and the setup page.  Alerts     what needs attention now, an alert LOG (kept in the browser, CSV download) and SOUNDS: every kind of alert has its own sound that you can change,
             test (the play button) or switch off. Critical alerts (box OUT OF ORDER, no board heard) repeat every minute until somebody
             presses Acknowledge. Also: a number in the tab title and the tab icon, and optional browser notifications.
             Sounds: siren (out of order), alarm (nothing heard), urgent (door opened during a run), three beeps (container low),
             two tones (board offline, cleaning off), falling tones (data old / no link), soft blip (early warning), chime (cleared), ding (customer finished, off by default).
  Reports    revenue and customers per day with a 7-day average, this week against last week, busiest hours, busiest weekdays, what each box earns, a day-by-day
             table, CSV downloads and "Print / save as PDF" (a clean report page).
  QR button  the QR code, link and a printable sign for the customer page.

SOUNDS AND NOTIFICATIONS: WHAT TO KNOW
  * Browsers keep a page silent until you have clicked or tapped on it once. The page shows "Turn on alert sounds" until you do. After that it can make noise
    as long as it stays open (a background tab is fine; a locked phone or a closed tab is not).
  * Alerts show up about one upload interval after they happen (the Online Server uploads every 30 s to 5 min).
  * ALERTS WHEN THE PAGE IS CLOSED: use the Blynk app. In the Online Server's setup page tick "Send a Blynk event with code alert", and in Blynk Console create the event
    with that code and switch on its push notification. The Blynk app on a phone then rings even when this page is closed. (This page cannot do that: a static page has no server.)
  * Tab icon / title numbers and notifications work only while the page is open somewhere.

THE CUSTOMER PAGE  (status.html)
  A public page for customers: Box 1, Box 2, Box 3 as FREE / IN USE (about N min left) / OUT OF ORDER, English and Filipino, with the prices and how to use it.
  It needs a SECOND Blynk device (the private token can also write, so it must not be published):
    1. Blynk Console > Devices > New device from the same template. Copy its Auth Token (this one may be public).
    2. Online Server setup page > "Public status device token" > paste > Save (needs the firmware of 2026-10-10, flash ESP32_ONLINESRV again).
    3. js/config.js: publicToken: 'THE_PUBLIC_TOKEN'  (or use the QR button: it puts #t=TOKEN into the address).
    4. Monitor > QR button: print the sign and stick it on each machine. The QR holds only the address of status.html (and the public token).
  Edit the prices and the "something wrong?" line in js/config.js. Try it without hardware: status.html?demo   (and ?demo=cycle).
  The Filipino texts are in js/status.js: please have a native speaker check them.
  OPTIONAL CUSTOMER NAMES (2026-10-14): while a machine is running a customer's cycle, the customer can tap "Add your name" on its card and type a name or nickname.
  Everybody sees "Maria's helmet" on that card; the customer's own phone shows "your helmet is done" at the end, and the name clears by itself when the cycle ends.
  It needs (1) one more String datastream, V5, in the Blynk template of the public device (DATA_CONTRACT.md, section 5b) and (2) for the exact "clears when the cycle
  ends" the Online Server firmware of 2026-10-14 (it adds each box's session counter to the public value; without it a name clears when the box leaves the running steps,
  or after 45 minutes). Without V5 the page works as before and says "names are not switched on yet" when a customer tries. js/config.js: names (on/off), namesPin.
  This is the only thing the customer page writes (into that one datastream, with the public token).

FIRMWARE OF 2026-10-10 (reflash ESP32_ONLINESRV): it now also counts every finished session per box and per hour (Reports: busiest hours/days, what each box earns;
  reset button in its setup page, "More settings") and writes the public status device. Without it the Reports tab shows "Needs the Online Server firmware of 2026-10-10"
  for those three charts; everything else works with the old firmware.

TRY THE SOUNDS WITHOUT HARDWARE:  index.html?demo=cycle#alerts  - alerts come and go every 2 minutes (a box goes out of order, a container runs low,
  a board drops off, a door is opened); press the play buttons in the Alerts tab to hear each sound.
