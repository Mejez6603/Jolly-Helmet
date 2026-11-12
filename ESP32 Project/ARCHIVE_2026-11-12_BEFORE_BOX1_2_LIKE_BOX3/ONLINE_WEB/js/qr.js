"use strict";
/* A small QR code maker (no library, no internet needed): byte mode, error correction level M, versions 1 to 10 (up to 213 characters).
   QR.svg("https://...") returns an <svg> string; QR.matrix("...") the dark/light modules. Written from the QR code standard (ISO 18004). */
const QR = (() => {
  const RAW = [26, 44, 70, 100, 134, 172, 196, 242, 292, 346];           // all codewords of version 1..10
  const ECC = [10, 16, 26, 18, 24, 16, 18, 22, 22, 26];                  // error correction codewords per block, level M
  const NBLK = [1, 1, 1, 2, 2, 4, 4, 4, 5, 5];                           // blocks, level M
  const mul = (x, y) => { let z = 0; for (let i = 7; i >= 0; i--) { z = (z << 1) ^ ((z >>> 7) * 0x11D); z ^= ((y >>> i) & 1) * x; } return z & 255; };
  function rsDivisor(deg) {
    const r = []; for (let i = 0; i < deg - 1; i++) r.push(0); r.push(1);
    let root = 1;
    for (let i = 0; i < deg; i++) { for (let j = 0; j < r.length; j++) { r[j] = mul(r[j], root); if (j + 1 < r.length) r[j] ^= r[j + 1]; } root = mul(root, 2); }
    return r;
  }
  function rsRemainder(data, div) {
    const r = div.map(() => 0);
    data.forEach(b => { const f = b ^ r.shift(); r.push(0); div.forEach((c, i) => { r[i] ^= mul(c, f); }); });
    return r;
  }
  const utf8 = s => Array.from(new TextEncoder().encode(s));

  function make(text) {
    const bytes = utf8(text);
    let ver = 0;
    for (let v = 1; v <= 10; v++) { const cap = RAW[v - 1] - NBLK[v - 1] * ECC[v - 1]; if (bytes.length + (v < 10 ? 2 : 3) <= cap) { ver = v; break; } }
    if (!ver) throw new Error('text too long for the QR code (max 213 characters)');
    const dataCw = RAW[ver - 1] - NBLK[ver - 1] * ECC[ver - 1], size = 17 + 4 * ver;
    // 1. bit stream: mode 0100, length, bytes, terminator, padding
    const bits = []; const put = (v, n) => { for (let i = n - 1; i >= 0; i--) bits.push((v >>> i) & 1); };
    put(4, 4); put(bytes.length, ver < 10 ? 8 : 16); bytes.forEach(b => put(b, 8));
    put(0, Math.min(4, dataCw * 8 - bits.length)); while (bits.length % 8) bits.push(0);
    for (let pad = 0xEC; bits.length < dataCw * 8; pad ^= 0xEC ^ 0x11) put(pad, 8);
    const data = []; for (let i = 0; i < bits.length; i += 8) data.push(parseInt(bits.slice(i, i + 8).join(''), 2));
    // 2. error correction, blocks interleaved
    const nb = NBLK[ver - 1], ecl = ECC[ver - 1], raw = RAW[ver - 1], nShort = nb - raw % nb, shortLen = Math.floor(raw / nb), div = rsDivisor(ecl), blocks = [];
    for (let i = 0, k = 0; i < nb; i++) {
      const dat = data.slice(k, k + shortLen - ecl + (i < nShort ? 0 : 1)); k += dat.length;
      const ecc = rsRemainder(dat, div); if (i < nShort) dat.push(0); blocks.push(dat.concat(ecc));
    }
    const all = [];
    for (let i = 0; i < blocks[0].length; i++) blocks.forEach((b, j) => { if (i !== shortLen - ecl || j >= nShort) all.push(b[i]); });
    // 3. the pattern
    const mod = Array.from({ length: size }, () => new Array(size).fill(false)), fn = Array.from({ length: size }, () => new Array(size).fill(false));
    const setF = (x, y, d) => { mod[y][x] = d; fn[y][x] = true; };
    for (let i = 0; i < size; i++) { setF(6, i, i % 2 === 0); setF(i, 6, i % 2 === 0); }
    const finder = (cx, cy) => { for (let dy = -4; dy <= 4; dy++) for (let dx = -4; dx <= 4; dx++) { const d = Math.max(Math.abs(dx), Math.abs(dy)), x = cx + dx, y = cy + dy; if (x >= 0 && x < size && y >= 0 && y < size) setF(x, y, d !== 2 && d !== 4); } };
    finder(3, 3); finder(size - 4, 3); finder(3, size - 4);
    if (ver > 1) {
      const na = Math.floor(ver / 7) + 2, step = Math.ceil((ver * 4 + 4) / (na * 2 - 2)) * 2, pos = [6];
      for (let p = size - 7; pos.length < na; p -= step) pos.splice(1, 0, p);
      pos.forEach((px, i) => pos.forEach((py, j) => { if ((i === 0 && j === 0) || (i === 0 && j === na - 1) || (i === na - 1 && j === 0)) return; for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) setF(px + dx, py + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1); }));
    }
    const fmt = mask => {                                                  // format information (level M = 00)
      const d = mask; let rem = d; for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
      const b = ((d << 10) | rem) ^ 0x5412, g = i => ((b >>> i) & 1) === 1;
      for (let i = 0; i <= 5; i++) setF(8, i, g(i)); setF(8, 7, g(6)); setF(8, 8, g(7)); setF(7, 8, g(8));
      for (let i = 9; i < 15; i++) setF(14 - i, 8, g(i));
      for (let i = 0; i < 8; i++) setF(size - 1 - i, 8, g(i));
      for (let i = 8; i < 15; i++) setF(8, size - 15 + i, g(i));
      setF(8, size - 8, true);
    };
    fmt(0);
    if (ver >= 7) { let rem = ver; for (let i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >>> 11) * 0x1F25); const b = (ver << 12) | rem; for (let i = 0; i < 18; i++) { const c = ((b >>> i) & 1) === 1, a = size - 11 + i % 3, k = Math.floor(i / 3); setF(a, k, c); setF(k, a, c); } }
    // 4. the data, zig-zag from the bottom right
    let bi = 0;
    for (let right = size - 1; right >= 1; right -= 2) {
      if (right === 6) right = 5;
      for (let vert = 0; vert < size; vert++) for (let j = 0; j < 2; j++) {
        const x = right - j, up = ((right + 1) & 2) === 0, y = up ? size - 1 - vert : vert;
        if (!fn[y][x] && bi < all.length * 8) { mod[y][x] = ((all[bi >>> 3] >>> (7 - (bi & 7))) & 1) === 1; bi++; }
      }
    }
    // 5. the best of the 8 masks (lowest penalty)
    const maskFn = [(x, y) => (x + y) % 2 === 0, (x, y) => y % 2 === 0, (x, y) => x % 3 === 0, (x, y) => (x + y) % 3 === 0, (x, y) => (Math.floor(x / 3) + Math.floor(y / 2)) % 2 === 0,
      (x, y) => x * y % 2 + x * y % 3 === 0, (x, y) => (x * y % 2 + x * y % 3) % 2 === 0, (x, y) => ((x + y) % 2 + x * y % 3) % 2 === 0];
    const apply = m => { for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) if (!fn[y][x] && maskFn[m](x, y)) mod[y][x] = !mod[y][x]; };
    const penalty = () => {
      let p = 0;
      for (let pass = 0; pass < 2; pass++) for (let a = 0; a < size; a++) {
        let run = 1, hist = '';
        for (let b = 1; b <= size; b++) {
          const cur = b < size ? (pass ? mod[b][a] : mod[a][b]) : null, prev = pass ? mod[b - 1][a] : mod[a][b - 1];
          if (cur === prev) run++; else { if (run >= 5) p += 3 + run - 5; run = 1; }
        }
        for (let b = 0; b < size; b++) hist += (pass ? mod[b][a] : mod[a][b]) ? '1' : '0';
        for (const pat of ['10111010000', '00001011101']) { let i = -1; while ((i = hist.indexOf(pat, i + 1)) >= 0) p += 40; }
      }
      for (let y = 0; y < size - 1; y++) for (let x = 0; x < size - 1; x++) { const c = mod[y][x]; if (c === mod[y][x + 1] && c === mod[y + 1][x] && c === mod[y + 1][x + 1]) p += 3; }
      let dark = 0; mod.forEach(r => r.forEach(c => { if (c) dark++; })); p += (Math.ceil(Math.abs(dark * 20 - size * size * 10) / (size * size)) - 1) * 10;
      return p;
    };
    let best = 0, bestP = Infinity;
    for (let m = 0; m < 8; m++) { apply(m); fmt(m); const p = penalty(); if (p < bestP) { bestP = p; best = m; } apply(m); }
    apply(best); fmt(best);
    return { size, ver, mask: best, modules: mod };
  }
  /* the picture: dark modules as one path, 4 modules of white margin (the standard asks for it) */
  function svg(text, px, label) {
    const q = make(text), m = 4, n = q.size + 2 * m; let d = '';
    for (let y = 0; y < q.size; y++) for (let x = 0; x < q.size; x++) if (q.modules[y][x]) d += `M${x + m} ${y + m}h1v1h-1z`;
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${n} ${n}" width="${px || 220}" height="${px || 220}" shape-rendering="crispEdges" role="img" aria-label="${(label || 'QR code').replace(/"/g, '')}"><rect width="${n}" height="${n}" fill="#fff"/><path d="${d}" fill="#000"/></svg>`;
  }
  return { make, svg, matrix: text => make(text).modules };
})();
