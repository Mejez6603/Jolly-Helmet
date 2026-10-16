ESP32_ENTRY - CUSTOMER HISTORY ENTRY  (2026-10-15)

A spare ESP32 with a simple form, so the team can put the previous customers (collected from the team's own records) on the LocalServer without knowing any code.
The page builds the history.json file by itself.

USE
  1. Power the spare board and the LocalServer.
  2. Join the Wi-Fi  JollyHelmet-Entry  (password 12345678). THE PAGE OPENS BY ITSELF (captive portal, like the LocalServer's Wi-Fi): the phone says "sign in to this
     network" / opens a small window with the entry page. If it does not, or if that small window cannot pick a file or keep a draft, open the normal browser and go to  http://192.168.5.1   (works on a phone; this board's own network is 192.168.5.x, the LocalServer is 192.168.4.1)
  3. ONE DAY AT A TIME: pick the date (date picker, or the arrows / Today / the day buttons that show every day that already has customers and how many),
     then press "Add a customer on this day". Each day has its own short list and its own count; a day with no customers shows 0 customers, 0 coins.
     Each customer is one card:  TIME (picker), BOX (list), COINS, STATUS (Finished / Cancelled), MINUTES.
     COINS AND MINUTES WORK BOTH WAYS: type the coins and the page works out the minutes, or type the minutes and it works out the coins, using the
     box's rate (the LocalServer's starting rates: Box 1 and Box 3 15 coins = 5 min, Box 2 10 coins = 3 min). The number the page worked out is shaded
     and a line under the card says so; type over it if the form says something else (the last number you typed is the one that is kept). Minutes may have
     decimals (7 coins at Box 1 = 2.33 min). If the price list was different on the day of a form, change the rates in the "Coins and minutes (the rates)"
     box of the day card (kept in this browser; "Use the LocalServer's starting rates" puts them back). A customer loaded from a file keeps both numbers as
     written until you edit one of them.
     A card with something missing turns red and says what. "Copy this customer" repeats a card and sets the new customer's TIME to the first one's time + its minutes (14:35 with 5 min -> 14:40; decimals are rounded to the nearest minute; past midnight the copy goes to the next day; no time or no minutes -> the time stays empty), "Sort by date and time" puts them in order.
     Faster for many customers: "Paste rows from Excel or Google Sheets" (columns: date, time, box, coins, status, minutes).
     To change what is already on the LocalServer: "What is on the LocalServer now" loads it into the cards.
  4. The page shows a summary (customers, cancelled, coins, per week) to compare with the paper forms.
  5. "Push to the LocalServer": the board joins the LocalServer's Wi-Fi (JollyHelmet-Local) by itself and sends the file to its /history/upload page. The
     LocalServer checks every row again and only replaces its history.json if all rows are good (this REPLACES the history there with what is on the page).
  6. After a successful push the board keeps a copy ("The copy saved on this board"). An unsent draft is also kept in the phone's browser, so closing
     the page does not lose typed customers.

THE FILE THE PAGE BUILDS (people who like code; the page has "Show the file")
    { "source": "previous data collected after the upgrade", "rows": [ ["2026-09-17","14:35",1,15,"F",5], ["2026-09-17","15:10",3,15,"C",2] ] }
    date YYYY-MM-DD, time HH:MM, box 1-3, coins 1-999, F finished / C cancelled, minutes (optional). At most 1000 rows / 64 KB. Never a name.

WHAT IT DOES TO THE LOCALSERVER
  The LocalServer's Daily / Weekly chart draws these rows in a lighter shade next to what the machines recorded, with the legend "lighter = previous data collected by the team after the upgrade"
  and a count of those customers. The file never changes the machine log, the totals, the CSV or what the Online Server receives.
  (Details: ESP32_LOCALSRV\HISTORY_FILE.txt.)

BOARD / LIBRARIES
  Any ESP32 or ESP32-S3 with the default settings (the default file-system partition holds the saved copy). Libraries: ESP Async WebServer + Async TCP.
  If you change the LocalServer's Wi-Fi name or password (AP_SSID / AP_PASSWORD in ESP32_LOCALSRV.ino), change LS_SSID / LS_PASS at the top of ESP32_ENTRY.ino too.
  The LocalServer needs the 2026-10-15 firmware (history file + the websocket crash fix) and the 16M Flash (3MB APP/9.9MB FATFS) partition scheme.
