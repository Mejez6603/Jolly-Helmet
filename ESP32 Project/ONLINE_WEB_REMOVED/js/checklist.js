"use strict";
/* The Maintenance checklist: the schedule of the manual (Technician guide, Maintenance) as tick boxes per machine, with "last done" and what is due.
   Ticks are kept in THIS browser only (the page is read-only and has no server to save to). To use the same list on two devices, use
   "Save a backup" on one and "Restore" on the other. The log can also be downloaded as CSV. */
const Checklist = (() => {
  const MACH = ['Box 1', 'Box 2', 'Box 3', 'ACS 1', 'ACS 2'], BOXES3 = ['Box 1', 'Box 2', 'Box 3'], CLEANERS = ['Box 1', 'Box 3'], ACS = ['ACS 1', 'ACS 2'];
  const GROUPS = [
    { id: 'daily',     title: 'Every day',      every: 1 },
    { id: 'weekly',    title: 'Every week',     every: 7 },
    { id: 'monthly',   title: 'Every month',    every: 30 },
    { id: 'quarterly', title: 'Every 3 months', every: 91 },
    { id: 'yearly',    title: 'Once a year or when worn', every: 365 }
  ];
  const TASKS = [
    ['daily', 'd1', 'Home page of the Control Center: every badge ONLINE and no red banner. This web page says LIVE.', null],
    ['daily', 'd2', 'Look at the 3 supply tanks in each ACS and the 2 main containers.', ['Box 1', 'Box 3', 'ACS 1', 'ACS 2']],
    ['daily', 'd3', 'Wipe the touch screens.', BOXES3],
    ['daily', 'd4', 'Collect the coins and note the revenue.', BOXES3],
    ['weekly', 'w1', 'Wipe the helmet chamber and the door seals.', BOXES3],
    ['weekly', 'w2', 'Open each door during a run: SAFETY PAUSE must show.', BOXES3],
    ['weekly', 'w3', 'Listen for each lock clicking.', BOXES3],
    ['weekly', 'w4', 'Look for leaks at pump tubes and mixers.', ['Box 1', 'Box 3', 'ACS 1', 'ACS 2']],
    ['weekly', 'w5', 'Test each UV-C lamp in Maintenance with the door closed (the relay must click; never look at a lit lamp).', BOXES3],
    ['weekly', 'w6', 'Check the humidifiers make mist.', CLEANERS],
    ['monthly', 'm1', 'Switch every relay by hand in Maintenance and listen for the click.', BOXES3],
    ['monthly', 'm2', 'Clean the ultrasonic sensor faces and the water-level probes.', MACH],
    ['monthly', 'm3', 'Compare each distance with a ruler (use live).', MACH],
    ['monthly', 'm4', 'Test the coin slot with every coin.', BOXES3],
    ['monthly', 'm5', 'Check screw terminals and plugs are tight and nothing is hot or discoloured.', MACH],
    ['monthly', 'm6', 'Clean the humidifier discs and the heater and fan air intakes.', BOXES3],
    ['monthly', 'm7', 'Test the safety slide of the enclosure door: with the power off, sliding it down must unlock the door by hand.', BOXES3],
    ['quarterly', 'q1', 'Capture the water sensors again, dry and wet, with the real liquids.', ['Box 1', 'Box 3', 'ACS 1', 'ACS 2']],
    ['quarterly', 'q2', 'Check the touch accuracy.', BOXES3],
    ['quarterly', 'q3', 'With the power off, check the mains wiring, earth and fuses.', BOXES3],
    ['quarterly', 'q4', 'Take a screenshot of every Control Center settings page and keep it.', null],
    ['yearly', 'y1', 'Replace the UV-C lamps at the end of their rated hours (their output fades before they stop glowing).', BOXES3],
    ['yearly', 'y2', 'Replace pump tubing and slow pumps.', ['Box 1', 'Box 3', 'ACS 1', 'ACS 2']],
    ['yearly', 'y3', 'Replace any lock that sticks or gets hot.', BOXES3],
    ['yearly', 'y4', 'Replace humidifier discs.', CLEANERS]
  ].map(([g, id, text, scope]) => ({ g, id, text, scope: scope || ['all'] }));
  const gEvery = Object.fromEntries(GROUPS.map(g => [g.id, g.every]));

  let data = { done: {}, log: [], who: '' };
  try { const d = JSON.parse(store.get('jh_check') || ''); if (d && d.done) data = Object.assign(data, d); } catch (e) {}
  const save = () => { if (data.log.length > 500) data.log.length = 500; store.set('jh_check', JSON.stringify(data)); };
  const DAY = 86400000;
  const sameDay = (a, b) => new Date(a).toDateString() === new Date(b).toDateString();
  const last = (id, sc) => (data.done[id] || {})[sc] || 0;

  /* ok / due / overdue / never for one task on one machine */
  function status(task, sc, now) {
    const t = last(task.id, sc), every = gEvery[task.g];
    if (!t) return 'never';
    if (every === 1) return sameDay(t, now) ? 'ok' : (now - t < 1.6 * DAY ? 'due' : 'overdue');
    const age = (now - t) / DAY;
    return age < every * 0.85 ? 'ok' : age <= every * 1.15 ? 'due' : 'overdue';
  }
  const worst = list => ['overdue', 'never', 'due', 'ok'].find(s => list.includes(s)) || 'ok';
  const taskStatus = (task, now) => worst(task.scope.map(sc => status(task, sc, now)));
  const label = { ok: 'done', due: 'due', overdue: 'OVERDUE', never: 'not done yet' };
  const cls = { ok: 'run', due: 'warn', overdue: 'bad', never: '' };
  const agoTxt = t => { if (!t) return 'never'; const d = (Date.now() - t) / DAY; return sameDay(t, Date.now()) ? 'today' : d < 1.5 ? 'yesterday' : Math.floor(d) + ' d ago'; };

  function counts() {
    const now = Date.now(), c = { due: 0, overdue: 0, never: 0, byGroup: {} };
    TASKS.forEach(t => { const s = taskStatus(t, now); if (s === 'due') c.due++; else if (s === 'overdue') c.overdue++; else if (s === 'never') c.never++; if (s !== 'ok') c.byGroup[t.g] = (c.byGroup[t.g] || 0) + 1; });
    return c;
  }
  /* overdue checks become "maintenance due" alerts (a soft chime, never louder than a notice) */
  function publish() {
    const now = Date.now(), list = [];
    GROUPS.forEach(g => {
      const n = TASKS.filter(t => t.g === g.id && (() => { const s = taskStatus(t, now); return s === 'overdue'; })()).length;     // "never" is not an alarm: a new list starts empty
      if (n) list.push({ id: 'due:' + g.id, type: 'due', text: `${g.title}: ${n} check${n > 1 ? 's are' : ' is'} overdue (Checklist tab)` });
    });
    if (typeof Alerts !== 'undefined') Alerts.setExtra(list);
    const c = counts(), b = $('dueBadge'); if (b) { const k = c.due + c.overdue; b.textContent = k || ''; b.className = 'tabbadge' + (k ? ' on ' + (c.overdue ? 'warning' : 'notice') : ''); }
  }

  function draw() {
    const el = $('checkBody'); if (!el) return;
    const now = Date.now(), c = counts();
    let h = `<div class="card" style="margin-bottom:14px"><div class="card-h"><div><b>${c.overdue ? c.overdue + ' check' + (c.overdue > 1 ? 's' : '') + ' overdue' : c.due ? c.due + ' check' + (c.due > 1 ? 's' : '') + ' due' : c.never ? c.never + ' of ' + TASKS.length + ' checks not ticked yet: tick each one the first time you do it' : 'Everything is up to date'}</b><small>Tap a machine under a check when you have done it there. Kept in this browser only; use the buttons below to move it to another device.</small></div></div>` +
      `<div style="display:flex;gap:8px;flex-wrap:wrap;align-items:end"><div style="flex:1;min-width:180px"><label for="ckWho">Your name (written in the log)</label><input id="ckWho" value="${esc(data.who)}" placeholder="who is doing the checks" autocomplete="off" data-ck="who"></div>` +
      `<button class="btn alt" type="button" data-ck="undo" style="margin:0">Undo the last tick</button><button class="btn alt" type="button" data-ck="csv" style="margin:0">Download the log (CSV)</button>` +
      `<button class="btn alt" type="button" data-ck="backup" style="margin:0">Save a backup</button><button class="btn alt" type="button" data-ck="restore" style="margin:0">Restore a backup</button><button class="btn alt" type="button" data-ck="print" style="margin:0">Print</button><input type="file" id="ckFile" accept=".json" hidden></div></div>`;
    GROUPS.forEach(g => {
      const rows = TASKS.filter(t => t.g === g.id);
      h += `<h2>${g.title}${c.byGroup[g.id] ? ` <span class="chip ${rows.some(t => taskStatus(t, now) === 'overdue') ? 'bad' : rows.some(t => taskStatus(t, now) === 'due') ? 'warn' : ''}">${c.byGroup[g.id]} to do</span>` : ''}</h2><div class="card ckcard">`;
      rows.forEach(t => {
        const s = taskStatus(t, now);
        h += `<div class="ckrow"><div class="cktxt">${esc(t.text)}</div><div class="chips" style="margin:0">` + t.scope.map(sc => {
          const st = status(t, sc, now), lt = last(t.id, sc);
          return `<button type="button" class="chip ckchip ${cls[st]}" data-ck="tick" data-task="${t.id}" data-scope="${esc(sc)}" title="${st === 'never' ? 'not done yet' : 'last done ' + new Date(lt).toLocaleString()}">${sc === 'all' ? (lt ? '&#10003; done ' + agoTxt(lt) : 'Mark done') : esc(sc) + ': ' + (lt ? agoTxt(lt) : 'never')}</button>`;
        }).join('') + `</div></div>`;
      });
      h += `</div>`;
    });
    h += `<h2>Recent ticks</h2><div class="card"><div class="scroll">` + (data.log.length ? `<table class="logt"><tr><th>When</th><th>Check</th><th>Where</th><th>Who</th></tr>` + data.log.slice(0, 40).map(e => { const t = TASKS.find(x => x.id === e.id); return `<tr><td>${new Date(e.t).toLocaleString([], { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })}</td><td>${esc(t ? t.text : e.id)}</td><td>${esc(e.sc === 'all' ? '-' : e.sc)}</td><td>${esc(e.who || '')}</td></tr>`; }).join('') + `</table>` : '<div class="empty">Nothing ticked yet.</div>') + `</div></div>`;
    el.innerHTML = h;
  }

  document.addEventListener('click', e => {
    const b = e.target.closest('[data-ck]'); if (!b) return;
    const k = b.dataset.ck;
    if (k === 'tick') {
      const id = b.dataset.task, sc = b.dataset.scope, prev = last(id, sc);
      (data.done[id] = data.done[id] || {})[sc] = Date.now();
      data.log.unshift({ t: Date.now(), id, sc, who: data.who, prev }); save(); draw(); publish();
    } else if (k === 'undo') {
      const e0 = data.log.shift(); if (!e0) return;
      if (e0.prev) data.done[e0.id][e0.sc] = e0.prev; else delete data.done[e0.id][e0.sc];
      save(); draw(); publish();
    } else if (k === 'csv') {
      const q = s => '"' + String(s).replace(/"/g, '""') + '"';
      download('jolly_helmet_maintenance_log.csv', 'when,check,where,who\r\n' + data.log.map(x => { const t = TASKS.find(z => z.id === x.id); return [new Date(x.t).toISOString().replace('T', ' ').slice(0, 19), q(t ? t.text : x.id), x.sc, q(x.who || '')].join(','); }).join('\r\n') + '\r\n');
    } else if (k === 'backup') download('jolly_helmet_checklist_backup.json', JSON.stringify(data), 'application/json');
    else if (k === 'restore') $('ckFile').click();
    else if (k === 'print') window.print();
  });
  document.addEventListener('input', e => { if (e.target.dataset && e.target.dataset.ck === 'who') { data.who = e.target.value; save(); } });
  document.addEventListener('change', e => {
    if (e.target.id !== 'ckFile' || !e.target.files[0]) return;
    const r = new FileReader();
    r.onload = () => { try { const d = JSON.parse(String(r.result).replace(/^﻿/, '')); if (!d || !d.done) throw 0; data = Object.assign({ done: {}, log: [], who: '' }, d); save(); draw(); publish(); } catch (x) { alert('That file is not a checklist backup.'); } };
    r.readAsText(e.target.files[0]); e.target.value = '';
  });
  setInterval(publish, 60000);
  return { draw, publish, counts };
})();
