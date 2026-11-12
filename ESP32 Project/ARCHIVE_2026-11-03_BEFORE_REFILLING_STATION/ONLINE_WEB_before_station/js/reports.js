"use strict";
/* The Reports tab: revenue and customers over the last 14 days, when the customers come, what each box earns, tables, CSV files and a printable report.

   Data (all from the "st" part of the Online Server's V0 value, see DATA_CONTRACT.md):
     dy  last 14 days [coins, customers] (needs the LocalServer and the clock)       -> daily charts, week against week, the table
     hr  finished sessions per hour of the day (24 numbers)                           -> busiest hours   \  counted by the Online Server since "sn"
     wd  finished sessions per weekday (7 numbers, Sunday first)                      -> busiest weekdays > (needs the Online Server firmware of 2026-10-10)
     bx  [sessions, coins] for Box 1, Box 2, Box 3                                    -> what each box earns /
     h   the last 10 finished sessions [seconds, pesos]                                -> sessions CSV                                                                */
const Reports = (() => {
  // top of a chart axis with 4 gridlines: 4 steps of a round whole number (1, 2, 3, 4, 5, 6, 8, 10 times a power of ten)
  const nice = v => { for (let p = 1; ; p *= 10) for (const f of [1, 2, 3, 4, 5, 6, 8, 10]) if (4 * f * p >= v) return 4 * f * p; };
  const fmtMoney = v => CONFIG.coin + Math.round(v).toLocaleString();
  const dayLabel = d => d.toLocaleDateString([], { month: 'short', day: 'numeric' });
  const wdShort = d => d.toLocaleDateString([], { weekday: 'short' });

  function daily() {
    const T = S.stats || {}, dy = T.dy;
    if (!Array.isArray(dy) || !dy.length) return null;
    const cv = nz(T.cv, 1), base = (S.live && S.live.ts > 1.6e9) ? S.live.ts * 1000 : Date.now();
    return dy.map((x, i) => { const d = new Date(base - (dy.length - 1 - i) * 86400000); return { d, coins: nz(x[0], 0), rev: nz(x[0], 0) * cv, cust: nz(x[1], 0) }; });
  }

  /* ---- a bar chart as an svg (colors come from CSS variables, so light and dark both work) ---- */
  function barSvg(o) {
    const n = o.vals.length, W = o.w || 560, H = o.h || 190, L = 38, R = 8, Tp = 16, B = 26, pw = W - L - R, ph = H - Tp - B;
    const mx = nice(Math.max(...o.vals, 1) * 1.05), bw = pw / n;
    let s = `<svg class="ch" viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(o.title)}">`;
    for (let i = 0; i <= 4; i++) {
      const y = Tp + ph - ph * i / 4, v = mx * i / 4;
      s += `<line x1="${L}" x2="${W - R}" y1="${y}" y2="${y}" class="gl"/><text x="${L - 5}" y="${y + 3}" text-anchor="end">${o.money ? fmtMoney(v) : (v >= 1000 ? Math.round(v / 100) / 10 + 'k' : Math.round(v * 10) / 10)}</text>`;
    }
    o.vals.forEach((v, i) => {
      const bh = Math.max(v > 0 ? 2 : 0, ph * v / mx), x = L + i * bw + bw * 0.16, w = bw * 0.68, y = Tp + ph - bh;
      const cls = (o.hi && o.hi.includes(i)) ? 'cbar hi' : 'cbar';
      s += `<rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${w.toFixed(1)}" height="${bh.toFixed(1)}" rx="3" class="${cls}"><title>${esc(o.labels[i])}: ${o.money ? fmtMoney(v) : v}${o.unit || ''}</title></rect>`;
      if (n <= 16 && v > 0) s += `<text x="${(x + w / 2).toFixed(1)}" y="${(y - 3).toFixed(1)}" text-anchor="middle" class="vl">${o.money ? Math.round(v) : v}</text>`;
      const showX = n <= 16 || i % (o.every || 3) === 0;
      if (showX) s += `<text x="${(x + w / 2).toFixed(1)}" y="${H - 9}" text-anchor="middle">${esc(o.xs ? o.xs[i] : o.labels[i])}</text>`;
    });
    if (o.line) {                                                // a moving average over the bars
      const pts = o.line.map((v, i) => v == null ? null : `${(L + i * bw + bw / 2).toFixed(1)},${(Tp + ph - ph * v / mx).toFixed(1)}`).filter(Boolean);
      if (pts.length > 1) s += `<polyline points="${pts.join(' ')}" class="avg"/>`;
    }
    return s + '</svg>';
  }
  const movAvg = (a, k) => a.map((_, i) => i < k - 1 ? null : a.slice(i - k + 1, i + 1).reduce((x, y) => x + y, 0) / k);
  const sum = a => a.reduce((x, y) => x + y, 0);
  const pctChange = (a, b) => b > 0 ? Math.round((a - b) / b * 100) : null;
  const delta = (a, b) => { const p = pctChange(a, b); return p === null ? '' : `<small class="${p >= 0 ? 'up' : 'down'}">${p >= 0 ? '&#9650;' : '&#9660;'} ${Math.abs(p)}% vs the 7 days before</small>`; };
  const tile = (v, l, extra) => `<div class="card kpi"><div class="v num">${v}</div><div class="l">${l}</div>${extra || ''}</div>`;
  const need = what => `<div class="empty">${what}</div>`;
  const NEWFW = 'Needs the Online Server firmware of 2026-10-10 (it counts every finished session per box and per hour) and the LocalServer on.';

  function build() {
    const T = S.stats || {}, days = daily(), cv = nz(T.cv, 1);
    let h = '';
    if (!S.stats) return `<div class="card empty">No analytics received yet. Connect first (Monitor tab), then come back.</div>`;
    if (days) {
      const rev = days.map(x => x.rev), cust = days.map(x => x.cust), n = days.length;
      const last7 = rev.slice(-7), prev7 = rev.slice(-14, -7), c7 = cust.slice(-7), p7 = cust.slice(-14, -7);
      const best = days.reduce((b, x) => x.rev > b.rev ? x : b, days[0]), tot = sum(rev), totC = sum(cust);
      h += `<div class="grid kpis" style="margin-bottom:14px">` +
        tile(fmtMoney(sum(last7)), 'Last 7 days revenue', prev7.length === 7 ? delta(sum(last7), sum(prev7)) : '') +
        tile(sum(c7).toLocaleString(), 'Last 7 days customers', p7.length === 7 ? delta(sum(c7), sum(p7)) : '') +
        tile(totC ? money(tot / totC) : '-', `Average per customer (${n} days)`) +
        tile(best.rev ? fmtMoney(best.rev) : '-', `Best day: ${best.rev ? wdShort(best.d) + ' ' + dayLabel(best.d) : '-'}`) +
        tile(fmtMoney(tot), `Total of the last ${n} days`) + `</div>`;
      h += `<div class="grid g2" style="margin-bottom:14px"><div class="card"><div class="card-h"><div><b>Revenue per day</b><small>${CONFIG.coin}, today on the right, line = 7-day average</small></div></div>` +
        barSvg({ title: 'Revenue per day', vals: rev, labels: days.map(x => wdShort(x.d) + ' ' + dayLabel(x.d)), xs: days.map(x => wdShort(x.d).slice(0, 2)), money: true, hi: [n - 1], line: movAvg(rev, 7) }) + `</div>` +
        `<div class="card"><div class="card-h"><div><b>Customers per day</b><small>sessions finished, today on the right</small></div></div>` +
        barSvg({ title: 'Customers per day', vals: cust, labels: days.map(x => wdShort(x.d) + ' ' + dayLabel(x.d)), xs: days.map(x => wdShort(x.d).slice(0, 2)), hi: [n - 1], line: movAvg(cust, 7) }) + `</div></div>`;
    } else h += `<div class="card" style="margin-bottom:14px">${need('Daily totals appear once the Online Server knows the time and has heard the LocalServer.')}</div>`;

    // when the customers come, what each box earns
    const hr = Array.isArray(T.hr) ? T.hr : null, wd = Array.isArray(T.wd) ? T.wd : null, bx = Array.isArray(T.bx) ? T.bx : null;
    const since = T.sn ? new Date(T.sn * 1000).toLocaleDateString([], { month: 'short', day: 'numeric', year: 'numeric' }) : '';
    h += `<div class="grid g3" style="margin-bottom:14px">`;
    if (hr && sum(hr) > 0) {
      const top = hr.map((v, i) => [v, i]).sort((a, b) => b[0] - a[0]).slice(0, 3).filter(x => x[0] > 0).map(x => x[1]), hh = i => { const a = i % 12 || 12; return a + (i < 12 ? ' AM' : ' PM'); };
      h += `<div class="card"><div class="card-h"><div><b>Busiest hours</b><small>sessions finished per hour of the day, since ${since}</small></div></div>` +
        barSvg({ title: 'Sessions per hour of the day', vals: hr, labels: hr.map((_, i) => hh(i) + ' to ' + hh((i + 1) % 24)), xs: hr.map((_, i) => String(i)), hi: top, every: 3, w: 420, h: 170 }) +
        `<div class="sub">Busiest: ${top.map(i => hh(i)).join(', ') || '-'}</div></div>`;
    } else h += `<div class="card"><div class="card-h"><div><b>Busiest hours</b></div></div>${need(hr ? 'No sessions counted yet.' : NEWFW)}</div>`;
    if (wd && sum(wd) > 0) {
      const names = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'], top = wd.indexOf(Math.max(...wd));
      h += `<div class="card"><div class="card-h"><div><b>Busiest days</b><small>sessions finished per weekday, since ${since}</small></div></div>` +
        barSvg({ title: 'Sessions per weekday', vals: wd, labels: names, xs: names.map(x => x.slice(0, 3)), hi: [top], w: 420, h: 170 }) + `<div class="sub">Busiest: ${names[top]}</div></div>`;
    } else h += `<div class="card"><div class="card-h"><div><b>Busiest days</b></div></div>${need(wd ? 'No sessions counted yet.' : NEWFW)}</div>`;
    if (bx && bx.length === 3 && sum(bx.map(x => x[0])) > 0) {
      const names = ['Box 1 (Cleaner)', 'Box 2 (Dryer)', 'Box 3 (Cleaner + Dryer)'], rev = bx.map(x => x[1] * cv), tot = sum(rev) || 1, mx = Math.max(...rev, 1);
      h += `<div class="card"><div class="card-h"><div><b>What each box earns</b><small>since ${since}</small></div></div><div class="hbars">` +
        bx.map((x, i) => `<div class="hb"><div class="hl"><span>${names[i]}</span><b class="num">${fmtMoney(rev[i])}</b></div><div class="bar"><i style="width:${Math.round(rev[i] / mx * 100)}%"></i></div><small>${x[0].toLocaleString()} sessions, ${Math.round(rev[i] / tot * 100)}% of the revenue, ${x[0] ? money(rev[i] / x[0]) : '-'} each</small></div>`).join('') + `</div></div>`;
    } else h += `<div class="card"><div class="card-h"><div><b>What each box earns</b></div></div>${need(bx ? 'No sessions counted yet.' : NEWFW)}</div>`;
    h += `</div>`;

    // table
    if (days) {
      h += `<div class="card"><div class="card-h"><div><b>Day by day</b><small>newest first</small></div></div><div class="scroll"><table class="logt"><tr><th>Date</th><th>Day</th><th class="num">Coins</th><th class="num">Revenue</th><th class="num">Customers</th><th class="num">Per customer</th></tr>` +
        days.slice().reverse().map(x => `<tr><td>${dayLabel(x.d)}</td><td>${wdShort(x.d)}</td><td class="num">${x.coins}</td><td class="num">${money(x.rev)}</td><td class="num">${x.cust}</td><td class="num">${x.cust ? money(x.rev / x.cust) : '-'}</td></tr>`).join('') + `</table></div></div>`;
    }
    h += `<p class="fine">Daily totals are counted by the Online Server from the LocalServer's running totals, per local day (time zone in its setup page); they cover the last 14 days. Hours, weekdays and boxes are counted from the moment the new firmware was started${since ? ' (' + since + ')' : ''} and can be reset in the Online Server's setup page.</p>`;
    return h;
  }

  function refresh() { const el = $('repBody'); if (!el || $('reportsTab').hidden) return; el.innerHTML = build(); const g = $('repGen'); if (g) g.textContent = new Date().toLocaleString(); }

  /* ---- files ---- */
  function dailyCsv() {
    const days = daily(); if (!days) return null;
    return 'date,weekday,coins,revenue,customers\r\n' + days.map(x => [x.d.toISOString().slice(0, 10), wdShort(x.d), x.coins, x.rev.toFixed(2), x.cust].join(',')).join('\r\n') + '\r\n';
  }
  function sessionsCsv() {
    const T = S.stats || {}, h = T.h; if (!Array.isArray(h) || !h.length) return null;
    return 'order,seconds,minutes,revenue\r\n' + h.map((x, i) => [i + 1, x[0], (x[0] / 60).toFixed(1), Number(x[1]).toFixed(2)].join(',')).join('\r\n') + '\r\n';
  }
  function usageCsv() {
    const T = S.stats || {}, hr = T.hr, bx = T.bx, cv = nz(T.cv, 1); if (!hr && !bx) return null;
    let s = 'what,name,sessions,revenue\r\n';
    if (bx) ['Box 1', 'Box 2', 'Box 3'].forEach((nm, i) => { s += `box,${nm},${bx[i][0]},${(bx[i][1] * cv).toFixed(2)}\r\n`; });
    if (hr) hr.forEach((v, i) => { s += `hour,${i}:00,${v},\r\n`; });
    if (T.wd) ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'].forEach((nm, i) => { s += `weekday,${nm},${T.wd[i]},\r\n`; });
    return s;
  }
  const stamp = () => new Date().toISOString().slice(0, 10);
  document.addEventListener('click', e => {
    const b = e.target.closest('[data-rep]'); if (!b) return;
    const k = b.dataset.rep;
    const get = { daily: dailyCsv, sessions: sessionsCsv, usage: usageCsv }[k];
    if (k === 'print') { window.print(); return; }
    const txt = get && get();
    if (!txt) { alert('There is no data for this file yet.'); return; }
    download(`jolly_helmet_${k}_${stamp()}.csv`, txt);
  });
  return { refresh, build };
})();
