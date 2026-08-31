#ifndef STYLE_CSS_H
#define STYLE_CSS_H

#include <Arduino.h>

const char STYLE_CSS[] PROGMEM = R"rawliteral(
:root {
  --bg: #090d16;
  --card: #111827;
  --card-border: #1f293d;
  --sidebar-bg: #0f172a;
  --accent: #38bdf8;
  --accent-glow: rgba(56, 189, 248, 0.25);
  --text: #f8fafc;
  --muted: #94a3b8;
  --success: #22c55e;
  --danger: #ef4444;
  --warning: #f59e0b;
  --node-bg: #0d1322;
  --sub-card: #080d1a;
  --modal-bg: #0f172a;
}

* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace, sans-serif; }
body { background-color: var(--bg); color: var(--text); overflow-x: hidden; }

/* Top Header Bar */
.top-header {
  position: sticky; top: 0; z-index: 50;
  background: rgba(15, 23, 42, 0.92); backdrop-filter: blur(8px);
  border-bottom: 1px solid var(--card-border);
  padding: 10px 16px; display: flex; justify-content: space-between; align-items: center;
}
.header-left { display: flex; align-items: center; gap: 12px; }
.burger-btn {
  background: transparent; border: none; color: var(--accent); font-size: 1.4rem;
  cursor: pointer; padding: 4px 8px; border-radius: 6px; display: flex; align-items: center;
}
.burger-btn:hover { background: var(--card-border); }
.app-title { font-size: 1.05rem; font-weight: 800; letter-spacing: 0.5px; }
.app-title span { color: var(--accent); }

/* Health Capsule */
.health-capsule {
  display: flex; align-items: center; gap: 8px;
  background: var(--card); border: 1px solid var(--card-border);
  padding: 4px 10px; border-radius: 9999px; font-size: 0.72rem; font-weight: 600;
}
.badge { padding: 3px 8px; border-radius: 9999px; font-size: 0.7rem; font-weight: 700; background: var(--danger); }
.badge.online { background: var(--success); }

/* Sidebar Drawer Navigation */
.sidebar-overlay { position: fixed; inset: 0; background: rgba(0, 0, 0, 0.65); z-index: 99; opacity: 0; pointer-events: none; transition: opacity 0.25s; }
.sidebar-overlay.active { opacity: 1; pointer-events: auto; }
.sidebar {
  position: fixed; top: 0; left: 0; bottom: 0; width: 280px;
  background: var(--sidebar-bg); border-right: 1px solid var(--card-border);
  z-index: 100; transform: translateX(-100%); transition: transform 0.25s ease;
  display: flex; flex-direction: column; padding: 20px 16px;
}
.sidebar.active { transform: translateX(0); }
.sidebar-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; padding-bottom: 12px; border-bottom: 1px solid var(--card-border); }
.sidebar-title { font-size: 0.95rem; font-weight: 700; color: var(--accent); }
.close-btn { background: none; border: none; color: var(--muted); font-size: 1.2rem; cursor: pointer; }

.nav-links { display: flex; flex-direction: column; gap: 8px; flex: 1; }
.nav-item {
  display: flex; align-items: center; gap: 12px; padding: 12px 14px; border-radius: 8px;
  color: var(--muted); text-decoration: none; font-size: 0.88rem; font-weight: 600; cursor: pointer; transition: 0.15s;
}
.nav-item:hover { background: var(--card); color: var(--text); }
.nav-item.active { background: var(--accent); color: #000; font-weight: 700; box-shadow: 0 0 15px var(--accent-glow); }
.sidebar-footer { border-top: 1px solid var(--card-border); padding-top: 14px; font-size: 0.75rem; color: var(--muted); }
.node-status-row { display: flex; justify-content: space-between; margin-bottom: 6px; }

/* Main Container & State Banner */
.container { max-width: 1250px; margin: 0 auto; padding: 14px; }
.refill-banner {
  background: var(--danger); color: #fff; padding: 12px 18px; border-radius: 10px;
  margin-bottom: 14px; display: flex; align-items: center; gap: 10px;
  font-weight: 700; font-size: 0.9rem; box-shadow: 0 0 20px rgba(239, 68, 68, 0.35);
  animation: refillPulse 2s ease-in-out infinite;
}
@keyframes refillPulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.72; } }
.state-banner {
  background: var(--card); border: 2px solid var(--accent); border-radius: 12px;
  padding: 14px 18px; margin-bottom: 16px; display: flex; justify-content: space-between; align-items: center;
  box-shadow: 0 0 20px rgba(56, 189, 248, 0.1);
}
.state-title { font-size: 1.15rem; font-weight: 800; color: #fff; }
.timer-display { font-size: 1.8rem; font-weight: 800; color: var(--warning); font-family: monospace; }

/* Tab Containers */
.tab-content { display: none; }
.tab-content.active { display: block; animation: fadeIn 0.2s ease; }
@keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

/* Buttons & Inputs */
button { background: var(--accent); color: #000; border: none; padding: 8px 12px; border-radius: 6px; font-weight: 700; font-size: 0.8rem; cursor: pointer; transition: 0.15s; }
button:hover { filter: brightness(1.15); }
button:active { transform: scale(0.97); }
button.btn-sec { background: #334155; color: #fff; }
button.btn-sec:hover { background: #475569; }
button.btn-add { background: var(--success); color: #000; }
button.btn-del { background: var(--danger); color: #fff; padding: 5px 8px; font-size: 0.75rem; border-radius: 4px; }
button.btn-del:hover { background: #dc2626; }
button.relay-on { background: var(--success); color: #fff; }

select, input[type="text"], input[type="number"] {
  background: #090d16; border: 1px solid var(--card-border); color: #fff;
  padding: 7px 9px; border-radius: 4px; font-size: 0.82rem; outline: none;
}
select:focus, input:focus { border-color: var(--accent); }
input[type="color"] { -webkit-appearance: none; border: none; width: 44px; height: 32px; border-radius: 4px; cursor: pointer; background: none; }
input[type="color"]::-webkit-color-swatch { border: 1px solid var(--card-border); border-radius: 4px; }

/* Diagnostic Cards */
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 14px; }
.card { background: var(--card); border: 1px solid var(--card-border); border-radius: 10px; padding: 16px; margin-bottom: 14px; }
.card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 8px; margin-bottom: 12px; }
.card-title { font-size: 0.95rem; color: var(--accent); font-weight: 700; text-transform: uppercase; }
.row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; font-size: 0.88rem; }
.label { color: var(--muted); }
.val { font-weight: 700; }
.door-open { color: var(--danger); font-weight: 700; }
.door-closed { color: var(--success); font-weight: 700; }

/* Box 1 Split View (Node A1 | Node A2) - side by side on wide screens, stacked on narrow */
.box-split { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 20px; align-items: start; }
.box-col-title {
  font-size: 0.95rem; font-weight: 800; color: var(--accent); text-transform: uppercase;
  letter-spacing: 0.04em; margin-bottom: 10px; padding-bottom: 8px; border-bottom: 2px solid var(--accent);
}

/* ============================================================= */
/* STATISTICS & FINANCIAL ANALYTICS DASHBOARD                    */
/* ============================================================= */
.stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 14px; margin-bottom: 14px; }
.stat-tile { background: var(--card); border: 1px solid var(--card-border); border-radius: 10px; padding: 18px 14px; text-align: center; }
.stat-label { color: var(--muted); font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.04em; margin-bottom: 8px; }
.stat-value { font-size: 1.7rem; font-weight: 800; color: var(--accent); font-family: monospace; }

.chart-wrap { display: flex; align-items: flex-end; gap: 8px; min-height: 160px; padding: 10px 4px 0 4px; }
.chart-col { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: flex-end; height: 150px; gap: 6px; }
.chart-bar { width: 100%; max-width: 34px; background: var(--accent); border-radius: 4px 4px 0 0; min-height: 3px; transition: height 0.3s ease; }
.chart-val { font-size: 0.65rem; color: var(--muted); font-family: monospace; white-space: nowrap; }

#toast { position: fixed; bottom: 20px; right: 20px; background: var(--success); color: #000; padding: 10px 18px; border-radius: 6px; font-weight: 700; font-size: 0.85rem; display: none; z-index: 300; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }

/* ============================================================= */
/* DESKTOP / BIG MONITOR LAYOUT (>= 900px)                       */
/* Burger drawer stays (left side, click to open/close) so it    */
/* doesn't permanently eat into the width - the reclaimed space  */
/* goes to a wider, 16:9-friendly composition and larger         */
/* at-a-glance numbers for viewing from across a room.           */
/* ============================================================= */
@media (min-width: 900px) {
  .top-header { padding: 16px 28px; }
  .container { max-width: 1700px; margin: 0 auto; padding: 24px 36px; }

  .app-title { font-size: 1.3rem; }
  .health-capsule { font-size: 0.82rem; padding: 6px 14px; }

  .state-banner { padding: 20px 30px; margin-bottom: 18px; }
  .state-title { font-size: 1.5rem; }
  .timer-display { font-size: 2.4rem; }
  #mach-step, #handshake-status { font-size: 1rem !important; }

  .stat-tile { padding: 22px 18px; }
  .stat-value { font-size: 2rem; }
  .stat-label { font-size: 0.8rem; }

  .card { padding: 22px; margin-bottom: 20px; }
  .card-title { font-size: 1.05rem; }
  .row { font-size: 0.95rem; }

  /* 16:9-friendly Statistics layout: stat tiles (2x2) beside a wide chart, not stacked */
  .stats-layout { display: grid; grid-template-columns: 400px 1fr; gap: 20px; align-items: stretch; }
  .stats-layout .stat-grid { grid-template-columns: repeat(2, 1fr); margin-bottom: 0; height: 100%; }
  .stats-layout .card { margin-bottom: 0; display: flex; flex-direction: column; }
  .chart-wrap { min-height: 280px; flex: 1; }
  .chart-col { height: 100%; }
}

/* Portrait Mobile Responsive Adjustments (<= 600px) */
@media (max-width: 600px) {
  body { padding: 0; }
  .container { padding: 8px; }
  .top-header { flex-direction: column; align-items: flex-start; gap: 8px; padding: 10px; }
  .health-capsule { width: 100%; justify-content: space-between; font-size: 0.68rem; }
  .state-banner { flex-direction: column; align-items: flex-start; gap: 8px; text-align: left; }
  .state-banner div:last-child { text-align: left !important; width: 100%; }
  .timer-display { font-size: 1.5rem; }

  .grid-2 { grid-template-columns: 1fr; }
  .stat-grid { grid-template-columns: repeat(2, 1fr); }
  .stat-value { font-size: 1.3rem; }
}
)rawliteral";

#endif // STYLE_CSS_H
