#ifndef SCRIPT_JS_H
#define SCRIPT_JS_H

#include <Arduino.h>

// Home page script of the Control Center (rewritten 2026-10-03). The previous script is archived in legacy_dashboard/.
const char SCRIPT_JS[] PROGMEM = R"rawliteral(
// The page asks the board for its status once a second (GET /api/status) and sends its commands as plain POSTs (/api/cmd). It used a websocket until 2026-10-15
// (see the LocalServer sketch for why that was dropped). linkUp is true while the board answers.
let linkUp = false;
async function sendCmd(obj) {
  const body = new URLSearchParams();
  Object.keys(obj).forEach(k => body.append(k, String(obj[k])));
  try { await fetch('/api/cmd', { method: 'POST', body }); setTimeout(pollStatus, 300); return true; }
  catch (e) { showToast('No link to the board'); return false; }
}

// Names, in the order the nodes number their states / steps (same lists as the settings pages)
const ST13 = ["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1"];
const ST2 = ["IDLE","OPEN A","SENSORS (old)","HEATING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","COOL DOWN","RATE B","CANCEL","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","HELMET NOTICE A","HELMET NOTICE B","OUT OF ORDER"];
const ACSSTEP = ["IDLE","ALC WATER IN","ALC ALCOHOL IN","SCT WATER IN","SCT SCENTED IN","DELIVER ALCOHOL","DELIVER SCENTED","WATER EMPTY","ALCOHOL EMPTY","SCENTED EMPTY","MAINTENANCE"];
const NODES = ["A1 Terminal (Box 1)","A2 Sensors (Box 1)","A3 Relays (Box 1)","S3A Brain (Box 1)","C1 Terminal (Box 2)","C2 Hub (Box 2)","S3C Brain (Box 2)","B1 Terminal (Box 3)","B2 Sensors (Box 3)","B3 Relays (Box 3)","S3B Brain (Box 3)","ACS 1 (Box 1)","ACS 2 (Box 3)"];
const WARN = [[1,'no echo from the alcohol mixer ultrasonic'],[2,'no echo from the scented mixer ultrasonic'],[4,'alcohol mixer: WL wet but the ultrasonic reads far from full'],[8,'scented mixer: WL wet but the ultrasonic reads far from full'],[16,'alcohol mixer holds a half-made mix'],[32,'scented mixer holds a half-made mix'],[64,'a pump hit its time limit']];
const PUMPS = ["alcohol pump","water pump (alc)","water pump (scn)","scented pump","mixer alc pump","mixer scn pump"];

// device list (the "Devices" line on the Dashboard)
const nodeList = document.getElementById('node-list');
nodeList.innerHTML = NODES.map((n, i) => `<div class="node-status-row"><span>${n}:</span><span id="nd-${i}" class="badge">OFFLINE</span></div>`).join('');


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
  renderRestate(d);
  renderAdj(d);
  document.getElementById('stat-sessions').innerText = d.stat_sessions;
  document.getElementById('stat-revenue').innerText = `PHP ${Number(d.stat_revenue).toFixed(2)}`;
  document.getElementById('stat-coins').innerText = d.stat_coins;
  document.getElementById('stat-duration').innerText = mmss(d.stat_avg_dur);

  // finished vs cancelled sessions
  const fc = document.getElementById('fc-body');
  if (fc && d.fin_n !== undefined) {
    const n = x => Number(x).toLocaleString('en-US');
    const p = x => 'PHP ' + Number(x).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
    const r = (a, b, c) => `<tr style="border-top:1px solid var(--border,#2a3550);"><td style="padding:5px 6px;">${a}</td><td style="padding:5px 6px;text-align:right;">${b}</td><td style="padding:5px 6px;text-align:right;">${c}</td></tr>`;
    // One tally of everything from the first day: what the machines counted + the rows of the JSON file (previous data). The line under the table says how much came from the file.
    const hf = Number(d.h_fin_n) || 0, hc = Number(d.h_can_n) || 0;
    const fN = Number(d.fin_n) + hf, cN = Number(d.can_n) + hc;
    const fCo = Number(d.fin_coins) + (Number(d.h_fin_c) || 0), cCo = Number(d.can_coins) + (Number(d.h_can_c) || 0);
    const cv = Number(d.coin_value) || 1;
    const avg = (sum, cnt) => cnt > 0 ? Math.round(sum / cnt) : 0;          // the minutes in the file are optional: only rows that have them count towards the average
    const fAvg = avg((Number(d.fin_dur) || 0) + (Number(d.h_fin_d) || 0), Number(d.fin_n) + (Number(d.h_fin_dn) || 0));
    const cAvg = avg((Number(d.can_dur) || 0) + (Number(d.h_can_d) || 0), Number(d.can_n) + (Number(d.h_can_dn) || 0));
    fc.innerHTML = r('Users served', n(fN), n(cN)) + r('Coins', n(fCo), n(cCo)) +
      r('Revenue', p(fCo * cv), p(cCo * cv)) + r('Avg. duration', mmss(fAvg), mmss(cAvg));
    const fs = document.getElementById('fc-src');
    if (fs) {
      const ymd = v => String(v).replace(/(\d{4})(\d\d)(\d\d)/, '$1-$2-$3');
      fs.innerHTML = (hf + hc) ? `Includes the JSON file (previous data collected by the team after the upgrade, ${ymd(d.hist_from)} to ${ymd(d.hist_to)}): <b>${n(hf)}</b> finished and <b>${n(hc)}</b> cancelled. Counted by the machines: <b>${n(d.fin_n)}</b> finished and <b>${n(d.can_n)}</b> cancelled.` : '';
    }
  }

  renderUsage(d);
  renderLog(d);

  const cv = document.getElementById('coin-value');
  if (cv && document.activeElement !== cv && d.coin_value !== undefined) cv.value = d.coin_value;
}

// -------------------------------------------------------------
// Usage chart: sessions and money per day (last 7 days) or per week (last 8 weeks), from the permanent session log.
// A bar is the money (coins x coin value); its orange part is what came from cancelled sessions.
// -------------------------------------------------------------
let usageMode = 'day';
try { usageMode = localStorage.getItem('jh_usage') === 'week' ? 'week' : 'day'; } catch (e) {}
function useMode(m) {
  usageMode = m;
  try { localStorage.setItem('jh_usage', m); } catch (e) {}
  if (window.lastStatus) renderUsage(window.lastStatus);
}
const DOW = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function ymdParts(n) { return { y: Math.floor(n / 10000), m: Math.floor(n / 100) % 100, d: n % 100 }; }
function ymdLabel(n, withDow) {
  const p = ymdParts(n);
  if (!withDow) return p.m + '/' + p.d;
  const dt = new Date(Date.UTC(p.y, p.m - 1, p.d));
  return DOW[dt.getUTCDay()] + ' ' + p.m + '/' + p.d;
}
// How many weeks the Weekly chart draws: 4 / 6 / 8 / 12 (the choice is remembered); automatic = 6 on a narrow (portrait) screen, 12 on a wide one
let weeksN = 0;
try { weeksN = Number(localStorage.getItem('jh_weeks')) || 0; } catch (e) {}
function weeksShown() { return [4, 6, 8, 12].includes(weeksN) ? weeksN : (window.innerWidth < 700 ? 6 : 12); }
function useWeeks(v) {
  weeksN = Number(v) || 0;
  try { localStorage.setItem('jh_weeks', String(weeksN)); } catch (e) {}
  if (window.lastStatus) renderUsage(window.lastStatus);
}
window.addEventListener('resize', () => { if (window.lastStatus) renderUsage(window.lastStatus); });
function renderUsage(d) {
  window.lastStatus = d;
  const chart = document.getElementById('stat-chart'), note = document.getElementById('use-note'), legend = document.getElementById('use-legend');
  const week = usageMode === 'week';
  const nW = weeksShown();
  document.getElementById('use-title').innerText = week ? `Weekly Usage (${nW} Weeks)` : 'Daily Usage (7 Days)';
  document.getElementById('use-day').style.outline = week ? '' : '2px solid var(--accent)';
  document.getElementById('use-week').style.outline = week ? '2px solid var(--accent)' : '';
  const sel = document.getElementById('use-weeks');
  sel.style.display = week ? '' : 'none';
  sel.value = String(nW);
  const rows = week ? (d.w12 ? d.w12.slice(-nW) : d.w12) : d.d7;
  if (!rows) {
    chart.innerHTML = '<span style="color:var(--muted); font-size:0.8rem;">The clock is not set, so days cannot be drawn yet. Set it below (or open this page on a phone after a power-on).</span>';
    note.innerText = d.lg_nodate ? `${d.lg_nodate} session(s) have no date yet.` : '';
    legend.innerHTML = '';
    return;
  }
  // a bucket: [date, machine sessions, machine coins, machine cancelled sessions, machine cancelled coins, then the same four from the history.json file]
  const cv = Number(d.coin_value) || 1;
  const total = r => (r[2] + (r[6] || 0)) * cv;
  const maxRev = Math.max(...rows.map(total), 1);
  let anyHist = false;
  chart.innerHTML = rows.map(r => {
    const T = r[2] + (r[6] || 0), money = T * cv;
    if ((r[5] || 0) > 0) anyHist = true;
    const pct = money > 0 ? Math.max(6, Math.round(money / maxRev * 100)) : 2;
    let off = 0;
    const seg = (coins, color, light) => {                     // stacked from the bottom: machine cancelled, machine finished, JSON cancelled, JSON finished
      if (!(coins > 0) || !(T > 0)) return '';
      const h = coins / T * 100, s = `<i style="position:absolute;left:0;right:0;bottom:${off}%;height:${h}%;background:${color};${light ? 'opacity:.45;' : ''}"></i>`;
      off += h; return s;
    };
    const bars = seg(r[4], 'var(--warning)', false) + seg(r[2] - r[4], 'var(--accent)', false) + seg(r[8] || 0, 'var(--warning)', true) + seg((r[6] || 0) - (r[8] || 0), 'var(--accent)', true);
    const lab = week ? 'wk ' + ymdLabel(r[0], false) : ymdLabel(r[0], true);
    return `<div class="chart-col${week ? '' : ' pick'}"${week ? '' : ` onclick="openDay(${r[0]})" title="Tap to see this day customer by customer"`}><div class="chart-val">${r[1] + (r[5] || 0)} cust</div><div class="chart-bar" style="height:${pct}%;position:relative;overflow:hidden;background:transparent;">${bars}</div><div class="chart-val">P${money.toFixed(0)}</div><div class="chart-val">${lab}</div></div>`;
  }).join('');
  // the legend is part of the chart: it appears whenever history.json rows are drawn in this window
  legend.innerHTML = anyHist ? '<span style="display:inline-block;width:10px;height:10px;background:var(--accent);margin-right:4px;"></span>recorded by the machines &nbsp;·&nbsp; <span style="display:inline-block;width:10px;height:10px;background:var(--accent);opacity:.45;margin-right:4px;"></span><b>lighter = previous data collected by the team after the upgrade</b> &nbsp;·&nbsp; orange = cancelled' : '';
  const sum = (i) => rows.reduce((a, r) => a + (r[i] || 0), 0);
  const cust = sum(1) + sum(5), canc = sum(3) + sum(7), money = (sum(2) + sum(6)) * cv;
  note.innerText = `${week ? 'These ' + nW + ' weeks' : 'These 7 days'}: ${cust} customers (${canc} cancelled), PHP ${money.toFixed(2)}.` +
    (anyHist ? ` Of these, ${sum(5)} customers (PHP ${(sum(6) * cv).toFixed(2)}) are previous data collected by the team after the upgrade.` : '') +
    (d.lg_nodate ? ` ${d.lg_nodate} machine session(s) still have no date and are not drawn.` : '');
}

// -------------------------------------------------------------
// Session log, storage and the clock
// -------------------------------------------------------------
const pad2 = n => String(n).padStart(2, '0');
function renderLog(d) {
  const clk = document.getElementById('clk-line'), lg = document.getElementById('log-line'), bar = document.getElementById('log-bar');
  if (!clk || d.clk === undefined) return;
  const hl = document.getElementById('hist-line');
  if (hl) {
    const ymd = n => { const p = ymdParts(n); return `${p.y}-${pad2(p.m)}-${pad2(p.d)}`; };
    hl.innerHTML = d.hist_msg ? `<b style="color:var(--danger)">${String(d.hist_msg).replace(/[<>&]/g, '')}</b>`
      : (d.hist_n > 0 ? `· <b>${d.hist_n}</b> row(s), ${ymd(d.hist_from)} to ${ymd(d.hist_to)} (drawn lighter in the chart)` : '· no file uploaded');
  }
  if (d.clk > 0) {
    const t = new Date((d.clk + d.clk_tz * 60) * 1000);
    const txt = `${t.getUTCFullYear()}-${pad2(t.getUTCMonth() + 1)}-${pad2(t.getUTCDate())} ${pad2(t.getUTCHours())}:${pad2(t.getUTCMinutes())}`;
    clk.innerHTML = `Clock: <b style="color:var(--success)">${txt}</b> (UTC${d.clk_tz >= 0 ? '+' : '-'}${Math.floor(Math.abs(d.clk_tz) / 60)}${Math.abs(d.clk_tz) % 60 ? ':' + pad2(Math.abs(d.clk_tz) % 60) : ''}) · set ${d.clk_src === 1 ? 'from a phone' : 'by hand'}. It is lost at a power cut until a phone sets it again.`;
  } else {
    clk.innerHTML = `Clock: <b style="color:var(--danger)">NOT SET</b> — sessions are saved without a date until it is set. <button class="btn-sec" onclick="setClockPhone(false)">Use this phone's time</button>`;
  }
  // the storage was damaged at this start and re-created empty: say so on the Home page (a board that is left alone should not just come back at 0 unnoticed)
  const al = document.getElementById('log-alert');
  if (al) {
    if (d.lg_fmt_new) { al.style.display = ''; al.innerHTML = `<b style="color:var(--danger)">The log storage was damaged and had to be re-created EMPTY at the last start</b> (${d.lg_fmt} time(s) since the board was first flashed). The old sessions and history.json are gone from this board: open <b>Settings</b> and restore your last backup CSV, and push the history file again from the ENTRY board if you use it.`; }
    else al.style.display = 'none';
  }
  // result of the last backup restore (it runs on the board a few seconds after the upload)
  const rl = document.getElementById('restore-line');
  if (rl) {
    if (d.lg_rstate === 2) rl.innerHTML = '<b>Restoring ... the board is merging the backup, this takes a few seconds.</b>';
    else if (d.lg_rmsg) rl.innerHTML = `<b style="color:${d.lg_rstate === 1 ? 'var(--success)' : 'var(--danger)'}">${String(d.lg_rmsg).replace(/&/g, '&amp;').replace(/</g, '&lt;')}</b>`;
    else rl.innerHTML = '';
  }
  if (!d.lg_ok) {
    lg.innerHTML = `<b style="color:var(--danger)">Log storage: NOT AVAILABLE.</b> In the Arduino IDE choose Tools &gt; Partition Scheme &gt; "16M Flash (3MB APP/9.9MB FATFS)" and upload again. The totals above still work.`;
    bar.style.width = '0%'; return;
  }
  const kb = x => (x / 1024).toFixed(x >= 1048576 ? 0 : 1) + ' KB', mb = x => (x / 1048576).toFixed(2) + ' MB';
  const free = d.lg_total - d.lg_used, pctN = Math.min(100, Math.round(d.lg_n / d.lg_cap * 100)), pctF = d.lg_total ? Math.round(d.lg_used / d.lg_total * 100) : 0;
  const left = Math.max(0, d.lg_cap - d.lg_n);
  const warn = d.lg_full || pctN >= 80 || pctF >= 80;
  lg.innerHTML = `Log storage (${d.lg_fs}): <b>${d.lg_n.toLocaleString('en-US')}</b> of ${d.lg_cap.toLocaleString('en-US')} sessions saved · room for about <b>${left.toLocaleString('en-US')}</b> more · file system ${mb(d.lg_used)} used, <b>${mb(free)} free</b> of ${mb(d.lg_total)}` +
    (d.lg_nodate ? ` · <span style="color:var(--warning)">${d.lg_nodate} without a date yet</span>` : '') +
    (warn ? ` · <b style="color:var(--danger)">${d.lg_full ? 'LOG FULL: new sessions are not saved any more. Download the CSV, then Reset Stats.' : 'getting full'}</b>` : '');
  bar.style.width = Math.max(pctN, pctF) + '%';
  bar.style.background = warn ? 'var(--danger)' : 'var(--accent)';
}
// -------------------------------------------------------------
// Day viewer: the customers the MACHINES recorded on one day, with their times (tap a bar of the Daily chart, or pick a date)
// -------------------------------------------------------------
const hms = s => pad2(Math.floor(s / 3600)) + ':' + pad2(Math.floor(s % 3600 / 60)) + ':' + pad2(s % 60);
async function openDay(ymd) {
  const card = document.getElementById('day-card');
  card.style.display = '';
  document.getElementById('day-title').innerText = 'Loading...';
  document.getElementById('day-table').innerHTML = '';
  try {
    const j = await (await fetch('/api/day?d=' + ymd, { cache: 'no-store' })).json();
    dayWhich = ((j.rows || []).length === 0 && (j.hrows || []).length > 0) ? 'j' : 'm';      // a day the machines did not record but the JSON file did opens on the JSON tab
    renderDay(j);
  } catch (e) { document.getElementById('day-title').innerText = 'Could not load this day'; }
  card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}
function openDayPick() {
  const v = document.getElementById('day-pick').value;
  if (!v) { showToast('Pick a date first'); return; }
  openDay(Number(v.replace(/-/g, '')));
}
function closeDay() { document.getElementById('day-card').style.display = 'none'; }
let dayWhich = 'm';                                   // which tab of the day viewer is open: 'm' machine sessions, 'j' the JSON file
function dayTab(w) { dayWhich = w; if (window.lastDay) renderDay(window.lastDay); }
function renderDay(j) {
  window.lastDay = j;
  const cv = Number(window.lastStatus && window.lastStatus.coin_value) || 1;
  const title = document.getElementById('day-title'), sum = document.getElementById('day-sum'), tab = document.getElementById('day-table'), note = document.getElementById('day-note');
  if (!j.ok) { title.innerText = 'Not a real date'; sum.innerText = ''; tab.innerHTML = ''; note.innerText = ''; return; }
  const dated = j.d !== 0, p = ymdParts(j.d);
  const mrows = j.rows || [], jrows = j.hrows || [];
  title.innerText = dated ? ymdLabel(j.d, true) + '/' + p.y : 'Sessions without a date';
  document.getElementById('dtab-m').innerText = `Machine (${mrows.length})`;
  document.getElementById('dtab-j').innerText = `JSON (${jrows.length})`;
  document.getElementById('dtab-j').style.display = dated ? '' : 'none';
  if (!dated) dayWhich = 'm';
  document.getElementById('dtab-m').classList.toggle('active', dayWhich === 'm');
  document.getElementById('dtab-j').classList.toggle('active', dayWhich === 'j');
  const mach = dayWhich === 'm', rows = mach ? mrows : jrows;
  const can = rows.filter(r => r[3]).length, coins = rows.reduce((a, r) => a + r[2], 0);
  sum.innerHTML = rows.length
    ? (mach ? `<b>${rows.length}</b> customer(s) recorded by the machines (${can} cancelled), <b>${coins}</b> coins, <b>PHP ${(coins * cv).toFixed(2)}</b>.`
            : `<b>${rows.length}</b> customer(s) from the JSON file: previous data collected by the team after the upgrade (${can} cancelled), <b>${coins}</b> coins, <b>PHP ${(coins * cv).toFixed(2)}</b>. <i>These are not machine sessions.</i>`)
    : (mach ? 'No sessions were recorded by the machines on this day.' : 'The JSON file has no customers for this day.');
  tab.innerHTML = rows.length ? '<tr><th>Time</th><th>Box</th><th>Coins</th><th>PHP</th><th>Status</th><th>Minutes</th></tr>' + rows.map(r =>
    `<tr${mach ? '' : ' style="opacity:.75"'}><td>${mach ? (dated ? hms(r[0]) : 'power-on +' + hms(r[0])) : hms(r[0]).slice(0, 5)}</td><td>${r[1]}</td><td>${r[2]}</td><td>${(r[2] * cv).toFixed(2)}</td><td style="${r[3] ? 'color:var(--warning)' : ''}">${r[3] ? 'Cancelled' : 'Finished'}</td><td>${r[4] === 65535 ? '-' : (r[4] / 60).toFixed(1)}</td></tr>`).join('') : '';
  note.innerText = (mach
    ? (dated ? '' : 'These sessions finished before the clock was set; set it in Settings and they get their real date. ')
    : 'Source: history.json, uploaded in Settings (made on the Entry board). Same lighter shade as in the chart. These rows are not part of the machine log, the totals or the CSV. ') +
    (j.trunc ? 'More customers than fit: only the first ones are listed.' : '');
}

// Dashboard / Settings tabs (the Settings tab holds the clock, the session log and storage, the history file, the coin value and Reset Stats)
function showTab(name) {
  const set = name === 'set';
  document.getElementById('pane-dash').style.display = set ? 'none' : '';
  document.getElementById('pane-set').style.display = set ? '' : 'none';
  document.getElementById('tab-dash').classList.toggle('active', !set);
  document.getElementById('tab-set').classList.toggle('active', set);
  try { sessionStorage.setItem('jh_tab', name); } catch (e) {}
  if (set && location.hash !== '#settings') history.replaceState(null, '', '#settings');
  else if (!set && location.hash === '#settings') history.replaceState(null, '', location.pathname);
}
(function () {
  let n = 'dash';
  try { n = sessionStorage.getItem('jh_tab') || 'dash'; } catch (e) {}
  if (location.hash === '#settings') n = 'set';
  showTab(n === 'set' ? 'set' : 'dash');
})();

// Restore the machine log from the CSV the board itself wrote (Settings). The board checks the file's last line and refuses anything that was changed.
async function uploadLogRestore() {
  const inp = document.getElementById('restore-file');
  if (!inp || !inp.files || !inp.files.length) { showToast('Choose the backup CSV file first'); return; }
  if (!confirm('Restore the log from this backup?\n\nSessions already on the board stay; sessions that are in the file and not on the board are added. Only an unchanged download of the log CSV is accepted.')) return;
  const fd = new FormData();
  fd.append('file', inp.files[0], 'backup.csv');
  try {
    const r = await fetch('/log/restore', { method: 'POST', body: fd });
    const j = await r.json();
    if (j.ok) { showToast('Backup received, restoring ...'); const rl = document.getElementById('restore-line'); if (rl) rl.innerHTML = '<b>Restoring ... the board is merging the backup, this takes a few seconds.</b>'; }
    else alert('The file was not accepted (nothing changed):\n' + j.msg);
  } catch (e) { showToast('Upload failed: no link'); }
}

// The team's previous-data file (history.json): upload (checked by the LocalServer, a bad file changes nothing), remove
async function uploadHistory() {
  const inp = document.getElementById('hist-file');
  if (!inp || !inp.files || !inp.files.length) { showToast('Choose the history.json file first'); return; }
  const fd = new FormData();
  fd.append('file', inp.files[0], 'history.json');
  try {
    const r = await fetch('/history/upload', { method: 'POST', body: fd });
    const j = await r.json();
    if (j.ok) showToast(`History loaded: ${j.rows} row(s)`);
    else alert('The file was not accepted (nothing changed):\n' + j.msg);
  } catch (e) { showToast('Upload failed: no link'); }
}
function removeHistory() {
  if (!linkUp) return;
  if (!confirm('Remove the previous-data file (history.json) from the LocalServer? (The machine log and the totals are not touched. Download the file first if you need it.)')) return;
  sendCmd({ c: 'hist_clear' });
  showToast('History file removed');
}
let clockAutoTried = false;
function setClockPhone(auto) {
  if (!linkUp) return;
  sendCmd({ c: 'set_time', epoch: Math.floor(Date.now() / 1000), tz: -new Date().getTimezoneOffset(), auto: auto ? 1 : 0 });
  if (!auto) showToast("Clock set from this phone");
}
function setClockManual() {
  const dv = document.getElementById('clk-date').value, tv = document.getElementById('clk-time').value;
  if (!dv || !tv || !linkUp) { showToast('Pick a date and a time first'); return; }
  const tz = -new Date().getTimezoneOffset();                       // the zone of the device you are typing on
  const p = dv.split('-').map(Number), q = tv.split(':').map(Number);
  const epoch = Date.UTC(p[0], p[1] - 1, p[2], q[0], q[1], 0) / 1000 - tz * 60;
  sendCmd({ c: 'set_time', epoch, tz, auto: 0 });
  showToast('Clock set');
}

// -------------------------------------------------------------
// -------------------------------------------------------------
// Corrected statistics: one line that says so, with the original figures and a way back
// -------------------------------------------------------------
function renderAdj(d) {
  const box = document.getElementById('adj-note');
  if (!box) return;
  if (!d.adj_on) { box.style.display = 'none'; return; }
  box.style.display = '';
  const n = x => Number(x).toLocaleString('en-US');
  const p = x => 'PHP ' + Number(x).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const at = d.adj_at ? new Date((d.adj_at - d.adj_tz * 60) * 1000).toISOString().slice(0, 10) : 'an unknown date';
  document.getElementById('adj-text').innerText = `Statistics corrected (data up to ${at}): the old price list charged more coins per minute than the new one, so the coins counted before then were restated at the new price list (${n(d.adj_base_coins)} coins / ${p(d.adj_base_rev)} counted, restated to ${n(d.adj_rest_coins)} coins). The original figures are kept and can be restored. Update #${d.adj_rev}.`;
}

function adjRestore() {
  if (!linkUp) return;
  if (!confirm('Put the original coin total back? (Coins sold since the correction stay counted.)')) return;
  sendCmd({ c: "adj_restore" });
  showToast("Original figures restored");
}

// -------------------------------------------------------------
// Restated old data (sent by the spare ESP32_RESTATE board): shown NEXT TO the collected figures
// -------------------------------------------------------------
function renderRestate(d) {
  const sec = document.getElementById('rs-sec');
  if (!sec) return;
  if (!d.rs_on) { sec.style.display = 'none'; return; }
  sec.style.display = '';
  const n = x => Number(x).toLocaleString('en-US');
  const p = x => 'PHP ' + Number(x).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const row = (a, b, c, hl) => `<tr style="${hl ? 'font-weight:700;' : ''}border-top:1px solid var(--border,#2a3550);"><td style="padding:5px 6px;">${a}</td><td style="padding:5px 6px;text-align:right;">${b}</td><td style="padding:5px 6px;text-align:right;">${c}</td></tr>`;
  const head = t => `<tr><td colspan="3" style="padding:8px 6px 2px;color:var(--muted);font-size:0.72rem;font-weight:700;letter-spacing:.04em;text-transform:uppercase;">${t}</td></tr>`;
  const at = d.rs_at ? new Date((d.rs_at - d.rs_tz * 60) * 1000).toISOString().slice(0, 16).replace('T', ' ') : 'unknown time';
  document.getElementById('rs-note').innerText = `update #${d.rs_rev}, old data captured ${at}, new list ${Number(d.rs_cpm).toFixed(2)} coins/min`;
  document.getElementById('rs-body').innerHTML =
    head('Old data (up to the capture)') +
    row('Customers served', n(d.rs_old_ss), n(d.rs_old_ss)) +
    row('Coins', n(d.rs_old_coins), n(d.rs_old_rcoins)) +
    row('Revenue', p(d.rs_old_rev), p(d.rs_old_rrev), true) +
    head('Since the capture (sold at the new list)') +
    row('Customers served', n(d.rs_new_ss), n(d.rs_new_ss)) +
    row('Coins', n(d.rs_new_coins), n(d.rs_new_coins)) +
    row('Revenue', p(d.rs_new_rev), p(d.rs_new_rev), true) +
    head('Total') +
    row('Coins', n(d.rs_tot_coins), n(d.rs_tot_rcoins)) +
    row('Revenue', p(d.rs_tot_rev), p(d.rs_tot_rrev), true);
  document.getElementById('rs-warn').style.display = d.rs_down ? '' : 'none';
}

function rsClear() {
  if (!linkUp) return;
  if (!confirm('Remove the restated figures from this page? The statistics above are not changed. (Press Update on the ESP32_RESTATE board to bring them back.)')) return;
  sendCmd({ c: "rs_clear" });
  showToast("Restated figures removed");
}

function resetStats() {
  if (!linkUp) return;
  if (!confirm('Reset all statistics? This empties the counters AND the session log, and removes the "corrected" note. It cannot be undone (download the log CSV first if you need it).')) return;
  sendCmd({ c: "reset_stats" });
  showToast("Statistics Reset!");
}

function refreshScreen(n) {
  fetch(`/api/b${n}/refresh`, { cache: "no-store" }).then(() => showToast(`Screen refresh sent to Box ${n}`)).catch(() => showToast("No link"));
}

function saveCoinValue() {
  const v = parseFloat(document.getElementById('coin-value').value);
  if (!linkUp || !(v >= 0.1)) return;
  sendCmd({ c: "save_coin_value", val: v });
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
// Status: asked for from the board once a second
// -------------------------------------------------------------
function setLink(up) {
  linkUp = up;
  const b = document.getElementById('ws-badge');
  b.innerText = up ? 'Online' : 'Offline';
  b.className = up ? 'badge online' : 'badge';
}
let polling = false, failCount = 0, wasDown = false;
async function pollStatus() {
  if (polling) return;
  polling = true;
  const ctl = new AbortController(), timer = setTimeout(() => ctl.abort(), 4000);
  try {
    const r = await fetch('/api/status', { cache: 'no-store', signal: ctl.signal });
    const d = await r.json();
    failCount = 0;
    if (!linkUp) { setLink(true); if (wasDown) { location.reload(); return; } }          // the board was away and is back: start the page fresh
    handleStatus(d);
  } catch (e) {
    if (++failCount >= 3) { if (linkUp) wasDown = true; setLink(false); }
  } finally { clearTimeout(timer); polling = false; }
}

let crashesLoaded = false;
const escHtml = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
async function loadCrashes() {                                                  // the crash reports the board keeps in its flash (Settings > This board)
  try {
    const j = await (await fetch('/api/crashes', { cache: 'no-store' })).json();
    const box = document.getElementById('crash-box'), list = document.getElementById('crash-list');
    const card = t => `<div style="margin:6px 0;padding:6px 8px;border:1px solid var(--card-border);border-radius:8px;word-break:break-word;">${t}</div>`;
    const items = (j.reports || []).map(s => card(escHtml(s)));
    if (j.heapnote) items.unshift(card('<b style="color:var(--danger)">Memory damage was found (the board may not have restarted):</b> ' + escHtml(j.heapnote)));
    list.innerHTML = items.join('');
    document.getElementById('crash-n').innerText = items.length;
    box.style.display = items.length ? '' : 'none';
    crashesLoaded = true;
  } catch (e) {}
}
function clearCrashes() {
  if (!confirm('Delete the stored crash reports from this board?')) return;
  sendCmd({ c: 'crash_clear' }).then(() => { crashesLoaded = false; setTimeout(loadCrashes, 700); });
}

function handleStatus(d) {
  // memory damage found by the board's own check while it is still running: say so at once (the same text is kept in the crash reports)
  const ha = document.getElementById('heap-alert');
  if (ha) {
    if (d.heap_bad) { ha.style.display = ''; ha.innerHTML = `<b style="color:var(--danger)">The board found damage in its memory.</b> Please take a screenshot of this and send it to the developer: ${escHtml(d.heap_bad)}`; }
    else ha.style.display = 'none';
  }
  if (!crashesLoaded && (d.crashes > 0 || d.heap_bad)) loadCrashes();

  document.getElementById('hdr-ram').innerText = `${(d.heap / 1024).toFixed(0)} KB`;
  const psramEl = document.getElementById('hdr-psram');
  psramEl.innerText = d.free_psram > 0 ? `${(d.free_psram / 1024 / 1024).toFixed(2)} MB` : '0 (not detected!)';
  psramEl.style.color = d.free_psram > 0 ? 'var(--success)' : 'var(--danger)';

  (d.n || []).forEach((on, i) => setNodeBadge('nd-' + i, !!on));
  { const nOn = (d.n || []).filter(x => x).length, dc = document.getElementById('dev-count'), dd = document.getElementById('dev-dot');
    if (dc) dc.innerText = nOn + ' of ' + NODES.length;
    if (dd) dd.style.color = nOn === NODES.length ? 'var(--success)' : (nOn === 0 ? 'var(--danger)' : 'var(--warning)'); }

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
  if (!clockAutoTried && d.clk === 0) { clockAutoTried = true; setClockPhone(true); }       // first look after a power-on: this phone sets the clock

  // board health: tells a power dip (BROWNOUT) apart from a crash or a watchdog
  const diag = document.getElementById('diag');
  if (diag && d.rst !== undefined) {
    const bad = (d.rst.indexOf('BROWNOUT') >= 0);
    diag.innerHTML = `This board: up ${d.up} s · last restart: <b style="color:${bad || d.crashes > 0 ? 'var(--warning)' : 'var(--success)'}">${d.rst}</b> · unexpected restarts ${d.crashes} of ${d.boots} starts · lowest free memory ${(d.minheap / 1024).toFixed(0)} KB · radio packets dropped ${d.rxdrop} · phones connected ${d.clients}` +
      (d.crash ? '<br><b style="color:var(--warning)">Before the last unexpected restart:</b> ' + String(d.crash).replace(/&/g, '&amp;').replace(/</g, '&lt;') : '') +
      (bad ? '<br><b style="color:var(--danger)">BROWNOUT = the supply dipped: use a stronger USB supply / shorter cable for this board, or a separate supply from the pumps and relays.</b>' : '');
  }
}
setInterval(pollStatus, 1000);
pollStatus();
)rawliteral";

#endif // SCRIPT_JS_H
