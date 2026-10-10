"use strict";
/* Turns the data in S into HTML. One function per card; renderAll() puts them on the page. */

/* ---- small building blocks ---- */
function spark(key) {                                   // a tiny line graph of one container level
  const a = S.hist[key]; if (!a || a.length < 3) return '';
  const w = 120, h = 24, n = a.length;
  const pts = a.map((v, i) => [i * w / (n - 1), h - 2 - Math.max(0, Math.min(100, v)) / 100 * (h - 4)]);
  const line = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
  return `<svg class="spark" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none" aria-hidden="true"><path class="f" d="${line} L${w} ${h} L0 ${h} Z"/><path class="s" d="${line}"/></svg>`;
}
function level(label, pct, key) {                       // a labelled bar, 0..100 %
  if (typeof pct !== 'number' || pct < 0) return '';
  const p = Math.max(0, Math.min(100, Math.round(pct)));
  const cls = p < 15 ? 'bad' : p < 35 ? 'warn' : '';
  return `<div class="lv"><div class="lv-t"><span>${esc(label)}</span><b class="num">${p}%</b></div><div class="bar ${cls}"><i style="width:${p}%"></i></div>${spark(key)}</div>`;
}
function stateClass(name) {
  if (name === 'OUT OF ORDER') return 'bad';
  if (name === 'MAINTENANCE') return 'maint';
  if (RUNNING.has(name)) return 'run';
  if (ATTN.has(name)) return 'warn';
  if (IDLE.has(name)) return '';
  return 'use';
}
function nodeFlags(L) {                                 // the 13 "heard / not heard" flags
  const n = L.n, f = Array.isArray(n) ? n.map(Number) : String(n == null ? '' : n).split('').map(Number);
  while (f.length < 13) f.push(L.n == null ? 1 : 0);    // no "n" at all: assume everything is there
  return f.slice(0, 13).map(v => v ? 1 : 0);
}

/* ---- one box ---- */
function boxCard(i, b, n) {
  const def = BOXES[i];
  const head = (right) => `<div class="card-h"><div><b>${def.name}</b><small>${def.role}</small></div>${right}</div>`;
  if (!b) return `<div class="card">${head(chip('no data', 'off'))}</div>`;
  const brainOn = n[def.brain], termOn = n[def.term], sensOn = n[def.sensors], relOn = n[def.relayBoard];
  const name = brainOn ? (def.st[b[0]] || ('step ' + b[0])) : 'NOT HEARD';
  const timer = nz(b[1], 0), coins = nz(b[2], 0), doors = nz(b[3], 0), helmet = b[4], wet = nz(b[7], 0), relays = nz(b[8], 0);
  let h = `<div class="card">` + head(`<span class="chip state ${brainOn ? stateClass(name) : 'off'}">${esc(name)}</span>`);
  if (brainOn && (RUNNING.has(name) || timer > 0)) h += `<div class="timer"><span class="big num">${mmss(timer)}</span><span class="bigl">time left</span></div>`;
  h += `<div class="row"><span>Coins counted</span><b class="num">${termOn ? coins : '-'}</b></div>`;
  if (!sensOn) {
    h += `<div class="chips">${chip('sensor board offline', 'off')}</div>`;
  } else {
    if (typeof helmet === 'number' && helmet >= 0) h += `<div class="row"><span>Helmet</span><b>${helmet > 0 && helmet < 15 ? 'inside' : 'not inside'}</b></div>`;
    h += `<div class="chips">` + def.doors.map((d, k) => ((doors >> k) & 1) ? chip(d + ' door OPEN', 'warn') : chip(d + ' closed', 'run')).join('') + `</div>`;
    if (def.tanks) {
      h += level('Alcohol container', b[5], 'b' + i + 'a') + level('Scented container', b[6], 'b' + i + 's');
      h += `<div class="chips">${chip('alcohol water sensor: ' + ((wet & 1) ? 'wet' : 'dry'))}${chip('scented water sensor: ' + ((wet & 2) ? 'wet' : 'dry'))}</div>`;
    }
  }
  if (relOn && b.length > 8) {
    const on = def.relays.filter((nm, k) => (relays >> k) & 1);
    h += `<div class="chips">${on.length ? on.map(nm => chip(nm + ' ON', 'use')).join('') : '<span class="meter">Nothing switched on.</span>'}</div>`;
  }
  return h + `</div>`;
}

/* ---- one refill station ---- */
function acsCard(i, a) {
  const title = `ACS ${i + 1}`, sub = i ? 'refills Box 3' : 'refills Box 1';
  if (!a || !a[0]) return `<div class="card"><div class="card-h"><div><b>${title}</b><small>${sub}</small></div>${chip('NOT HEARD', 'off')}</div></div>`;
  const [ok, step, flags, empty, warn, pa, ps, pumps, sec] = a;
  const name = ACSSTEP[step] || ('step ' + step);
  const cls = step >= 7 && step <= 9 ? 'bad' : step === 10 ? 'maint' : step === 0 ? '' : 'run';
  let h = `<div class="card"><div class="card-h"><div><b>${title}</b><small>${sub}</small></div><span class="chip state ${cls}">${esc(name)}</span></div>`;
  h += level('Alcohol mixer', pa, 'a' + i + 'a') + level('Scented mixer', ps, 'a' + i + 's');
  let c = '';
  c += (flags & 1) ? chip('ALCOHOL MIXER LOW', 'bad') : ''; c += (flags & 4) ? chip('SCENTED MIXER LOW', 'bad') : '';
  c += (empty & 1) ? chip('WATER TANK EMPTY', 'bad') : ''; c += (empty & 2) ? chip('ALCOHOL TANK EMPTY', 'bad') : ''; c += (empty & 4) ? chip('SCENTED TANK EMPTY', 'bad') : '';
  c += (flags & 128) ? chip('box is asking for liquid', 'use') : '';
  PUMPS.forEach((nm, k) => { if ((pumps >> k) & 1) c += chip('running: ' + nm, 'run'); });
  h += `<div class="chips">${c || '<span class="meter">All supplies available, no pump running.</span>'}</div>`;
  ACSWARN.forEach(w => { if (warn & w[0]) h += `<div class="meter" style="margin-top:5px">&#9888; ${esc(w[1])}</div>`; });
  if (step >= 7 && step <= 9) h += `<div class="meter" style="margin-top:5px">Checking again in ${sec} s.</div>`;
  return h + `</div>`;
}

/* ---- everything that needs attention ---- */
function alertsFor(L) {
  const out = [], n = nodeFlags(L);
  (L.b || []).forEach((b, i) => {
    if (!b) return; const def = BOXES[i];
    if (n[def.brain]) {
      const name = def.st[b[0]] || '';
      if (name === 'OUT OF ORDER') out.push(`${def.name} is OUT OF ORDER`);
      if (name === 'SELECT DRY ONLY') out.push(`${def.name}: CLEANING is switched off (a container is low). Drying still works.`);
      if (name === 'SAFETY PAUSE') out.push(`${def.name}: paused, a door was opened during a run`);
    }
    if (def.tanks && n[def.sensors]) {
      if (nz(b[5], -1) >= 0 && b[5] < 15) out.push(`${def.name}: alcohol container is low (${Math.round(b[5])}%)`);
      if (nz(b[6], -1) >= 0 && b[6] < 15) out.push(`${def.name}: scented container is low (${Math.round(b[6])}%)`);
    }
  });
  (L.a || []).forEach((a, i) => {
    if (!a || !a[0]) return; const t = 'ACS ' + (i + 1), flags = a[2], empty = a[3];
    if (empty & 1) out.push(`${t}: WATER tank is empty`); if (empty & 2) out.push(`${t}: ALCOHOL tank is empty`); if (empty & 4) out.push(`${t}: SCENTED tank is empty`);
    if (flags & 1) out.push(`${t}: alcohol mixer container is low`); if (flags & 4) out.push(`${t}: scented mixer container is low`);
  });
  if (L.n != null) {
    const off = n.map((v, i) => v ? null : NODES[i]).filter(Boolean);
    if (off.length && off.length < 13) out.push('Not heard (offline): ' + off.join(', '));
    if (off.length === 13) out.push('The Online Server hears no machine board at all (radio or power problem at the site).');
  }
  return out;
}

/* ---- analytics ---- */
function weekday(offsetDays) {                           // short weekday name of "today minus offsetDays"
  const base = (S.live && S.live.ts > 1.6e9) ? S.live.ts * 1000 : Date.now();
  return new Date(base - offsetDays * 86400000).toLocaleDateString(undefined, { weekday: 'short' });
}
function renderStats() {
  const T = S.stats || {}, cv = nz(T.cv, 1);
  const tile = (v, l) => `<div class="card kpi"><div class="v num">${v}</div><div class="l">${l}</div></div>`;
  if (!S.stats) { $('kpis').innerHTML = `<div class="card empty" style="grid-column:1/-1">No analytics received yet (the "st" part of datastream v0).</div>`; }
  else {
    let k = tile(nz(T.ss, 0).toLocaleString(), 'Customers served') + tile(money(nz(T.rv, 0)), 'Revenue') + tile(nz(T.cn, 0).toLocaleString(), 'Coins counted') + tile(mmss(nz(T.ad, 0)), 'Average session');
    const dy = T.dy;
    if (Array.isArray(dy) && dy.length) { const td = dy[dy.length - 1]; k += tile(money(nz(td[0], 0) * cv), "Today's revenue") + tile(nz(td[1], 0).toLocaleString(), "Today's customers"); }
    $('kpis').innerHTML = k;
  }
  // last finished sessions
  const h = (T.h || []).slice(-10);
  if (!h.length) $('hist').innerHTML = '<div class="empty">No completed cycles yet.</div>';
  else {
    const mx = Math.max(...h.map(x => x[1]), 1);
    $('hist').innerHTML = `<div class="hist">` + h.map(x => `<div class="hc"><div class="t num">${CONFIG.coin}${Math.round(x[1])}</div><div class="b" style="height:${Math.max(6, Math.round(x[1] / mx * 78))}%"></div><div class="d num">${mmss(x[0])}</div></div>`).join('') + `</div><div class="sub">Revenue and length of the last ${h.length} sessions (oldest left).</div>`;
  }
  // last 14 days
  const dy = T.dy;
  if (!Array.isArray(dy) || !dy.length) { $('days').innerHTML = '<div class="empty">Daily totals appear once the Online Server knows the time and has seen the LocalServer.</div>'; return; }
  const rev = dy.map(x => nz(x[0], 0) * cv), mx = Math.max(...rev, 1);
  $('days').innerHTML = `<div class="days">` + rev.map((r, i) => `<div class="dc"><div class="t num">${r ? CONFIG.coin + Math.round(r) : ''}</div><div class="b ${i === rev.length - 1 ? 'today' : ''}" style="height:${Math.max(3, Math.round(r / mx * 78))}%"></div><div class="d">${weekday(rev.length - 1 - i)}</div></div>`).join('') + `</div><div class="sub">Revenue per day, today on the right.</div>`;
}

/* ---- boards and the Online Server's own health ---- */
function renderSys() {
  const L = S.live || {}, T = S.stats || {}, sy = T.sys || {}, n = nodeFlags(L);
  let h = `<div class="nodes">` + NODES.map((nm, i) => `<div class="node"><span>${esc(nm)}</span>${chip(n[i] ? 'ONLINE' : 'OFFLINE', n[i] ? 'run' : 'off')}</div>`).join('') + `</div>`;
  const rows = [];
  if (sy.up != null) rows.push(['Online Server running for', fmtUp(sy.up)]);
  if (sy.rst) rows.push(['Last restart reason', esc(sy.rst) + (sy.boots != null ? ` (start #${sy.boots})` : '')]);
  if (sy.rssi) rows.push(['Wi-Fi signal at the site', sy.rssi + ' dBm']);
  if (sy.heap != null) rows.push(['Free memory', Math.round(sy.heap / 1024) + ' KB']);
  if (rows.length) h += `<div style="margin-top:10px">` + rows.map(r => `<div class="row"><span>${r[0]}</span><b class="num">${r[1]}</b></div>`).join('') + `</div>`;
  if (sy.mq != null && sy.mqmax > 0) {
    const p = Math.min(100, Math.round(sy.mq / sy.mqmax * 100));
    h += `<div class="lv"><div class="lv-t"><span>Blynk messages used this month</span><b class="num">${Number(sy.mq).toLocaleString()} / ${Number(sy.mqmax).toLocaleString()}</b></div><div class="bar ${p > 90 ? 'bad' : p > 70 ? 'warn' : ''}"><i style="width:${p}%"></i></div></div>`;
  } else if (sy.mq != null) {
    h += `<div class="row"><span>Blynk uploads this month</span><b class="num">${Number(sy.mq).toLocaleString()}</b></div>`;
  }
  $('sys').innerHTML = h;
}

/* ---- the whole page ---- */
function renderAll() {
  const L = S.live;
  $('connect').hidden = true; $('view').hidden = false;
  const ban = [];
  if (S.err) ban.push(`<div class="note"><b>Cannot read Blynk:</b> ${esc(S.err)}${/token/i.test(S.err) ? ' Open Settings and check the token.' : ''}</div>`);
  if (S.noData && !S.err) ban.push(`<div class="note"><b>Connected to Blynk, but no data yet.</b> The Online Server has not written datastream v0 yet (check that it is powered, that its setup page says ONLINE, and that datastream v0 exists in the Blynk template).</div>`);
  if (L) { const al = alertsFor(L); if (al.length) ban.push(`<div class="alert">Needs attention<ul>${al.map(a => `<li>${esc(a)}</li>`).join('')}</ul></div>`); }
  $('banners').innerHTML = ban.join('');
  if (L) {
    const n = nodeFlags(L);
    $('boxes').innerHTML = [0, 1, 2].map(i => boxCard(i, (L.b || [])[i], n)).join('');
    $('acs').innerHTML = [0, 1].map(i => acsCard(i, (L.a || [])[i])).join('');
  } else { $('boxes').innerHTML = ''; $('acs').innerHTML = ''; }
  $('data').hidden = !L;                                // nothing to show until the first data arrives
  renderStats(); renderSys();
}
