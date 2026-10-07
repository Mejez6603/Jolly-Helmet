#ifndef SCRIPT_JS_H
#define SCRIPT_JS_H

#include <Arduino.h>

// Home page script of the Control Center (rewritten 2026-10-03). The previous script is archived in legacy_dashboard/.
const char SCRIPT_JS[] PROGMEM = R"rawliteral(
let ws = new WebSocket(`ws://${location.host}/ws`);

// Names, in the order the nodes number their states / steps (same lists as the settings pages)
const ST13 = ["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1"];
const ST2 = ["IDLE","OPEN A","SENSORS (old)","HEATING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","COOL DOWN","RATE B","CANCEL","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","HELMET NOTICE A","HELMET NOTICE B","OUT OF ORDER"];
const ACSSTEP = ["IDLE","ALC WATER IN","ALC ALCOHOL IN","SCT WATER IN","SCT SCENTED IN","DELIVER ALCOHOL","DELIVER SCENTED","WATER EMPTY","ALCOHOL EMPTY","SCENTED EMPTY","MAINTENANCE"];
const NODES = ["A1 Terminal (Box 1)","A2 Sensors (Box 1)","A3 Relays (Box 1)","S3A Brain (Box 1)","C1 Terminal (Box 2)","C2 Hub (Box 2)","S3C Brain (Box 2)","B1 Terminal (Box 3)","B2 Sensors (Box 3)","B3 Relays (Box 3)","S3B Brain (Box 3)","ACS 1 (Box 1)","ACS 2 (Box 3)"];
const WARN = [[1,'no echo from the alcohol mixer ultrasonic'],[2,'no echo from the scented mixer ultrasonic'],[4,'alcohol mixer: WL wet but the ultrasonic reads far from full'],[8,'scented mixer: WL wet but the ultrasonic reads far from full'],[16,'alcohol mixer holds a half-made mix'],[32,'scented mixer holds a half-made mix'],[64,'a pump hit its time limit']];
const PUMPS = ["alcohol pump","water pump (alc)","water pump (scn)","scented pump","mixer alc pump","mixer scn pump"];

// node status rows in the sidebar footer
const nodeList = document.getElementById('node-list');
nodeList.innerHTML = NODES.map((n, i) => `<div class="node-status-row"><span>${n}:</span><span id="nd-${i}" class="badge">OFFLINE</span></div>`).join('');

function toggleSidebar() {
  document.getElementById('sidebar').classList.toggle('active');
  document.getElementById('sidebar-overlay').classList.toggle('active');
}

function showToast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 2200);
}

function setNodeBadge(id, isOnline) {
  const el = document.getElementById(id);
  if (!el) return;
  el.innerText = isOnline ? 'ONLINE' : 'OFFLINE';
  el.className = isOnline ? 'badge online' : 'badge';
}

function mmss(sec) {
  return String(Math.floor(sec / 60)).padStart(2, '0') + ':' + String(sec % 60).padStart(2, '0');
}

function chip(t, c) { return `<span class="chip ${c || ''}">${t}</span>`; }

// -------------------------------------------------------------
// Revenue
// -------------------------------------------------------------
function renderStats(d) {
  document.getElementById('stat-sessions').innerText = d.stat_sessions;
  document.getElementById('stat-revenue').innerText = `PHP ${Number(d.stat_revenue).toFixed(2)}`;
  document.getElementById('stat-coins').innerText = d.stat_coins;
  document.getElementById('stat-duration').innerText = mmss(d.stat_avg_dur);

  const chart = document.getElementById('stat-chart');
  const hist = d.stat_history || [];
  if (hist.length === 0) {
    chart.innerHTML = '<span style="color:var(--muted); font-size:0.8rem;">No completed cycles yet.</span>';
    return;
  }
  const maxRev = Math.max(...hist.map(h => h.r), 1);
  chart.innerHTML = hist.map(h => {
    const pct = Math.max(6, Math.round((h.r / maxRev) * 100));
    return `<div class="chart-col"><div class="chart-bar" style="height:${pct}%;"></div><div class="chart-val">P${h.r.toFixed(0)}</div></div>`;
  }).join('');

  const cv = document.getElementById('coin-value');
  if (cv && document.activeElement !== cv && d.coin_value !== undefined) cv.value = d.coin_value;
}

function resetStats() {
  if (ws.readyState !== WebSocket.OPEN) return;
  if (!confirm('Reset all statistics counters? This cannot be undone.')) return;
  ws.send(JSON.stringify({ cmd: "reset_stats" }));
  showToast("Statistics Reset!");
}

function refreshScreen(n) {
  fetch(`/api/b${n}/refresh`, { cache: "no-store" }).then(() => showToast(`Screen refresh sent to Box ${n}`)).catch(() => showToast("No link"));
}

function saveCoinValue() {
  const v = parseFloat(document.getElementById('coin-value').value);
  if (ws.readyState !== WebSocket.OPEN || !(v >= 0.1)) return;
  ws.send(JSON.stringify({ cmd: "save_coin_value", val: v }));
  showToast("Coin value saved");
}

// -------------------------------------------------------------
// Boxes and ACS
// -------------------------------------------------------------
function renderBox(prefix, names, state, timer, coins, alerts, label) {
  const name = names[state] || ('state ' + state);
  document.getElementById(prefix + '-state').innerText = name;
  document.getElementById(prefix + '-coins').innerText = `Coins counted: ${coins}`;
  document.getElementById(prefix + '-timer').innerText = mmss(timer);
  if (name === 'OUT OF ORDER') alerts.push(label + ' is OUT OF ORDER');
  if (name === 'SELECT DRY ONLY') alerts.push(label + ': CLEANING is greyed out (a container is low and the refill gave up) - drying still works');
}

function renderAcs(i, a, alerts) {
  const label = 'ACS ' + (i + 1);
  const [ok, step, flags, empty, warn, pa, ps, pumps, sec] = a;
  const stepEl = document.getElementById(`acs${i}-step`);
  stepEl.innerText = ok ? (ACSSTEP[step] || step) : 'NOT HEARD';
  stepEl.className = ok && step < 7 ? 'badge online' : 'badge';
  document.getElementById(`acs${i}-pa`).style.width = (ok ? pa : 0) + '%';
  document.getElementById(`acs${i}-ps`).style.width = (ok ? ps : 0) + '%';
  let c = '';
  if (ok) {
    c += (flags & 1) ? chip('ALCOHOL MIXER LOW', 'bad') : '';
    c += (flags & 4) ? chip('SCENTED MIXER LOW', 'bad') : '';
    c += (empty & 1) ? chip('WATER EMPTY', 'bad') : '';
    c += (empty & 2) ? chip('ALCOHOL EMPTY', 'bad') : '';
    c += (empty & 4) ? chip('SCENTED EMPTY', 'bad') : '';
    c += (flags & 128) ? chip('box is asking', 'run') : '';
    PUMPS.forEach((n, k) => { if ((pumps >> k) & 1) c += chip('running: ' + n, 'run'); });
    WARN.forEach(w => { if (warn & w[0]) c += `<div class="acs-note">⚠ ${w[1]}</div>`; });
    if (step >= 7 && step <= 9) c += `<div class="acs-note">retrying in ${sec} s</div>`;
    if (empty & 1) alerts.push(label + ': WATER supply is empty');
    if (empty & 2) alerts.push(label + ': ALCOHOL supply is empty');
    if (empty & 4) alerts.push(label + ': SCENTED supply is empty');
    if (flags & 1) alerts.push(label + ': alcohol mixer container is low');
    if (flags & 4) alerts.push(label + ': scented mixer container is low');
  }
  document.getElementById(`acs${i}-chips`).innerHTML = c || '<span class="label" style="font-size:0.75rem;">No empty supply, no pump running.</span>';
}

// -------------------------------------------------------------
// WebSocket
// -------------------------------------------------------------
ws.onopen = () => {
  const b = document.getElementById('ws-badge');
  b.innerText = 'Online';
  b.className = 'badge online';
};

ws.onclose = () => {
  const b = document.getElementById('ws-badge');
  b.innerText = 'Offline';
  b.className = 'badge';
  setTimeout(() => location.reload(), 3000);
};

ws.onmessage = (evt) => {
  let d;
  try { d = JSON.parse(evt.data); } catch (e) { return; }

  document.getElementById('hdr-ram').innerText = `${(d.heap / 1024).toFixed(0)} KB`;
  const psramEl = document.getElementById('hdr-psram');
  psramEl.innerText = d.free_psram > 0 ? `${(d.free_psram / 1024 / 1024).toFixed(2)} MB` : '0 (not detected!)';
  psramEl.style.color = d.free_psram > 0 ? 'var(--success)' : 'var(--danger)';

  (d.n || []).forEach((on, i) => setNodeBadge('nd-' + i, !!on));

  const alerts = [];
  renderBox('b1', ST13, d.b1s, d.b1t, d.b1c, alerts, 'Box 1');
  renderBox('b2', ST2,  d.b2s, d.b2t, d.b2c, alerts, 'Box 2');
  renderBox('b3', ST13, d.b3s, d.b3t, d.b3c, alerts, 'Box 3');
  (d.acs || []).forEach((a, i) => renderAcs(i, a, alerts));

  const banner = document.getElementById('refill-banner');
  if (alerts.length > 0) {
    document.getElementById('refill-banner-text').innerText = alerts.join('  ·  ');
    banner.style.display = 'flex';
  } else {
    banner.style.display = 'none';
  }

  renderStats(d);

  // board health: tells a power dip (BROWNOUT) apart from a crash or a watchdog
  const diag = document.getElementById('diag');
  if (diag && d.rst !== undefined) {
    const bad = (d.rst.indexOf('BROWNOUT') >= 0);
    diag.innerHTML = `This board: up ${d.up} s · last restart: <b style="color:${bad || d.crashes > 0 ? 'var(--warning)' : 'var(--success)'}">${d.rst}</b> · unexpected restarts ${d.crashes} of ${d.boots} starts · lowest free memory ${(d.minheap / 1024).toFixed(0)} KB · radio packets dropped ${d.rxdrop} · phones connected ${d.clients}` +
      (bad ? '<br><b style="color:var(--danger)">BROWNOUT = the supply dipped: use a stronger USB supply / shorter cable for this board, or a separate supply from the pumps and relays.</b>' : '');
  }
};
)rawliteral";

#endif // SCRIPT_JS_H
