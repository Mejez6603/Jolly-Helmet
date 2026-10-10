"use strict";
/* Reading Blynk. The page only ever READS (HTTPS GET on blynk.cloud's external API); it never writes a value.

   Datastream V0 holds one JSON text written by the Online Server - see DATA_CONTRACT.md:
     { seq, ts, iv, n, b:[3 boxes], a:[2 refill stations], st:{ analytics + health } }
   (A separate datastream V1 holding just the "st" part is also understood.)                                   */

/* what the page knows right now */
const S = {
  live: null,          // the parsed V0 object
  stats: null,         // the "st" part (or the parsed V1)
  liveRaw: '', statsRaw: '',
  lastSeq: null, lastChange: 0,    // when the "seq" counter last changed (browser clock)
  noData: false,       // Blynk answered, but V0 is empty
  err: null,           // text of the last error, or null
  okAt: 0,             // when Blynk last answered correctly
  hist: {}             // short in-page history for the sparklines
};

async function blynkGet(path) {
  const url = `https://${cfg.server}/external/api/${path}`;
  const ctl = new AbortController(), kill = setTimeout(() => ctl.abort(), 12000);
  let r;
  try { r = await fetch(url, { cache: 'no-store', signal: ctl.signal }); } finally { clearTimeout(kill); }
  const text = await r.text();
  let j; try { j = JSON.parse(text); } catch (e) { j = text; }
  if (!r.ok || (j && j.error)) throw new Error((j && j.error && j.error.message) || ('Blynk answered HTTP ' + r.status));
  return j;
}
const parseMaybe = v => { if (v == null) return null; if (typeof v === 'object') return v; try { return JSON.parse(v); } catch (e) { return null; } };

/* One round trip: getAll returns every datastream that has a value, e.g. { "v0": "{...json...}" } */
async function pollOnce() {
  const all = DEMO ? demoAll() : await blynkGet('getAll?token=' + encodeURIComponent(cfg.token));
  const rawL = all.v0 == null ? '' : (typeof all.v0 === 'string' ? all.v0 : JSON.stringify(all.v0));
  const rawS = all.v1 == null ? '' : (typeof all.v1 === 'string' ? all.v1 : JSON.stringify(all.v1));
  S.okAt = Date.now(); S.err = null;
  if (!rawL) { S.live = null; S.noData = true; return; }
  S.noData = false;
  if (rawL !== S.liveRaw) {
    const L = parseMaybe(rawL);
    if (L) {
      S.live = L; S.liveRaw = rawL;
      if (L.st) S.stats = L.st;
      if (L.seq !== S.lastSeq || S.lastSeq === null) { S.lastSeq = L.seq; S.lastChange = Date.now(); pushHistory(L); }
    }
  }
  if (!(S.live && S.live.st) && rawS !== S.statsRaw) { const T = parseMaybe(rawS); if (T) { S.stats = T; S.statsRaw = rawS; } }
}

/* keeps the last ~90 container levels for the little graphs (only while the page stays open) */
function pushHistory(L) {
  const add = (k, v) => { if (typeof v !== 'number' || v < 0) return; const a = S.hist[k] || (S.hist[k] = []); a.push(v); if (a.length > 90) a.shift(); };
  (L.b || []).forEach((b, i) => { add('b' + i + 'a', b && b[5]); add('b' + i + 's', b && b[6]); });
  (L.a || []).forEach((a, i) => { add('a' + i + 'a', a && a[5]); add('a' + i + 's', a && a[6]); });
}
