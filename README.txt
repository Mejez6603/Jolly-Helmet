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
