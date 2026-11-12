REMOVED FROM THE ONLINE WEB PAGE ON 2026-10-23 (the team asked: no Checklist, no Fix it, no Manual tab, and no "Fix it" links on the alerts)

These files are the code of those three tabs, kept here in case they are wanted back. They are NOT part of ONLINE_WEB, so they are not uploaded with it.
  js\checklist.js     the Maintenance checklist (also made "maintenance due" alerts and the badge on the tab)
  js\wizard.js        the Fix it guide (yes / no questions)
  js\wizard_data.js   the questions and answers of the Fix it guide
  js\manual.js        the Manual tab (customer guide, encrypted team chapters, search)
  manual\             customer_guide.pdf, customer_index.js, team_manual.js = what MANUAL\build_web_manual.ps1 made for the Manual tab

To bring a tab back: copy its js file back into ONLINE_WEB\js, add its <section> and tab link to index.html (the old index.html is in the backup of that day), add the <script> line,
and add the entry in js\tabs.js. The Word manual and its PDF are untouched (ESP32 Project\MANUAL).
