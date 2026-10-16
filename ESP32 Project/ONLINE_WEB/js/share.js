"use strict";
/* The QR button: the address of the customer status page (status.html) as a QR code, a link to copy, and a sign to print.
   The QR code holds only the address of the page and (if you typed it) the PUBLIC status token. Never the private token. */
(function () {
  const dlg = $('qrDlg');
  const pubToken = () => { const t = (CONFIG.publicToken || store.get('jh_pubtoken') || '').trim(); return t && t === cfg.token ? '' : t; };   // never the private token
  function pageUrl() {
    const base = new URL('status.html', location.href);
    base.search = ''; base.hash = '';
    const t = CONFIG.publicToken ? '' : pubToken();                 // a token typed here goes into the address; one set in config.js does not need to
    return base.href + (t ? '#t=' + encodeURIComponent(t) : '');
  }
  function showNote() {
    let msg = '';
    if (location.protocol === 'file:') msg = 'This page is opened from a file on your computer, so the address above only works on this computer. Put the folder on a web site (see README.txt) and open the page from there to make the QR code.';
    else if (!pubToken()) msg = 'Without the public status token the customer page shows "no information". Create the second Blynk device, paste its token into the Online Server setup page ("Public status device token") and here (or in js/config.js, publicToken).';
    const note = $('qrNote'); note.textContent = msg; note.style.color = msg ? 'var(--warn)' : '';
  }
  function draw() {
    const url = pageUrl(), box = $('qrBox');
    $('qrUrl').textContent = url;
    showNote();
    try { box.innerHTML = QR.svg(url, 220, 'QR code of the customer status page'); } catch (e) { box.textContent = 'The address is too long for a QR code: ' + e.message; }
    $('qrTok').value = CONFIG.publicToken ? '' : store.get('jh_pubtoken') || '';
    $('qrTok').disabled = !!CONFIG.publicToken; $('qrTok').placeholder = CONFIG.publicToken ? '(set in js/config.js)' : 'token of the PUBLIC status Blynk device';
  }
  $('qrBtn').onclick = () => { draw(); dlg.showModal(); };
  $('qrClose').onclick = () => dlg.close();
  $('qrTok').addEventListener('input', e => {
    const v = e.target.value.trim();
    if (v && v === cfg.token) { store.set('jh_pubtoken', ''); draw(); $('qrNote').textContent = 'That is the PRIVATE token of this monitor. It must never go into a QR code. Use the token of the second, public status device.'; $('qrNote').style.color = 'var(--bad)'; return; }
    store.set('jh_pubtoken', v); const url = pageUrl(); $('qrUrl').textContent = url; showNote(); try { $('qrBox').innerHTML = QR.svg(url, 220); } catch (x) {}
  });
  $('qrCopy').onclick = () => { const u = pageUrl(); (navigator.clipboard ? navigator.clipboard.writeText(u) : Promise.reject()).then(() => { $('qrCopy').textContent = 'Copied'; setTimeout(() => { $('qrCopy').textContent = 'Copy the link'; }, 1500); }).catch(() => prompt('Copy this link:', u)); };
  $('qrOpen').onclick = () => window.open(pageUrl(), '_blank');
  $('qrPrint').onclick = () => {                                     // a sign, one page: big QR code and what to do
    const url = pageUrl(), w = window.open('', '_blank'); if (!w) { alert('The browser blocked the new window. Allow pop-ups for this page and try again.'); return; }
    w.document.write(`<!doctype html><meta charset="utf-8"><title>Jolly Helmet sign</title><style>
      @page{size:A5;margin:10mm} body{font-family:Arial,Helvetica,sans-serif;text-align:center;color:#14325c;margin:0}
      h1{font-size:30px;margin:6px 0 2px} h2{font-size:18px;margin:0 0 10px;color:#b26a00} svg{width:70%;height:auto;margin:6px auto;display:block}
      p{font-size:17px;margin:6px 24px;color:#1d2433} img{height:70px} small{color:#56607a;word-break:break-all;display:block;margin:10px 20px;font-size:11px}</style>
      <img src="${new URL('img/logo.png', location.href).href}" alt=""><h1>Which machine is free?</h1><h2>Scan to see Box 1, Box 2 and Box 3</h2>${QR.svg(url, 400)}
      <p>Point your phone camera at the square. It shows which machines are <b>free</b>, <b>busy</b> or <b>out of order</b>.</p><small>${url.replace(/#.*/, '')}</small>`);
    w.document.close(); setTimeout(() => w.print(), 400);
  };
})();
