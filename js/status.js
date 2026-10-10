"use strict";
/* The customer status page. Reads ONE Blynk datastream (v0) of the PUBLIC status device and shows, for each machine, whether it is free,
   in use (about how long) or out of order. It reads only: it cannot start or change anything.

   Which token it uses:  js/config.js publicToken  >  #t=... in the address (the QR code)  >  jh_pubtoken saved in this browser.
   Staff convenience: on a phone that already has the private monitor token, that token is used as a last resort (and a note says so).
   What the public device holds:  { seq, ts, iv, b:[[step, seconds left, heard] x3] }   (DATA_CONTRACT.md, section 5). The full monitor value
   (with "n" and 9 numbers per box) is understood too.                                                                            */
const TX = {
  en: {
    title: 'Which machine is free?', tag: 'Make your Helmet Happy Again!', lang: 'Filipino',
    free: 'FREE', freeH: 'Walk up and tap the screen to start.', busy: 'IN USE', busyT: 'About {m} min left.', busyN: 'Somebody is using it right now.', paused: 'IN USE', pausedH: 'It is paused for a moment.',
    oos: 'OUT OF ORDER', oosH: 'Please use another machine.', maint: 'NOT AVAILABLE', maintH: 'It is being looked after. Please use another machine.',
    dry: 'FREE (DRYING ONLY)', dryH: 'Cleaning is not available right now. Drying works.', noinfo: 'NO INFORMATION', noinfoH: 'Please look at the machine itself.',
    updated: 'Updated {t} ago', loading: 'Getting the latest information...',
    stale: 'The information is not up to date. Please look at the machine itself.', err: 'Cannot get the status right now. Please look at the machine itself.',
    notoken: 'This page is not set up yet: the staff has to add the public status token.', staff: 'Staff view: this page is using the private monitor token of this phone. Customers need the public status token.',
    howT: 'How it works', how: ['Pick a machine that says FREE.', 'Tap its screen, answer the questions and put the coins in.', 'Put your helmet in, close the door and wait for the timer.', 'Take your helmet out when the screen says so.'],
    helpT: 'Something wrong?', foot: 'This page only shows the state of the machines.', min: 'min', s: 's'
  },
  tl: {
    title: 'Alin ang libre?', tag: 'Make your Helmet Happy Again!', lang: 'English',
    free: 'LIBRE', freeH: 'Lumapit at i-tap ang screen para magsimula.', busy: 'GINAGAMIT', busyT: 'Mga {m} min na lang.', busyN: 'May gumagamit ngayon.', paused: 'GINAGAMIT', pausedH: 'Sandaling naka-pause.',
    oos: 'SIRA', oosH: 'Pakigamit ang ibang makina.', maint: 'HINDI MAGAGAMIT', maintH: 'Inaayos pa. Pakigamit ang ibang makina.',
    dry: 'LIBRE (PAMPATUYO LANG)', dryH: 'Hindi muna magagamit ang paglilinis. Gumagana ang pagpapatuyo.', noinfo: 'WALANG IMPORMASYON', noinfoH: 'Pakitingnan mismo ang makina.',
    updated: 'Na-update {t} na ang nakalipas', loading: 'Kinukuha ang pinakabagong impormasyon...',
    stale: 'Hindi bago ang impormasyon. Pakitingnan mismo ang makina.', err: 'Hindi makuha ang status ngayon. Pakitingnan mismo ang makina.',
    notoken: 'Hindi pa naka-set up ang page na ito: kailangang idagdag ng staff ang public status token.', staff: 'Staff view: ginagamit ng page na ito ang private monitor token ng phone na ito. Kailangan ng mga customer ang public status token.',
    howT: 'Paano gamitin', how: ['Pumili ng makinang LIBRE.', 'I-tap ang screen, sagutin ang mga tanong at ilagay ang mga barya.', 'Ilagay ang helmet, isara ang pinto at hintayin ang timer.', 'Kunin ang helmet kapag sinabi ng screen.'],
    helpT: 'May problema?', foot: 'Ipinapakita lamang ng page na ito ang kalagayan ng mga makina.', min: 'min', s: 's'
  }
};
const SS = { lang: store.get('jh_lang') || ((navigator.language || '').toLowerCase().match(/^(fil|tl)/) ? 'tl' : 'en'), data: null, at: 0, err: '', staff: false, token: '', timer: null };
const T = k => TX[SS.lang][k];

function pickToken() {
  const m = /[#&]t=([^&]+)/.exec(location.hash);
  if (m) return decodeURIComponent(m[1]);
  if (CONFIG.publicToken) return CONFIG.publicToken;
  const saved = store.get('jh_pubtoken'); if (saved) return saved;
  if (cfg.token) { SS.staff = true; return cfg.token; }                  // a staff phone that already has the private token
  return '';
}

/* the step name of a box -> what a customer needs to know */
function classify(i, b, L) {
  const heard = b.length >= 3 ? !!b[2] : (L.n != null ? String(L.n).charAt(BOXES[i].brain) === '1' : true);
  if (!heard) return { k: 'noinfo' };
  const name = BOXES[i].st[b[0]] || '';
  if (name === 'OUT OF ORDER') return { k: 'oos' };
  if (name === 'MAINTENANCE') return { k: 'maint' };
  if (name === 'SELECT DRY ONLY') return { k: 'dryonly' };
  if (name === 'IDLE' || name === 'TAPS') return { k: 'free' };
  if (name === 'SAFETY PAUSE') return { k: 'busy', paused: true };
  const secs = Number(b[1]) || 0;
  return { k: 'busy', secs: RUNNING.has(name) && secs > 0 ? secs : 0 };
}

function draw() {
  const L = SS.data, now = Date.now();
  $('title').textContent = T('title'); $('tag').textContent = T('tag'); $('lang').textContent = T('lang');
  document.documentElement.lang = SS.lang === 'tl' ? 'fil' : 'en';
  $('howT').textContent = T('howT'); $('how').innerHTML = T('how').map(x => `<li>${esc(x)}</li>`).join('');
  $('helpT').textContent = T('helpT'); $('help').textContent = CONFIG.contact; $('foot').textContent = T('foot');
  let notice = '', stale = false;
  if (!SS.token) notice = `<div class="note">${esc(T('notoken'))}</div>`;
  else if (SS.err) notice = `<div class="note">${esc(T('err'))}</div>`;
  else if (L) {
    const iv = nz(L.iv, 60), age = (L.ts > 1.6e9) ? Math.max(0, now / 1000 - L.ts) : (now - SS.at) / 1000;
    $('upd').textContent = T('updated').replace('{t}', ago(age));
    if (age > Math.max(240, iv * 4 + 60)) { stale = true; notice = `<div class="note">${esc(T('stale'))}</div>`; }
  } else $('upd').textContent = T('loading');
  if (SS.staff) notice += `<div class="note">${esc(T('staff'))}</div>`;
  $('notice').innerHTML = notice;
  const bad = !SS.token || SS.err || stale || !L;
  const icons = { free: '&#10003;', busy: '&#9202;', oos: '&#10005;', maint: '&#9874;', dryonly: '&#10003;', noinfo: '?' };
  $('boxes').innerHTML = [0, 1, 2].map(i => {
    const def = BOXES[i], b = L && L.b && L.b[i];
    const c = (bad || !b) ? { k: 'noinfo' } : classify(i, b, L);
    let stt, hint;
    if (c.k === 'free') { stt = T('free'); hint = T('freeH'); }
    else if (c.k === 'busy') {
      stt = T('busy');
      if (c.paused) hint = T('pausedH');
      else if (c.secs) { const left = Math.max(0, c.secs - (now - SS.at) / 1000); hint = T('busyT').replace('{m}', `<span class="tl">${Math.max(1, Math.ceil(left / 60))}</span>`); }
      else hint = T('busyN');
    } else if (c.k === 'oos') { stt = T('oos'); hint = T('oosH'); }
    else if (c.k === 'maint') { stt = T('maint'); hint = T('maintH'); }
    else if (c.k === 'dryonly') { stt = T('dry'); hint = T('dryH'); }
    else { stt = T('noinfo'); hint = T('noinfoH'); }
    const price = CONFIG.prices && CONFIG.prices[def.name];
    return `<div class="bx ${c.k}"><div class="ic" aria-hidden="true">${icons[c.k]}</div><div><div class="nm">${esc(def.name)}<small>${esc(def.role)}</small></div><div class="stt">${esc(stt)}</div><div class="hint">${hint}</div>${price ? `<div class="pr">${esc(price)}</div>` : ''}</div></div>`;
  }).join('');
}

async function poll() {
  clearTimeout(SS.timer);
  if ((!document.hidden || !SS.data) && SS.token) {
    try {
      const all = DEMO ? demoPublic() : await blynkPublic();
      const raw = all.v0 == null ? '' : (typeof all.v0 === 'string' ? all.v0 : JSON.stringify(all.v0));
      const L = raw ? JSON.parse(raw) : null;
      SS.err = '';
      if (L && (!SS.data || L.seq !== SS.data.seq)) SS.at = Date.now();
      SS.data = L;
    } catch (e) { SS.err = (e && e.message) || 'error'; }
    draw();
  }
  SS.timer = setTimeout(poll, Math.max(10, CONFIG.statusPollSec) * 1000);
}
async function blynkPublic() {
  const ctl = new AbortController(), kill = setTimeout(() => ctl.abort(), 12000);
  try {
    const r = await fetch(`https://${cfg.server}/external/api/getAll?token=${encodeURIComponent(SS.token)}`, { cache: 'no-store', signal: ctl.signal });
    const text = await r.text(); let j; try { j = JSON.parse(text); } catch (e) { j = text; }
    if (!r.ok || (j && j.error)) throw new Error((j && j.error && j.error.message) || 'HTTP ' + r.status);
    return j;
  } finally { clearTimeout(kill); }
}

$('lang').onclick = () => { SS.lang = SS.lang === 'en' ? 'tl' : 'en'; store.set('jh_lang', SS.lang); draw(); };
document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
SS.token = DEMO ? 'demo' : pickToken();
if (!DEMO && /#t=/.test(location.hash)) { /* keep the token in the address: it is the public one */ }
draw(); poll(); setInterval(draw, 1000);                   // draw() every second keeps "updated x ago" and the minutes left moving
/* light / dark: the same choice as the monitor page (button there), or  status.html?theme=light */
(function () { const q = new URLSearchParams(location.search).get('theme'), s = store.get('jh_theme'); const t = (q === 'light' || q === 'dark') ? q : s; if (t) document.documentElement.dataset.theme = t; })();
