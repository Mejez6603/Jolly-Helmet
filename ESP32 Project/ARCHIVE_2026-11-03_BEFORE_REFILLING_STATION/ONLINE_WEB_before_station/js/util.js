"use strict";
/* Small helpers used by every other script. */

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;' }[c]));
const nz = (v, d) => (typeof v === 'number' && isFinite(v)) ? v : d;        // number or default

const mmss = s => { s = Math.max(0, Math.round(s)); return String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0'); };
const money = v => CONFIG.coin + Number(v).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
const chip = (t, c) => `<span class="chip ${c || ''}">${esc(t)}</span>`;

function fmtUp(s) {                                    // 93784 -> "1d 2h"
  s = Math.round(s);
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m ${s % 60}s`;
}
function ago(sec) {                                    // seconds -> "12 s", "5 min", "2.5 h", "3 d"
  if (sec < 60) return Math.round(sec) + ' s';
  if (sec < 3600) return Math.round(sec / 60) + ' min';
  if (sec < 86400) return (sec / 3600).toFixed(1) + ' h';
  return Math.round(sec / 86400) + ' d';
}

/* localStorage that never throws (private windows, blocked storage) */
const store = {
  get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch (e) {} },
  del(k) { try { localStorage.removeItem(k); } catch (e) {} }
};
