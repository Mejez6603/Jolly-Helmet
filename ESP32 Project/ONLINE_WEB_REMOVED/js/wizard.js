"use strict";
/* "Fix it": the 7 troubleshooting decision trees of the manual as a question-and-answer guide.
   The trees themselves are in js/wizard_data.js (made from the same drawing that is printed in the manual, so the web page and the manual
   always say the same). The guide asks one yes / no question at a time and ends with what to do. If something is wrong right now (Alerts), the
   matching guide is suggested at the top. */
const Wizard = (() => {
  const REF = { 'a board shows offline': 'tree2', 'refill station does not refill': 'tree5' };       // quoted names inside the advice that point to another guide
  const st = { tree: null, node: null, path: [], finished: false, origin: null };
  const T = id => WIZARD_TREES.find(t => t.id === id);
  const out = (t, n) => t.edges.filter(e => e.a === n);

  function open(id, quiet) {
    if (!T(id)) return;
    st.tree = id; st.path = []; st.finished = false; st.origin = null;
    const t = T(id); st.node = (out(t, 'S')[0] || {}).b || 'S';
    if (!quiet && location.hash.split('/')[0] !== '#fix') location.hash = '#fix'; draw();
  }
  const reset = () => { st.tree = null; st.node = null; st.path = []; st.finished = false; draw(); };

  const cap = s => s ? s.charAt(0).toUpperCase() + s.slice(1) : s;
  function advice(text) {                                      // plain text -> html with links to the other guides
    let t = text.replace(/^(Yes|No):\s*/i, '');
    return esc(t).replace(/&quot;([^&]+)&quot;/g, (m, q) => REF[q.toLowerCase()] ? `<button type="button" class="lnk" data-wz="open" data-tree="${REF[q.toLowerCase()]}">&ldquo;${q}&rdquo;</button>` : m);
  }

  function nextCheck() {                                       // after advice that did not help: the question's other answer
    for (let i = st.path.length - 1; i >= 0; i--) {
      const p = st.path[i], t = T(st.tree);
      if (t.nodes[p.node].k !== 'dec') continue;
      const others = out(t, p.node).filter(e => e.l !== p.label);
      if (others.length) return others[0].b;
    }
    return null;
  }

  function summary() {
    const t = T(st.tree);
    let s = `Jolly Helmet - fix-it guide\r\nProblem: ${t.title}\r\n`;
    st.path.forEach((p, i) => { const n = t.nodes[p.node]; s += `${i + 1}. ${n.t.replace(/^(Yes|No):\s*/i, '')}${p.label ? ' -> ' + p.label : ''}\r\n`; });
    const cur = t.nodes[st.node];
    s += st.finished ? 'Result: it works now.\r\n' : `Now at: ${cur ? cur.t : ''}\r\n`;
    return s;
  }

  function draw() {
    const el = $('wizBody'); if (!el) return;
    if (!st.tree) {                                            // the list of problems
      const sug = [...new Set((typeof Alerts !== 'undefined' ? Alerts.counts().all : []).map(a => AL_TYPES[a.type].fix).filter(Boolean))];
      let h = '';
      if (sug.length) h += `<div class="card" style="margin-bottom:14px;border-left:5px solid var(--amber)"><b>Suggested for what is happening now</b><div class="wzlist" style="margin-top:8px">` + sug.map(id => `<button type="button" class="wzbtn" data-wz="open" data-tree="${id}">${esc(T(id).title)}</button>`).join('') + `</div></div>`;
      h += `<div class="card"><b>What is the problem?</b><div class="wzlist" style="margin-top:8px">` + WIZARD_TREES.map((t, i) => `<button type="button" class="wzbtn" data-wz="open" data-tree="${t.id}"><span class="wzno">${i + 1}</span>${esc(t.title)}</button>`).join('') + `</div></div>` +
        `<p class="fine">These are the same decision trees as in the manual (Reference, Troubleshooting decision trees). Always switch the mains off before reaching into a box.</p>`;
      el.innerHTML = h; return;
    }
    const t = T(st.tree), n = t.nodes[st.node] || { k: 'end', t: '' };
    let h = `<div class="wzhead"><button type="button" class="btn alt" data-wz="reset" style="margin:0">&larr; All problems</button><b>${esc(t.title)}</b></div>`;
    if (st.path.length) h += `<div class="wzpath">` + st.path.map(p => { const x = t.nodes[p.node]; return `<span class="chip ${p.label ? '' : 'use'}">${esc(x.t.replace(/^(Yes|No):\s*/i, '').slice(0, 46))}${p.label ? ': <b>' + esc(cap(p.label)) + '</b>' : ''}</span>`; }).join('') + `</div>`;
    if (st.finished) {
      h += `<div class="card wzdone"><div class="big" style="font-size:26px;color:var(--ok)">&#10003; Fixed</div><p>Good. If it comes back, run the guide again, and write down what happened (Checklist, Alerts log).</p><button class="btn" data-wz="reset" type="button">Back to the list</button> <button class="btn alt" data-wz="copy" type="button">Copy a summary</button></div>`;
    } else if (n.k === 'dec') {
      h += `<div class="card wzq"><div class="wzlab">QUESTION ${st.path.filter(p => t.nodes[p.node].k === 'dec').length + 1}</div><div class="wztxt">${esc(n.t)}</div><div class="wzans">` +
        out(t, st.node).map(e => `<button type="button" class="wzans-b ${/^(yes|ok)$/i.test(e.l) ? 'yes' : /^no$/i.test(e.l) ? 'no' : ''}" data-wz="ans" data-to="${e.b}" data-label="${esc(e.l)}">${esc(cap(e.l) || 'Next')}</button>`).join('') + `</div></div>`;
    } else if (n.k === 'act') {
      const nx = nextCheck(), onward = out(t, st.node)[0];
      h += `<div class="card wzact"><div class="wzlab">WHAT TO DO</div><div class="wztxt">${advice(n.t)}</div><div class="wzans">` +
        `<button type="button" class="wzans-b yes" data-wz="fixed">It works now</button>` +
        (onward ? `<button type="button" class="wzans-b" data-wz="ans" data-to="${onward.b}" data-label="">Still not fixed</button>` : nx ? `<button type="button" class="wzans-b" data-wz="skip" data-to="${nx}">Still not fixed: next check</button>` : `<button type="button" class="wzans-b" data-wz="stuck">Still not fixed</button>`) + `</div></div>`;
    } else {                                                    // an end box (or "still stuck")
      h += `<div class="card wzact"><div class="wzlab">NEXT STEP</div><div class="wztxt">${esc(n.t || 'Ask a technician.')}</div><div class="wzans"><button class="btn" type="button" data-wz="copy" style="margin:0">Copy a summary for the technician</button><button type="button" class="wzans-b" data-wz="reset">Back to the list</button></div></div>`;
    }
    if (st.path.length && !st.finished) h += `<p><button type="button" class="lnk" data-wz="back">&larr; one step back</button></p>`;
    el.innerHTML = h;
  }

  document.addEventListener('click', e => {
    const b = e.target.closest('[data-wz]'); if (!b) return;
    const k = b.dataset.wz, t = st.tree && T(st.tree);
    if (k === 'open') open(b.dataset.tree);
    else if (k === 'reset') reset();
    else if (k === 'ans') { st.path.push({ node: st.node, label: b.dataset.label }); st.node = b.dataset.to; draw(); }
    else if (k === 'skip') { st.path.push({ node: st.node, label: '' }); st.node = b.dataset.to; draw(); }
    else if (k === 'stuck') { st.path.push({ node: st.node, label: '' }); const end = Object.keys(t.nodes).find(x => t.nodes[x].k === 'end'); st.node = end || 'E'; if (!t.nodes[st.node]) t.nodes[st.node] = { k: 'end', t: 'Ask a technician and send the summary of this guide.' }; draw(); }
    else if (k === 'fixed') { st.path.push({ node: st.node, label: '' }); st.finished = true; draw(); }
    else if (k === 'back') { const p = st.path.pop(); if (p) { st.node = p.node; st.finished = false; } draw(); }
    else if (k === 'copy') {
      const s = summary();
      (navigator.clipboard ? navigator.clipboard.writeText(s) : Promise.reject()).then(() => { b.textContent = 'Copied'; }).catch(() => { prompt('Copy this:', s); });
    }
  });
  return { open, draw };
})();
