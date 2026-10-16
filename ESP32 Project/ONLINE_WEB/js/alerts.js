"use strict";
/* Alerts: finds what needs attention in the latest data, keeps a log, and tells people - with a sound, a banner, a browser notification
   and a number in the tab title / icon. Every kind of alert has its own sound (Alerts tab, "Sounds") that can be changed or switched off.

   How fast: the Online Server uploads every 30 s to 5 min (its setup page), the page reads every few seconds, so an alert shows within about
   one upload interval after it happens. A page that is closed cannot play a sound or show anything: use the Blynk app for that
   (Blynk event "alert", see README.txt).

   An alert has a fixed id (for example "oos:0" = Box 1 out of order), so the page can tell a NEW alert from one that was already there. */
const SEV = { critical: 3, warning: 2, notice: 1, info: 0 };
const SEV_NAME = { critical: 'CRITICAL', warning: 'WARNING', notice: 'NOTICE', info: 'INFO' };

/* what each kind of alert is called, how serious it is, which sound it starts with, whether it repeats until somebody acknowledges it,
   and which "Fix it" guide (js/wizard_data.js) can help */
const AL_TYPES = {
  out_of_order:  { sev: 'critical', label: 'A box is OUT OF ORDER',                        sound: 'siren',   repeat: true,  fix: 'tree1' },
  tank_empty:    { sev: 'critical', label: 'An ACS supply tank is empty',                  sound: 'alarm',   repeat: true,  fix: 'tree5' },
  all_offline:   { sev: 'critical', label: 'No machine board is heard at all',             sound: 'alarm',   repeat: true,  fix: 'tree2' },
  safety_pause:  { sev: 'warning',  label: 'A door was opened during a run (SAFETY PAUSE)', sound: 'urgent', repeat: false, fix: null },
  container_low: { sev: 'warning',  label: 'A main container is LOW (under 15 %)',         sound: 'beeps3',  repeat: false, fix: 'tree5' },
  mixer_low:     { sev: 'warning',  label: 'An ACS mixer container is low',                sound: 'twotone', repeat: false, fix: 'tree5' },
  cleaning_off:  { sev: 'warning',  label: 'Cleaning is switched off (a container is low)', sound: 'twotone', repeat: false, fix: 'tree1' },
  board_offline: { sev: 'warning',  label: 'A board is offline',                           sound: 'twotone', repeat: false, fix: 'tree2' },
  stale:         { sev: 'warning',  label: 'The data is old, or Blynk cannot be read',     sound: 'descend', repeat: false, fix: 'tree6' },
  early_low:     { sev: 'notice',   label: 'A container is getting low (early warning)',   sound: 'soft',    repeat: false, fix: 'tree5' },
  due:           { sev: 'notice',   label: 'A maintenance check is due',                   sound: 'soft',    repeat: false, fix: null },
  cleared:       { sev: 'info',     label: 'An alert cleared (back to normal)',            sound: 'chime',   repeat: false, fix: null },
  session:       { sev: 'info',     label: 'A customer finished a session',                sound: 'ding',    repeat: false, fix: null, off: true }
};

const Alerts = (() => {
  const load = (k, d) => { try { return JSON.parse(store.get(k) || '') || d; } catch (e) { return d; } };
  const cfg = Object.assign({ types: {}, repeatSec: 60, notify: false, playClear: true }, load('jh_alert_cfg', {}));
  let log = load('jh_alert_log', []);                              // newest first, at most 300 entries
  const active = new Map();                                       // id -> { id, type, sev, text, t0, acked, entry }
  log.filter(e => !e.t1 && e.type !== 'session').forEach(e => active.set(e.id, { id: e.id, type: e.type, sev: e.sev, text: e.text, t0: e.t0, acked: !!e.ack, entry: e }));
  let lastRemind = 0, pending = null, lastSessions = null, extra = [];
  const typeCfg = t => Object.assign({ on: !AL_TYPES[t].off, sound: AL_TYPES[t].sound }, cfg.types[t] || {});
  const saveCfg = () => store.set('jh_alert_cfg', JSON.stringify(cfg));
  const saveLog = () => { if (log.length > 300) log.length = 300; store.set('jh_alert_log', JSON.stringify(log)); };
  const when = t => new Date(t).toLocaleString([], { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
  const link = (t, text) => t ? `<button type="button" class="lnk" data-act="fix" data-tree="${t}">${text || 'Fix it'}</button>` : '';

  /* ---------------- what is wrong right now ---------------- */
  function detect(L) {
    const out = [], w = warnPct();
    const add = (id, type, text) => out.push({ id, type, text });
    if (S.err) add('stale:err', 'stale', 'Cannot read Blynk: ' + S.err);
    else if (S.link === 'dead') add('stale:old', 'stale', 'The data on the page is old (the Online Server stopped uploading, or the internet at the site is down)');
    if (L) {
      const n = nodeFlags(L);
      (L.b || []).forEach((b, i) => {
        if (!b) return; const def = BOXES[i];
        if (n[def.brain]) {
          const name = def.st[b[0]] || '';
          if (name === 'OUT OF ORDER') add('oos:' + i, 'out_of_order', `${def.name} is OUT OF ORDER`);
          if (name === 'SELECT DRY ONLY') add('dry:' + i, 'cleaning_off', `${def.name}: CLEANING is switched off (a container is low). Drying still works.`);
          if (name === 'SAFETY PAUSE') add('pause:' + i, 'safety_pause', `${def.name}: paused, a door was opened during a run`);
        }
        if (def.tanks && n[def.sensors]) [['a', 'alcohol', 5], ['s', 'scented', 6]].forEach(([k, nm, ix]) => {
          const v = nz(b[ix], -1); if (v < 0) return;
          if (v < 15) add(`low:${i}${k}`, 'container_low', `${def.name}: ${nm} container is low (${Math.round(v)}%)`);
          else if (w && v < w) add(`early:${i}${k}`, 'early_low', `${def.name}: ${nm} container is getting low (${Math.round(v)}%)`);
        });
      });
      (L.a || []).forEach((a, i) => {
        if (!a || !a[0]) return; const t = 'ACS ' + (i + 1), flags = a[2], empty = a[3];
        [[1, 'WATER'], [2, 'ALCOHOL'], [4, 'SCENTED']].forEach(([bit, nm]) => { if (empty & bit) add(`tank:${i}${bit}`, 'tank_empty', `${t}: ${nm} supply tank is empty`); });
        [[1, 'alcohol', 5, 'a'], [4, 'scented', 6, 's']].forEach(([bit, nm, ix, k]) => {
          if (flags & bit) add(`mix:${i}${k}`, 'mixer_low', `${t}: ${nm} mixer container is low`);
          else { const v = nz(a[ix], -1); if (w && v >= 0 && v < w && v >= 15) add(`mixe:${i}${k}`, 'early_low', `${t}: ${nm} mixer container is getting low (${Math.round(v)}%)`); }
        });
      });
      if (L.n != null) {
        const off = n.map((v, i) => v ? -1 : i).filter(i => i >= 0);
        if (off.length === 13) add('off:all', 'all_offline', 'The Online Server hears no machine board at all (radio or power problem at the site)');
        else off.forEach(i => add('off:' + i, 'board_offline', NODES[i] + ' is offline'));
      }
    }
    return out.concat(extra);
  }

  /* ---------------- the update step (after every reading) ---------------- */
  function update(L) {
    const now = Date.now(), list = detect(L), ids = new Set(list.map(a => a.id)), fresh = [], gone = [];
    for (const [id, a] of active) if (!ids.has(id)) { a.entry.t1 = now; active.delete(id); gone.push(a); }
    list.forEach(a => {
      if (active.has(a.id)) { active.get(a.id).text = a.text; return; }
      const sev = AL_TYPES[a.type].sev, entry = { id: a.id, type: a.type, sev, text: a.text, t0: now, t1: 0, ack: 0 };
      log.unshift(entry); const rec = { id: a.id, type: a.type, sev, text: a.text, t0: now, acked: false, entry };
      active.set(a.id, rec); fresh.push(rec);
    });
    // a customer finished a session (the "customers served" counter went up)
    const ss = S.stats && S.stats.ss;
    if (typeof ss === 'number') {
      if (lastSessions !== null && ss > lastSessions) { const e = { id: 'session', type: 'session', sev: 'info', text: `A customer finished (${ss.toLocaleString()} served in total)`, t0: now, t1: now, ack: 1 }; if (typeCfg('session').on) { log.unshift(e); playFor('session', e.text); } }
      lastSessions = ss;
    }
    if (fresh.length || gone.length) saveLog();
    if (fresh.length) {
      const top = fresh.slice().sort((a, b) => SEV[b.sev] - SEV[a.sev]).find(a => typeCfg(a.type).on);
      if (top) playFor(top.type, top.text);
      fresh.filter(a => typeCfg(a.type).on && SEV[a.sev] >= 1).slice(0, 3).forEach(a => notify(a));
      if (fresh.length > 3) notifyText('Jolly Helmet', (fresh.length - 3) + ' more alerts. Open the page to see them.', 'more');
    } else if (gone.length && cfg.playClear && typeCfg('cleared').on && gone.some(a => SEV[a.sev] >= 2)) playFor('cleared', 'An alert cleared');
    refreshUi();
  }

  function playFor(type, text) {
    const t = typeCfg(type);
    if (!t.on || t.sound === 'none') return;
    if (Sound.play(t.sound) === 0 && Sound.st.on && !Sound.ready()) pending = t.sound;       // the browser still needs one tap before it allows sound
  }
  function notifyText(title, body, tag, sev) {
    if (!cfg.notify || !('Notification' in window) || Notification.permission !== 'granted') return;
    if (!document.hidden && sev !== 'critical') return;                                      // a visible page shows the banner instead
    try { new Notification(title, { body, tag, icon: 'img/logo.png', requireInteraction: sev === 'critical' }); } catch (e) {}
  }
  function notify(a) { notifyText(`Jolly Helmet: ${SEV_NAME[a.sev]}`, a.text, a.id, a.sev); }

  /* reminders: a critical alert that nobody acknowledged repeats its sound */
  setInterval(() => {
    if (!cfg.repeatSec || Sound.st.on === false) return;
    const now = Date.now();
    if (pending && Sound.ready()) { const p = pending; pending = null; Sound.play(p); lastRemind = now; return; }
    if (now - lastRemind < cfg.repeatSec * 1000) return;
    const loud = [...active.values()].filter(a => !a.acked && AL_TYPES[a.type].repeat && typeCfg(a.type).on).sort((a, b) => SEV[b.sev] - SEV[a.sev])[0];
    if (loud) { lastRemind = now; Sound.play(typeCfg(loud.type).sound); }
  }, 4000);

  function ack(id) {
    [...active.values()].forEach(a => { if (!id || a.id === id) { a.acked = true; a.entry.ack = 1; } });
    saveLog(); refreshUi();
  }

  /* ---------------- number in the title and in the tab icon ---------------- */
  const iconEl = document.querySelector('link[rel~="icon"]'), baseIcon = iconEl ? iconEl.href : '';
  let logoImg = null;
  function drawIcon(n, sev) {
    if (!iconEl) return;
    if (!n) { iconEl.href = baseIcon; return; }
    const draw = () => {
      const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d');
      if (logoImg && logoImg.complete && logoImg.naturalWidth) g.drawImage(logoImg, 0, 4, 56, 56 * logoImg.naturalHeight / logoImg.naturalWidth);
      g.fillStyle = sev === 'critical' ? '#d32f2f' : sev === 'warning' ? '#f59e0b' : '#1c5fa8'; g.beginPath(); g.arc(46, 18, 17, 0, 7); g.fill();
      g.fillStyle = '#fff'; g.font = 'bold 22px Arial'; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText(n > 9 ? '9+' : String(n), 46, 19);
      iconEl.href = c.toDataURL('image/png');
    };
    if (!logoImg) { logoImg = new Image(); logoImg.onload = draw; logoImg.src = 'img/logo.png'; } else draw();
  }
  function counts() {
    const list = [...active.values()].filter(a => SEV[a.sev] >= 1);
    const unacked = list.filter(a => !a.acked);
    const top = list.slice().sort((a, b) => SEV[b.sev] - SEV[a.sev])[0];
    return { all: list, unacked, top: top ? top.sev : null };
  }
  function refreshTitle() {
    const c = counts(), base = document.title.replace(/^\(\d+\)\s*/, '');
    document.title = c.unacked.length ? `(${c.unacked.length}) ${base}` : base;
  }

  /* ---------------- drawing ---------------- */
  const sevChip = s => `<span class="chip ${s === 'critical' ? 'bad' : s === 'warning' ? 'warn' : s === 'notice' ? 'use' : ''}">${SEV_NAME[s]}</span>`;
  function banner() {                                             // the box at the top of the Monitor tab
    const c = counts(), parts = [];
    if (Sound.st.on && !Sound.ready() && !Sound.dismissed) parts.push(`<div class="note" style="display:flex;gap:10px;align-items:center;flex-wrap:wrap"><span>&#128263; Alert sounds are waiting: the browser needs one tap before it lets this page make noise.</span><button class="btn" data-act="sound-on" type="button" style="margin:0">Turn on alert sounds</button></div>`);
    if (c.all.length) {
      const sorted = c.all.slice().sort((a, b) => SEV[b.sev] - SEV[a.sev]);
      parts.push(`<div class="alert ${c.top}"><div style="display:flex;justify-content:space-between;gap:8px;flex-wrap:wrap;align-items:center"><span>Needs attention (${c.all.length})</span>${c.unacked.length ? '<button class="btn" data-act="ack-all" type="button" style="margin:0;padding:5px 12px">Acknowledge (stop the repeating sound)</button>' : '<span class="chip">acknowledged</span>'}</div><ul>` +
        sorted.map(a => `<li>${sevChip(a.sev)} ${esc(a.text)} ${link(AL_TYPES[a.type].fix)}</li>`).join('') + '</ul></div>');
    }
    return parts.join('');
  }
  function refreshUi() {
    const c = counts();
    const bd = $('alertBadge'); if (bd) { bd.textContent = c.unacked.length || ''; bd.className = 'tabbadge' + (c.unacked.length ? ' on ' + (c.top || '') : ''); }
    refreshTitle(); drawIcon(c.unacked.length, c.top);
    if (typeof drawBanners === 'function') drawBanners();
    if (!$('alertsTab').hidden) drawTab();
  }

  function drawTab() {
    const c = counts(), now = Date.now();
    // active
    $('alActive').innerHTML = `<div class="card-h"><div><b>Active now</b><small>${c.all.length ? c.all.length + ' alert' + (c.all.length > 1 ? 's' : '') + (c.unacked.length ? '' : ', all acknowledged') : 'nothing needs attention'}</small></div>${c.unacked.length ? '<button class="btn" data-act="ack-all" type="button" style="margin:0;padding:6px 12px">Acknowledge all</button>' : ''}</div>` +
      (c.all.length ? c.all.slice().sort((a, b) => SEV[b.sev] - SEV[a.sev]).map(a => `<div class="row"><span>${sevChip(a.sev)} ${esc(a.text)}<br><small>since ${when(a.t0)} (${ago((now - a.t0) / 1000)})</small></span><span>${link(AL_TYPES[a.type].fix)}</span></div>`).join('') : '<div class="empty">All clear.</div>');
    // sounds + notifications
    const perm = ('Notification' in window) ? Notification.permission : 'unsupported';
    const opts = cur => Object.keys(Sound.PATTERNS).map(k => `<option value="${k}"${k === cur ? ' selected' : ''}>${esc(Sound.PATTERNS[k].label)}</option>`).join('');
    const ss = Sound.state();
    $('alSound').innerHTML = `<div class="card-h"><div><b>Sounds and notifications</b><small>${!Sound.st.on ? 'sounds are switched off' : Sound.ready() ? 'sound is ready' : 'tap "Turn on / test" once: the browser needs a tap before it lets a page make noise'}</small></div></div>` +
      `<label class="chk"><input type="checkbox" data-set="sound-on" ${Sound.st.on ? 'checked' : ''}><span>Play alert sounds on this device</span></label>` +
      `<label for="alVol">Volume</label><input id="alVol" type="range" min="0" max="100" value="${Math.round(Sound.st.vol * 100)}" data-set="vol"><div style="margin-top:6px"><button class="btn" data-act="sound-test" type="button" style="margin-top:6px">Turn on / test</button></div>` +
      `<label for="alRep">Repeat an unacknowledged CRITICAL alert every</label><select id="alRep" data-set="repeat">${[[0, 'never'], [30, '30 seconds'], [60, '1 minute'], [120, '2 minutes'], [300, '5 minutes']].map(([v, t]) => `<option value="${v}"${cfg.repeatSec === v ? ' selected' : ''}>${t}</option>`).join('')}</select>` +
      `<label for="alWarn">Early warning for containers at</label><select id="alWarn" data-set="warn">${[[0, 'off'], [20, '20 %'], [25, '25 %'], [30, '30 %'], [35, '35 %'], [40, '40 %']].map(([v, t]) => `<option value="${v}"${warnPct() === v ? ' selected' : ''}>${t}</option>`).join('')}</select>` +
      `<label class="chk"><input type="checkbox" data-set="clear" ${cfg.playClear ? 'checked' : ''}><span>Play a chime when a warning clears</span></label>` +
      `<label class="chk"><input type="checkbox" data-set="notify" ${cfg.notify && perm === 'granted' ? 'checked' : ''} ${perm === 'unsupported' || perm === 'denied' ? 'disabled' : ''}><span>Show a browser notification when the page is in the background${perm === 'denied' ? ' (blocked in this browser: allow notifications for this site in the address bar)' : perm === 'unsupported' ? ' (not supported here)' : ''}</span></label>` +
      `<table class="sndt"><tr><th>Alert</th><th>On</th><th>Sound</th><th></th></tr>` + Object.keys(AL_TYPES).map(t => { const T = AL_TYPES[t], k = typeCfg(t); return `<tr><td>${sevChip(T.sev)} ${esc(T.label)}${T.repeat ? ' <small>(repeats)</small>' : ''}</td><td><input type="checkbox" data-type="${t}" data-set="type-on" ${k.on ? 'checked' : ''} aria-label="${esc(T.label)} on"></td><td><select data-type="${t}" data-set="type-sound" aria-label="Sound for ${esc(T.label)}">${opts(k.sound)}</select></td><td><button class="ibtn2" type="button" data-act="sound-try" data-type="${t}" title="Play">&#9654;</button></td></tr>`; }).join('') + `</table>` +
      `<p class="fine">Sounds play only while this page is open (a background tab is fine). To be told when the page is closed, use the Blynk app: README.txt, "Alerts when the page is closed".</p>`;
    // log
    $('alLog').innerHTML = `<div class="card-h"><div><b>Alert log</b><small>kept in this browser (last ${Math.min(log.length, 300)} entries)</small></div><div><button class="btn alt" data-act="log-csv" type="button" style="margin:0">Download CSV</button> <button class="btn alt" data-act="log-clear" type="button" style="margin:0">Clear</button></div></div>` +
      (log.length ? `<div class="scroll"><table class="logt"><tr><th>Time</th><th>Level</th><th>What</th><th>Lasted</th></tr>` + log.slice(0, 100).map(e => `<tr><td>${when(e.t0)}</td><td>${sevChip(e.sev)}</td><td>${esc(e.text)}</td><td>${e.type === 'session' ? '' : e.t1 ? ago((e.t1 - e.t0) / 1000) : '<b>still on</b>'}</td></tr>`).join('') + `</table></div>` : '<div class="empty">Nothing has happened yet while this page was open.</div>');
  }

  /* ---------------- clicks ---------------- */
  document.addEventListener('click', e => {
    const b = e.target.closest('[data-act]'); if (!b) return;
    const a = b.dataset.act;
    if (a === 'ack-all') ack();
    else if (a === 'sound-on') { Sound.set('on', true); Sound.unlock(); setTimeout(() => { if (Sound.ready()) Sound.play('chime'); refreshUi(); }, 150); }
    else if (a === 'sound-test') { Sound.set('on', true); Sound.unlock(); setTimeout(() => { Sound.play('chime'); refreshUi(); }, 150); }
    else if (a === 'sound-try') { const t = b.dataset.type; Sound.unlock(); setTimeout(() => Sound.test(typeCfg(t).sound), 120); }
    else if (a === 'fix') { if (typeof Wizard !== 'undefined') Wizard.open(b.dataset.tree); }
    else if (a === 'log-csv') download('jolly_helmet_alert_log.csv', csv());
    else if (a === 'log-clear') { if (confirm('Clear the alert log of this browser?')) { log = log.filter(x => !x.t1 ? true : false); saveLog(); drawTab(); } }
  });
  document.addEventListener('change', e => {
    const el = e.target.closest('[data-set]'); if (!el) return;
    const k = el.dataset.set;
    if (k === 'sound-on') { Sound.set('on', el.checked); if (el.checked) Sound.unlock(); }
    else if (k === 'vol') Sound.set('vol', el.value / 100);
    else if (k === 'repeat') { cfg.repeatSec = Number(el.value); saveCfg(); }
    else if (k === 'warn') { store.set('jh_warn', el.value); if (S.live) renderAll(); update(S.live); }
    else if (k === 'clear') { cfg.playClear = el.checked; saveCfg(); }
    else if (k === 'notify') {
      if (el.checked && 'Notification' in window) Notification.requestPermission().then(p => { cfg.notify = p === 'granted'; saveCfg(); drawTab(); });
      else { cfg.notify = false; saveCfg(); }
    } else if (k === 'type-on') { (cfg.types[el.dataset.type] = cfg.types[el.dataset.type] || {}).on = el.checked; saveCfg(); }
    else if (k === 'type-sound') { (cfg.types[el.dataset.type] = cfg.types[el.dataset.type] || {}).sound = el.value; saveCfg(); Sound.unlock(); setTimeout(() => Sound.test(el.value), 120); }
  });
  document.addEventListener('input', e => { const el = e.target.closest('[data-set="vol"]'); if (el) Sound.set('vol', el.value / 100); });

  /* ---------------- files ---------------- */
  function csv() {
    const q = s => '"' + String(s).replace(/"/g, '""') + '"', d = t => t ? new Date(t).toISOString().replace('T', ' ').slice(0, 19) : '';
    return 'started,ended,level,type,text,seconds\r\n' + log.map(e => [d(e.t0), d(e.t1), e.sev, e.type, q(e.text), e.t1 ? Math.round((e.t1 - e.t0) / 1000) : ''].join(',')).join('\r\n') + '\r\n';
  }
  return { update, ack, banner, drawTab, refreshUi, refreshTitle, csv, counts, setExtra(list) { extra = list || []; update(S.live); }, log: () => log };
})();

/* a file download without a server */
function download(name, text, type) {
  const url = URL.createObjectURL(new Blob(['﻿' + text], { type: type || 'text/csv;charset=utf-8' }));
  const a = document.createElement('a'); a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}
