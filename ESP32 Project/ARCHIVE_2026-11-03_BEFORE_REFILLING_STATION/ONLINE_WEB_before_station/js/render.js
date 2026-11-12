"use strict";
/* Turns the data in S into HTML. One function per card; renderAll() puts them on the page. */

/* ---- small building blocks ---- */
function stateClass(name) {
  if (name === 'OUT OF ORDER') return 'bad';
  if (name === 'MAINTENANCE') return 'maint';
  if (RUNNING.has(name)) return 'run';
  if (ATTN.has(name)) return 'warn';
  if (IDLE.has(name)) return '';
  return 'use';
}
function nodeFlags(L) {                                 // the "heard / not heard" flags, one per board in NODES
  const n = L.n, f = Array.isArray(n) ? n.map(Number) : String(n == null ? '' : n).split('').map(Number);
  while (f.length < NODES.length) f.push(L.n == null ? 1 : 0);    // no "n" at all: assume everything is there
  return f.slice(0, NODES.length).map(v => v ? 1 : 0);
}

/* ---- one box, drawn as a little machine: screen, coin slot, helmet window with UV tubes, mist, heater, fan and drain pumps, doors with their locks, and the main container beside it ---- */
function boxDrawing(def, f) {
  const k = nm => def.relays.indexOf(nm), has = nm => k(nm) >= 0;
  const R = nm => f.relOn && has(nm) && ((f.relays >> k(nm)) & 1) === 1;             // is this relay ON now (unknown = off while the relay board is silent)
  const lb = (x, y, t, a) => `<text class="bx-lb" x="${x}" y="${y}"${a ? ` text-anchor="${a}"` : ''}>${esc(t)}</text>`;
  let s = `<rect class="bx-body" x="10" y="4" width="212" height="322" rx="16"/>`;
  /* the touch screen */
  s += `<rect class="bx-scr ${f.brainOn ? stateClass(f.name) : 'off'}" x="30" y="16" width="164" height="48" rx="6"/><text class="bx-st" x="112" y="35" text-anchor="middle">${esc(f.name)}</text>`;
  if (f.brainOn && (RUNNING.has(f.name) || f.timer > 0)) s += `<text class="bx-tm" x="112" y="55" text-anchor="middle">${mmss(f.timer)}</text>`;
  /* coin slot (the light is the coin power relay) and the helmet sensor */
  const hs = !f.sensOn || !(typeof f.helmet === 'number' && f.helmet >= 0) ? 'unk' : (f.helmet > 0 && f.helmet < 15 ? 'in' : 'out');
  s += `<rect class="bx-slot" x="30" y="70" width="104" height="20" rx="10"/><circle class="bx-led ${R('Coin slot power') ? 'on' : ''}" cx="44" cy="80" r="4"/><text class="bx-sm" x="92" y="84" text-anchor="middle">coins ${f.termOn ? f.coins : '-'}</text>`;
  s += `<rect class="bx-slot" x="140" y="70" width="54" height="20" rx="10"/><text class="bx-sm" x="167" y="84" text-anchor="middle">${{ unk: 'helmet ?', in: 'helmet in', out: 'no helmet' }[hs]}</text>`;
  /* the window: UV tubes top and bottom, the helmet in the middle */
  s += `<rect class="bx-win" x="30" y="96" width="164" height="126" rx="8"/>`;
  def.relays.filter(r => /^UV/.test(r)).slice(0, 2).forEach((nm, j) => {
    const y = j ? 206 : 108, on = R(nm);
    s += lb(190, y - 3, nm, 'end') + (on ? `<rect class="bx-uvh" x="40" y="${y - 3}" width="144" height="14" rx="7"/>` : '') + `<rect class="bx-uv ${on ? 'on' : ''}" x="44" y="${y}" width="136" height="8" rx="4"><title>${esc(nm)} ${on ? 'ON' : 'off'}</title></rect>`;
  });
  s += `<path class="bx-hel ${hs}" d="M88 176 L88 152 C88 134 99 124 112 124 C125 124 136 134 136 152 L136 176 Z"/><rect class="bx-vis ${hs}" x="94" y="144" width="36" height="13" rx="4"/>`;
  if (hs === 'unk') s += `<text class="bx-q" x="112" y="162" text-anchor="middle">?</text>`;
  [['Humidifier 1 (alcohol)', 54, 'H1'], ['Humidifier 3 (both)', 112, 'H3'], ['Humidifier 2 (scented)', 170, 'H2']].forEach(([nm, cx, t]) => {
    if (!has(nm)) return; const on = R(nm);
    s += `<rect class="bx-noz" x="${cx - 7}" y="180" width="14" height="5" rx="2"><title>${esc(nm)} ${on ? 'ON' : 'off'}</title></rect>`;
    if (on) s += [-4, 4].map(d => `<path class="bx-mist" d="M${cx + d} 177 q-4 -5 0 -8 t0 -8 t0 -8"/>`).join('');
    s += lb(cx, 194, t, 'middle');
  });
  if (has('Heater')) {
    const on = R('Heater'), d = 'M150 152 l6 -14 l6 14 l6 -14 l6 14 l6 -14 l6 14';
    s += (on ? `<path class="bx-heath" d="${d}"/>` : '') + `<path class="bx-heat ${on ? 'on' : ''}" d="${d}"><title>Heater ${on ? 'ON' : 'off'}</title></path>` + lb(168, 166, 'heater', 'middle');
  }
  if (has('Fan')) {
    const on = R('Fan');
    s += `<g transform="translate(62 146)"><g class="bx-fan ${on ? 'on' : ''}"><circle class="bx-fanr" r="14"/>` + [0, 90, 180, 270].map(a => `<path d="M0 0 C4 -4 4 -11 0 -12 C-4 -11 -4 -4 0 0Z" transform="rotate(${a})"/>`).join('') + `<title>Fan ${on ? 'ON' : 'off'}</title></g></g>` + lb(62, 170, 'fan', 'middle');
  }
  /* drain pumps under the window */
  const dr = def.relays.filter(r => /^Drain pump/.test(r));
  dr.forEach((nm, j) => {
    const cx = dr.length === 1 ? 112 : (j ? 164 : 60), on = R(nm);
    s += `<rect class="bx-pump ${on ? 'on' : ''}" x="${cx - 14}" y="232" width="28" height="12" rx="3"><title>${esc(nm)} ${on ? 'ON' : 'off'}</title></rect><path class="bx-drop ${on ? 'on' : ''}" d="M${cx} 247 c-3 4 -3 7 0 7 s3 -3 0 -7z"/>` + lb(cx, 264, nm.replace('Drain pump ', 'drain '), 'middle');
  });
  /* doors (open / closed from the sensor board) with the padlock above each (the lock relay) */
  const slot = 212 / def.doors.length;
  def.doors.forEach((d, i) => {
    const cx = 10 + slot * (i + .5), open = f.sensOn && ((f.doors >> i) & 1) === 1, ln = d === 'ACS side' ? 'ACS door lock' : d + ' lock';
    if (has(ln)) s += `<path class="bx-shk" d="M${cx - 3} 275 v-3 a3 3 0 0 1 6 0 v3"/><rect class="bx-lk ${!f.relOn ? 'unk' : R(ln) ? 'on' : ''}" x="${cx - 4.5}" y="275" width="9" height="7" rx="1.5"><title>${esc(ln)} ${R(ln) ? 'ON' : 'off'}</title></rect>`;
    s += open ? `<path class="bx-door open" d="M${cx - 9} 286 L${cx + 6} 290 V308 L${cx - 9} 306 Z"><title>${esc(d)} door OPEN</title></path>` : `<rect class="bx-door ${f.sensOn ? 'shut' : 'unk'}" x="${cx - 9}" y="286" width="18" height="22" rx="2"><title>${esc(d)} door ${f.sensOn ? 'closed' : 'not known'}</title></rect>`;
    s += lb(cx, 320, d, 'middle');
  });
  /* the main container beside the machine, with its hose */
  const p = f.lvl >= 0 ? Math.max(0, Math.min(100, f.lvl)) : -1, w = warnPct(), th = 226, fh = p < 0 ? 0 : p / 100 * th;
  const lc = p < 0 ? 'unk' : p < 15 ? 'low' : (w && p < w) ? 'early' : 'ok';
  s += `<path class="bx-hose" d="M222 150 H238"/><rect class="bx-tank" x="238" y="16" width="30" height="${th + 4}" rx="6"/>`;
  if (fh) s += `<rect class="bx-fill ${lc}" x="240" y="${(18 + th - fh).toFixed(1)}" width="26" height="${fh.toFixed(1)}" rx="4"/>`;
  else s += `<text class="bx-q" x="253" y="135" text-anchor="middle">?</text>`;
  s += `<text class="bx-pc" x="253" y="268" text-anchor="middle">${p < 0 ? '-' : Math.round(p) + '%'}</text>` + lb(253, 282, 'main', 'middle') + lb(253, 292, 'container', 'middle');
  return s;
}
function boxCard(i, b, n) {
  const def = BOXES[i];
  const head = (right) => `<div class="card-h"><div><b>${def.name}</b><small>${def.role}</small></div>${right}</div>`;
  if (!b) return `<div class="card">${head(chip('no data', 'off'))}</div>`;
  const brainOn = n[def.brain], termOn = n[def.term], sensOn = n[def.sensors], relOn = n[def.relayBoard];
  const f = { brainOn, termOn, sensOn, relOn, name: brainOn ? (def.st[b[0]] || ('step ' + b[0])) : 'NOT HEARD',
              timer: nz(b[1], 0), coins: nz(b[2], 0), doors: nz(b[3], 0), helmet: b[4], relays: nz(b[8], 0), lvl: sensOn ? nz(b[5], -1) : -1 };
  const same = def.sensors === def.relayBoard, off = [];
  if (!sensOn) off.push(same ? 'sensor and relay board offline' : 'sensor board offline');
  if (!relOn && !(same && !sensOn)) off.push('relay board offline');
  const say = `${def.name}: ${f.name}${brainOn && f.timer > 0 ? ', ' + mmss(f.timer) + ' left' : ''}${termOn ? ', ' + f.coins + ' coins' : ''}${f.lvl >= 0 ? ', main container ' + Math.round(f.lvl) + '%' : ''}`;
  return `<div class="card">` + head(`<span class="chip state ${brainOn ? stateClass(f.name) : 'off'}">${esc(f.name)}</span>`)
    + `<svg class="bx-svg" viewBox="0 0 280 332" role="img" aria-label="${esc(say)}">${boxDrawing(def, f)}</svg>`
    + (off.length ? `<div class="chips">${off.map(t => chip(t, 'off')).join('')}</div>` : '') + `</div>`;
}
/* ---- the banners at the top of the Monitor tab (errors, then everything that needs attention: see alerts.js) ---- */
function drawBanners() {
  const ban = [];
  if (S.err) ban.push(`<div class="note"><b>Cannot read Blynk:</b> ${esc(S.err)}${/token/i.test(S.err) ? ' Open Settings and check the token.' : ''}</div>`);
  if (S.noData && !S.err) ban.push(`<div class="note"><b>Connected to Blynk, but no data yet.</b> The Online Server has not written datastream v0 yet (check that it is powered, that its setup page says ONLINE, and that datastream v0 exists in the Blynk template).</div>`);
  ban.push(Alerts.banner());
  const h = ban.join('');
  const el = $('banners'); if (el && el.dataset.h !== h) { el.dataset.h = h; el.innerHTML = h; }
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
    // restated old data (the spare ESP32_RESTATE board's Update button): shown NEXT TO what was really collected, never instead of it
    const R = T.rs;
    if (Array.isArray(R) && R.length >= 7) {
      const cvOld = nz(R[6], 100) / 100, down = R[5] < 0, newCn = down ? 0 : nz(R[5], 0);
      const collRev = nz(R[3], 0) * cvOld + newCn * cv, restRev = nz(R[4], 0) * cvOld + newCn * cv;
      const when = R[1] ? new Date(R[1] * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : 'an unknown date';
      if (R[7] === 1) {                                  // mode 1: the figures above ARE the corrected ones; only say so (and what they were)
        const dt = R[1] ? new Date(R[1] * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : 'an unknown date';
        k += `<div class="card empty" style="grid-column:1/-1;text-align:left">Statistics corrected (data up to ${esc(dt)}): the old price list charged more coins per minute than the new one, so the coins counted before then were restated at the new price list (${nz(R[3], 0).toLocaleString()} coins / ${money(nz(R[3], 0) * cvOld)} counted, restated to ${nz(R[4], 0).toLocaleString()} coins). The original figures are archived. Update #${R[0]}.</div>`;
      } else {
      k += tile(money(restRev), `Revenue restated at the new price list<small>collected: ${money(collRev)}</small>`)
        + tile((nz(R[4], 0) + newCn).toLocaleString(), `Coins restated<small>collected: ${(nz(R[3], 0) + newCn).toLocaleString()}</small>`);
      k += `<div class="card empty" style="grid-column:1/-1;text-align:left">Restated figures (update #${R[0]}): the old data up to ${esc(when)} (${nz(R[2], 0).toLocaleString()} customers, ${nz(R[3], 0).toLocaleString()} coins collected) is restated to ${nz(R[4], 0).toLocaleString()} coins at the new price list; everything sold after that is as collected. The coins really collected are never changed.${down ? ' <b>The LocalServer\'s statistics were reset after the capture, so the figures since then are not counted.</b>' : ''}</div>`;
      }
    }
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

/* ---- the network map (Blynk - site Wi-Fi - Online Server, then the three boxes and the LocalServer) ---- */
const NM_ICON = {
  net:  '<svg viewBox="0 0 64 64"><circle cx="32" cy="32" r="22"/><path d="M10 32h44M32 10c9 9 9 35 0 44M32 10c-9 9-9 35 0 44M14 20h36M14 44h36"/></svg>',
  wifi: '<svg viewBox="0 0 64 64"><path d="M8 46h48v8H8zM14 46l6-14h24l6 14"/><path d="M44 26c5 0 8-3 8-3M46 18c6 0 10-4 10-4"/><circle cx="16" cy="50" r="1.5"/><circle cx="24" cy="50" r="1.5"/><path d="M32 32V18"/></svg>',
  os:   '<svg viewBox="0 0 64 64"><rect x="10" y="16" width="44" height="32" rx="5"/><path d="M18 24h10M18 32h10M36 24h10"/><circle cx="44" cy="38" r="3"/><path d="M22 48v6M42 48v6M18 54h28"/></svg>',
  box:  '<svg viewBox="0 0 64 64"><rect x="14" y="8" width="36" height="48" rx="4"/><rect x="20" y="14" width="24" height="18" rx="2"/><path d="M22 40h20M22 46h12"/><circle cx="44" cy="46" r="2"/></svg>',
  ls:   '<svg viewBox="0 0 64 64"><rect x="12" y="10" width="40" height="14" rx="3"/><rect x="12" y="28" width="40" height="14" rx="3"/><circle cx="20" cy="17" r="1.8"/><circle cx="20" cy="35" r="1.8"/><path d="M32 42v8M20 54h24M32 50v4"/></svg>'
};
function nmNode(icon, title, subHtml, st) {
  return `<div class="nm-node"><span class="nm-dot ${st}"></span>${NM_ICON[icon]}${esc(title)}<small>${subHtml}</small></div>`;
}
function nmBars(rssi) {                                    // four little bars for the Wi-Fi signal at the site
  const q = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  return '<span class="nm-bars">' + [1, 2, 3, 4].map(i => `<i class="${i <= q ? 'on' : ''}" style="height:${4 + i * 3}px"></i>`).join('') + '</span>';
}
function networkMap(L, T, sy) {
  const link = S.link, stale = link === 'dead' || link === 'err';          // the boards' flags are old: show them as unknown, not green
  const dotOf = { live: 'ok', late: 'warn', dead: 'bad', err: 'bad', nodata: 'warn' }[link] || 'off';
  const lineOf = { live: '', late: 'bad', dead: 'bad', err: 'bad' }[link] ?? 'off';       // the lines: solid = fine, red dashes = broken, grey dashes = not known yet
  const known = !!S.live && L.n != null && !stale, n = known ? nodeFlags(L) : [];
  const netSub = { live: 'Blynk · OK', late: 'Blynk · delayed', dead: 'Blynk · old data', err: 'cannot read Blynk', nodata: 'Blynk · no data yet' }[link] || 'Blynk';
  const wifiSub = (sy.rssi ? sy.rssi + ' dBm<br>' + nmBars(sy.rssi) : 'site Wi-Fi');
  const osSub = link === 'live' || link === 'late' ? (sy.up != null ? 'running ' + esc(fmtUp(sy.up)) : 'listens only') : (link === 'dead' ? 'not uploading' : 'listens only');
  const boxes = BOXES.map((def, i) => {
    const ids = [...new Set([def.term, def.sensors, def.relayBoard, def.brain])], missing = ids.filter(k => !n[k]);
    if (!known) return nmNode('box', def.name, stale ? 'no recent data' : 'not reported', 'off');
    const st = !missing.length ? 'ok' : missing.length < ids.length ? 'warn' : 'bad';
    const miss = missing.length && missing.length < ids.length ? `<span class="nm-miss">missing: ${missing.map(k => esc(NODES[k].replace(/ \(Box \d\)/, ''))).join(', ')}</span>` : '';
    return nmNode('box', def.name, `${ids.length - missing.length} of ${ids.length} boards${miss ? '<br>' + miss : ''}`, st);
  }).join('');
  const lsHeard = T.cv != null || T.ss != null;
  const ls = nmNode('ls', 'LocalServer', stale || !S.live ? 'no recent data' : lsHeard ? 'totals received' : 'not heard yet', stale || !S.live ? 'off' : lsHeard ? 'ok' : 'bad');
  return `<div class="nmap">
    <div class="nm-top">
      <div class="nm-tl">${nmNode('net', 'Internet', netSub, dotOf)}<div class="nm-link ${lineOf}"></div>${nmNode('wifi', 'Wi-Fi', wifiSub, dotOf)}<div class="nm-link nm-l2 ${lineOf}"></div></div>
      <div class="nm-vl ${lineOf}"></div>
      ${nmNode('os', 'Online Server', osSub, dotOf)}
      <div class="nm-tr"></div>
    </div>
    <div class="nm-drop"></div>
    <div class="nm-groups">
      <div class="nm-group"><div class="nm-gtitle">Machines heard on the radio</div><div class="nm-items">${boxes}</div></div>
      <div class="nm-group nm-ls"><div class="nm-gtitle">Server heard</div><div class="nm-items">${ls}</div></div>
    </div>
    <div class="nm-legend"><span><i class="ok"></i>heard / working</span><span><i class="warn"></i>some missing</span><span><i class="bad"></i>not heard / failed</span><span><i class="off"></i>unknown</span></div>
  </div>`;
}
function nmBracket() {                                     // the horizontal line over the two groups reaches exactly from the middle of one to the middle of the other
  const g = document.querySelector('#sys .nm-groups'); if (!g) return;
  const gs = [...g.querySelectorAll('.nm-group')], r = g.getBoundingClientRect();
  const c = gs.map(e => { const b = e.getBoundingClientRect(); return b.left + b.width / 2 - r.left; }); c.push(r.width / 2);
  g.style.setProperty('--bl', Math.min(...c) + 'px'); g.style.setProperty('--br', (r.width - Math.max(...c)) + 'px');
}
addEventListener('resize', nmBracket);

/* ---- the network map and the Online Server's own health ---- */
function renderSys() {
  const L = S.live || {}, T = S.stats || {}, sy = T.sys || {};
  let h = networkMap(L, T, sy);
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
  nmBracket();
}

/* ---- the whole page ---- */
function renderAll() {
  const L = S.live;
  $('connect').hidden = true; $('view').hidden = false;
  drawBanners();
  if (L) {
    const n = nodeFlags(L);
    $('boxes').innerHTML = [0, 1, 2].map(i => boxCard(i, (L.b || [])[i], n)).join('');
    renderLevels(L);
  } else { $('boxes').innerHTML = ''; $('levels').innerHTML = ''; }
  $('data').hidden = !L;                                // nothing to show until the first data arrives
  renderStats(); renderSys();
  if (typeof Reports !== 'undefined') Reports.refresh();
}
