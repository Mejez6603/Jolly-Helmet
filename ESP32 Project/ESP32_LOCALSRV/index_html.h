#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP32 CONTROL CENTER</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>

  <!-- Top Navigation Header -->
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

  <!-- Sidebar Drawer Navigation -->
  <div class="sidebar-overlay" id="sidebar-overlay" onclick="toggleSidebar()"></div>
  <aside class="sidebar" id="sidebar">
    <div class="sidebar-header">
      <div class="sidebar-title">NAVIGATION MENU</div>
      <button class="close-btn" onclick="toggleSidebar()">✕</button>
    </div>
    <nav class="nav-links">
      <div class="nav-item active" onclick="switchTab('tab-stats', this)">
        <span>📊</span> Statistics & Revenue
      </div>
      <div class="nav-item" onclick="switchTab('tab-box1', this)">
        <span>📦</span> Box 1
      </div>
      <div class="nav-item" onclick="switchTab('tab-box2', this)">
        <span>🔥</span> Box 2 (Heater)
      </div>
      <div class="nav-item" onclick="switchTab('tab-acs', this)">
        <span>🧪</span> Alcohol Container System
      </div>
      <div class="nav-item" onclick="switchTab('tab-settings', this)">
        <span>⚙️</span> Settings
      </div>
    </nav>
    <div class="sidebar-footer">
      <div class="node-status-row"><span>Node A1 (Terminal):</span><span id="sb-a1-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node A2 (Actuators):</span><span id="sb-a2-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node C1 (Terminal):</span><span id="sb-c1-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node C2 (Actuators):</span><span id="sb-c2-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node ACS (D1):</span><span id="sb-acs-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node S3A (Box 1 Config):</span><span id="sb-s3a-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node S3C (Box 2 Config):</span><span id="sb-s3c-status" class="badge">OFFLINE</span></div>
    </div>
  </aside>

  <!-- Main Content Container -->
  <main class="container">

    <!-- Refill Notification - visible on every tab, hidden when nothing needs attention -->
    <div id="refill-banner" class="refill-banner" style="display:none;">
      <span>⚠️</span> <span id="refill-banner-text"></span>
    </div>

    <!-- Machine State Banners - both boxes, visible on every tab -->
    <div class="dual-state-banner">
      <div class="state-banner">
        <div>
          <div class="label">Box 1 State Machine</div>
          <div id="mach-state" class="state-title">STATE: IDLE</div>
          <div id="mach-step" style="color:var(--accent); font-size:0.9rem; font-weight:600; margin-top:2px;">Step: None</div>
        </div>
        <div style="text-align:right;">
          <div class="label">Accumulating Timer</div>
          <div id="active-timer" class="timer-display">00:00</div>
          <div id="handshake-status" style="font-size:0.75rem; color:var(--muted);">Handshake: Waiting</div>
        </div>
      </div>
      <div class="state-banner">
        <div>
          <div class="label">Box 2 State Machine</div>
          <div id="box2-mach-state" class="state-title">STATE: IDLE</div>
        </div>
        <div style="text-align:right;">
          <div class="label">Accumulating Timer</div>
          <div id="box2-active-timer" class="timer-display">00:00</div>
          <div id="box2-handshake-status" style="font-size:0.75rem; color:var(--muted);">Handshake: Waiting</div>
        </div>
      </div>
    </div>

    <!-- ==================== TAB 1: STATISTICS & FINANCIAL ANALYTICS ==================== -->
    <section id="tab-stats" class="tab-content active">
      <div class="stats-layout">
        <div class="stat-grid">
          <div class="stat-tile">
            <div class="stat-label">Users Served</div>
            <div id="stat-sessions" class="stat-value">0</div>
          </div>
          <div class="stat-tile">
            <div class="stat-label">Total Revenue</div>
            <div id="stat-revenue" class="stat-value" style="color:var(--success);">PHP 0.00</div>
          </div>
          <div class="stat-tile">
            <div class="stat-label">Avg. Cycle Duration</div>
            <div id="stat-duration" class="stat-value" style="color:var(--warning);">00:00</div>
          </div>
          <div class="stat-tile">
            <div class="stat-label">Total Coins Inserted</div>
            <div id="stat-coins" class="stat-value">0</div>
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title">Revenue — Last 10 Completed Cycles</span>
            <button class="btn-sec" onclick="resetStats()">Reset Demo Data</button>
          </div>
          <div id="stat-chart" class="chart-wrap">
            <span style="color:var(--muted); font-size:0.8rem;">No completed cycles yet.</span>
          </div>
        </div>
      </div>
    </section>

    <!-- ==================== TAB 2: BOX 1 (NODE A1 + NODE A2 SIDE BY SIDE) ==================== -->
    <section id="tab-box1" class="tab-content">
      <div class="box-split">
        <div class="box-col">
          <div class="box-col-title">📱 Node A1 — Terminal</div>
          <div class="card">
            <div class="card-header"><span class="card-title">Allan Coin Slot (MED Mode)</span></div>
            <div class="row"><span class="label">Raw Pulse Count:</span><span id="a1-pulses" class="val" style="font-size:1.1rem; color:var(--warning);">0</span></div>
            <div class="row"><span class="label">Calculated Credit:</span><span id="a1-credit" class="val" style="font-size:1.1rem; color:var(--success);">PHP 0.00</span></div>
            <div class="row" style="margin-top:12px;">
              <span class="label">Seconds per Coin:</span>
              <input type="number" id="sec-per-coin" value="20" min="5" max="300">
              <button onclick="saveSecPerCoin()">Save</button>
            </div>
            <div style="margin-top:12px;"><button class="btn-sec" style="width:100%;" onclick="sendCmdA1(3, 0)">Reset Coin Counter</button></div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">ST7789 TFT & Touch Calibration</span></div>
            <div class="row"><span class="label">Touch Coordinates:</span><span id="a1-touch" class="val">X:0 Y:0 (IDLE)</span></div>
            <div class="row" style="margin-top:14px;">
              <span class="label">Live Color Stream:</span>
              <div style="display:flex; align-items:center; gap:8px;">
                <input type="color" id="tft-color" value="#000080" onchange="sendTFTColor(this.value)">
                <span id="color-hex" class="val" style="font-family:monospace;">#000080</span>
              </div>
            </div>
            <div style="margin-top:16px;"><button style="width:100%;" onclick="sendCmdA1(2, 250)">Trigger Buzzer (250ms Test)</button></div>
          </div>
        </div>

        <div class="box-col">
          <div class="box-col-title">⚡ Node A2 — Actuator Hub</div>
          <div class="card">
            <div class="card-header"><span class="card-title">Sensors & Safety Handshake</span></div>
            <div class="row"><span class="label">Helmet Distance (US2):</span><span id="a2-helmet" class="val">0.0 cm</span></div>
            <div class="row"><span class="label">Alcohol Tank Level (US1):</span><span id="a2-alcohol" class="val">0.0 cm</span></div>
            <div class="row"><span class="label">Enclosure Door (P27):</span><span id="a2-door-enc" class="val">--</span></div>
            <div class="row"><span class="label">Maintenance Panel (P14):</span><span id="a2-door-pan" class="val">--</span></div>
            <div class="row"><span class="label">Service Backdoor (P19):</span><span id="a2-door-bak" class="val">--</span></div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Manual Relay Testing</span></div>
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:8px;">
              <button id="r-0" class="btn-sec" onclick="toggleRelay(0)">Enc Lock (P4)</button>
              <button id="r-1" class="btn-sec" onclick="toggleRelay(1)">Pan Lock (P16)</button>
              <button id="r-2" class="btn-sec" onclick="toggleRelay(2)">Bak Lock (P17)</button>
              <button id="r-3" class="btn-sec" onclick="toggleRelay(3)">Mist Pump (P18)</button>
              <button id="r-4" class="btn-sec" onclick="toggleRelay(4)" style="grid-column: span 2;">UV Light Strip (P5)</button>
            </div>
          </div>
        </div>
      </div>
    </section>

    <!-- ==================== TAB 3: BOX 2 / HEATER (NODE C1 + NODE C2 SIDE BY SIDE) ==================== -->
    <section id="tab-box2" class="tab-content">
      <div class="box-split">
        <div class="box-col">
          <div class="box-col-title">📱 Node C1 — Terminal</div>
          <div class="card">
            <div class="card-header"><span class="card-title">Allan Coin Slot (MED Mode)</span></div>
            <div class="row"><span class="label">Raw Pulse Count:</span><span id="c1-pulses" class="val" style="font-size:1.1rem; color:var(--warning);">0</span></div>
            <div class="row"><span class="label">Calculated Credit:</span><span id="c1-credit" class="val" style="font-size:1.1rem; color:var(--success);">PHP 0.00</span></div>
            <div class="row" style="margin-top:12px;">
              <span class="label">Seconds per Coin:</span>
              <input type="number" id="sec-per-coin-c2" value="20" min="5" max="300">
              <button onclick="saveSecPerCoinC2()">Save</button>
            </div>
            <div style="margin-top:12px;"><button class="btn-sec" style="width:100%;" onclick="sendCmdC1(3, 0)">Reset Coin Counter</button></div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">ST7789 TFT & Touch</span></div>
            <div class="row"><span class="label">Touch Coordinates:</span><span id="c1-touch" class="val">X:0 Y:0 (IDLE)</span></div>
            <div style="margin-top:16px;"><button style="width:100%;" onclick="sendCmdC1(2, 250)">Trigger Buzzer (250ms Test)</button></div>
          </div>
        </div>

        <div class="box-col">
          <div class="box-col-title">⚡ Node C2 — Actuator Hub</div>
          <div class="card">
            <div class="card-header"><span class="card-title">Sensors & Safety Handshake</span></div>
            <div class="row"><span class="label">Helmet Distance (US):</span><span id="c2-helmet" class="val">0.0 cm</span></div>
            <div class="row"><span class="label">Enclosure Door (P27):</span><span id="c2-door-enc" class="val">--</span></div>
            <div class="row"><span class="label">Maintenance Panel (P14):</span><span id="c2-door-pan" class="val">--</span></div>
            <div class="row"><span class="label">Service Backdoor (P19):</span><span id="c2-door-bak" class="val">--</span></div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Manual Relay Testing</span></div>
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:8px;">
              <button id="c2-r-0" class="btn-sec" onclick="toggleC2Relay(0)">Enc Lock (P4)</button>
              <button id="c2-r-1" class="btn-sec" onclick="toggleC2Relay(1)">Pan Lock (P16)</button>
              <button id="c2-r-2" class="btn-sec" onclick="toggleC2Relay(2)">Bak Lock (P17)</button>
              <button id="c2-r-3" class="btn-sec" onclick="toggleC2Relay(3)">Heater (P18)</button>
              <button id="c2-r-4" class="btn-sec" onclick="toggleC2Relay(4)">UV Light (P5)</button>
              <button id="c2-r-5" class="btn-sec" onclick="toggleC2Relay(5)">Fan (P23)</button>
            </div>
          </div>
        </div>
      </div>
    </section>

    <!-- ==================== TAB 4: ALCOHOL CONTAINER SYSTEM (D1) ==================== -->
    <section id="tab-acs" class="tab-content">
      <div class="grid-2">
        <div class="card">
          <div class="card-header"><span class="card-title">Tank Levels</span></div>
          <div class="row"><span class="label">Water:</span><span id="acs-water" class="val">-- cm</span></div>
          <div class="row"><span class="label">Scented Liquid:</span><span id="acs-scented" class="val">-- cm</span></div>
          <div class="row"><span class="label">Alcohol:</span><span id="acs-alcohol" class="val">-- cm</span></div>
          <div class="row"><span class="label">Mixer:</span><span id="acs-mixer" class="val">-- cm</span></div>
          <div class="row" style="margin-top:12px;"><span class="label">ACS Status:</span><span id="acs-busy" class="badge">IDLE</span></div>
        </div>

        <div class="card">
          <div class="card-header"><span class="card-title">Manual Relay Testing (one at a time only)</span></div>
          <div style="display:grid; grid-template-columns: 1fr 1fr; gap:8px;">
            <button id="acs-r-1" class="btn-sec" onclick="toggleACSRelay(1)">Water Pump (P21)</button>
            <button id="acs-r-2" class="btn-sec" onclick="toggleACSRelay(2)">Scented Pump (P19)</button>
            <button id="acs-r-3" class="btn-sec" onclick="toggleACSRelay(3)">Alcohol Pump (P18)</button>
            <button id="acs-r-4" class="btn-sec" onclick="toggleACSRelay(4)">Mixer Pump (P5)</button>
            <button id="acs-r-5" class="btn-sec" onclick="toggleACSRelay(5)" style="grid-column: span 2;">Mixing Machine (P25)</button>
          </div>
          <div style="margin-top:14px;">
            <button style="width:100%; background:var(--warning); color:#000;" onclick="toggleACSRelay(0)">Open Side Lock (P23) - Auto-Relocks in 5s</button>
          </div>
          <div style="margin-top:10px;">
            <button id="acs-maint-btn" style="width:100%; background:var(--danger); color:#fff;" onclick="toggleACSMaintenance()">Enable Maintenance Mode (Flush / Transport Prep)</button>
          </div>
        </div>
      </div>
    </section>

    <!-- ==================== TAB 5: DYNAMIC SETTINGS ==================== -->
    <section id="tab-settings" class="tab-content">
      <div class="grid-2">
        <div class="card">
          <div class="card-header"><span class="card-title">Coin Economy</span></div>
          <div class="row"><span class="label">Coin Value (PHP per pulse):</span>
            <input type="number" id="cfg-coin-value" value="1" min="0.5" step="0.5">
          </div>
          <div class="row"><span class="label">Box 1: Minimum Coins Required to Start:</span>
            <input type="number" id="cfg-min-coins" value="0" min="0" max="50">
          </div>
          <div class="row"><span class="label">Box 1: Maximum Coins Allowed:</span>
            <input type="number" id="cfg-max-coins" value="0" min="0" max="50">
          </div>
          <div class="row"><span class="label">Box 2: Minimum Coins Required to Start:</span>
            <input type="number" id="cfg-min-coins-c2" value="0" min="0" max="50">
          </div>
          <div class="row"><span class="label">Box 2: Maximum Coins Allowed:</span>
            <input type="number" id="cfg-max-coins-c2" value="0" min="0" max="50">
          </div>
          <div class="row"><span class="label">Box 2: Cool Down Duration:</span>
            <select id="cfg-cooldown-ratio-c2">
              <option value="1">Full (same as Heating time)</option>
              <option value="0.75">Three Quarter (0.75x Heating time)</option>
              <option value="0.5">Half (0.5x Heating time)</option>
              <option value="0.25">Quarter (0.25x Heating time)</option>
            </select>
          </div>
          <div class="row" style="margin-top:6px;"><span class="label" style="font-size:0.75rem; color:var(--muted);">Each box won't leave Welcome/Idle until its own minimum is inserted. 0 = any single coin works. Independent per box - Seconds per Coin for each is set on that box's own tab. Each box's Maximum caps how many coins add cycle time (Box 1: rejects the Time Allot selection instead of letting it through; Box 2: extra coins are discarded, not refunded or blocked - no relay yet on the Allan Coin Slot to physically stop it). 0 = no cap. Box 2's Cool Down runs the Fan for this fraction of however long Heating actually ran (e.g. Half on a 20-minute Heating session = 10 minutes of Cool Down) - lets you dial in the shortest wait that still hands back a wearable, not-too-hot helmet.</span></div>
        </div>

        <div class="card">
          <div class="card-header"><span class="card-title">Helmet Detection Distance</span></div>
          <div class="row"><span class="label">Box 1 / Node A2 (cm):</span>
            <input type="number" id="cfg-helmet-a2" value="15" min="1" max="200">
          </div>
          <div class="row"><span class="label">Box 2 / Node C2 (cm):</span>
            <input type="number" id="cfg-helmet-c2" value="30" min="1" max="200">
          </div>
          <div class="row" style="margin-top:6px;"><span class="label" style="font-size:0.75rem; color:var(--muted);">Different mounting angles per box - tune independently.</span></div>
        </div>

        <div class="card">
          <div class="card-header"><span class="card-title">Box 1 Alcohol Tank</span></div>
          <div class="row"><span class="label">Low Threshold (%):</span>
            <input type="number" id="cfg-alc-low" value="10" min="0" max="100">
          </div>
          <div class="row"><span class="label">Refill-Complete Threshold (%):</span>
            <input type="number" id="cfg-alc-high" value="90" min="0" max="100">
          </div>
        </div>

        <div class="card">
          <div class="card-header"><span class="card-title">ACS Tank Thresholds</span></div>
          <div class="row"><span class="label">Low Threshold (cm):</span>
            <input type="number" id="cfg-acs-low" value="11" min="1" max="50" step="0.1">
          </div>
          <div class="row"><span class="label">Full/Safe Threshold (cm):</span>
            <input type="number" id="cfg-acs-full" value="3.3" min="0" max="50" step="0.1">
          </div>
          <div class="row" style="margin-top:6px;"><span class="label" style="font-size:0.75rem; color:var(--muted);">Applies to Water/Scented/Alcohol/Mixer tanks alike. Pushed live to Node ACS.</span></div>
        </div>

        <div class="card" style="grid-column: 1 / -1;">
          <div class="card-header"><span class="card-title">ACS Mix Ratio</span></div>
          <div style="display:grid; grid-template-columns: 1fr 1fr 1fr; gap:12px;">
            <div class="row" style="flex-direction:column; align-items:flex-start; gap:4px;"><span class="label">Alcohol (%)</span>
              <input type="number" id="cfg-mix-alcohol" value="70" min="0" max="100" style="width:100%;">
            </div>
            <div class="row" style="flex-direction:column; align-items:flex-start; gap:4px;"><span class="label">Water (%)</span>
              <input type="number" id="cfg-mix-water" value="28" min="0" max="100" style="width:100%;">
            </div>
            <div class="row" style="flex-direction:column; align-items:flex-start; gap:4px;"><span class="label">Scented Liquid (%)</span>
              <input type="number" id="cfg-mix-scented" value="2" min="0" max="100" style="width:100%;">
            </div>
          </div>
          <div style="margin-top:10px;"><span class="label" style="font-size:0.75rem; color:var(--muted);">Automatically normalized to sum to 100% on save, regardless of what's typed - no need to make these add up exactly.</span></div>
        </div>
      </div>

      <div style="margin-top:16px;">
        <button style="width:100%; background:var(--success); color:#fff; padding:14px; font-size:1rem;" onclick="saveDynamicConfig()">Save All Settings</button>
      </div>
    </section>

  </main>

  <div id="toast">Saved Successfully!</div>
  <script src="/script.js"></script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
