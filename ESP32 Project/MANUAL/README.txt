JOLLY HELMET USER MANUAL - how to edit
======================================
* JollyHelmet_UserManual_v9.docx = the NEWEST editable Word version (2026-10-16). It is v8 as it was SAVED on disk (every change typed in Word until then is kept) plus the new Control Center:
                                   one navigation bar and the Devices line (the old Home page text), the day viewer (Machine / JSON tabs), the combined Finished and cancelled tally, the Settings tab
                                   (clock, session log, backup restore, previous data), crash reports kept on the board, the ENTRY board chapter, the OPEN A / OPEN B beep, the Maintenance
                                   2-second safety with its bypass and the drain pump warning, C1 coin on GPIO 32, optional customer names (Blynk V5), policies, glossary, settings reference,
                                   firmware dates and Rev 9. All Control Center pictures are new (images\v9). Revision 9. Work in v9 from now on; close v8 in Word first (v8 has no changes of yours after 2026-10-16 03:47,
                                   or they are not in v9). Made by build\patch_docx.html?in=<copy of v8>&out=JollyHelmet_UserManual_v9.docx&src=home_update.src.html (new ops: replace-png, cell-set).
* JollyHelmet_UserManual_v9.pdf  = the PDF Word makes of v9. The web copy (ONLINE_WEB\manual) comes from the same file.
* JollyHelmet_UserManual_v8.docx = v7 with the LocalServer Wi-Fi renamed ESP32_LOCAL -> JollyHelmet-Local (2026-10-16).
* JollyHelmet_UserManual_v7.docx = the editable Word version of 2026-10-11. It is YOUR v6 (every change you typed in Word is kept) with the 6 box flowcharts and the
                                   7 troubleshooting decision trees redrawn in BLACK AND WHITE (no colours): black box = start or end, thick border = the machine is running (or "do this"),
                                   double border = a fault, diamond = a decision, dashed box = a note. The two "how to read" paragraphs say so. Revision 7. Work in v7 from now on;
                                   close v6 in Word first. The INDEX now works: its hidden index marks (XE) no longer carry "hidden" formatting, which made Word skip all 567 of them (the PDF index said "No index entries found" before).
* JollyHelmet_UserManual_v7.pdf  = the PDF Word makes of v7. Made by build_web_manual.ps1 (the web manual is made from the same file).
* JollyHelmet_UserManual_v6.docx = the version with the safety slide, before the black-and-white charts (2026-10-11). It is YOUR v5 (every change you typed in Word is kept) plus the SAFETY SLIDE of the
                                   enclosure door (lower left: slide it down to unlock the door by hand when the solenoid lock is broken or there is no power). It is now in the
                                   customer guide (a new part "If the door will not open" with a drawing, the safety list, the questions), the admin troubleshooting table, the
                                   technician guide (door locks, a new part "Safety slide on the enclosure door", firmware safety list, monthly maintenance test, hardware troubleshooting),
                                   Safety and emergencies, the decision tree "The door will not open" (redrawn), the Fix it guide and the monthly Checklist of the web page,
                                   Settings reference (fixed values), Policies, Glossary and the Revision history (rev 6). Work in v6 from now on; close v5 in Word first.
* JollyHelmet_UserManual_v6.pdf  = the PDF Word makes of v6. Made by build_web_manual.ps1.
* JollyHelmet_UserManual_v5.docx = the version with the online page (before the safety slide). It is YOUR v4 (every change you typed in Word is still there: the Cirkit
                                   pictures, the contact names, the removed notes...) plus the online page: a new Admin chapter "Online page (Monitor, Alerts,
                                   Reports)", a "Which machine is free?" part for customers, new Technician parts (customer status page and second Blynk device,
                                   firmware of 2026-10-10, alerts when the page is closed, the Manual tab and the team password, online page problems), new rows
                                   in the Settings reference, Glossary, Revision history (rev 5) and Policies, and 5 pictures of the web page.
                                   Open it in Word and click Yes when it asks to update fields: that fills the page numbers of the Contents and builds the Index.

* JollyHelmet_UserManual_v5.pdf  = the PDF of v5.
* JollyHelmet_UserManual_v4.docx / .pdf = the version before the online page was added.
* JollyHelmet_UserManual_v3.docx, v2, and JollyHelmet_UserManual.docx = older Word versions (v3 can be deleted).
* JollyHelmet_UserManual.pdf     = the OLD printable PDF made from the HTML source (landscape spreads, about 118 pages). It does not have your Word edits.
* JollyHelmet_UserManual.html    = the source of that old PDF (open it in any browser).
* rebuild_all.ps1               = rebuilds the OLD HTML-based PDF (needs only Microsoft Edge). The Word file is the master now.
* build_pdf.ps1                  = only rebuilds the old HTML-based PDF.
* images\                        = the logo, the Control Center screenshots, the touch-screen pictures (images\screens) and the Online Server /
                                   web page screenshots (images\online) used by both versions.
* images\cirkit\                 = put your Cirkit Designer exports here (PNG or JPG) with these names, then run rebuild_all.ps1:
                                     cirkit_overview, cirkit_box1, cirkit_box2, cirkit_box3, cirkit_acs
                                   Until a picture is there, the PDF shows a dashed slot with the file name. In the Word file just click the dashed
                                   box and use Insert > Pictures.
* photos\                        = put real photos here: box1.jpg, box2.jpg, box3.jpg (they replace the dashed "Photo" boxes in the PDF).
                                   In the Word file just click a dashed "Photo" box and insert the picture there.
* build\                         = how the pieces were made:
    tech_flow.src.html  the Technician guide text (headings, tables, lists, screenshots)      figs.html  the 6 drawings (svg)
    ref_flow.src.html   the Reference section text: safety, charts, settings reference, schematic slots, policies, glossary, version, index
    flowgen.html        draws the 6 box flowcharts and the 7 troubleshooting decision trees (open it through the local web server; it saves figs_ref.html)
    index_terms.json    the list of terms of the Index (name + search pattern); the PDF and the Word index are both built from it
    assemble_tech.ps1   puts the sources into JollyHelmet_UserManual.html and fills in the build-time facts: file dates of the sketches, library
                        versions, the protocol-file checksum, today's date, the Cirkit pictures (run rebuild_all.ps1 instead of running it alone)
    make_docx.html      builds the Word file from the HTML (needs a small local web server, only useful if the HTML changes a lot)
    Preview of a few spreads: open JollyHelmet_UserManual.html?only=19-22 (numbers count from 0).

Things to fill in (search for the square brackets, and for the yellow "confirm" marks):
  - Reference > Safety and emergencies: the "Who to call" names and numbers; "confirm" on the key release for the doors.
  - Reference > Policies: every "confirm" is a proposal for the owner to decide (refunds after a fault, children, liability, operating hours,
    where to report a fault, changing the default Wi-Fi passwords).
  - Technician guide, yellow "confirm": the TFT VCC rail (5 V or 3.3 V), and whether every HC-SR04 ECHO has its divider.
The admin pages print the Wi-Fi password (12345678), and the Online Server chapter prints the setup Wi-Fi password: give the admin and technician parts
only to the team, not to customers. Never write the Blynk token into the manual.
The touch-screen pictures are drawn from the same text the machine itself uses, so they match the real display.
Pin tables come from the flashed firmware and RevisedJollyHelmet_Checked.xlsx. Relay input numbers (IN1...) come from the Cirkit drawing.
The settings reference (defaults and ranges) was read from the firmware sketches (S3A, S3B, S3C, ACS1, ONLINESRV) on 2026-10-10.

EDITED IN WORD? MAKE THE PDF AND THE WEB MANUAL FROM THE WORD FILE  (added 2026-10-10)
* build_web_manual.ps1  = right-click > Run with PowerShell. Needs Microsoft Word on this PC (Word may stay open with your file: only a copy is used).
    - writes JollyHelmet_UserManual_v4.pdf next to the Word file (a PDF of exactly your Word file, with its contents, index and bookmarks);
    - rewrites ..\ONLINE_WEB\manual\ (customer_guide.pdf = public, team_manual.js = the whole manual ENCRYPTED with the team password,
      customer_index.js = chapter list and search text) for the Manual tab of the online web page.
    - It asks for the team password (min. 6 characters). Use the same one every time or tell the team the new one.
  Before you run it: in Word press Ctrl+A, then F9 (updates the contents page and the index), and save. (Or run it as
  build_web_manual.ps1 -UpdateFields and it updates them in its own copy.)
* From now on the WORD FILE is the master for the text you edited. Do not rebuild v4 from the HTML (make_docx.html) over it, or your edits are lost.

ADDING NEW TEXT TO THE WORD FILE WITHOUT LOSING YOUR EDITS  (build\patch_docx.html)
  The v5 text was added by a small tool, not by rebuilding the document: build\patch_docx.html opens a COPY of your Word file as a zip, inserts the new paragraphs,
  tables and pictures at named headings (the texts are in build\web_features.src.html, the pictures in images\web), adds a few rows and bullets to existing tables, and
  writes a NEW file. Your original is never changed. It needs the local helper server (see "How the pieces were made"); ask for it again if the web page changes and the
  manual should follow.

THE FLOWCHARTS IN BLACK AND WHITE  (build\flowgen.html)
  flowgen.html draws the 6 box flowcharts and the 7 decision trees and writes figs_ref.html. Black and white is the default; add ?color to its address for the old coloured
  version. To put new drawings into a Word file: build\patch_docx.html?in=...&out=...&src=bw_charts.src.html (the source lists the 13 pictures to replace by heading).
