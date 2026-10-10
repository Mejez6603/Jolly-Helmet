"use strict";
/* The page's engine: the refresh loop, the status badge, the Settings window and the theme button. Loaded last. */

/* ---- status badge + "updated x ago" (runs every second) ---- */
function tickStatus() {
  const pill = $('pill'), t = $('pillT'), ag = $('agoText');
  if (S.err) {
    pill.className = 'pill dead'; t.textContent = /token/i.test(S.err) ? 'BAD TOKEN' : 'NO LINK';
    ag.textContent = S.okAt ? 'last answer ' + ago((Date.now() - S.okAt) / 1000) + ' ago' : ''; return;
  }
  if (!S.okAt) { pill.className = 'pill'; t.textContent = 'CONNECTING'; ag.textContent = ''; return; }
  if (!S.live) { pill.className = 'pill late'; t.textContent = 'NO DATA'; ag.textContent = ''; return; }
  const iv = nz(S.live.iv, 60), now = Date.now();
  const age = (S.live.ts > 1.6e9) ? Math.max(0, now / 1000 - S.live.ts) : (now - S.lastChange) / 1000;   // device time if it has one
  ag.textContent = 'updated ' + ago(age) + ' ago';
  if (age <= iv * 2 + 15)      { pill.className = 'pill live'; t.textContent = DEMO ? 'DEMO' : 'LIVE'; }
  else if (age <= iv * 6 + 30) { pill.className = 'pill late'; t.textContent = 'DELAYED'; }
  else                         { pill.className = 'pill dead'; t.textContent = 'OLD DATA'; }
}

/* ---- refresh loop ---- */
let timer = null;
async function loop(force) {
  clearTimeout(timer);
  if (force || !document.hidden) {                       // a hidden tab does not poll (saves Blynk requests)
    try { await pollOnce(); }
    catch (e) {
      S.err = (e && e.message) || 'network error';
      if (/Failed to fetch|NetworkError|Load failed|abort/i.test(S.err)) S.err = 'no internet, or Blynk did not answer.';
    }
    renderAll(); tickStatus();
  }
  timer = setTimeout(loop, (S.err && /token/i.test(S.err) ? 30 : cfg.pollSec) * 1000);
}
document.addEventListener('visibilitychange', () => { if (!document.hidden) loop(); });

function start() {
  if (!DEMO && !cfg.token) { $('connect').hidden = false; $('view').hidden = true; $('pillT').textContent = 'NOT CONNECTED'; return; }
  if (DEMO) $('foot').textContent = 'DEMO MODE: the numbers on this page are made up. Read-only monitor.';
  loop(true); setInterval(tickStatus, 1000);
}

/* ---- Settings window ---- */
const dlg = $('dlg');
function openSettings() {
  $('tk1').value = cfg.token; $('srv').value = cfg.server; $('poll').value = String(cfg.pollSec);
  $('cfgnote').textContent = CONFIG.token ? 'The token is written inside js/config.js, so changing it here has no effect.' : 'Stored only in this browser.';
  dlg.showModal();
}
$('setBtn').onclick = openSettings;
$('close').onclick = () => dlg.close();
$('save').onclick = () => { store.set('jh_token', $('tk1').value.trim()); store.set('jh_server', $('srv').value); store.set('jh_poll', $('poll').value); location.reload(); };
$('forget').onclick = () => { store.del('jh_token'); location.reload(); };
$('go0').onclick = () => { const v = $('tk0').value.trim(); if (v) { store.set('jh_token', v); location.reload(); } };
$('tk0').addEventListener('keydown', e => { if (e.key === 'Enter') $('go0').click(); });
$('demo0').onclick = () => { location.search = '?demo'; };

/* ---- light / dark ---- */
$('themeBtn').onclick = () => {
  const cur = document.documentElement.dataset.theme || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  const nx = cur === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = nx; store.set('jh_theme', nx);
};
if (store.get('jh_theme')) document.documentElement.dataset.theme = store.get('jh_theme');

start();
