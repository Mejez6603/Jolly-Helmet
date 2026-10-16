"use strict";
/* Level gauges: one half-circle dial for every container (the 4 main containers of Box 1 and Box 3, the 4 mixer containers of ACS 1 and ACS 2).
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
function gaugeCard(title, sub, pct, key) {
  const known = typeof pct === 'number' && pct >= 0, w = warnPct();
  const p = known ? Math.max(0, Math.min(100, pct)) : 0;
  const st = !known ? 'unknown' : p < 15 ? 'low' : (w && p < w) ? 'early' : 'ok';
  const cx = 60, cy = 54, r = 46;
  let svg = `<svg viewBox="0 0 120 80" class="dial" role="img" aria-label="${esc(title)}: ${known ? Math.round(p) + ' percent' : 'unknown'}">`;
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
  }
  const lab = { ok: 'OK', early: 'GETTING LOW', low: 'LOW: REFILL NOW', unknown: 'NOT KNOWN' }[st];
  return `<div class="card gauge ${st}">${svg}<div class="gt">${esc(title)}</div><div class="gsub">${esc(sub)}</div><div class="gs"><span class="chip ${st === 'ok' ? 'run' : st === 'early' ? 'warn' : st === 'low' ? 'bad' : 'off'}">${lab}</span></div><div class="gn">${note}</div></div>`;
}

/* ---- the Levels section of the Monitor page ---- */
function renderLevels(L) {
  const el = $('levels'); if (!el) return;
  const n = nodeFlags(L), cards = [];
  (L.b || []).forEach((b, i) => {
    const def = BOXES[i]; if (!b || !def.tanks) return;
    const ok = n[def.sensors];
    cards.push(gaugeCard(def.name + ' alcohol', 'main container', ok ? nz(b[5], -1) : -1, 'b' + i + 'a'));
    cards.push(gaugeCard(def.name + ' scented', 'main container', ok ? nz(b[6], -1) : -1, 'b' + i + 's'));
  });
  (L.a || []).forEach((a, i) => {
    const ok = a && a[0];
    cards.push(gaugeCard('ACS ' + (i + 1) + ' alcohol mixer', i ? 'refills Box 3' : 'refills Box 1', ok ? nz(a[5], -1) : -1, 'a' + i + 'a'));
    cards.push(gaugeCard('ACS ' + (i + 1) + ' scented mixer', i ? 'refills Box 3' : 'refills Box 1', ok ? nz(a[6], -1) : -1, 'a' + i + 's'));
  });
  let tanks = '';
  (L.a || []).forEach((a, i) => {
    if (!a || !a[0]) { tanks += `<div class="row"><span>ACS ${i + 1} supply tanks</span>${chip('NOT HEARD', 'off')}</div>`; return; }
    const e = a[3], t = (b, nm) => ((e & b) ? chip(nm + ' EMPTY', 'bad') : chip(nm + ' ok', 'run'));
    tanks += `<div class="row"><span>ACS ${i + 1} supply tanks <small>(no sensor: "empty" means a pump ran and the level did not rise)</small></span><span class="chips" style="margin:0">${t(1, 'water')}${t(2, 'alcohol')}${t(4, 'scented')}</span></div>`;
  });
  el.innerHTML = `<div class="grid gauges">${cards.join('')}</div>` + (tanks ? `<div class="card" style="margin-top:12px">${tanks}</div>` : '') +
    `<div class="sub">Red below 15 %, amber below the early warning line (${warnPct() ? warnPct() + ' %' : 'off'}; change it in the Alerts tab). 0 % is the refill line: a box stops cleaning there.</div>`;
}
