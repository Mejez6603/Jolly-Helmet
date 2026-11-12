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

/* ---- one box, drawn as a little machine: screen, coin slot, helmet window with UV tubes, mist, heater and fan, doors with their locks, and the main container beside it ---- */
function boxDrawing(def, f) {
  const k = nm => def.relays.indexOf(nm), has = nm => k(nm) >= 0;
  const R = nm => f.relOn && has(nm) && ((f.relays >> k(nm)) & 1) === 1;             // is this relay ON now (unknown = off while the relay board is silent)
  const lb = (x, y, t, a) => `<text class="bx-lb" x="${x}" y="${y}"${a ? ` text-anchor="${a}"` : ''}>${esc(t)}</text>`;
  let s = `<rect class="bx-body" x="10" y="4" width="212" height="284" rx="16"/>`;
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
  [['Humidifier 1', 54, 'Mist 1'], ['Humidifier 2', 112, 'Mist 2'], ['Humidifier 3', 170, 'Mist 3']].forEach(([nm, cx, t]) => {
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
  /* doors (open / closed from the sensor board) with the padlock above each (the lock relay) */
  const slot = 212 / def.doors.length;
  def.doors.forEach((d, i) => {
    const cx = 10 + slot * (i + .5), open = f.sensOn && ((f.doors >> i) & 1) === 1, ln = d + ' lock';
    if (has(ln)) s += `<path class="bx-shk" d="M${cx - 3} 237 v-3 a3 3 0 0 1 6 0 v3"/><rect class="bx-lk ${!f.relOn ? 'unk' : R(ln) ? 'on' : ''}" x="${cx - 4.5}" y="237" width="9" height="7" rx="1.5"><title>${esc(ln)} ${R(ln) ? 'ON' : 'off'}</title></rect>`;
    s += open ? `<path class="bx-door open" d="M${cx - 9} 248 L${cx + 6} 252 V270 L${cx - 9} 268 Z"><title>${esc(d)} door OPEN</title></path>` : `<rect class="bx-door ${f.sensOn ? 'shut' : 'unk'}" x="${cx - 9}" y="248" width="18" height="22" rx="2"><title>${esc(d)} door ${f.sensOn ? 'closed' : 'not known'}</title></rect>`;
    s += lb(cx, 282, d, 'middle');
  });
  /* the main container beside the machine, with its hose; its level comes from the Refilling Station (f.rf) or, on older firmware, from the box's own sensor (f.lvl) */
  const th = 196, rfk = Array.isArray(f.rf) && f.rf[1] >= 0 && f.rf[1] <= 4;
  let p, lc, word;
  if (rfk) { const mx = Math.max(1, nz(f.rf[6], 1200)); p = Math.max(0, Math.min(100, nz(f.rf[0], 0) / mx * 100)); lc = ['low', 'low', 'early', 'high', 'ok'][f.rf[1]]; word = RF_STATE[f.rf[1]]; }
  else { p = f.lvl >= 0 ? Math.max(0, Math.min(100, f.lvl)) : -1; const w = warnPct(); lc = p < 0 ? 'unk' : p < 15 ? 'low' : (w && p < w) ? 'early' : 'ok'; word = p < 0 ? '-' : Math.round(p) + '%'; }
  const fh = p < 0 ? 0 : p / 100 * th;
  s += `<path class="bx-hose" d="M222 150 H238"/><rect class="bx-tank" x="238" y="16" width="30" height="${th + 4}" rx="6"/>`;
  if (fh) s += `<rect class="bx-fill ${lc}" x="240" y="${(18 + th - fh).toFixed(1)}" width="26" height="${fh.toFixed(1)}" rx="4"/>`;
  else if (!rfk) s += `<text class="bx-q" x="253" y="125" text-anchor="middle">?</text>`;
  const rfNw = Array.isArray(f.rf) && f.rf[1] === -2;                                      /* the station does not watch this box for now */
  if (((f.rf !== undefined && f.rf !== null) || f.rfOff) && !rfNw) {                       /* the refill valve above the tank: bow-tie, blue while the valve is open */
    const open = rfk && f.rf[2];
    s += `<path class="bx-valve ${open ? 'on' : ''}" d="M246 2 L253 8 L246 14 Z M260 2 L253 8 L260 14 Z"><title>Refill valve ${open ? 'OPEN' : 'closed'}</title></path>`;
  }
  s += `<text class="bx-pc" x="253" y="238" text-anchor="middle">${word}</text>` + lb(253, 252, 'main', 'middle') + lb(253, 262, 'container', 'middle');
  if (f.rfOff) s += `<text class="bx-warn" x="253" y="274" text-anchor="middle">station off</text>`;
  else if (rfNw) s += lb(253, 274, 'not watched', 'middle');
  else if (rfk && f.rf[3]) s += `<text class="bx-warn" x="253" y="274" text-anchor="middle">refill failed</text>`;
  else if (rfk && f.rf[2]) s += `<text class="bx-wet" x="253" y="274" text-anchor="middle">refilling</text>`;
  else if (rfk && f.rf[7]) s += `<text class="bx-wet" x="253" y="274" text-anchor="middle">waiting</text>`;  return s;
}
function boxCard(i, b, n, rfi) {                                // rfi = { r: [raw, state, valve, failed, edges...] or null, off: station not heard } when the firmware has the Refilling Station
  const def = BOXES[i];
  const head = (right) => `<div class="card-h"><div><b>${def.name}</b><small>${def.role}</small></div>${right}</div>`;
  if (!b) return `<div class="card">${head(chip('Inactive', 'off'))}</div>`;
  const brainOn = n[def.brain], termOn = n[def.term], sensOn = n[def.sensors], relOn = n[def.relayBoard];
  /* the chip says only Active or Inactive: Active = every board of this box is heard AND the data is fresh (the screen in the drawing still shows the step) */
  const stale = S.link === 'dead' || S.link === 'err', ids = [...new Set([def.term, def.sensors, def.relayBoard, def.brain])];
  const active = !stale && ids.every(k => n[k]);
  const f = { brainOn, termOn, sensOn, relOn, name: brainOn ? (def.st[b[0]] || ('step ' + b[0])) : 'INACTIVE',
              timer: nz(b[1], 0), coins: nz(b[2], 0), doors: nz(b[3], 0), helmet: b[4], relays: nz(b[8], 0), lvl: sensOn ? nz(b[5], -1) : -1,
              rf: rfi && !rfi.off ? rfi.r : (rfi ? null : undefined), rfOff: !!(rfi && rfi.off) };
  const same = def.sensors === def.relayBoard, off = [];
  if (!sensOn) off.push(same ? 'sensor and relay board inactive' : 'sensor board inactive');
  if (!relOn && !(same && !sensOn)) off.push('relay board inactive');
  const say = `${def.name} ${active ? 'active' : 'inactive'}: ${f.name}${brainOn && f.timer > 0 ? ', ' + mmss(f.timer) + ' left' : ''}${termOn ? ', ' + f.coins + ' coins' : ''}${Array.isArray(f.rf) && f.rf[1] >= 0 ? ', main container ' + RF_STATE[f.rf[1]] : (f.lvl >= 0 ? ', main container ' + Math.round(f.lvl) + '%' : '')}`;
  return `<div class="card">` + head(`<span class="chip state ${active ? 'run' : 'off'}">${active ? 'Active' : 'Inactive'}</span>`)
    + `<svg class="bx-svg" viewBox="0 0 280 296" role="img" aria-label="${esc(say)}">${boxDrawing(def, f)}</svg>`
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
  rs:   '<svg viewBox="0 0 64 64"><path d="M22 8h20l-2 14H24z"/><path d="M32 22v10M20 32h24M24 32v10h16V32"/><path d="M32 42v8M26 50h12M22 56h20"/><circle cx="32" cy="37" r="2"/></svg>',
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
  /* Every part of the map is either Active (green) or Inactive (red). Anything that is old, silent or not known counts as Inactive. */
  const link = S.link, stale = link === 'dead' || link === 'err';          // the boards' flags are old: nothing can be called Active
  const up = link === 'live' || link === 'late';                           // data is arriving
  const dotOf = up ? 'ok' : 'bad';
  const lineOf = up ? '' : 'bad';                                          // the lines: solid = Active, red dashes = Inactive
  const known = !!S.live && L.n != null && !stale, n = known ? nodeFlags(L) : [];
  const nmSt = ok => `<b class="nm-st ${ok ? 'ok' : 'bad'}">${ok ? 'Active' : 'Inactive'}</b>`;
  const netSub = nmSt(up);
  const wifiSub = nmSt(up) + (up && sy.rssi ? '<br>' + sy.rssi + ' dBm<br>' + nmBars(sy.rssi) : '');
  const osSub = nmSt(up) + (up && sy.up != null ? '<br>running ' + esc(fmtUp(sy.up)) : '');
  const boxes = BOXES.map((def, i) => {
    const ids = [...new Set([def.term, def.sensors, def.relayBoard, def.brain])], missing = ids.filter(k => !n[k]);
    if (!known) return nmNode('box', def.name, nmSt(false), 'bad');
    /* a box is Active only when ALL of its boards are heard; if some are silent it is Inactive and the map names the silent ones */
    if (missing.length) {
      const list = missing.length < ids.length ? `<br><span class="nm-miss">missing: ${missing.map(k => esc(NODES[k].replace(/^Box \d /, ''))).join(', ')}</span>` : '';
      return nmNode('box', def.name, nmSt(false) + list, 'bad');
    }
    return nmNode('box', def.name, nmSt(true), 'ok');
  }).join('');
  const hasRf = Array.isArray(L.rf);
  const open = hasRf ? [0, 1, 2].filter(i => L.rf[i] && L.rf[i][2]).map(i => i + 1) : [];
  const waiting = hasRf ? [0, 1, 2].filter(i => L.rf[i] && L.rf[i][7]).map(i => i + 1) : [];
  const rsNode = !known || !hasRf ? nmNode('rs', 'Refilling Station', nmSt(false), 'bad')
    : n[STATION] ? nmNode('rs', 'Refilling Station', nmSt(true) + (open.length ? '<br>refilling Box ' + open.join(', ') + (waiting.length ? '<br><span class="nm-miss">waiting: Box ' + waiting.join(', ') + '</span>' : '') : ''), 'ok')
    : nmNode('rs', 'Refilling Station', nmSt(false) + '<br><span class="nm-miss">all boxes: drying only</span>', 'bad');
  const lsHeard = T.cv != null || T.ss != null, lsOn = !stale && !!S.live && lsHeard;
  const ls = nmNode('ls', 'LocalServer', nmSt(lsOn), lsOn ? 'ok' : 'bad');
  return `<div class="nmap">
    <div class="nm-top">
      <div class="nm-tl">${nmNode('net', 'Internet', netSub, dotOf)}<div class="nm-link ${lineOf}"></div>${nmNode('wifi', 'Wi-Fi', wifiSub, dotOf)}<div class="nm-link nm-l2 ${lineOf}"></div></div>
      <div class="nm-vl ${lineOf}"></div>
      ${nmNode('os', 'Online Server', osSub, dotOf)}
      <div class="nm-tr"></div>
    </div>
    <div class="nm-drop"></div>
    <div class="nm-groups">
      <div class="nm-group"><div class="nm-gtitle">Machines heard on the radio</div><div class="nm-items">${boxes}${rsNode}</div></div>
      <div class="nm-group nm-ls"><div class="nm-gtitle">Server heard</div><div class="nm-items">${ls}</div></div>
    </div>
    <div class="nm-legend"><span><i class="ok"></i>Active</span><span><i class="bad"></i>Inactive</span></div>
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
    $('boxes').innerHTML = [0, 1, 2].map(i => boxCard(i, (L.b || [])[i], n, Array.isArray(L.rf) ? { r: L.rf[i], off: !n[STATION] } : null)).join('');
    renderLevels(L);
  } else { $('boxes').innerHTML = ''; $('levels').innerHTML = ''; }
  $('data').hidden = !L;                                // nothing to show until the first data arrives
  renderStats(); renderSys();
  if (typeof Reports !== 'undefined') Reports.refresh();
}
