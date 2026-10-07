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
    .coin-row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-top:10px; font-size:0.85rem; }
  </style>
</head>
<body>

  <header class="top-header">
    <div class="header-left">
      <button class="burger-btn" onclick="toggleSidebar()">☰</button>
      <div class="app-title">ESP32 <span>CONTROL CENTER</span></div>
    </div>
    <div class="health-capsule">
      <span id="ws-badge" class="badge">WS Offline</span>
      <span>RAM: <b id="hdr-ram">-- KB</b></span>
      <span>PSRAM: <b id="hdr-psram">-- KB</b></span>
    </div>
  </header>

  <div class="sidebar-overlay" id="sidebar-overlay" onclick="toggleSidebar()"></div>
  <aside class="sidebar" id="sidebar">
    <div class="sidebar-header">
      <div class="sidebar-title">NAVIGATION MENU</div>
      <button class="close-btn" onclick="toggleSidebar()">✕</button>
    </div>
    <nav class="nav-links">
      <a class="nav-item active" href="/"><span>🏠</span> Home &amp; Statistics</a>
      <a class="nav-item" href="/box1"><span>📦</span> Box 1 Settings</a>
      <a class="nav-item" href="/box2"><span>🔥</span> Box 2 (Heater) Settings</a>
      <a class="nav-item" href="/box3"><span>🧴</span> Box 3 (Clean + Dry) Settings</a>
      <a class="nav-item" href="/acs"><span>🧪</span> ACS 1 / ACS 2 Settings</a>
    </nav>
    <div class="sidebar-footer" id="node-list"></div>
  </aside>

  <main class="container">

    <div id="refill-banner" class="refill-banner" style="display:none;">
      <span>⚠️</span> <span id="refill-banner-text"></span>
    </div>

    <div class="sec-title">Configure and calibrate</div>
    <div class="cfg-links">
      <a href="/box1">📦 Box 1<small>levels · rules · phases · relays</small></a>
      <a href="/box2">🔥 Box 2<small>heat · rules · relays</small></a>
      <a href="/box3">🧴 Box 3<small>levels · rules · phases · drying</small></a>
      <a href="/acs">🧪 ACS 1 / 2<small>lines · mix · pumps</small></a>
    </div>

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
          <span class="card-title">Revenue — Last 10 Completed Cycles</span>
          <button class="btn-sec" onclick="resetStats()">Reset Stats</button>
        </div>
        <div id="stat-chart" class="chart-wrap">
          <span style="color:var(--muted); font-size:0.8rem;">No completed cycles yet.</span>
        </div>
        <div class="coin-row">
          <span class="label">Coin value (PHP per pulse):</span>
          <input type="number" id="coin-value" value="1" min="0.1" step="0.5" style="width:90px;">
          <button onclick="saveCoinValue()">Save</button>
        </div>
      </div>
    </div>

    <div id="diag" style="color:var(--muted);font-size:0.75rem;margin-top:16px;line-height:1.6;"></div>

  </main>

  <div id="toast">Saved Successfully!</div>
  <script src="/script.js"></script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
