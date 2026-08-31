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
    </nav>
    <div class="sidebar-footer">
      <div class="node-status-row"><span>Node A1 (Terminal):</span><span id="sb-a1-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node A2 (Actuators):</span><span id="sb-a2-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node C1 (Terminal):</span><span id="sb-c1-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node C2 (Actuators):</span><span id="sb-c2-status" class="badge">OFFLINE</span></div>
      <div class="node-status-row"><span>Node ACS (D1):</span><span id="sb-acs-status" class="badge">OFFLINE</span></div>
    </div>
  </aside>

  <!-- Main Content Container -->
  <main class="container">

    <!-- Refill Notification - visible on every tab, hidden when nothing needs attention -->
    <div id="refill-banner" class="refill-banner" style="display:none;">
      <span>⚠️</span> <span id="refill-banner-text"></span>
    </div>

    <!-- Machine State Banner -->
    <div class="state-banner">
      <div>
        <div class="label">State Machine Engine</div>
        <div id="mach-state" class="state-title">STATE: IDLE</div>
        <div id="mach-step" style="color:var(--accent); font-size:0.9rem; font-weight:600; margin-top:2px;">Step: None</div>
      </div>
      <div style="text-align:right;">
        <div class="label">Accumulating Timer</div>
        <div id="active-timer" class="timer-display">00:00</div>
        <div id="handshake-status" style="font-size:0.75rem; color:var(--muted);">Handshake: Waiting</div>
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
      <div class="state-banner" style="margin-bottom:16px;">
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

      <div class="box-split">
        <div class="box-col">
          <div class="box-col-title">📱 Node C1 — Terminal</div>
          <div class="card">
            <div class="card-header"><span class="card-title">Allan Coin Slot (MED Mode)</span></div>
            <div class="row"><span class="label">Raw Pulse Count:</span><span id="c1-pulses" class="val" style="font-size:1.1rem; color:var(--warning);">0</span></div>
            <div class="row"><span class="label">Calculated Credit:</span><span id="c1-credit" class="val" style="font-size:1.1rem; color:var(--success);">PHP 0.00</span></div>
            <div class="row" style="margin-top:12px;"><span class="label" style="font-size:0.75rem; color:var(--muted);">Shares the same Seconds-per-Coin setting as Box 1 (see Box 1 tab).</span></div>
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

  </main>

  <div id="toast">Saved Successfully!</div>
  <script src="/script.js"></script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
