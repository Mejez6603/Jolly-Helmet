"use strict";
/* The Manual tab (index.html#manual).
   PUBLIC part : manual/customer_guide.pdf + manual/customer_index.js (chapter list and the text of each page, for the search box).
   TEAM part   : manual/team_manual.js holds the WHOLE manual (admin, technician, reference) encrypted with the team password
                 (AES-256-CBC + HMAC-SHA256, key from PBKDF2-SHA256). It is only fetched and decrypted here, in the browser, after the
                 password is typed. Without the password the file is unreadable, so it is safe on a public web site.
   Both are made from the Word file by MANUAL\build_web_manual.ps1 (run it again whenever the Word file changes).
   Page numbers: "abs" = the page of the full PDF (page 1 = the cover). The printed page number is abs - 1 (the cover has none). */
(function () {
  const PUB = window.JH_MANUAL_PUBLIC || null;
  const st = { unlocked: false, toc: [], pages: [], first: PUB ? PUB.first : 1, url: '', blob: '', cur: 0 };
  // the browser's own PDF viewer inside the page (desktop Chrome, Edge, Firefox); phones open the PDF in their own viewer instead
  const inline = navigator.pdfViewerEnabled === true || (navigator.pdfViewerEnabled === undefined && !/Android|iPhone|iPad|Mobile/i.test(navigator.userAgent));

  /* ---------------- data ---------------- */
  function useCustomer() {
    st.unlocked = false; st.first = PUB ? PUB.first : 1;
    st.toc = PUB ? PUB.toc : [];
    st.pages = PUB ? PUB.pages.map((t, i) => ({ p: PUB.first + i, t })) : [];
    st.url = 'manual/customer_guide.pdf';
  }
  const printed = p => Math.max(1, p - 1);
  const local = p => st.unlocked ? p : Math.max(1, p - st.first + 1);        // page inside the PDF that is open

  /* ---------------- viewer ---------------- */
  function show(abs, btn) {
    st.cur = abs;
    const lp = local(abs), url = (st.unlocked ? st.blob : st.url) + '#page=' + lp;
    $('mTab').href = url;
    const box = $('mFrame'); box.textContent = '';
    if (!(PUB || st.unlocked)) { box.innerHTML = '<p class="empty">The manual files were not found next to this page (folder <b>manual</b>). Run MANUAL\\build_web_manual.ps1 and upload that folder.</p>'; return; }
    if (inline) {
      const f = document.createElement('iframe');                             // a new frame each time: changing only the #page of an open PDF frame does not always move it
      f.src = url; f.title = 'Jolly Helmet user manual'; f.className = 'pdf'; box.appendChild(f);
    } else {
      box.innerHTML = '<div class="empty"><p>Your browser opens PDF files in its own viewer.</p><p><a class="btn" target="_blank" rel="noopener" href="' + esc(url) + '">Open the manual at page ' + printed(abs) + '</a></p></div>';
    }
    const all = [...document.querySelectorAll('#mToc button')], pick = btn || all.find(b => +b.dataset.p === abs);      // one chapter highlighted (several can start on the same page)
    all.forEach(b => b.classList.toggle('on', b === pick));
  }

  /* ---------------- chapter list ---------------- */
  function drawToc() {
    const nav = $('mToc'); nav.textContent = '';
    const items = st.toc.filter(e => e.l <= 2);
    items.forEach(e => {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'l' + e.l; b.dataset.p = e.p;
      b.innerHTML = '<span>' + esc(e.t) + '</span><small>' + printed(e.p) + '</small>';
      b.onclick = () => show(e.p, b);
      nav.appendChild(b);
    });
    if (!st.unlocked) {
      const d = document.createElement('div'); d.className = 'mlock';
      d.innerHTML = '<b>Admin guide, Technician guide, Reference</b><span>Team chapters: settings, wiring, maintenance, safety, flowcharts and the index.</span>';
      const b = document.createElement('button'); b.className = 'btn'; b.type = 'button'; b.textContent = 'Unlock with the team password'; b.onclick = openDlg;
      d.appendChild(b); nav.appendChild(d);
    }
  }
  function chrome() {
    $('mSub').textContent = st.unlocked ? 'The whole manual: customer, admin, technician and reference chapters.' : 'The customer guide. The admin, technician and reference chapters are for the team.';
    $('mLockBtn').textContent = st.unlocked ? 'Lock the team chapters' : 'Team chapters: unlock';
    const d = $('mDown');
    if (st.unlocked) { d.href = st.blob; d.setAttribute('download', 'JollyHelmet_UserManual.pdf'); d.textContent = 'Download the whole manual (PDF)'; }
    else { d.href = st.url; d.setAttribute('download', 'JollyHelmet_CustomerGuide.pdf'); d.textContent = 'Download the customer guide (PDF)'; }
    $('mNote').textContent = inline ? '' : 'Tip: on a phone the PDF opens in the phone\'s own viewer.';
  }

  /* ---------------- search ---------------- */
  function chapterOf(p) { let c = ''; st.toc.forEach(e => { if (e.l <= 2 && e.p <= p) c = e.t; }); return c; }
  function search(q) {
    const box = $('mResults'); q = q.trim().toLowerCase();
    if (q.length < 2) { box.hidden = true; $('mToc').hidden = false; return; }
    const words = q.split(/\s+/);
    const hits = [];
    st.pages.forEach(pg => {
      const t = pg.t.toLowerCase(); let score = 0;
      for (const w of words) { const c = t.split(w).length - 1; if (!c) return; score += c; }
      hits.push({ pg, score });
    });
    hits.sort((a, b) => b.score - a.score || a.pg.p - b.pg.p);
    box.textContent = '';
    if (!hits.length) box.innerHTML = '<p class="empty">Nothing found' + (st.unlocked ? '.' : ' in the customer guide. Unlock the team chapters to search everything.') + '</p>';
    hits.slice(0, 15).forEach(h => {
      const t = h.pg.t, i = t.toLowerCase().indexOf(words[0]), a = Math.max(0, i - 50), snip = (a ? '…' : '') + t.slice(a, i + 90) + '…';
      const re = new RegExp('(' + words.map(w => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|') + ')', 'ig');
      const b = document.createElement('button'); b.type = 'button';
      b.innerHTML = '<b>' + esc(chapterOf(h.pg.p) || 'Manual') + '</b> <small>page ' + printed(h.pg.p) + '</small><span>' + esc(snip).replace(re, '<mark>$1</mark>') + '</span>';
      b.onclick = () => show(h.pg.p);
      box.appendChild(b);
    });
    box.hidden = false; $('mToc').hidden = true;
  }

  /* ---------------- team password ---------------- */
  const b64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
  async function decrypt(bytes, password) {                    // file = "JHM1" | rounds(4, big endian) | salt(16) | iv(16) | hmac(32) | ciphertext
    if (String.fromCharCode(...bytes.slice(0, 4)) !== 'JHM1') throw new Error('not a manual file');
    const rounds = new DataView(bytes.buffer, bytes.byteOffset).getUint32(4), salt = bytes.slice(8, 24), iv = bytes.slice(24, 40), mac = bytes.slice(40, 72), ct = bytes.slice(72);
    const base = await crypto.subtle.importKey('raw', new TextEncoder().encode(password), 'PBKDF2', false, ['deriveBits']);
    const keys = new Uint8Array(await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations: rounds }, base, 512));
    const macKey = await crypto.subtle.importKey('raw', keys.slice(32), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']);
    const data = new Uint8Array(iv.length + ct.length); data.set(iv); data.set(ct, iv.length);
    if (!(await crypto.subtle.verify('HMAC', macKey, mac, data))) throw new Error('wrong password');
    const aes = await crypto.subtle.importKey('raw', keys.slice(0, 32), 'AES-CBC', false, ['decrypt']);
    return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CBC', iv }, aes, ct));
  }
  function loadScript(src) {
    return new Promise((res, rej) => { const s = document.createElement('script'); s.src = src; s.onload = res; s.onerror = () => rej(new Error('missing ' + src)); document.head.appendChild(s); });
  }
  async function unlock(password) {
    if (!window.crypto || !crypto.subtle) throw new Error('This browser cannot decrypt here (open the page over https).');
    if (!window.JH_MANUAL_TEAM) await loadScript('manual/team_manual.js');
    const T = window.JH_MANUAL_TEAM; if (!T) throw new Error('team_manual.js is empty');
    const idx = JSON.parse(new TextDecoder().decode(await decrypt(b64(T.idx), password)));
    const pdf = await decrypt(b64(T.pdf), password);
    if (st.blob) URL.revokeObjectURL(st.blob);
    st.blob = URL.createObjectURL(new Blob([pdf], { type: 'application/pdf' }));
    st.unlocked = true; st.toc = idx.toc; st.pages = idx.pages.map((t, i) => ({ p: i + 1, t }));
    try { sessionStorage.setItem('jh_team', password); } catch (e) {}          // only for this tab; gone when the tab is closed
  }
  function lock() { try { sessionStorage.removeItem('jh_team'); } catch (e) {} if (st.blob) URL.revokeObjectURL(st.blob); st.blob = ''; useCustomer(); refresh(st.first); }
  const dlg = $('mDlg');
  function openDlg() { $('mMsg').textContent = ''; $('mPw').value = ''; dlg.showModal(); setTimeout(() => $('mPw').focus(), 50); }
  async function tryUnlock() {
    const pw = $('mPw').value; if (!pw) return;
    $('mMsg').textContent = 'Checking...'; $('mGo').disabled = true;
    try { await unlock(pw); dlg.close(); refresh(st.cur || st.first); }
    catch (e) { $('mMsg').textContent = /wrong password/.test(e.message) ? 'That is not the team password.' : e.message; }
    $('mGo').disabled = false;
  }
  $('mGo').onclick = tryUnlock;
  $('mPw').addEventListener('keydown', e => { if (e.key === 'Enter') tryUnlock(); });
  $('mCancel').onclick = () => dlg.close();
  $('mLockBtn').onclick = () => { if (st.unlocked) lock(); else openDlg(); };

  function refresh(abs) { drawToc(); chrome(); $('mSearch').value = ''; search(''); show(abs); }

  /* ---------------- shown by the tab bar (js/tabs.js) the first time the Manual tab opens ---------------- */
  let started = false;
  async function openTab() {
    if (started) return;
    started = true; useCustomer();
    let saved = null; try { saved = sessionStorage.getItem('jh_team'); } catch (e) {}
    if (saved) { try { await unlock(saved); } catch (e) { useCustomer(); } }
    const m = /^#manual\/(\d+)/.exec(location.hash), want = m ? +m[1] + 1 : st.first;      // #manual/49 = printed page 49
    refresh(st.unlocked || want >= st.first && want < st.first + (PUB ? PUB.pages.length : 0) ? want : st.first);
  }
  $('mSearch').addEventListener('input', e => search(e.target.value));
  window.ManualTab = { show: openTab };
})();
