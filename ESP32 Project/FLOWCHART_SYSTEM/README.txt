JOLLY HELMET - WHOLE-SYSTEM FLOWCHARTS      made 2026-10-23, REDRAWN 2026-11-03 for the Refilling Station
Assumptions: Box 1, Box 2 and Box 3 are the SAME build (UV + Cleaning + Drying), as Box 3 is today (the trimmed 36-step flow, BOX 3 STEP corrected.txt).
             Every box has ONE main container with ONE cleaning liquid. ONE Refilling Station (ESP32-WROOM, 3 water-level sensors, 3 valves) fills all three containers
             from one supply, ONE VALVE AT A TIME, and tells everybody the levels. It replaced the two ACS boards.

FILES
  SYSTEM_Flowcharts.pdf    all 11 charts, one per page (A3, portrait or landscape).
  png\NN_name.png          each chart as a picture (drawn at x2), for the document team.
  diagrams\NN_name.mmd     the Mermaid code of each chart. To edit: open https://mermaid.live , paste the text of one .mmd file, change it, export PNG / SVG.
                           (The first 3 lines "--- title: ... ---" are the chart title; leave them in. In a sequence chart a ";" ends the line: do not use it in a text.)
  system_flowcharts.html   all charts on one web page (open in Edge/Chrome, needs internet because Mermaid is loaded from cdn.jsdelivr.net).
  render_all.ps1           rebuilds the page, the PDF and the PNGs from diagrams\*.mmd (right-click > Run with PowerShell; needs Edge + internet).

THE CHARTS
  01 The whole system      people, the 3 boxes (each with its main container), the Refilling Station and its supply, the ESP-NOW radio, LocalServer, Online Server, ENTRY board,
                           Blynk, the web pages, the Blynk app.
  02 Customer flow, part 1 start screen, CHECKING MAIN CONTAINER (2 s), Welcome, the decision on the tap (station heard + container MID or more, else dry only), mode, insert coin,
                           cancel, and the OUT OF ORDER / MAINTENANCE screens.   (from BOX 3 STEP corrected.txt)
  03 Customer flow, part 2 door, helmet, cleaning / drying, cancel (abort), auto-dry, cooling down, take the helmet, thank you, safety pause.
  04 Inside one box        who sends what to whom over ESP-NOW (TFT display, main controller, sensors, relay controller, Refilling Station, LocalServer, Online Server), and who shows OUT OF ORDER.
  05 The Refilling Station its logic: levels, the queue (EMPTY first, then LOW, first come first served), one valve at a time, MAX stops it, no rise = FAILED, retry every 60 s,
                           what it broadcasts, the manual valve test.
  06 LocalServer           start-up, radio in / out, web pages (Box pages, Refilling Station page), session log, crash reports, ENTRY board.
  07 Online Server         start-up, the 1-second snapshot and alerts, the upload burst to Blynk, the router-style setup page (Wi-Fi tested before the token).
  08 Blynk and the web     what is written to Blynk, what the Monitor page (boxes drawn as machines, level dials from the Refilling Station, network map, Alerts, Reports) and the
                           customer page read.
  09 The complete system   (added 2026-11-07) ONE chart from end to end, top to bottom: 1 hardware (the three boxes, the Refilling Station and its supply), 2 ESP-NOW radio,
                           3 LocalServer + its local database (settings, session log, backup), 4 Online Server + its saved setup, 5 Blynk cloud = the online database,
                           6 code and hosting (ONLINE_WEB folder -> GitHub -> Vercel -> the website), 7 who looks at it (owner, customer phone, Blynk app).
                           The website only delivers the page files; the page in the browser then reads Blynk itself (A = page files, B = data). The GitHub and Vercel boxes show the usual
                           static-site set-up (every push to GitHub is deployed by Vercel); they were drawn from that, not read from the real accounts, so correct the text if the real set-up differs.
  10 The website, Monitor  (added 2026-11-07) what the owner's page (index.html) does inside the browser: token known or not, the refresh loop (10 s, only while the tab is visible),
                           HTTPS GET getAll from Blynk, error / empty / data, how old the data is -> Status Active or Inactive, then the Monitor, Reports and Alerts tabs
                           (alert colours, sounds, log), the QR / Settings / theme buttons. It only READS Blynk.
  11 The website, customer (added 2026-11-07) the customer page (status.html, from the QR sticker): public token, GET every 30 s, data usable or "not up to date",
                           each box FREE / IN USE / OUT OF ORDER / NO INFORMATION, English / Filipino, and the optional nickname (V5, the only value the website ever writes).
                           (Charts 10 and 11 start where the 09 chart's WEBSITE box ends. Both charts show Vercel as the place the page files come from, as chart 09 does.)
  The screens the customer sees (with pictures) are in  ..\FLOWCHART_BOX3 .

DATA FLOW DIAGRAM (added 2026-11-07, folder DATA_FLOW)   Input - Process - Output with a Feedback and Improvement band, laid out like the user's reference picture, in the Jolly Helmet colours (mascot orange with a navy outline, cream cards, navy arrows; the colours are the variables at the top of the HTML file).
  JollyHelmet_DataFlow.png (3800 x 2520), JollyHelmet_DataFlow.pdf (one page, vector), JollyHelmet_DataFlow.html (the source: edit the card texts at the bottom of the file), render_dfd.ps1 (rebuilds the PNG and PDF, needs Edge, no internet), logo.png.
  INPUT: customer, box input devices (coin acceptor, helmet + door sensors), owner / technician, team (ENTRY board).   PROCESS: the box system (main controllers) -> cleaning, drying, Refilling Station -> box outputs;
  the radio -> Local Server + Local Database + backup -> Online Server -> Blynk Cloud -> Website.   OUTPUT: cleaned and dried helmet, box screen and beeps, refilled container, session record, Control Center,
  Online Monitor, Alerts, Reports, customer status page.   FEEDBACK: alerts and faults, refill problems, reports and rules, crash reports and updates.
  Not in it on purpose: change money (there is no coin hopper), ratings, time picker, the old ACS boards. The Website box says "on Vercel" as chart 09 does (drawn from the usual set-up, not read from the real account).

CONCEPTUAL FRAMEWORK, IPO (added 2026-11-08, folder IPO_FRAMEWORK)   Input - Process - Output with the Feedback loop, in the plain black-and-white layout of the user's theoretical framework template (Figure 1, IPO diagram).
  JollyHelmet_IPO_Framework.png (with the "Figure 1" caption like the template), JollyHelmet_IPO_Framework_no_caption.png (for a Word caption), JollyHelmet_IPO_Framework.pdf (one page),
  JollyHelmet_IPO_Framework.html (source: edit the texts, the figure number is in id="cap"), render_ipo.ps1 (rebuilds the three files, needs Edge).
  It replaces the OLD framework in ..\CONCEPTUAL_FRAMEWORK (that folder is outdated: ratings, free retry, alcohol / scented solution, pumps, safety slide; it was left untouched).
  It shows the current design plus the revisions after implementation: disinfectant fogging solution (one liquid), true UV-C 254 nm lamps, the shortened customer flow, ESP-NOW -> Local Server -> Online Server -> Blynk -> website.

CONCEPTUAL FRAMEWORK WITH ICONS (added 2026-11-09, folder IPO_FRAMEWORK_ICONS)   The same Input - Process - Output framework as IPO_FRAMEWORK, redrawn in the layout of the user's "Figure 2" picture (one picture-icon per item,
  headings Input / Process / Output, outputs on a bracket line) in the Jolly Helmet colours (mascot orange, navy; the box icon carries the real mascot logo.png). IPO_FRAMEWORK (plain boxes) is kept as it was.
  JollyHelmet_Framework_Icons.png (with the "Figure 2" caption), JollyHelmet_Framework_Icons_no_caption.png (for a Word caption), JollyHelmet_Framework_Icons.pdf (one page, vector),
  JollyHelmet_Framework_Icons.html (source: the icons are small SVG drawings in the ICONS list, the nodes and arrows are the lines at the bottom; figure number = id="capn", caption = id="capt"), logo.png (needed next to the HTML),
  render_icons.ps1 (rebuilds the three files, needs Edge, no internet).
  INPUT: Customers, Touch Display and Coin Acceptor, Box Sensors (helmet and doors), Admin (Owner, Technician).   PROCESS: Display Board (ESP32) (moved here from Input on 2026-11-12), Box Main Controller (ESP32-S3) -> Disinfectant Fogging, True UV-C 254 nm, Drying -> Jolly Helmet Box;
  Refilling Station (levels to the controller, refills the box container); Local Server (Offline) with Local Database and Backup File (CSV) -> Online Server -> Blynk Cloud -> Remote Monitoring Website.
  OUTPUT: Cleaned and Dried Helmet, Transaction Record, System Report, Notification.   Left out to keep it readable: the ENTRY board (previous customers) and the customer status page / QR (inside the website icon).
  2026-11-10: the three groups are now BOXED - one rounded navy box each for Input, Process and Output with a cream header band (the group() lines above the nodes in the HTML: x, y, width, height, title); Admin moved up so all three boxes have the same height, arrows cross the box borders.
  2026-11-12: FEEDBACK box added under the three groups (the user's theoretical framework Figure 1 is Input -> Process -> Output with Output -> Feedback -> Input): group(420, 1520, 960, 300, 'Feedback') and the loop arrows Output -> Feedback -> Input (the last two wire() lines).
  Frame height FH is now 1850 (page 1976 with the caption, 1866 without): HTML (3 places), render_icons.ps1 window sizes.
  Feedback content (the user's choice, replacing my first version with comments / sales reports / revision): Refilling the Container, Maintenance, Faults (icons refill, maintain, fault; the old comment / stats / improve icons were removed). Three items side by side, no arrows between them.
  Same day: the Display Board (ESP32) node moved from the Input box into the Process box (node at 530,800); Input box narrowed to x16-420, Process box x440-1436; the Touch Display <-> Display Board wire now crosses the Input/Process border. Text of the plain IPO_FRAMEWORK file still has the older feedback wording (not changed).

DATA FLOW DIAGRAM, LEVEL 1 (added 2026-11-09, folder DFD_LEVEL1)   A TRUE data flow diagram (the orange picture in DATA_FLOW is really a conceptual framework), laid out like the user's GPS-tracker DFD template:
  black and white, external entities = rectangles with a shadow, processes = rounded boxes with a number strip (1.0 - 5.0), data stores = open-ended bars (D1, D2), every arrow labelled.
  2026-11-12 (user request): the PROCESS DESCRIPTIONS side panel was REMOVED, and the "Box status (broadcast)" flow 1.0 -> 4.0 is now a plain straight line (no hop bumps where it crosses the two radio lines). The picture size stayed 3960 x 2680 (the right side is just empty now). The flow() function still supports hops (a second argument) if you ever want them back.
  JollyHelmet_DFD_Level1.png (3960 x 2680), JollyHelmet_DFD_Level1.pdf (one page, vector), JollyHelmet_DFD_Level1.html (the source: entities / processes / stores / flows are the lines at the bottom of the file, each flow is a list of corner points),
  render_dfd1.ps1 (rebuilds the PNG and PDF, needs Edge, no internet).
  ENTITIES: Customer, Box sensors and actuators, Main containers and water supply, Team (ENTRY board), Owner / Technician, Customer (phone).   PROCESSES: 1.0 Customer transaction and cycle control (box main controller),
  2.0 Container refilling (Refilling Station), 3.0 Local recording and control (Local Server), 4.0 Online data upload (Online Server), 5.0 Online monitoring (website).   STORES: D1 Local database (settings, session log, totals), D2 Blynk cloud data.
  Not drawn on purpose: GitHub / Vercel hosting (not data), change money, ratings, the old ACS boards. The Blynk app push is in the note under the figure.

DATA FLOW DIAGRAMS, LEVEL 2 AND LEVEL 3 (added 2026-11-09, folder DFD_LEVEL2_3)   Seven black-and-white pages in the same style as Level 1 (entities = shadowed rectangles, processes = rounded boxes with a number strip, data stores = open-ended bars,
  dashed boxes = the neighbours of the process from the level above, so every page is balanced with its parent: the flows that cross its edge are the flows of the parent process).
  PAGES: Level 2 for 1.0 (box main controller: 1.1 check, 1.2 customer interface, 1.3 book the payment, 1.4 run the steps, 1.5 switch the outputs, 1.6 report the status / keep the settings; store D3 box settings),
  2.0 (Refilling Station: 2.1 levels, 2.2 queue, 2.3 valves, 2.4 settings, 2.5 broadcast; store D4), 3.0 (Local Server: 3.1 receive radio, 3.2 record, 3.3 Control Center, 3.4 settings, 3.5 send totals; stores D1.1 settings, D1.2 session log, D1.3 history and crashes),
  4.0 (Online Server: 4.1 listen, 4.2 statistics, 4.3 alerts, 4.4 upload to Blynk, 4.5 setup; stores D5 snapshot / statistics / alerts, D6 setup, D2.1 private V0, D2.2 public V0, D2.3 alert event), 5.0 (website: 5.1 live data, 5.2 monitor and reports, 5.3 alerts, 5.4 status page, 5.5 nickname; D2.1, D2.2, D2.3 nickname V5).
  Level 3 for 1.4 (inside the box main controller: 1.4.1 read the inputs, 1.4.2 decide the next step, 1.4.3 time the step, 1.4.4 relay mask, 1.4.5 screen, 1.4.6 report the step; store D3.1 session state; the step list from charts 02 and 03 is printed on the page)
  and for 2.3 (inside the Refilling Station: 2.3.1 check the valve is free, 2.3.2 open and close the valve (the only process that drives the valves), 2.3.3 watch the level rise, 2.3.4 report the result and retry, 2.3.5 manual valve test).
  FILES: JollyHelmet_DFD_Level2_Level3.pdf (7 pages, vector), png\ (one 3960 x 2680 picture per page), JollyHelmet_DFD_L2_L3.html (page shell and style, ?d=L2-1 ... L3-2 or ?d=all), dfd_engine.js (the drawing helper: ent / proc / store / flow with ports 'id.side:k',
  automatic Z / U / L routing, hops over crossings, auto labels), dfd_specs.js (THE CONTENT: one block per page, all texts, coordinates and flows), render_dfd23.ps1 (rebuilds the PNGs and the PDF; ".\render_dfd23.ps1 -Only 'L2-1','L3-2'" renders only those pictures, needs Edge, no internet).
  To change a text or a flow edit dfd_specs.js and run render_dfd23.ps1. Facts used: charts 02 / 03 (customer flow), 05 (Refilling Station), 06 - 08, ONLINE_WEB\DATA_CONTRACT.md (nickname V5 lives in the PUBLIC device).
  Not drawn: other Level 3 pages (3.x Local Server, 4.4 upload burst, 5.x website) - they can be added as new blocks in dfd_specs.js.

OUT OF ORDER (decided with the user, 2026-10-23)
  1. The box main controller S3 is silent: its own TFT display (B1 today) shows OUT OF ORDER by itself, because nobody is left to send it a screen. (Already in the A1/B1 firmware.)
  2. The helmet sensor (ultrasonic), the enclosure-door sensor or the relay board stops answering: the main controller enters OUT OF ORDER and takes no coins (S3B: in CHECKING, WELCOME
     and SELECT MODE, before a coin is taken).
  The Designer file has no step leading into OUT OF ORDER: the firmware enters it.

SOURCES USED (so the charts can be checked)
  BOX 3 STEP corrected.txt (customer flow), Shared_Common.h (device ids, packets, the Refilling Station's rules), ESP32_REFILL.ino (chart 05), ESP32_LOCALSRV (pages and endpoints),
  ESP32_ONLINESRV (loop, portal), ONLINE_WEB\DATA_CONTRACT.md (what is written to Blynk and read by the pages), ESP32_ENTRY\README.txt.

THE CHARTS SHOW THE TARGET. What is still different in today's firmware:
  1. DONE 2026-11-12: Box 1 (S3A) and Box 2 (S3C) were rebuilt to the Box 3 flow as ONE program per node type (S3A = S3B = S3C, A1 = B1 = C1, A2 = B2 = C2, A3 = B3 = C3; only box_config.h differs).
     All three boxes and the Refilling Station are written for the new design and compile, but nothing is flashed yet.
  2. Box 2 hardware: Box 2 now has C1 (TFT display), C2 (sensors) and C3 (relay controller, device id 15). C3 is a NEW board: Box 2's old relays (on C2) must be re-wired to a
     13-channel board like B3's. Same for Box 1: A3 gets a heater relay (GPIO 26). The Refilling Station must have the Box 1 and Box 2 sensors and valves wired (or WATCH_BOX set to false).
     The charts above did not change: they already showed three identical boxes.
  3. The old ACS firmware (ESP32_ACS1 / ESP32_ACS2 / ESP32_ACS_CONFIG_BENCH) is archived, not part of the system any more.

CUSTOMER CLEANING CYCLE, PART 2 (added 2026-11-12, folder CLEANING_CYCLE_PART2)   The user's own older flowchart picture ("1 - Customer cleaning cycle, Part 2", Verdana style, F and A connectors) REDRAWN as a vector page because no source file existed anywhere in the project.
  Requested change: the NO line of "Drying time up?" now goes straight into the "Drying: heater and fan on (relays)" box with its own arrowhead (before, its arrowhead ended on the "No (Drying)" line and the two lines merged); the two arrows enter the box at two separate points. Nothing else was changed, the texts are the ones in the picture.
  Customer_Cleaning_Cycle_Part2.png (1700 x 2416), Customer_Cleaning_Cycle_Part2.pdf (one page), Customer_Cleaning_Cycle_Part2.html (source: the boxes, diamonds and arrows are the lines at the bottom, each arrow is a list of corner points), render_part2.ps1 (rebuilds the PNG and PDF, needs Edge, no internet).
  The text is the OLD wording (mist sprays liquid, UV light on) - not the current disinfectant fogging + true UV-C. The unlabelled arrow from "Cleaning time up?" straight down into Drying is also as in the user's picture (the diamond has No on the left, Yes on the right and that extra exit).

REFILLING STATION CHART (added 2026-11-12, folder REFILLING_STATION_CHART)   The user's own picture "3 - Refilling station" (ESP32-WROOM, 3 sensors, 3 valves; connectors C and B) REDRAWN as a vector page, again because no source file existed.
  Requested change: the NO line of "Any container low?" now goes straight into the "Read the 3 water-level sensors (Box 1, 2, 3 containers)" box with its own arrowhead (before, its arrowhead ended on the long loop line coming back from "Close the valve", so the two lines merged). The "Close the valve" loop now enters the same box above it, the "Receive settings" line enters from the right - three separate arrows, nothing else changed, texts as in the picture.
  Refilling_Station_Chart.png (1756 x 2320), Refilling_Station_Chart.pdf (one page), Refilling_Station_Chart.html (source, same helper functions as CLEANING_CYCLE_PART2), render_station_chart.ps1 (rebuilds the PNG and PDF, needs Edge, no internet).
  The text is the user's wording, not checked against the firmware (the real station has the EMPTY-first / LOW logic, stall 30 s / 5 s, 3 minute FAILED and the 60 s retry, which this simple chart does not show).

NOT DRAWN (to keep the charts readable)
  The per-board pin maps (the Refilling Station's pins are decided: sensors GPIO 34 / 35 / 32, valve relays GPIO 21 / 19 / 18, see PROJECT_HANDOFF.md and the pin sheet block F1), the bench and test sketches.
