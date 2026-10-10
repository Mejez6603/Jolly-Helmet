JOLLY HELMET USER MANUAL - how to edit
======================================
* JollyHelmet_UserManual_v2.docx = the EDITABLE Word version with the new Technician guide (components, pinouts, circuit, firmware, maintenance).
                                   Open it in Word and click Yes when it asks to update fields: that fills the page numbers in the Contents page.
* JollyHelmet_UserManual.docx    = the earlier Word version (customer + admin only). It was open in Word when v2 was made, so it was not overwritten.
* JollyHelmet_UserManual.pdf     = the printable PDF (landscape spreads, 8 x 5.17 in, like the VGuide manual), now 62 pages + covers.
* JollyHelmet_UserManual.html    = the source of the PDF (open it in any browser; edit the text in a text editor).
* build_pdf.ps1                  = right-click > Run with PowerShell to rebuild the PDF after editing the HTML (needs only Microsoft Edge).
* images\                        = the logo, the Control Center screenshots and the touch-screen pictures (images\screens) used by both versions.
* photos\                        = put real photos here: box1.jpg, box2.jpg, box3.jpg (they replace the dashed "Photo" boxes in the PDF).
                                   In the Word file just click a dashed "Photo" box and insert the picture there.
* build\                         = how the pieces were made:
    tech_flow.src.html  the Technician guide text (headings, tables, lists)      figs.html  the 5 circuit drawings (svg)
    assemble_tech.ps1   puts both into JollyHelmet_UserManual.html (run it after editing either file, then run build_pdf.ps1)
    make_docx.html      builds the Word file from the HTML (needs a small local web server, only useful if the HTML changes a lot)
    Preview of a few spreads: open JollyHelmet_UserManual.html?only=19-22 (numbers count from 0).

Things to fill in (search for the square brackets, and for the yellow "confirm" marks in the Technician guide):
  - Cover: [Member Name 1..4]            - Back cover: [School / contact details here], [Email or phone number here]
  - Technician guide, yellow "confirm": UV and humidifier voltages, pump voltage, fan voltage, ultrasonic model, number and rating of
    the 12 V supplies and DC-DC converters, JD-VCC jumper setting, TFT VCC rail.
The admin pages print the Wi-Fi password (12345678): give the admin and technician parts only to the team, not to customers.
The touch-screen pictures are drawn from the same text the machine itself uses, so they match the real display.
Pin tables come from the flashed firmware and RevisedJollyHelmet_Checked.xlsx. Relay input numbers (IN1...) come from the Cirkit drawing.
