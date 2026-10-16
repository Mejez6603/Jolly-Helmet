#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>

// Home page of the Control Center (rewritten 2026-10-03). The previous dashboard is archived in legacy_dashboard/.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP32 CONTROL CENTER</title>
  <link rel="stylesheet" href="/style.css">
  <style>
    .cfg-links { display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:10px; margin-bottom:16px; }
    .cfg-links a { display:block; text-align:center; text-decoration:none; background:var(--card); border:1px solid var(--accent); color:var(--accent); border-radius:10px; padding:14px 8px; font-weight:800; font-size:0.95rem; }
    .cfg-links a small { display:block; color:var(--muted); font-weight:600; font-size:0.7rem; margin-top:4px; }
    .sec-title { font-size:0.8rem; color:var(--muted); text-transform:uppercase; letter-spacing:0.05em; margin:6px 0 8px; }
    .state-banner a.go { color:var(--accent); font-size:0.75rem; text-decoration:none; font-weight:700; }
    .bar { height:12px; background:#090d16; border:1px solid var(--card-border); border-radius:6px; overflow:hidden; margin:4px 0 8px; }
    .bar i { display:block; height:100%; background:var(--accent); }
    .chip { display:inline-block; border-radius:10px; padding:2px 9px; margin:2px 4px 2px 0; font-size:0.72rem; font-weight:700; background:#334155; }
    .chip.bad { background:var(--danger); } .chip.good { background:var(--success); color:#000; } .chip.run { background:var(--accent); color:#000; }
    .acs-note { color:var(--warning); font-size:0.78rem; margin-top:4px; }
    .navbar { display:flex; gap:6px; margin:0 0 14px; overflow-x:auto; scrollbar-width:none; -webkit-overflow-scrolling:touch; }
    .navbar::-webkit-scrollbar { display:none; }
    .nav-pill { flex:none; background:var(--card); border:1px solid var(--card-border); color:var(--muted); border-radius:99px; padding:9px 16px; font-weight:700; font-size:0.85rem; text-decoration:none; white-space:nowrap; cursor:pointer; font-family:inherit; }
    .nav-pill:hover { color:var(--text); border-color:var(--accent); }
    .nav-pill.active { color:#fff; border-color:var(--accent); background:rgba(56,189,248,0.22); }
    #devices { margin:0 0 12px; }
    #devices summary { cursor:pointer; font-size:0.82rem; font-weight:700; list-style:none; display:flex; align-items:center; gap:8px; }
    #devices summary::-webkit-details-marker { display:none; }
    #devices summary::after { content:"\25BE"; margin-left:auto; color:var(--accent); }
    #devices[open] summary::after { transform:rotate(180deg); }
    #node-list { margin-top:10px; display:grid; grid-template-columns:repeat(auto-fit,minmax(230px,1fr)); gap:2px 22px; font-size:0.78rem; color:var(--muted); }
    .tabs { display:flex; gap:8px; margin:0 0 14px; }
    .tab { flex:1; max-width:220px; background:var(--card); border:1px solid var(--card-border); color:var(--muted); border-radius:10px; padding:11px 8px; font-weight:800; font-size:0.9rem; }
    .tab.active { color:var(--accent); border-color:var(--accent); }
    #stat-chart .chart-col.pick { cursor:pointer; }
    #day-table th { color:var(--muted); font-size:0.68rem; font-weight:600; text-transform:uppercase; text-align:right; padding:4px 4px; }
    #day-table td { padding:5px 4px; text-align:right; border-top:1px solid var(--card-border); }
    #day-table th:first-child, #day-table td:first-child { text-align:left; }
    .coin-row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-top:10px; font-size:0.85rem; }
  </style>
</head>
<body>

  <header class="top-header">
    <div class="header-left">
      <div class="app-title">ESP32 <span>CONTROL CENTER</span></div>
    </div>
    <div class="health-capsule">
      <span id="ws-badge" class="badge">Connecting</span>
      <span>RAM: <b id="hdr-ram">-- KB</b></span>
      <span>PSRAM: <b id="hdr-psram">-- KB</b></span>
    </div>
  </header>



  <main class="container">

    <div id="refill-banner" class="refill-banner" style="display:none;">
      <span>⚠️</span> <span id="refill-banner-text"></span>
    </div>

    <nav class="navbar">
      <button id="tab-dash" class="nav-pill active" onclick="showTab('dash')">Dashboard</button>
      <button id="tab-set" class="nav-pill" onclick="showTab('set')">&#9881; Settings</button>
      <a class="nav-pill" href="/box1">Box 1</a>
      <a class="nav-pill" href="/box2">Box 2</a>
      <a class="nav-pill" href="/box3">Box 3</a>
      <a class="nav-pill" href="/acs">ACS 1 / 2</a>
    </nav>

    <div id="pane-dash">
    <div id="heap-alert" style="display:none;margin:10px 0 0;padding:10px 12px;border:1px solid var(--danger);border-radius:10px;background:rgba(239,68,68,0.12);font-size:0.82rem;line-height:1.5;"></div>
    <div id="log-alert" style="display:none;margin:10px 0 0;padding:10px 12px;border:1px solid var(--danger);border-radius:10px;background:rgba(239,68,68,0.12);font-size:0.85rem;line-height:1.5;"></div>
    <details id="devices" class="card">
      <summary><span id="dev-dot" style="color:var(--muted);">&#9679;</span> Devices: <b id="dev-count">-- of 13</b> online <span style="color:var(--muted);font-weight:500;">(tap to see which)</span></summary>
      <div id="node-list"></div>
    </details>
    <div class="sec-title">The three boxes</div>
    <div class="dual-state-banner">
      <div class="state-banner">
        <div>
          <div class="label">Box 1 · <a class="go" href="/box1">settings →</a> · <a class="go" href="#" onclick="refreshScreen(1);return false;">refresh screen</a></div>
          <div id="b1-state" class="state-title">--</div>
          <div id="b1-coins" style="font-size:0.78rem; color:var(--muted);">Coins: 0</div>
        </div>
        <div style="text-align:right;">
          <div class="label">Time left</div>
          <div id="b1-timer" class="timer-display">00:00</div>
        </div>
      </div>
      <div class="state-banner">
        <div>
          <div class="label">Box 2 · <a class="go" href="/box2">settings →</a> · <a class="go" href="#" onclick="refreshScreen(2);return false;">refresh screen</a></div>
          <div id="b2-state" class="state-title">--</div>
          <div id="b2-coins" style="font-size:0.78rem; color:var(--muted);">Coins: 0</div>
        </div>
        <div style="text-align:right;">
          <div class="label">Time left</div>
          <div id="b2-timer" class="timer-display">00:00</div>
        </div>
      </div>
      <div class="state-banner">
        <div>
          <div class="label">Box 3 · <a class="go" href="/box3">settings →</a> · <a class="go" href="#" onclick="refreshScreen(3);return false;">refresh screen</a></div>
          <div id="b3-state" class="state-title">--</div>
          <div id="b3-coins" style="font-size:0.78rem; color:var(--muted);">Coins: 0</div>
        </div>
        <div style="text-align:right;">
          <div class="label">Time left</div>
          <div id="b3-timer" class="timer-display">00:00</div>
        </div>
      </div>
    </div>

    <div class="sec-title">Refill systems</div>
    <div class="grid-2">
      <div class="card">
        <div class="card-header"><span class="card-title">ACS 1 · Box 1</span><a class="go" href="/acs" style="color:var(--accent);font-size:0.75rem;text-decoration:none;font-weight:700;">settings →</a></div>
        <div class="row"><span class="label">Status:</span><span id="acs0-step" class="badge">--</span></div>
        <div class="label">Alcohol mixer container</div><div class="bar"><i id="acs0-pa" style="width:0%"></i></div>
        <div class="label">Scented mixer container</div><div class="bar"><i id="acs0-ps" style="width:0%"></i></div>
        <div id="acs0-chips"></div>
      </div>
      <div class="card">
        <div class="card-header"><span class="card-title">ACS 2 · Box 3</span><a class="go" href="/acs" style="color:var(--accent);font-size:0.75rem;text-decoration:none;font-weight:700;">settings →</a></div>
        <div class="row"><span class="label">Status:</span><span id="acs1-step" class="badge">--</span></div>
        <div class="label">Alcohol mixer container</div><div class="bar"><i id="acs1-pa" style="width:0%"></i></div>
        <div class="label">Scented mixer container</div><div class="bar"><i id="acs1-ps" style="width:0%"></i></div>
        <div id="acs1-chips"></div>
      </div>
    </div>

    <div class="sec-title">Statistics &amp; revenue (all three boxes)</div>
    <div class="stats-layout">
      <div class="stat-grid">
        <div class="stat-tile"><div class="stat-label">Users Served</div><div id="stat-sessions" class="stat-value">0</div></div>
        <div class="stat-tile"><div class="stat-label">Total Revenue</div><div id="stat-revenue" class="stat-value" style="color:var(--success);">PHP 0.00</div></div>
        <div class="stat-tile"><div class="stat-label">Avg. Cycle Duration</div><div id="stat-duration" class="stat-value" style="color:var(--warning);">00:00</div></div>
        <div class="stat-tile"><div class="stat-label">Total Coins Inserted</div><div id="stat-coins" class="stat-value">0</div></div>
      </div>
      <div class="card">
        <div class="card-header">
          <span class="card-title"><span id="use-title">Daily Usage (7 Days)</span> <span style="font-weight:400;color:var(--muted);font-size:0.72rem;">(orange = cancelled)</span></span>
          <span style="display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end;">
            <button class="btn-sec" id="use-day" onclick="useMode('day')">Daily</button>
            <button class="btn-sec" id="use-week" onclick="useMode('week')">Weekly</button>
            <select id="use-weeks" onchange="useWeeks(this.value)" style="display:none;background:#334155;color:#fff;border:0;border-radius:6px;padding:6px 8px;"><option value="4">4 weeks</option><option value="6">6 weeks</option><option value="8">8 weeks</option><option value="12">12 weeks</option></select>
            
          </span>
        </div>
        <div id="stat-chart" class="chart-wrap">
          <span style="color:var(--muted); font-size:0.8rem;">No sessions yet.</span>
        </div>
        <div id="use-legend" style="color:var(--muted);font-size:0.72rem;margin-top:6px;"></div>
        <div id="use-note" style="color:var(--muted);font-size:0.72rem;margin-top:4px;"></div>
        <div class="coin-row">
          <span class="label">View a day:</span>
          <input type="date" id="day-pick"> <button class="btn-sec" onclick="openDayPick()">Show the customers</button>
          <button class="btn-sec" onclick="openDay(0)">Sessions without a date</button>
        </div>
        <div style="color:var(--muted);font-size:0.72rem;margin-top:4px;">Tip: in the Daily chart, tap a bar to see that day customer by customer.</div>
      </div>
    </div>

        <div class="card" id="day-card" style="display:none;margin-top:12px;">
      <div class="card-header"><span class="card-title" id="day-title">Day</span><button class="btn-sec" onclick="closeDay()">Close</button></div>
      <div class="tabs" style="margin:0 0 10px;"><button id="dtab-m" class="tab active" onclick="dayTab('m')">Machine</button><button id="dtab-j" class="tab" onclick="dayTab('j')">JSON</button></div>
      <div id="day-sum" style="font-size:0.85rem;line-height:1.5;margin-bottom:8px;"></div>
      <div style="overflow-x:auto;"><table id="day-table" style="width:100%;border-collapse:collapse;font-size:0.8rem;min-width:330px;"></table></div>
      <div id="day-note" style="color:var(--muted);font-size:0.72rem;line-height:1.5;margin-top:8px;"></div>
    </div>

    <div class="card" style="margin-top:12px;">
      <div class="card-header"><span class="card-title">Finished and cancelled sessions</span></div>
      <table style="width:100%;border-collapse:collapse;font-size:0.85rem;">
        <thead><tr style="color:var(--muted);text-align:right;"><th style="text-align:left;padding:4px 6px;font-weight:600;"></th><th style="padding:4px 6px;font-weight:600;">Finished</th><th style="padding:4px 6px;font-weight:600;">Cancelled</th></tr></thead>
        <tbody id="fc-body"></tbody>
      </table>
      <div id="fc-src" class="label" style="font-size:0.78rem;margin-top:8px;line-height:1.5;"></div>
      <div class="label" style="font-size:0.75rem;margin-top:8px;line-height:1.5;">Finished = the cycle ran to the end. Cancelled = the customer aborted mid-cycle; the coins they paid still count as revenue. Both are users served. The machines count from the update that added these two columns; the rows of the JSON file are added to that, so the table is the whole tally from the first day. Box 1 and Box 3 book the coins their own S3 accepted; a payment cancelled on the INSERT COIN screen is not counted.</div>
    </div>

    <div id="adj-note" style="display:none;color:var(--muted);font-size:0.78rem;margin:10px 0 4px;line-height:1.5;">
      <span id="adj-text"></span> <button class="btn-sec" onclick="adjRestore()">Restore original</button>
    </div>

    <div id="rs-sec" style="display:none;">
      <div class="sec-title">Restated at the new price list <span id="rs-note" style="font-weight:400;color:var(--muted);font-size:0.75rem;text-transform:none;letter-spacing:0;"></span></div>
      <div class="card">
        <table style="width:100%;border-collapse:collapse;font-size:0.85rem;">
          <thead><tr style="color:var(--muted);text-align:right;"><th style="text-align:left;padding:4px 6px;font-weight:600;"></th><th style="padding:4px 6px;font-weight:600;">As collected</th><th style="padding:4px 6px;font-weight:600;">Restated</th></tr></thead>
          <tbody id="rs-body"></tbody>
        </table>
        <div id="rs-warn" style="display:none;color:var(--danger);font-size:0.8rem;margin-top:8px;">The totals above are lower than at the capture: the statistics were reset after it, so the "since the capture" lines are not valid.</div>
        <div class="coin-row">
          <span class="label">Sent by the spare ESP32_RESTATE board. The coins really collected are never changed.</span>
          <button class="btn-sec" onclick="rsClear()">Remove</button>
        </div>
      </div>
    </div>

    
    </div>

    <div id="pane-set" style="display:none;">
    <div class="sec-title">Configure and calibrate</div>
    <div class="cfg-links">
      <a href="/box1">📦 Box 1<small>levels · rules · phases · relays</small></a>
      <a href="/box2">🔥 Box 2<small>heat · rules · relays</small></a>
      <a href="/box3">🧴 Box 3<small>levels · rules · phases · drying</small></a>
      <a href="/acs">🧪 ACS 1 / 2<small>lines · mix · pumps</small></a>
    </div>

    <div class="sec-title">Clock, session log and history</div>
    <div class="card" style="margin-top:12px;">
      <div class="card-header"><span class="card-title">Session log &amp; clock</span><a href="/log.csv" class="go" style="color:var(--accent);font-size:0.78rem;text-decoration:none;font-weight:700;">Download log (CSV) ↓</a></div>
      <div id="clk-line" style="font-size:0.85rem;line-height:1.5;"></div>
      <div class="coin-row">
        <input type="date" id="clk-date"> <input type="time" id="clk-time">
        <button onclick="setClockManual()">Set date &amp; time</button>
        <button class="btn-sec" onclick="setClockPhone(false)">Use this phone's time</button>
      </div>
      <div id="log-line" style="font-size:0.85rem;line-height:1.5;margin-top:10px;"></div>
      <div class="bar" style="margin-top:6px;"><i id="log-bar" style="width:0%"></i></div>
      <div style="color:var(--muted);font-size:0.72rem;line-height:1.5;margin-top:6px;">Every session the machines report is saved here with its date and time (never a name). The LocalServer has no clock of its own: a phone sets it when the Control Center is opened after a power-on; sessions that finish before that get their date as soon as the clock is set. Entries older than a year are dropped automatically.</div>

      <div style="margin-top:14px;padding-top:10px;border-top:1px solid var(--card-border);font-size:0.85rem;line-height:1.5;"><b>Restore the machine log from a backup</b></div>
      <div style="color:var(--muted);font-size:0.72rem;line-height:1.5;margin-top:4px;">The <b>Download log (CSV)</b> link above is the backup: download it now and then and keep the file. If the log is ever lost (a power cut damaged the storage, an upload erased it, Reset Stats by mistake), choose that file here. Only an <b>unchanged download</b> is accepted (the last line of the file is a check that is refused if anything was edited or the file was saved again in a spreadsheet). Sessions already on the board stay, nothing is added twice, and the totals above are raised to at least the log's tally. Corrections do not go here: they belong in the JSON file below, which is labelled.</div>
      <div class="coin-row">
        <input type="file" id="restore-file" accept=".csv,text/csv" style="max-width:100%;">
        <button onclick="uploadLogRestore()">Restore from backup CSV</button>
      </div>
      <div id="restore-line" style="font-size:0.82rem;line-height:1.5;margin-top:6px;"></div>

      <div style="margin-top:14px;padding-top:10px;border-top:1px solid var(--card-border);font-size:0.85rem;line-height:1.5;"><b>Previous data (history.json)</b> <span id="hist-line"></span></div>
      <div class="coin-row">
        <input type="file" id="hist-file" accept=".json,application/json" style="max-width:100%;">
        <button onclick="uploadHistory()">Upload history.json</button>
        <a href="/history.json" style="color:var(--accent);font-weight:700;text-decoration:none;">Download the file ↓</a>
        <button class="btn-sec" onclick="removeHistory()">Remove file</button>
      </div>
      <div style="color:var(--muted);font-size:0.72rem;line-height:1.5;margin-top:6px;">For the days before this log existed (the LocalServer then only counted revenue up). The team edits <b>history.json</b> on a PC (one row per customer: date, time, box, coins, F finished / C cancelled, minutes; never a name) and uploads it here. The chart above draws these rows in a lighter shade ("previous data collected by the team after the upgrade") beside what the machines recorded. The file does not change the machine log, the totals at the top of the page or the CSV; the Finished and cancelled card adds it and says how much came from it.</div>
    </div>

    <div class="sec-title">Statistics</div>
      <div class="card">
        <div class="coin-row" style="margin-top:0;">
          <span class="label">Coin value (PHP per pulse):</span>
          <input type="number" id="coin-value" value="1" min="0.1" step="0.5" style="width:90px;">
          <button onclick="saveCoinValue()">Save</button>
        </div>
        <div class="coin-row">
          <button class="btn-sec" onclick="resetStats()">Reset Stats</button>
          <span class="label">Empties the counters and the machine session log (download the log CSV first). The history file stays.</span>
        </div>
      </div>

      <div class="sec-title">This board</div>
      <div id="diag" style="color:var(--muted);font-size:0.75rem;margin-top:16px;line-height:1.6;"></div>
      <details id="crash-box" style="display:none;margin-top:10px;font-size:0.75rem;line-height:1.5;color:var(--muted);">
        <summary style="cursor:pointer;color:var(--warning);font-weight:700;">Crash reports kept on this board (<span id="crash-n">0</span>) - tap to read</summary>
        <div style="margin:6px 0;">Newest first. They stay in the board's flash (a power cut does not erase them). If the unit restarted by itself, take a screenshot of this and send it to the developer.</div>
        <div id="crash-list"></div>
        <button class="btn-sec" onclick="clearCrashes()">Delete these reports</button>
      </details>

  
    </div>

  </main>

  <div id="toast">Saved Successfully!</div>
  <script src="/script.js"></script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
