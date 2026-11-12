/* Jolly Helmet - tiny data flow diagram engine (black and white, Gane-Sarson / Yourdon style like DFD_LEVEL1).
   One page = 1980 x 1340 px.  Nodes are placed by hand, arrows are routed from a port of one node to a port of another.
   Port spec  'id.side' or 'id.side:k'   side = l r t b ; k = y (for l / r) or x (for t / b), default = the middle of that side.
   Route: opposite sides = Z shape (option m = the x or y of the middle run), same side = U shape (m), l/r with t/b = L shape,
          or via:[[x,y],...] = explicit corner points.   Horizontal runs hop over vertical arrows they cross. */
(function () {
  const NS = 'http://www.w3.org/2000/svg';
  let pageNo = 0;

  window.makeDFD = function (host, meta) {
    pageNo++;
    const mid = 'ah' + pageNo;
    const page = document.createElement('section');
    page.className = 'pg'; host.appendChild(page);
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('width', 1980); svg.setAttribute('height', 1340); svg.setAttribute('viewBox', '0 0 1980 1340');
    svg.innerHTML = `<defs><marker id="${mid}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7.5" markerHeight="7.5" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#000"/></marker></defs>`;
    page.appendChild(svg);
    const N = {}, flows = [];

    const el = (cls, x, y, w, h, html) => {
      const d = document.createElement('div'); d.className = cls;
      d.style.cssText = `left:${x}px;top:${y}px;` + (w ? `width:${w}px;` : '') + (h ? `height:${h}px;` : '');
      d.innerHTML = html; page.appendChild(d); return d;
    };

    const api = {
      ent(k, x, y, w, h, t, s) { N[k] = { x, y, w, h }; el('ent', x, y, w, h, `<b>${t}</b>${s ? `<span>${s}</span>` : ''}`); },
      proc(k, x, y, w, h, n, t, s, o) { N[k] = { x, y, w, h }; el('proc' + (o && o.out ? ' out' : ''), x, y, w, h, `<div class="num">${n}</div><div class="body"><b>${t}</b>${s ? `<span>${s}</span>` : ''}</div>`); },
      store(k, x, y, w, h, id, t, s) { N[k] = { x, y, w, h }; el('store', x, y, w, h, `<div class="id">${id}</div><div class="nm"><b>${t}</b>${s ? `<span>${s}</span>` : ''}</div>`); },
      text(x, y, html, cls) { el(cls || 'free', x, y, 0, 0, html); },
      lbl(x, y, html, a) {
        const d = el('lbl', x, y, 0, 0, html);
        const tr = { bc: '-50%,-100%', tc: '-50%,0', lm: '0,-50%', rm: '-100%,-50%', cc: '-50%,-50%' }[a || 'bc'];
        d.style.transform = `translate(${tr})`;
        d.style.textAlign = (a === 'lm') ? 'left' : (a === 'rm') ? 'right' : 'center';
        return d;
      },
      flow(from, to, label, o) {
        if (label && typeof label === 'object') { o = label; label = o.l; }
        o = o || {};
        const A = port(from), B = port(to);
        let pts = [[A.x, A.y]];
        const hA = A.side === 'l' || A.side === 'r', hB = B.side === 'l' || B.side === 'r';
        if (o.via) { pts = pts.concat(o.via); }
        else if (hA && hB) {
          if (A.side !== B.side) { const mx = o.m !== undefined ? o.m : (A.x + B.x) / 2; if (A.y !== B.y) pts.push([mx, A.y], [mx, B.y]); }
          else { const mx = o.m !== undefined ? o.m : (A.side === 'r' ? Math.max(A.x, B.x) + 40 : Math.min(A.x, B.x) - 40); pts.push([mx, A.y], [mx, B.y]); }
        } else if (!hA && !hB) {
          if (A.side !== B.side) { const my = o.m !== undefined ? o.m : (A.y + B.y) / 2; if (A.x !== B.x) pts.push([A.x, my], [B.x, my]); }
          else { const my = o.m !== undefined ? o.m : (A.side === 'b' ? Math.max(A.y, B.y) + 40 : Math.min(A.y, B.y) - 40); pts.push([A.x, my], [B.x, my]); }
        } else if (hA) pts.push([B.x, A.y]); else pts.push([A.x, B.y]);
        pts.push([B.x, B.y]);
        flows.push({ pts, label, o });
      },
      /* a long arrow with no label of its own that shares the end of another (a merge): same as flow() */
      finish() {
        /* vertical segments for the hops */
        const V = [];
        flows.forEach((f, fi) => { for (let i = 1; i < f.pts.length; i++) { const [x0, y0] = f.pts[i - 1], [x1, y1] = f.pts[i]; if (x0 === x1 && y0 !== y1) V.push({ x: x0, y1: Math.min(y0, y1), y2: Math.max(y0, y1), f: fi }); } });
        flows.forEach((f, fi) => {
          let d = 'M' + f.pts[0].join(',');
          for (let i = 1; i < f.pts.length; i++) {
            const [x0, y0] = f.pts[i - 1], [x1, y1] = f.pts[i];
            if (y0 === y1 && x0 !== x1) {
              const dir = x1 > x0 ? 1 : -1;
              const hs = V.filter(v => v.f !== fi && v.x > Math.min(x0, x1) + 14 && v.x < Math.max(x0, x1) - 14 && y0 > v.y1 + 2 && y0 < v.y2 - 2).map(v => v.x).sort((a, b) => dir * (a - b));
              const seen = [];
              hs.forEach(h => { if (seen.some(s => Math.abs(s - h) < 20)) return; seen.push(h); d += ` L${h - 9 * dir},${y0} a9,9 0 0 ${dir > 0 ? 1 : 0} ${18 * dir},0`; });
            }
            d += ` L${x1},${y1}`;
          }
          const p = document.createElementNS(NS, 'path');
          p.setAttribute('d', d); p.setAttribute('fill', 'none'); p.setAttribute('stroke', '#000'); p.setAttribute('stroke-width', '2'); p.setAttribute('stroke-linejoin', 'round');
          p.setAttribute('marker-end', `url(#${mid})`); svg.appendChild(p);
        });
        flows.forEach(f => { if (f.label) placeLabel(f); });
      }
    };

    function port(spec) {
      const m = /^([^.]+)\.([lrtb])(?::(-?\d+(?:\.\d+)?))?$/.exec(spec);
      if (!m) throw new Error('bad port ' + spec);
      const n = N[m[1]]; if (!n) throw new Error('no node ' + m[1]);
      const side = m[2], k = m[3] !== undefined ? +m[3] : null;
      if (side === 'l') return { x: n.x, y: k !== null ? k : n.y + n.h / 2, side };
      if (side === 'r') return { x: n.x + n.w, y: k !== null ? k : n.y + n.h / 2, side };
      if (side === 't') return { x: k !== null ? k : n.x + n.w / 2, y: n.y, side };
      return { x: k !== null ? k : n.x + n.w / 2, y: n.y + n.h, side };
    }

    function placeLabel(f) {
      const o = f.o;
      if (o.lp) { api.lbl(o.lp[0], o.lp[1], f.label, o.lp[2] || 'bc'); return; }
      let bi = 0, bl = -1;
      for (let i = 1; i < f.pts.length; i++) {
        const L = Math.abs(f.pts[i][0] - f.pts[i - 1][0]) + Math.abs(f.pts[i][1] - f.pts[i - 1][1]);
        if (o.ls !== undefined ? (i - 1 === o.ls) : (L > bl)) { bl = L; bi = i; if (o.ls !== undefined) break; }
      }
      const [x0, y0] = f.pts[bi - 1], [x1, y1] = f.pts[bi];
      const fr = o.lo !== undefined ? o.lo : .5;
      const px = x0 + (x1 - x0) * fr + (o.dx || 0), py = y0 + (y1 - y0) * fr + (o.dy || 0);
      if (y0 === y1) api.lbl(px, o.below ? py + 6 : py - 5, f.label, o.below ? 'tc' : 'bc');
      else api.lbl(o.left ? px - 9 : px + 9, py, f.label, o.left ? 'rm' : 'lm');
    }

    /* title block, legend, note */
    el('ttl', 50, 36, 0, 0, `<b>JOLLY HELMET</b><div>${meta.title}</div><div class="sub">${meta.sub}</div>`);
    el('leg', 50, 1252, 0, 0, `
      <span><i style="width:30px;height:18px;border:2.5px solid #000;box-shadow:3px 3px 0 #000"></i>External entity</span>
      <span><i style="width:34px;height:20px;border:2.5px solid #000;border-radius:8px"></i>Process of this level</span>
      <span><i style="width:34px;height:20px;border:2.5px dashed #000;border-radius:8px"></i>Other process (from Level 1 or 2)</span>
      <span><i style="width:44px;height:18px;border-top:2.5px solid #000;border-bottom:2.5px solid #000;border-left:2.5px solid #000"></i>Data store</span>
      <span><i style="width:36px;height:0;border-top:2px solid #000"></i>&#9654;&nbsp;Data flow</span>`);
    if (meta.note) el('note', 50, 1292, 0, 0, meta.note);
    return api;
  };
})();
