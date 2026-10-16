"use strict";
/* Made-up data for  index.html?demo  and  index.html?demo=alerts.
   It has exactly the shape of a real Blynk answer ({ v0: "<json text>" }), so the same code path is used. */
const DEMO_T0 = Date.now();

function demoAll() {
  const cycle = demoParam === 'cycle';                                // ?demo=cycle: alerts come and go every 2 minutes (to hear the sounds)
  const alerts = demoParam === 'alerts';
  const t = (Date.now() - DEMO_T0) / 1000 + 25;                       // seconds since the page opened (the scenarios loop)
  const clamp = v => Math.max(0, Math.min(100, Math.round(v)));
  const seq = Math.floor(t / 5);                                      // "new data" every 5 s

  // Box 1 (step numbers of ST13): TAPS 10, RATE A 12, INSERT COIN 14, CLEANING 5, FINISH 8
  const p1 = t % 150; let b1;
  if (p1 < 20) b1 = [10, 0, 0, 0, 0]; else if (p1 < 30) b1 = [12, 0, 0, 0, 0]; else if (p1 < 40) b1 = [14, 0, Math.min(9, Math.floor((p1 - 30) / 1.2)), 0, 0];
  else if (p1 < 130) b1 = [5, Math.round(130 - p1), 9, 0, 7]; else b1 = [8, 0, 9, p1 < 136 ? 1 : 0, 0];
  const rl1 = b1[0] === 5 ? (1 << 3 | 1 << 4 | 1 << 5 | 1 << 6 | 1 << 7 | 1 << 11) : (b1[0] === 14 ? 1 << 11 : 0);

  // Box 2 (ST2): TAPS 8, INSERT COIN 12, HEATING 3, COOL DOWN 13, FINISH 6
  const p2 = (t + 40) % 120; let b2;
  if (p2 < 30) b2 = [8, 0, 0, 0, 0]; else if (p2 < 40) b2 = [12, 0, 4, 0, 0]; else if (p2 < 90) b2 = [3, Math.round(90 - p2), 6, 0, 8];
  else if (p2 < 110) b2 = [13, Math.round(110 - p2), 6, 0, 8]; else b2 = [6, 0, 6, 1, 0];
  const rl2 = b2[0] === 3 ? (1 << 3 | 1 << 4) : (b2[0] === 13 ? 1 << 5 : 0);

  // Box 3 (ST13): TAPS 10, SELECT MODE 29, DRYING 31, COOL DOWN 32, FINISH 8
  const p3 = (t + 70) % 200; let b3;
  if (p3 < 25) b3 = [10, 0, 0, 0, 0]; else if (p3 < 35) b3 = [29, 0, 0, 0, 0]; else if (p3 < 135) b3 = [31, Math.round(135 - p3), 8, 0, 6];
  else if (p3 < 165) b3 = [32, Math.round(165 - p3), 8, 0, 6]; else b3 = [8, 0, 8, 0, 0];
  const rl3 = b3[0] === 31 ? (1 << 12 | 1 << 4 | 1 << 7) : 0;

  const cc = ((Date.now() - DEMO_T0) / 1000 + 25) % 120;
  const cyOos = cycle && cc >= 20 && cc < 50, cyLow = cycle && cc >= 40 && cc < 75, cyEarly = cycle && cc >= 25 && cc < 40, cyTank = cycle && cc >= 60 && cc < 95, cyOff = cycle && cc >= 85 && cc < 110, cyPause = cycle && cc >= 100 && cc < 115;
  if (alerts || cyOos) b2 = [24, 0, 0, 0, 0];                       // Box 2 OUT OF ORDER
  if (cyPause) b1[0] = 9;                                           // Box 1 SAFETY PAUSE
  const offA2 = alerts || cyOff;
  const box1 = offA2 ? [b1[0], b1[1], b1[2], 0, -1, -1, -1, 0, 0]                                   // Box 1 sensor board (A2) offline: no doors, helmet or levels
                      : [...b1, clamp(72 + 10 * Math.sin(t / 220)), clamp(55 + 8 * Math.sin(t / 170 + 1)), 0, rl1];
  const L = {
    seq, ts: Math.floor(Date.now() / 1000), iv: 60,
    n: offA2 ? '1011111111111' : '1111111111111',
    b: [
      box1,
      [...b2, -1, -1, 0, rl2],
      [...b3, clamp((alerts || cyLow) ? 12 : cyEarly ? 26 : 64 + 6 * Math.sin(t / 190)), clamp(41 + 6 * Math.sin(t / 140)), 1, rl3]
    ],
    a: [
      [1, 0, 0, 0, 0, clamp(84 + 4 * Math.sin(t / 90)), clamp(66 + 5 * Math.sin(t / 110)), 0, 0],
      (alerts || cyTank) ? [1, 9, 0, 4, 0, 88, 12, 0, 240] : [1, (Math.floor(t / 20) % 5), 0, 0, 0, 60 + (t % 60) / 3, 70, 0, 0]
    ]
  };

  const n0 = 41 + Math.floor(t / 150), coins = n0 * 8 + 3, hist = [], dy = [];
  for (let i = 9; i >= 0; i--) hist.push([300 + ((n0 * 37 + i * 61) % 420), 15 * (1 + ((n0 + i * 3) % 4))]);
  const perDay = [22, 31, 18, 40, 27, 35, 12, 44, 29, 38, 25, 33, 41, 20];       // coins per day (oldest first); the last one is "today"
  perDay.forEach((c, i) => dy.push([c + (i === 13 ? Math.floor(t / 150) * 8 : 0), Math.round(c / 8)]));
  const hr = [0, 0, 0, 0, 0, 1, 3, 6, 9, 12, 14, 11, 8, 10, 13, 15, 18, 22, 25, 19, 11, 6, 2, 1].map(v => v + Math.floor(n0 / 20)), wd = [18, 12, 15, 17, 21, 34, 41];
  const bx = [[Math.round(n0 * 0.46), Math.round(n0 * 0.46) * 15], [Math.round(n0 * 0.2), Math.round(n0 * 0.2) * 10], [Math.round(n0 * 0.34), Math.round(n0 * 0.34) * 15]];
  const rs = [2, Math.floor(Date.now() / 1000) - 3 * 86400, 20, 200, 83, coins - 83, 100, 1];          // corrected old data (made up): [update, captured at, customers, coins counted, restated coins, coins since, cv x100, mode]
  L.st = { cv: 1, ss: n0, cn: coins, rv: coins, ad: 412, rs, h: hist, dy, sn: Math.floor(Date.now() / 1000) - 9 * 86400, bx, hr, wd,
    sys: { up: Math.floor(t + 18000), rst: 'power-on', boots: 3, rssi: -58, heap: 162000, mq: 31800 + Math.floor(t % 1000), mqmax: 100000 } };
  return { v0: JSON.stringify(L) };
}

/* The same boxes as the PUBLIC status device would carry them: { seq, ts, iv, b:[[step, seconds left, heard, sessions] x3] } (see DATA_CONTRACT.md, section 5).
   "sessions" is each box's completed-sessions counter: it goes up when the cycle ends (Box 1: end of the cleaning, Box 2: end of the heating, Box 3: end of the drying),
   which is when a customer's optional name disappears. (The names themselves live in SS.demoNames, see status.js.) */
function demoPublic() {
  const L = JSON.parse(demoAll().v0), t = (Date.now() - DEMO_T0) / 1000 + 25;
  const sess = [50 + Math.floor((t + 20) / 150), 20 + Math.floor((t + 70) / 120), 70 + Math.floor((t + 135) / 200)];
  const b = L.b.map((x, i) => { const heard = L.n[BOXES[i].brain] === '1' ? 1 : 0; return [heard ? x[0] : 0, heard ? x[1] : 0, heard, heard ? sess[i] : 0]; });
  return { v0: JSON.stringify({ seq: L.seq, ts: L.ts, iv: L.iv, b }) };
}
