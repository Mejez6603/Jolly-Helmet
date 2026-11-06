"use strict";
/* Level gauges: 3 half-circle dials - the main container of Box 1, of Box 2 and of Box 3 (the Refilling Station fills them).
   The dial turns amber at the EARLY WARNING line (Settings, default 30 %) and red below 15 %, so somebody can refill BEFORE a box shows OUT OF ORDER.
   Under each dial: "falling / steady / rising" and, when it is falling, about how long until the refill line (0 %) - worked out from the readings this
   browser has seen (kept in this browser, about the last 6 hours).

   Scale: 0 % = the refill line (the box stops cleaning there), 100 % = full. See DATA_CONTRACT.md. */

/* ---- readings over time, kept in this browser ---- */
const Hist = (() => {
  const KEY = 'jh_hist2', MAX = 360, MIN_GAP = 90 * 1000;       // at most one point every 90 s per series, 360 points
  let data = {};
  try { data = JSON.parse(store.get(KEY) || '{}') || {}; } catch (e) { data = {}; }
  let dirty = false;
  function push(key, v, t) {
    if (typeof v !== 'number' || v < 0) return;
    const a = data[key] || (data[key] = []);
    if (a.length && t - a[a.length - 1][0] < MIN_GAP) return;
    a.push([t, Math.round(v)]); if (a.length > MAX) a.shift(); dirty = true;
  }
  function save() { if (dirty) { store.set(KEY, JSON.stringify(data)); dirty = false; } }
  setInterval(save, 20000); addEventListener('pagehide', save);
  /* straight line through the last 90 minutes of points: percent per hour (negative = falling) */
  function slope(key) {
    const a = data[key]; if (!a || a.length < 4) return null;
    const t1 = a[a.length - 1][0], pts = a.filter(p => t1 - p[0] <= 90 * 60 * 1000);
    if (pts.length < 4 || t1 - pts[0][0] < 10 * 60 * 1000) return null;
    const n = pts.length, mx = pts.reduce((s, p) => s + p[0], 0) / n, my = pts.reduce((s, p) => s + p[1], 0) / n;
    let num = 0, den = 0; pts.forEach(p => { num += (p[0] - mx) * (p[1] - my); den += (p[0] - mx) * (p[0] - mx); });
    return den ? num / den * 3600000 : null;
  }
  return { push, slope, save, data: () => data };
})();

/* the early warning line (percent) the user chose; 0 = no early warning */
function warnPct() { const v = Number(store.get('jh_warn')); return store.get('jh_warn') === null ? 30 : (isFinite(v) ? v : 30); }

/* ---- one dial ---- */
function dialArc(a0, a1, r, cx, cy) {                           // angles in percent of the half circle (0 = left end, 100 = right end)
  const p = v => { const th = Math.PI * (1 - v / 100); return [cx + r * Math.cos(th), cy - r * Math.sin(th)]; };
  const s = p(a0), e = p(a1);
  return `M${s[0].toFixed(1)} ${s[1].toFixed(1)} A${r} ${r} 0 0 1 ${e[0].toFixed(1)} ${e[1].toFixed(1)}`;
}
function gaugeCard(title, sub, pct, key, stale) {
  const known = typeof pct === 'number' && pct >= 0, w = warnPct();
  const p = known ? Math.max(0, Math.min(100, pct)) : 0;
  const st = !known ? 'unknown' : p < 15 ? 'low' : (w && p < w) ? 'early' : 'ok';
  const cx = 60, cy = 54, r = 46;
  let svg = `<svg viewBox="0 0 120 80" class="dial" role="img" aria-label="${esc(title)}: ${known ? Math.round(p) + ' percent' : 'inactive'}">`;
  svg += `<path d="${dialArc(0, 15, r, cx, cy)}" class="z-low"/>`;
  svg += `<path d="${dialArc(15, w > 15 ? w : 15.01, r, cx, cy)}" class="z-early"/>`;
  svg += `<path d="${dialArc(w > 15 ? w : 15, 100, r, cx, cy)}" class="z-ok"/>`;
  if (known) {
    const th = Math.PI * (1 - p / 100), nx = cx + (r - 9) * Math.cos(th), ny = cy - (r - 9) * Math.sin(th);
    svg += `<line x1="${cx}" y1="${cy}" x2="${nx.toFixed(1)}" y2="${ny.toFixed(1)}" class="needle"/><circle cx="${cx}" cy="${cy}" r="4" class="hub"/>`;
  }
  svg += `<text x="${cx}" y="${cy + 23}" text-anchor="middle" class="dv">${known ? Math.round(p) + '%' : '-'}</text>`;
  svg += `<text x="${cx - r}" y="${cy + 11}" text-anchor="middle" class="dl">0</text><text x="${cx + r}" y="${cy + 11}" text-anchor="middle" class="dl">100</text></svg>`;
  let note = '';
  if (known) {
    const sl = Hist.slope(key);
    if (sl !== null) {
      if (sl <= -0.8) {
        const h = p / -sl;
        note = `&#8595; falling, about ${h < 1 ? Math.max(1, Math.round(h * 60)) + ' min' : h < 48 ? (h < 10 ? h.toFixed(1) : Math.round(h)) + ' h' : Math.round(h / 24) + ' days'} to the refill line`;
      } else if (sl >= 0.8) note = '&#8593; rising (being refilled?)';
      else note = '&#8594; steady';
    } else note = 'watching the trend...';
    if (st !== 'ok') note = (st === 'low' ? '<span class="gbad">LOW: refill now</span>' : 'getting low') + (note ? ' · ' + note : '');
  }
  const active = known && !stale;                              // the chip says only Active or Inactive; the level is the number in the dial and the line under the chip
  return `<div class="card gauge ${st}">${svg}<div class="gt">${esc(title)}</div><div class="gsub">${esc(sub)}</div><div class="gs"><span class="chip ${active ? 'run' : 'off'}">${active ? 'Active' : 'Inactive'}</span></div><div class="gn">${note}</div></div>`;
}

/* ---- a dial from the Refilling Station's reading: rf[i] = [raw, state 0-4, valve open, refill failed, mid edge, high edge, max edge, waiting in the queue] ----
   EMPTY and LOW = red (the box offers drying only), MID = yellow, HIGH = yellow to green, MAX = green. The edges are the box's own calibration (LocalServer). */
const RF_STATE = ['EMPTY', 'LOW', 'MID', 'HIGH', 'MAX'];
function stateGaugeCard(title, sub, r, stale) {
  const known = Array.isArray(r) && r[1] >= 0 && r[1] <= 4;
  const raw = known ? nz(r[0], 0) : 0, st = known ? r[1] : -1;
  const mx = known ? Math.max(1, nz(r[6], 1200)) : 1200, pct = v => Math.max(0, Math.min(100, v / mx * 100));
  const m = known ? pct(nz(r[4], 500)) : 41.7, h = known ? Math.max(m, pct(nz(r[5], 1000))) : 83.3, cap = Math.max(h, 97);
  const cx = 60, cy = 54, rad = 46;
  let svg = `<svg viewBox="0 0 120 80" class="dial" role="img" aria-label="${esc(title)}: ${known ? RF_STATE[st] : 'inactive'}">`;
  svg += `<path d="${dialArc(0, m, rad, cx, cy)}" class="z-low"/><path d="${dialArc(m, h, rad, cx, cy)}" class="z-early"/>`;
  svg += `<path d="${dialArc(h, cap, rad, cx, cy)}" class="z-high"/><path d="${dialArc(cap, 100, rad, cx, cy)}" class="z-max"/>`;
  if (known) {
    const th = Math.PI * (1 - pct(raw) / 100), nx = cx + (rad - 9) * Math.cos(th), ny = cy - (rad - 9) * Math.sin(th);
    svg += `<line x1="${cx}" y1="${cy}" x2="${nx.toFixed(1)}" y2="${ny.toFixed(1)}" class="needle"/><circle cx="${cx}" cy="${cy}" r="4" class="hub"/>`;
  }
  svg += `<text x="${cx}" y="${cy + 23}" text-anchor="middle" class="dv">${known ? RF_STATE[st] : '-'}</text>`;
  svg += `<text x="${cx - rad}" y="${cy + 11}" text-anchor="middle" class="dl">0</text><text x="${cx + rad}" y="${cy + 11}" text-anchor="middle" class="dl">${Math.round(mx)}</text></svg>`;
  const cls = !known ? 'unknown' : st <= 1 ? 'low' : st === 2 ? 'early' : st === 3 ? 'high' : 'ok';
  const notWatched = Array.isArray(r) && r[1] === -2;                             /* -2 = the station does not watch this box for now (it watches Box 3 only) */
  const fault = Array.isArray(r) && !known && !notWatched;                        /* the station is heard but reports no level for this box = its water-level sensor is faulty */
  /* the chip says only Active (the station reads this container and the data is fresh) or Inactive (not watched, sensor fault, nothing heard, or old data);
     the level itself is the big word in the dial, and what it means for the box (drying only, refill failed...) is in the line under the chip */
  const active = known && !stale;
  const note = notWatched ? 'not read or refilled by the station for now' : fault ? '<span class="gbad">the water-level sensor reads wrongly: check its wiring (the box offers drying only)</span>' : !known ? '' : r[3] ? '<span class="gbad">refill FAILED: the level is not rising</span>' : r[2] ? '<span class="gwet">refilling now (valve open)</span>' : r[7] ? '<span class="gwet">waiting for its turn (one valve at a time)</span>' : 'reading ' + Math.round(raw) + (st <= 1 ? ' · <span class="gbad">drying only</span>' : '');
  return `<div class="card gauge ${cls}">${svg}<div class="gt">${esc(title)}</div><div class="gsub">${esc(sub)}</div><div class="gs"><span class="chip ${active ? 'run' : 'off'}">${active ? 'Active' : 'Inactive'}</span></div><div class="gn">${note}</div></div>`;
}

/* ---- the Levels section of the Monitor page: 3 dials = the main container of Box 1, Box 2 and Box 3 ---- */
function renderLevels(L) {
  const el = $('levels'); if (!el) return;
  const n = nodeFlags(L), cards = [], rf = Array.isArray(L.rf) ? L.rf : null, stale = S.link === 'dead' || S.link === 'err';   // old data: nothing is Active
  BOXES.forEach((def, i) => {
    if (rf) { cards.push(stateGaugeCard(def.name + ' main container', Array.isArray(rf[i]) && rf[i][1] === -2 ? 'not watched for now' : 'filled by the Refilling Station', n[STATION] ? rf[i] : null, stale)); return; }
    const b = (L.b || [])[i], ok = b && n[def.sensors];                  // firmware without the Refilling Station: the old percent reading
    cards.push(gaugeCard(def.name + ' main container', 'filled by the Refilling Station', ok ? nz(b[5], -1) : -1, 'b' + i, stale));
  });
  const note = rf
    ? 'Red = EMPTY or LOW: the box offers drying only. Yellow = MID. Yellow to green = HIGH. Green = MAX (full). The Refilling Station opens a box\'s valve by itself when its container is EMPTY or LOW, one valve at a time (EMPTY first, then LOW). Active = the station reads this container; Inactive = it does not (not watched, sensor fault, nothing heard, or old data). For now the station watches and refills Box 3 only, so the other boxes say Inactive.'
    : `Red below 15 %, amber below the early warning line (${warnPct() ? warnPct() + ' %' : 'off'}; change it in the Alerts tab). For a main container 0 % is the refill line: the box stops cleaning there.`;
  el.innerHTML = `<div class="grid gauges">${cards.join('')}</div><div class="sub">${note}</div>`;
}