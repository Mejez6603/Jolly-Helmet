"use strict";
/* The customer status page. Reads the datastreams of the PUBLIC status Blynk device and shows, for each machine, whether it is free,
   in use (about how long) or out of order. It cannot start or change anything on a machine.

   OPTIONAL CUSTOMER NAMES (2026-10-14). While a machine is running a customer's cycle, the customer can type a name or nickname on its card. Everybody
   sees it there ("Maria's helmet"); the customer's own phone also says "your helmet is done" and the name clears by itself. This is the only thing
   the page ever writes, and only into one extra String datastream of the PUBLIC device (js/config.js: names, namesPin, default V5):
     [ ["Maria", 12, 1791540500], 0, 0 ]      = one slot per box (Box 1, Box 2, Box 3): [name, the box's session counter when it was typed, server time], or 0.
   A name is valid only while the box is in a step where a helmet is being processed (OPEN A ... CLEANING / HEATING / DRYING) AND the box's session counter is
   still the one stored with the name. The counter (4th number of every box in the public V0, firmware of 2026-10-14) goes up when the cycle ends, so the name
   disappears at that moment, whoever is looking. Without that counter (older firmware) a name is dropped when the box leaves those steps or after 45 minutes.
   Whatever sits in that datastream is untrusted text (anybody holding the public token can write to it): it is parsed defensively and only ever shown as text.

   Which token it uses:  js/config.js publicToken  >  #t=... in the address (the QR code)  >  jh_pubtoken saved in this browser.
   Staff convenience: on a phone that already has the private monitor token, that token is used as a last resort (and a note says so); names are off then.
   What the public device holds:  { seq, ts, iv, b:[[step, seconds left, heard, sessions] x3] }   (DATA_CONTRACT.md, section 5). The full monitor value
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
    whose: "{n}'s helmet", addName: 'Add your name', optional: '(optional)', yourName: 'Your name', edit: 'Edit', remove: 'Remove', save: 'Save', cancel: 'Cancel',
    namePh: 'Name or nickname', nameHint: 'A name or nickname. Other customers can see it. It disappears when your helmet is done.',
    saving: 'Saving...', nameErr: 'Could not save the name. Please try again.', nameTaken: 'Somebody has already added a name for this machine.', namesOff: 'Names are not switched on yet. Please ask the staff.',
    doneT: '{n}, your helmet in {b} is done!', doneH: 'Take it out when the screen says so.', ok: 'OK',
    howT: 'How it works', how: ['Pick a machine that says FREE.', 'Tap its screen, answer the questions and put the coins in.', 'Put your helmet in, close the door and wait for the timer.', 'Optional: tap "Add your name" on your machine. Your name shows on its card while your helmet is being looked after, and goes away by itself when it is done.', 'Take your helmet out when the screen says so.'],
    helpT: 'Something wrong?', foot: 'This page shows the state of the machines and the names customers choose to add.', min: 'min', s: 's'
  },
  tl: {
    title: 'Alin ang libre?', tag: 'Make your Helmet Happy Again!', lang: 'English',
    free: 'LIBRE', freeH: 'Lumapit at i-tap ang screen para magsimula.', busy: 'GINAGAMIT', busyT: 'Mga {m} min na lang.', busyN: 'May gumagamit ngayon.', paused: 'GINAGAMIT', pausedH: 'Sandaling naka-pause.',
    oos: 'SIRA', oosH: 'Pakigamit ang ibang makina.', maint: 'HINDI MAGAGAMIT', maintH: 'Inaayos pa. Pakigamit ang ibang makina.',
    dry: 'LIBRE (PAMPATUYO LANG)', dryH: 'Hindi muna magagamit ang paglilinis. Gumagana ang pagpapatuyo.', noinfo: 'WALANG IMPORMASYON', noinfoH: 'Pakitingnan mismo ang makina.',
    updated: 'Na-update {t} na ang nakalipas', loading: 'Kinukuha ang pinakabagong impormasyon...',
    stale: 'Hindi bago ang impormasyon. Pakitingnan mismo ang makina.', err: 'Hindi makuha ang status ngayon. Pakitingnan mismo ang makina.',
    notoken: 'Hindi pa naka-set up ang page na ito: kailangang idagdag ng staff ang public status token.', staff: 'Staff view: ginagamit ng page na ito ang private monitor token ng phone na ito. Kailangan ng mga customer ang public status token.',
    whose: 'Helmet ni {n}', addName: 'Ilagay ang pangalan mo', optional: '(opsyonal)', yourName: 'Pangalan mo', edit: 'Baguhin', remove: 'Alisin', save: 'I-save', cancel: 'Kanselahin',
    namePh: 'Pangalan o palayaw', nameHint: 'Pangalan o palayaw. Makikita ito ng ibang customer. Mawawala ito kapag tapos na ang helmet mo.',
    saving: 'Sine-save...', nameErr: 'Hindi ma-save ang pangalan. Pakisubukan ulit.', nameTaken: 'May naglagay na ng pangalan para sa makinang ito.', namesOff: 'Hindi pa naka-on ang mga pangalan. Magtanong sa staff.',
    doneT: '{n}, tapos na ang helmet mo sa {b}!', doneH: 'Kunin ito kapag sinabi ng screen.', ok: 'OK',
    howT: 'Paano gamitin', how: ['Pumili ng makinang LIBRE.', 'I-tap ang screen, sagutin ang mga tanong at ilagay ang mga barya.', 'Ilagay ang helmet, isara ang pinto at hintayin ang timer.', 'Opsyonal: i-tap ang "Ilagay ang pangalan mo" sa makinang ginagamit mo. Lalabas ang pangalan mo sa card habang inaasikaso ang helmet mo at kusa itong mawawala kapag tapos na.', 'Kunin ang helmet kapag sinabi ng screen.'],
    helpT: 'May problema?', foot: 'Ipinapakita ng page na ito ang kalagayan ng mga makina at ang mga pangalang inilalagay ng mga customer.', min: 'min', s: 's'
  }
};
const SS = { lang: store.get('jh_lang') || ((navigator.language || '').toLowerCase().match(/^(fil|tl)/) ? 'tl' : 'en'), data: null, at: 0, err: '', staff: false, token: '', timer: null,
  names: [null, null, null], mine: null, edit: -1, busy: [false, false, false], msg: ['', '', ''], draft: ['', '', ''], whoSig: ['', '', ''], doneInfo: null, doneSig: '', hk: false, lastClean: 0,
  namesOff: false, demoNames: '', demoSeed: false };
const T = k => TX[SS.lang][k];

/* ---- the optional names ---- */
const NAMES_ON = CONFIG.names !== false;
const PIN = String(CONFIG.namesPin || 'v5').toLowerCase();
const NAME_MAX = 12;                                   // characters
const NAME_STALE_SEC = 45 * 60;                        // without a session counter: a name older than this is ignored
const MINE_KEEP_SEC = 3 * 3600;                        // this phone forgets "my name" after this long
const DONE_SHOW_MS = 20 * 60 * 1000;                   // the "your helmet is done" banner stays this long unless dismissed
/* steps in which a customer's helmet is being looked after (a name may be added and shown): after OPEN A until the cycle ends */
const NAME_STEPS = new Set(['OPEN A', 'SENSORS A', 'CLOSE DOOR A', 'HELMET NOTICE A', 'CLEANING', 'HEATING', 'DRYING', 'SAFETY PAUSE', 'ABORT']);

function cleanName(s) {                                // untrusted text -> a short plain name
  return Array.from(String(s == null ? '' : s).replace(/[\u0000-\u001f\u007f<>]/g, '').replace(/\s+/g, ' ').trim()).slice(0, NAME_MAX).join('').trim();
}
function parseNames(raw) {                             // the names datastream -> three records {n, s, t} or null
  const out = [null, null, null];
  if (!raw) return out;
  let a; try { a = JSON.parse(typeof raw === 'string' ? raw : JSON.stringify(raw)); } catch (e) { return out; }
  if (!Array.isArray(a)) return out;
  for (let i = 0; i < 3; i++) {
    const r = a[i];
    if (!Array.isArray(r) || typeof r[0] !== 'string') continue;
    const n = cleanName(r[0]); if (!n) continue;
    out[i] = { n, s: (typeof r[1] === 'number' && isFinite(r[1]) && r[1] >= 0) ? Math.floor(r[1]) : -1, t: (typeof r[2] === 'number' && isFinite(r[2])) ? r[2] : 0 };
  }
  return out;
}
function loadMine() {
  try {
    const m = JSON.parse(store.get('jh_mine') || 'null');
    if (m && typeof m.n === 'string' && m.box >= 0 && m.box < 3) return { box: m.box | 0, n: cleanName(m.n), s: nz(m.s, -1), t: nz(m.t, 0) };
  } catch (e) {}
  return null;
}
function saveMine(m) { SS.mine = m; if (m) store.set('jh_mine', JSON.stringify(m)); else store.del('jh_mine'); }
const isMine = (i, rec) => !!(rec && SS.mine && SS.mine.box === i && SS.mine.n === rec.n);

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
  if (!heard) return { k: 'noinfo', step: '', sess: -1 };
  const name = BOXES[i].st[b[0]] || '';
  const sess = (b.length > 3 && typeof b[3] === 'number' && isFinite(b[3]) && b[3] >= 0) ? Math.floor(b[3]) : -1;   // the public value's 4th number (the full monitor value has 9 numbers per box: index 3 is the doors there, so ignore it when there are more than 4)
  const sessOk = b.length === 4 ? sess : -1;
  const base = { step: name, sess: sessOk };
  if (name === 'OUT OF ORDER') return { k: 'oos', ...base };
  if (name === 'MAINTENANCE') return { k: 'maint', ...base };
  if (name === 'SELECT DRY ONLY') return { k: 'dryonly', ...base };
  if (name === 'IDLE' || name === 'TAPS') return { k: 'free', ...base };
  if (name === 'SAFETY PAUSE') return { k: 'busy', paused: true, ...base };
  const secs = Number(b[1]) || 0;
  return { k: 'busy', secs: RUNNING.has(name) && secs > 0 ? secs : 0, ...base };
}

/* ---- is the data usable, and what does each box show right now ---- */
function staleData() {
  const L = SS.data; if (!L) return true;
  const iv = nz(L.iv, 60), age = (L.ts > 1.6e9) ? Math.max(0, Date.now() / 1000 - L.ts) : (Date.now() - SS.at) / 1000;
  return age > Math.max(240, iv * 4 + 60);
}
const dataBad = () => !SS.token || !!SS.err || !SS.data || staleData();
function classOf(i) { const b = SS.data && SS.data.b && SS.data.b[i]; return (!dataBad() && b) ? classify(i, b, SS.data) : null; }
function serverNow() { const L = SS.data; return (L && L.ts > 1.6e9) ? L.ts + (Date.now() - SS.at) / 1000 : Date.now() / 1000; }

/* a name record is shown only while the helmet is being looked after AND it still belongs to the cycle it was typed in */
function nameLive(rec, c) {
  if (!rec || !c || c.k !== 'busy' || !NAME_STEPS.has(c.step)) return false;
  if (rec.s >= 0 && c.sess >= 0) return rec.s === c.sess;
  return rec.t > 1.6e9 ? (serverNow() - rec.t) <= NAME_STALE_SEC : false;
}
/* the cycle behind a name is definitely over (a clear answer: the box is free, or busy with something else or in a new cycle) */
function nameEnded(rec, c) { return !!(rec && c && (c.k === 'free' || (c.k === 'busy' && !nameLive(rec, c)))); }

function draw() {
  const L = SS.data, now = Date.now();
  $('title').textContent = T('title'); $('tag').textContent = T('tag'); $('lang').textContent = T('lang');
  document.documentElement.lang = SS.lang === 'tl' ? 'fil' : 'en';
  $('howT').textContent = T('howT'); $('how').innerHTML = T('how').map(x => `<li>${esc(x)}</li>`).join('');
  $('helpT').textContent = T('helpT'); $('help').textContent = CONFIG.contact; $('foot').textContent = T('foot');
  let notice = '';
  if (!SS.token) notice = `<div class="note">${esc(T('notoken'))}</div>`;
  else if (SS.err) notice = `<div class="note">${esc(T('err'))}</div>`;
  else if (L) {
    const iv = nz(L.iv, 60), age = (L.ts > 1.6e9) ? Math.max(0, now / 1000 - L.ts) : (now - SS.at) / 1000;
    $('upd').textContent = T('updated').replace('{t}', ago(age));
    if (staleData()) notice = `<div class="note">${esc(T('stale'))}</div>`;
  } else $('upd').textContent = T('loading');
  if (SS.staff) notice += `<div class="note">${esc(T('staff'))}</div>`;
  $('notice').innerHTML = notice;
  const bad = dataBad();
  const icons = { free: '&#10003;', busy: '&#9202;', oos: '&#10005;', maint: '&#9874;', dryonly: '&#10003;', noinfo: '?' };
  const host = $('boxes');
  if (host.children.length !== 3) {                        // the three cards are built once: only their contents change, so a name box that is open is never rebuilt
    host.innerHTML = [0, 1, 2].map(i => `<div class="bx noinfo" id="bx${i}"><div class="ic" id="ic${i}" aria-hidden="true"></div><div class="mid" id="mid${i}" aria-live="polite"></div><div class="who" id="who${i}"></div></div>`).join('');
  }
  [0, 1, 2].forEach(i => {
    const def = BOXES[i], b = L && L.b && L.b[i];
    const c = (bad || !b) ? { k: 'noinfo', step: '', sess: -1 } : classify(i, b, L);
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

    // the optional name
    const live = (NAMES_ON && !bad && b && nameLive(SS.names[i], c)) ? SS.names[i] : null;
    const canName = NAMES_ON && !SS.namesOff && !SS.staff && !bad && !!b && c.k === 'busy' && NAME_STEPS.has(c.step);
    const mine = isMine(i, live);
    const mode = live ? (mine ? (SS.edit === i ? 'edit' : 'mine') : 'other') : (canName ? (SS.edit === i ? 'edit' : 'add') : 'none');
    if (SS.edit === i && mode !== 'edit') SS.edit = -1;    // the box moved on while the editor was open
    const own = live ? `<div class="own"><span aria-hidden="true">&#128100;</span> ${esc(T('whose').replace('{n}', () => live.n))}</div>` : '';

    $('bx' + i).className = 'bx ' + c.k;
    setHtml($('ic' + i), icons[c.k]);
    setHtml($('mid' + i), `<div class="nm">${esc(def.name)}<small>${esc(def.role)}</small></div><div class="stt">${esc(stt)}</div><div class="hint">${hint}</div>${own}${price ? `<div class="pr">${esc(price)}</div>` : ''}`);
    renderWho(i, mode, live);
  });
  renderDone();
}

function setHtml(el, h) { if (el._h !== h) { el._h = h; el.innerHTML = h; } }          // only touch the page when something changed (no needless re-announcing by screen readers)

/* the name editor of one card: rebuilt only when what it shows changes (so typing is never interrupted by the one-second redraw) */
function renderWho(i, mode, rec) {
  const msg = SS.busy[i] ? T('saving') : SS.msg[i];
  const sig = [mode, rec ? rec.n : '', SS.busy[i] ? 1 : 0, msg, SS.lang].join('|');
  if (SS.whoSig[i] === sig) return;
  SS.whoSig[i] = sig;
  const el = $('who' + i);
  let h = '';
  if (mode === 'add') h = `<button type="button" class="nbtn" data-act="add" data-i="${i}">&#65291; ${esc(T('addName'))}<small>${esc(T('optional'))}</small></button>`;
  else if (mode === 'edit') h = `<form class="nform" data-i="${i}"><label class="nl" for="nin${i}">${esc(T('nameHint'))}</label><div class="nrow"><input id="nin${i}" type="text" maxlength="${NAME_MAX}" autocomplete="off" autocapitalize="words" enterkeyhint="done" placeholder="${esc(T('namePh'))}"><button type="submit" class="nsave">${esc(T('save'))}</button><button type="button" class="ncan" data-act="cancel" data-i="${i}">${esc(T('cancel'))}</button></div></form>`;
  else if (mode === 'mine') h = `<div class="nown"><span>${esc(T('yourName'))}: <b>${esc(rec.n)}</b></span><span class="nact"><button type="button" class="nlink" data-act="edit" data-i="${i}">${esc(T('edit'))}</button><button type="button" class="nlink" data-act="remove" data-i="${i}">${esc(T('remove'))}</button></span></div>`;
  if (msg) h += `<div class="nmsg${SS.busy[i] ? '' : ' err'}">${esc(msg)}</div>`;
  el.innerHTML = h;
  if (mode === 'edit') { const inp = $('nin' + i); if (inp) { inp.value = SS.draft[i] || (rec ? rec.n : ''); inp.focus(); inp.select(); } }
}

/* "your helmet is done": shown on the phone that added the name */
function renderDone() {
  const d = SS.doneInfo, el = $('donebanner');
  if (d && Date.now() - d.at > DONE_SHOW_MS) SS.doneInfo = null;
  const sig = SS.doneInfo ? [SS.doneInfo.box, SS.doneInfo.n, SS.lang].join('|') : '';
  if (sig === SS.doneSig) return;
  SS.doneSig = sig;
  if (!SS.doneInfo) { el.innerHTML = ''; return; }
  const t = T('doneT').replace('{n}', () => SS.doneInfo.n).replace('{b}', () => BOXES[SS.doneInfo.box].name);
  el.innerHTML = `<div class="done" role="status"><div class="dtx"><b>${esc(t)}</b><span>${esc(T('doneH'))}</span></div><button type="button" class="dok" data-act="dismiss">${esc(T('ok'))}</button></div>`;
}

/* ---- reading and writing the names (the only write this page makes) ---- */
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function blynkRaw(path, extra) {
  const ctl = new AbortController(), kill = setTimeout(() => ctl.abort(), 12000);
  try {
    const r = await fetch(`https://${cfg.server}/external/api/${path}?token=${encodeURIComponent(SS.token)}${extra ? '&' + extra : ''}`, { cache: 'no-store', signal: ctl.signal });
    const text = await r.text();
    if (!r.ok) {
      let msg = ''; try { const j = JSON.parse(text); msg = (j && j.error && j.error.message) || ''; } catch (e) {}
      throw Object.assign(new Error(msg || 'HTTP ' + r.status), { http: r.status });
    }
    return text;
  } finally { clearTimeout(kill); }
}
async function blynkPublic() {
  const text = await blynkRaw('getAll');
  let j; try { j = JSON.parse(text); } catch (e) { j = text; }
  if (j && j.error) throw new Error((j.error && j.error.message) || 'error');
  return j;
}
async function readNamesFresh() {
  if (DEMO) return parseNames(SS.demoNames);
  const all = await blynkPublic();
  return parseNames(all[PIN]);
}
async function writeNames(slots) {
  const json = JSON.stringify(slots.map(r => r ? [r.n, r.s, Math.floor(r.t)] : 0));
  if (DEMO) { SS.demoNames = json; return; }
  await blynkRaw('update', `${PIN}=${encodeURIComponent(json)}`);
}
/* the list as it should be written: every record whose cycle is over is dropped (when the data is good enough to tell) */
function keepLive(cur) { return cur.map((r, j) => { const c = classOf(j); return (r && c && nameEnded(r, c)) ? null : r; }); }
function nameFailed(i, e) {
  const m = String((e && e.message) || '');
  if (e && e.http === 400 && !/token/i.test(m)) { SS.namesOff = true; SS.msg[i] = T('namesOff'); }       // the names datastream does not exist on the server
  else SS.msg[i] = T('nameErr');
}

async function saveName(i, raw) {
  const n = cleanName(raw);
  if (!n) { SS.edit = -1; draw(); return; }
  if (SS.busy[i]) return;
  SS.busy[i] = true; SS.msg[i] = ''; SS.draft[i] = n; draw();                 // the typed name stays in the box if saving fails
  try {
    const c = classOf(i);
    if (!c || c.k !== 'busy' || !NAME_STEPS.has(c.step)) throw Object.assign(new Error('not now'), { soft: true });
    let ok = false;
    for (let attempt = 0; attempt < 2 && !ok; attempt++) {
      const cur = await readNamesFresh();
      if (cur[i] && nameLive(cur[i], c) && !isMine(i, cur[i])) { SS.msg[i] = T('nameTaken'); SS.names = cur; SS.edit = -1; SS.draft[i] = ''; return; }
      const rec = { n, s: c.sess >= 0 ? c.sess : -1, t: serverNow() };
      const next = keepLive(cur); next[i] = rec;
      await writeNames(next);
      if (!DEMO) { await sleep(700); const chk = await readNamesFresh(); ok = !!(chk[i] && chk[i].n === n); SS.names = chk; }
      else { ok = true; SS.names = parseNames(SS.demoNames); }
    }
    if (!ok) throw new Error('not saved');
    saveMine({ box: i, n, s: c.sess >= 0 ? c.sess : -1, t: Math.floor(serverNow()) });
    SS.edit = -1; SS.draft[i] = '';
  } catch (e) { nameFailed(i, e); }
  finally { SS.busy[i] = false; draw(); }
}

async function removeName(i) {
  if (SS.busy[i]) return;
  SS.busy[i] = true; SS.msg[i] = ''; draw();
  try {
    const cur = await readNamesFresh();
    if (cur[i] && isMine(i, cur[i])) { cur[i] = null; await writeNames(cur); }
    SS.names = cur; saveMine(null);
  } catch (e) { nameFailed(i, e); }
  finally { SS.busy[i] = false; draw(); }
}

/* Housekeeping after every refresh:
   1. this phone's own name: when its cycle is over, show "your helmet is done" and clear the name;
   2. any other name whose cycle is over (nobody was looking when it ended): the first page that sees it clears it. */
async function nameHousekeeping() {
  if (!NAMES_ON || SS.namesOff || SS.staff || SS.hk || dataBad()) return;
  let doClean = false;
  const m = SS.mine;
  if (m) {
    const c = classOf(m.box);
    if (c && nameEnded(m, c)) {
      if (serverNow() - m.t <= 90 * 60) {                  // a phone coming back much later does not get an old "done" banner
        SS.doneInfo = { box: m.box, n: m.n, at: Date.now() };
        try { if (navigator.vibrate) navigator.vibrate([200, 100, 200]); } catch (e) {}
      }
      saveMine(null); doClean = true;
      draw();
    } else if (serverNow() - m.t > MINE_KEEP_SEC) saveMine(null);
  }
  if (!doClean) doClean = SS.names.some((r, j) => { const c = classOf(j); return r && c && nameEnded(r, c); });
  if (!doClean || Date.now() - SS.lastClean < 20000) return;
  SS.lastClean = Date.now(); SS.hk = true;
  try {
    const cur = await readNamesFresh();
    const next = keepLive(cur);
    if (next.some((r, j) => r !== cur[j])) { await writeNames(next); SS.names = next; }
  } catch (e) { /* the next refresh tries again */ }
  SS.hk = false; draw();
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
      SS.names = parseNames(DEMO ? SS.demoNames : all[PIN]);
      if (DEMO && !SS.demoSeed && L && L.b[2] && NAME_STEPS.has(BOXES[2].st[L.b[2][0]])) {          // demo: somebody ("Jun") already added a name on Box 3
        SS.demoSeed = true; SS.demoNames = JSON.stringify([0, 0, ['Jun', L.b[2][3], Math.floor(L.ts)]]); SS.names = parseNames(SS.demoNames);
      }
    } catch (e) { SS.err = (e && e.message) || 'error'; }
    draw();
    nameHousekeeping().catch(() => { SS.hk = false; });
  }
  SS.timer = setTimeout(poll, Math.max(10, CONFIG.statusPollSec) * 1000);
}

$('lang').onclick = () => { SS.lang = SS.lang === 'en' ? 'tl' : 'en'; store.set('jh_lang', SS.lang); draw(); };
$('boxes').addEventListener('click', e => {
  const t = e.target.closest && e.target.closest('[data-act]'); if (!t) return;
  const i = Number(t.dataset.i), act = t.dataset.act;
  if (act === 'add' || act === 'edit') { SS.edit = i; SS.msg[i] = ''; draw(); }
  else if (act === 'cancel') { SS.edit = -1; SS.msg[i] = ''; SS.draft[i] = ''; draw(); }
  else if (act === 'remove') removeName(i);
});
$('boxes').addEventListener('submit', e => {
  e.preventDefault();
  const f = e.target.closest && e.target.closest('form'); if (!f) return;
  saveName(Number(f.dataset.i), (f.querySelector('input') || {}).value);
});
$('donebanner').addEventListener('click', e => { if (e.target.closest && e.target.closest('[data-act="dismiss"]')) { SS.doneInfo = null; renderDone(); } });
document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
SS.token = DEMO ? 'demo' : pickToken();
SS.mine = loadMine();
if (!DEMO && /#t=/.test(location.hash)) { /* keep the token in the address: it is the public one */ }
draw(); poll(); setInterval(draw, 1000);                   // draw() every second keeps "updated x ago" and the minutes left moving
/* light / dark: the same choice as the monitor page (button there), or  status.html?theme=light */
(function () { const q = new URLSearchParams(location.search).get('theme'), s = store.get('jh_theme'); const t = (q === 'light' || q === 'dark') ? q : s; if (t) document.documentElement.dataset.theme = t; })();
