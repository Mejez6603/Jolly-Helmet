BOX 3 - CUSTOMER SCREEN FLOW (TFT)      redrawn 2026-11-03 from "ESP32 Project\BOX 3 STEP corrected.txt" (36 steps, with the Refilling Station)
  (earlier versions: 2026-10-23 evening from the same file before the Refilling Station, and 04:48 from BOX 3 STEP.txt with the refill steps)

WHICH FILE IS DRAWN
  BOX 3 STEP corrected.txt = your BOX 3 STEP.txt with the Refilling Station changes: CHECKING C is the 2-second "Checking Main Container" screen (reads the station, sensor bit 32 = refill),
  WELCOME decides at the tap: touch AND Refilling Station offline, or touch AND container EMPTY / LOW -> SELECT DRY ONLY, else SELECT MODE; OUT OF ORDER watches the helmet + enclosure-door
  sensors (mask 3). Import it in the Designer (the published Box Cycle Designer already knows the new conditions), check it, export, and save over BOX 3 STEP.txt.
  The chart header always shows the file it was drawn from.

FILES
  BOX3_Flow_TFT.pdf    the flowchart on ONE page (poster size, prints scaled onto A3). Every box = one TFT screen + what the machine does there.
  BOX3_Flow_TFT.png    the same chart as one big picture (3360 x 5280) for the document team.
  screens\NN_STEP.png  every TFT screen of the file on its own, 640 x 480 (same drawing rules as the Designer preview). NN = the step's place in the file.
                       Steps named CL ... (Cleaning mode) and DR ... (Drying mode) are two copies of the same screens.
  box3_flow.html       the chart as a web page (open it in Edge/Chrome; it needs internet only for the font).

HOW TO READ IT
  Black header = start / end screen. Thick border = the machine is running. Dashed border = side screen (a question or a notice), linked by the numbers 1 2 3 4.
  Double border = fault. Text on an arrow = what moves the screen on. The numbers on the pictures (time, coins, progress bar, status line) are examples.
  The notes at the bottom of the chart say that every connection drawn was checked against the file.

WHEN THE FLOW CHANGES
  Save the new "Copy as JSON" over  ESP32 Project\BOX 3 STEP.txt  and right-click  render_flow.ps1 > Run with PowerShell.
  (To draw another export: powershell -File render_flow.ps1 -Json "..\other file.txt".)
  It rebuilds box3_flow.html, the PDF, the PNG and all the screens. Pictures, captions (relays, times, buttons, price) and the connection check come from the file itself.
  If a step is renamed, added or re-linked the notes at the bottom turn red and say what no longer matches; then the layout in box3_flow.template.html (NODES and the arrows) has to be adjusted.
  render_flow.ps1 needs Microsoft Edge.

NOT IN THE CHART
  SAFETY PAUSE (a door opens during a run) is built into the box and is not a step in the file.

ALSO NOT IN THE CHART (built into the boxes, 2026-11-03): with a helmet shut inside, OUT OF ORDER and power cuts never lock it in. The screen board B1 then adds the white lines "Your helmet is still inside." / "Take it out, then close the door." under OUT OF ORDER and beeps; the lock opens (S3B after 2 s, or the relay controller B3 after 15 s without S3B). After a power cut the restarted S3B hands a helmet that is still inside back first (OPEN B). The picture 36_OUT_OF_ORDER.png is the plain Designer screen without those lines.
