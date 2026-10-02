#ifndef SCRIPT_JS_H
#define SCRIPT_JS_H

#include <Arduino.h>

const char SCRIPT_JS[] PROGMEM = R"rawliteral(
let ws = new WebSocket(`ws://${location.host}/ws`);
let relayStates = [false, false, false, false, false];
let acsRelayStates = [false, false, false, false, false, false];
let relayStatesC2 = [false, false, false, false, false, false];
const COIN_VALUE_PESO = 1.0; // 1 pulse = PHP 1

// -------------------------------------------------------------
// Navigation & SPA Handling
// -------------------------------------------------------------
function toggleSidebar() {
  document.getElementById('sidebar').classList.toggle('active');
  document.getElementById('sidebar-overlay').classList.toggle('active');
}

function switchTab(tabId, el) {
  document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(item => item.classList.remove('active'));
  document.getElementById(tabId).classList.add('active');
  el.classList.add('active');
  toggleSidebar();
}

// -------------------------------------------------------------
// Node A1 Direct Commands
// -------------------------------------------------------------
function sendTFTColor(hex) {
  document.getElementById('color-hex').innerText = hex.toUpperCase();
  let r = parseInt(hex.substr(1, 2), 16);
  let g = parseInt(hex.substr(3, 2), 16);
  let b = parseInt(hex.substr(5, 2), 16);
  let rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 1, cmd: 1, color: rgb565 }));
  }
}

function sendCmdA1(cmd, param) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 1, cmd: cmd, param: param }));
    if (cmd === 2) showToast("Buzzer Triggered (250ms)");
    if (cmd === 3) showToast("Coin Pulses Reset");
  }
}

function saveSecPerCoin() {
  const val = parseInt(document.getElementById('sec-per-coin').value);
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ cmd: "save_spc", val: val }));
    showToast("Seconds per Coin Saved!");
  }
}

function saveSecPerCoinC2() {
  const val = parseInt(document.getElementById('sec-per-coin-c2').value);
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ cmd: "save_spc_c2", val: val }));
    showToast("Seconds per Coin Saved!");
  }
}

// -------------------------------------------------------------
// Dynamic Settings (thresholds, ratios, coin economy)
// -------------------------------------------------------------
// Set the moment any Settings field is touched, cleared only after a successful save.
// Without this, the per-field "skip while focused" guard in renderDynamicConfig() still
// leaves a real gap: clicking away from a field to reach the Save button un-focuses it,
// and if a 1Hz WS update lands in that instant (before the click itself registers), it
// silently reverts the just-typed value back to the server's stale copy - so Save ends up
// sending the reverted value instead of what was actually typed.
let settingsFieldsTouched = false;
// focusin bubbles (plain focus doesn't), so one listener on the tab covers every input in it.
const settingsTabEl = document.getElementById('tab-settings');
if (settingsTabEl) settingsTabEl.addEventListener('focusin', () => { settingsFieldsTouched = true; });

function saveDynamicConfig() {
  if (ws.readyState !== WebSocket.OPEN) return;
  ws.send(JSON.stringify({
    cmd: "save_config",
    coin_value:       parseFloat(document.getElementById('cfg-coin-value').value),
    min_coins_required: parseInt(document.getElementById('cfg-min-coins').value),
    max_coins_allowed: parseInt(document.getElementById('cfg-max-coins').value),
    min_coins_required_c2: parseInt(document.getElementById('cfg-min-coins-c2').value),
    max_coins_allowed_c2: parseInt(document.getElementById('cfg-max-coins-c2').value),
    cool_down_ratio_c2: parseFloat(document.getElementById('cfg-cooldown-ratio-c2').value),
    helmet_a2_cm:     parseFloat(document.getElementById('cfg-helmet-a2').value),
    helmet_c2_cm:     parseFloat(document.getElementById('cfg-helmet-c2').value),
    alc_low_pct:      parseFloat(document.getElementById('cfg-alc-low').value),
    alc_high_pct:     parseFloat(document.getElementById('cfg-alc-high').value),
    acs_low_cm:       parseFloat(document.getElementById('cfg-acs-low').value),
    acs_full_cm:      parseFloat(document.getElementById('cfg-acs-full').value),
    mix_alcohol_pct:  parseFloat(document.getElementById('cfg-mix-alcohol').value),
    mix_water_pct:    parseFloat(document.getElementById('cfg-mix-water').value),
    mix_scented_pct:  parseFloat(document.getElementById('cfg-mix-scented').value)
  }));
  settingsFieldsTouched = false; // safe to resume live-syncing - values now match what was just saved
  showToast("Settings Saved & Pushed!");
}

// Populates the Settings tab's inputs from live telemetry - but not at all once the user has
// touched any field in this tab, until the next successful save (see settingsFieldsTouched
// above). The per-field focus check alone isn't enough to prevent the click-away race.
function renderDynamicConfig(d) {
  if (settingsFieldsTouched) return;
  const setIfPresent = (id, val) => {
    const el = document.getElementById(id);
    if (el && val !== undefined && document.activeElement !== el) el.value = val;
  };
  setIfPresent('cfg-coin-value',  d.cfg_coin_value);
  setIfPresent('cfg-min-coins', d.cfg_min_coins_required);
  setIfPresent('cfg-max-coins', d.cfg_max_coins_allowed);
  setIfPresent('cfg-min-coins-c2', d.cfg_min_coins_required_c2);
  setIfPresent('cfg-max-coins-c2', d.cfg_max_coins_allowed_c2);
  setIfPresent('cfg-cooldown-ratio-c2', d.cfg_cool_down_ratio_c2);
  setIfPresent('cfg-helmet-a2',   d.cfg_helmet_a2_cm);
  setIfPresent('cfg-helmet-c2',   d.cfg_helmet_c2_cm);
  setIfPresent('cfg-alc-low',     d.cfg_alc_low_pct);
  setIfPresent('cfg-alc-high',    d.cfg_alc_high_pct);
  setIfPresent('cfg-acs-low',     d.cfg_acs_low_cm);
  setIfPresent('cfg-acs-full',    d.cfg_acs_full_cm);
  setIfPresent('cfg-mix-alcohol', Math.round(d.cfg_mix_alcohol_pct * 10) / 10);
  setIfPresent('cfg-mix-water',   Math.round(d.cfg_mix_water_pct * 10) / 10);
  setIfPresent('cfg-mix-scented', Math.round(d.cfg_mix_scented_pct * 10) / 10);
}

// -------------------------------------------------------------
// Node C1 Direct Commands (Box 2 Terminal)
// -------------------------------------------------------------
function sendCmdC1(cmd, param) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 4, cmd: cmd, param: param }));
    if (cmd === 2) showToast("Buzzer Triggered (250ms)");
    if (cmd === 3) showToast("Coin Pulses Reset");
  }
}

// -------------------------------------------------------------
// Node A2 Relay / Solenoid Control
// -------------------------------------------------------------
function toggleRelay(idx) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 2, cmd: 4, relayIdx: idx, state: relayStates[idx] ? 0 : 1 }));
  }
}

function toggleC2Relay(idx) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 5, cmd: 4, relayIdx: idx, state: relayStatesC2[idx] ? 0 : 1 }));
  }
}

function toggleACSRelay(idx) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ target: 3, cmd: 4, relayIdx: idx, state: acsRelayStates[idx] ? 0 : 1 }));
  }
}

let acsMaintenanceMode = false;

function toggleACSMaintenance() {
  if (ws.readyState !== WebSocket.OPEN) return;
  const turningOn = !acsMaintenanceMode;
  if (turningOn && !confirm('Enable Maintenance Mode? This pauses the automatic Mixer refill until turned off again.')) return;
  ws.send(JSON.stringify({ cmd: "acs_maintenance", state: turningOn ? 1 : 0 }));
}

// -------------------------------------------------------------
// Statistics & Financial Analytics Dashboard
// -------------------------------------------------------------
function renderStats(d) {
  document.getElementById('stat-sessions').innerText = d.stat_sessions;
  document.getElementById('stat-revenue').innerText = `PHP ${Number(d.stat_revenue).toFixed(2)}`;
  document.getElementById('stat-coins').innerText = d.stat_coins;

  const avgMin = String(Math.floor(d.stat_avg_dur / 60)).padStart(2, '0');
  const avgSec = String(d.stat_avg_dur % 60).padStart(2, '0');
  document.getElementById('stat-duration').innerText = `${avgMin}:${avgSec}`;

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
}

function resetStats() {
  if (ws.readyState !== WebSocket.OPEN) return;
  if (!confirm('Reset all statistics counters? This cannot be undone.')) return;
  ws.send(JSON.stringify({ cmd: "reset_stats" }));
  showToast("Statistics Reset!");
}

// -------------------------------------------------------------
// Utility Helpers
// -------------------------------------------------------------
function showToast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 2200);
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function setNodeBadge(id, isOnline) {
  const el = document.getElementById(id);
  el.innerText = isOnline ? 'ONLINE' : 'OFFLINE';
  el.className = isOnline ? 'badge online' : 'badge';
}

function setDoorBadge(id, isOpen) {
  const el = document.getElementById(id);
  el.innerText = isOpen ? 'OPEN' : 'CLOSED';
  el.className = isOpen ? 'val door-open' : 'val door-closed';
}

// -------------------------------------------------------------
// WebSocket Telemetry Handlers
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
  try { d = JSON.parse(evt.data); } catch(e) { return; }

  document.getElementById('hdr-ram').innerText = `${(d.heap / 1024).toFixed(0)} KB`;
  const psramEl = document.getElementById('hdr-psram');
  if (psramEl) {
    psramEl.innerText = d.free_psram > 0 ? `${(d.free_psram / 1024 / 1024).toFixed(2)} MB` : '0 (not detected!)';
    psramEl.style.color = d.free_psram > 0 ? 'var(--success)' : 'var(--danger)';
  }
  setNodeBadge('sb-a1-status', d.a1_online);
  setNodeBadge('sb-a2-status', d.a2_online);

  document.getElementById('mach-state').innerText = `STATE: ${d.step_name}`;
  document.getElementById('mach-step').innerText = `Alcohol Tank: ${d.a2_alc_pct.toFixed(0)}%`;

  const mins = String(Math.floor(d.active_timer / 60)).padStart(2, '0');
  const secs = String(d.active_timer % 60).padStart(2, '0');
  document.getElementById('active-timer').innerText = `${mins}:${secs}`;

  const hs = document.getElementById('handshake-status');
  hs.innerText = d.handshake_ok ? 'Handshake: READY' : 'Handshake: WAITING (Door/Dist)';
  hs.style.color = d.handshake_ok ? 'var(--success)' : 'var(--warning)';

  const spcInput = document.getElementById('sec-per-coin');
  if (spcInput && document.activeElement !== spcInput) spcInput.value = d.sec_per_coin;
  const spcC2Input = document.getElementById('sec-per-coin-c2');
  if (spcC2Input && document.activeElement !== spcC2Input) spcC2Input.value = d.sec_per_coin_c2;

  document.getElementById('a1-pulses').innerText = d.a1_pulses;
  document.getElementById('a1-credit').innerText = `PHP ${(d.a1_pulses * COIN_VALUE_PESO).toFixed(2)}`;
  document.getElementById('a1-touch').innerText = d.a1_tp ? `X:${d.a1_tx} Y:${d.a1_ty} (TOUCH)` : `X:${d.a1_tx} Y:${d.a1_ty} (IDLE)`;

  document.getElementById('a2-helmet').innerText = `${d.a2_us_helm.toFixed(1)} cm`;
  document.getElementById('a2-alcohol').innerText = `${d.a2_us_alc.toFixed(1)} cm (~${d.a2_alc_pct.toFixed(0)}%)`;
  setDoorBadge('a2-door-enc', d.a2_m_enc);
  setDoorBadge('a2-door-pan', d.a2_m_pan);
  setDoorBadge('a2-door-bak', d.a2_m_bak);

  relayStates = [d.a2_r_enc, d.a2_r_pan, d.a2_r_bak, d.a2_r_hum, d.a2_r_uv];
  for (let i = 0; i < 5; i++) {
    const btn = document.getElementById(`r-${i}`);
    if (btn) btn.className = relayStates[i] ? 'relay-on' : 'btn-sec';
  }

  setNodeBadge('sb-c1-status', d.c1_online);
  setNodeBadge('sb-c2-status', d.c2_online);

  document.getElementById('box2-mach-state').innerText = `STATE: ${d.box2_step_name}`;
  const b2mins = String(Math.floor(d.box2_active_timer / 60)).padStart(2, '0');
  const b2secs = String(d.box2_active_timer % 60).padStart(2, '0');
  document.getElementById('box2-active-timer').innerText = `${b2mins}:${b2secs}`;
  const b2hs = document.getElementById('box2-handshake-status');
  b2hs.innerText = d.box2_handshake_ok ? 'Handshake: READY' : 'Handshake: WAITING (Door/Dist)';
  b2hs.style.color = d.box2_handshake_ok ? 'var(--success)' : 'var(--warning)';

  document.getElementById('c1-pulses').innerText = d.c1_pulses;
  document.getElementById('c1-credit').innerText = `PHP ${(d.c1_pulses * COIN_VALUE_PESO).toFixed(2)}`;
  document.getElementById('c1-touch').innerText = d.c1_tp ? `X:${d.c1_tx} Y:${d.c1_ty} (TOUCH)` : `X:${d.c1_tx} Y:${d.c1_ty} (IDLE)`;

  document.getElementById('c2-helmet').innerText = `${d.c2_us_helm.toFixed(1)} cm`;
  setDoorBadge('c2-door-enc', d.c2_m_enc);
  setDoorBadge('c2-door-pan', d.c2_m_pan);
  setDoorBadge('c2-door-bak', d.c2_m_bak);

  relayStatesC2 = [d.c2_r_enc, d.c2_r_pan, d.c2_r_bak, d.c2_r_heat, d.c2_r_uv, d.c2_r_fan];
  for (let i = 0; i < 6; i++) {
    const btn = document.getElementById(`c2-r-${i}`);
    if (btn) btn.className = relayStatesC2[i] ? 'relay-on' : 'btn-sec';
  }

  setNodeBadge('sb-acs-status', d.acs_online);
  setNodeBadge('sb-s3a-status', d.s3a_online);
  setNodeBadge('sb-s3c-status', d.s3c_online);
  setLowVal('acs-water', d.acs_water, d.acs_water_low);
  setLowVal('acs-scented', d.acs_scented, d.acs_scented_low);
  setLowVal('acs-alcohol', d.acs_alcohol, d.acs_alcohol_low);
  setLowVal('acs-mixer', d.acs_mixer, d.acs_mixer_low);

  acsMaintenanceMode = !!d.acs_maint;
  const maintBtn = document.getElementById('acs-maint-btn');
  if (maintBtn) {
    maintBtn.innerText = acsMaintenanceMode ? 'Disable Maintenance Mode (Resume Automatic Refill)' : 'Enable Maintenance Mode (Flush / Transport Prep)';
    maintBtn.style.background = acsMaintenanceMode ? 'var(--success)' : 'var(--danger)';
  }

  const busyBadge = document.getElementById('acs-busy');
  let acsStatusText = d.acs_auto || (d.acs_busy ? 'BUSY' : 'IDLE');
  if (d.acs_auto === 'Waiting for Ingredients') {
    const low = [];
    if (d.acs_water_low) low.push('Water');
    if (d.acs_scented_low) low.push('Scented');
    if (d.acs_alcohol_low) low.push('Alcohol');
    if (low.length) acsStatusText = `Needs Refill: ${low.join(', ')}`;
  }
  if (acsMaintenanceMode) acsStatusText = 'MAINTENANCE MODE';
  busyBadge.innerText = acsStatusText;
  busyBadge.className = acsMaintenanceMode || (d.acs_auto && d.acs_auto !== 'Idle') || d.acs_busy ? 'badge' : 'badge online';

  acsRelayStates = d.acs_relays || [false, false, false, false, false, false];
  for (let i = 1; i < 6; i++) {
    const btn = document.getElementById(`acs-r-${i}`);
    if (btn) btn.className = acsRelayStates[i] ? 'relay-on' : 'btn-sec';
  }

  updateRefillBanner(d);
  renderStats(d);
  renderDynamicConfig(d);
};

// Visible on every tab - lists anything currently needing a refill, hidden otherwise.
function updateRefillBanner(d) {
  const items = [];
  if (d.acs_water_low) items.push('ACS Water');
  if (d.acs_scented_low) items.push('ACS Scented Liquid');
  if (d.acs_alcohol_low) items.push('ACS Alcohol');
  if (d.acs_mixer_low) items.push('ACS Mixer');
  if (d.a2_alc_pct <= 10) items.push('Humidifier Tank (A2)');

  const banner = document.getElementById('refill-banner');
  if (items.length > 0) {
    document.getElementById('refill-banner-text').innerText = `Refill Needed: ${items.join(', ')}`;
    banner.style.display = 'flex';
  } else {
    banner.style.display = 'none';
  }
}

function setLowVal(id, distCm, isLow) {
  const el = document.getElementById(id);
  if (!el || distCm === undefined) return;
  el.innerText = `${distCm.toFixed(1)} cm${isLow ? ' (LOW)' : ''}`;
  el.style.color = isLow ? 'var(--danger)' : 'var(--text)';
}
)rawliteral";

#endif // SCRIPT_JS_H
