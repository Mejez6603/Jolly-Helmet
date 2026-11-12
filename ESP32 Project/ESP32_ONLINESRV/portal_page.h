#pragma once
#include <Arduino.h>

// The Online Server's own setup page (captive portal). Served at http://192.168.4.1 after joining the Wi-Fi "JollyHelmet-Online".
// Router-style admin page (2026-10-23): a header with the "Setup" tab, a left menu (a hamburger button on phones) with Network Map, Internet,
// Blynk and More settings. It only talks to the board through /api/status, /api/scan, /api/save, /api/forget and /api/resetusage.
// Plain HTML + CSS + JS, everything inline (the board has no internet while the page is used), no libraries.
// The look was agreed on a standalone preview first: ESP32 Project\PREVIEW_ONLINE_UI\online_ui_preview.html (mock data).
const char PORTAL_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jolly Helmet Online Server</title>
<style>
:root{--navy:#14325c;--navy2:#0f2747;--amber:#f59e0b;--bg:#e9eff6;--card:#fff;--line:#aebbd0;--text:#1d2433;--muted:#5b6678;--ok:#1b9a4b;--warn:#f59e0b;--bad:#d93025;--off:#a9b3c4;--rad:10px}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;background:var(--bg);color:var(--text)}
button{font:inherit;cursor:pointer}
svg{display:block}
/* ---------------------------------------------------------------- header */
header{height:64px;background:linear-gradient(180deg,var(--navy),var(--navy2));color:#fff;display:flex;align-items:flex-end;gap:16px;padding:0 18px;position:sticky;top:0;z-index:30;box-shadow:0 2px 6px rgba(0,0,0,.25)}
.brand{display:flex;align-items:center;gap:10px;height:64px;min-width:0}
.brand .logo{width:34px;height:34px;flex:none}
.brand b{font-size:20px;letter-spacing:.5px;font-weight:800;line-height:1}
.brand small{display:block;font-size:11.5px;opacity:.8;letter-spacing:.4px;margin-top:3px}
.tab{background:var(--bg);color:var(--navy);font-weight:800;padding:0 26px;height:42px;line-height:42px;border-radius:8px 8px 0 0;margin-left:18px;letter-spacing:.3px;position:relative;top:0}
.tab::after{content:"";position:absolute;left:0;right:0;bottom:0;height:3px;background:var(--amber)}
.pill{margin-left:auto;align-self:center;display:flex;align-items:center;gap:8px;background:rgba(255,255,255,.12);border-radius:99px;padding:6px 14px;font-weight:800;font-size:13px;letter-spacing:.4px;white-space:nowrap}
.pill i{width:11px;height:11px;border-radius:50%;background:var(--off);display:inline-block}
.pill.ok i{background:#3ddc84}.pill.warn i{background:var(--amber)}.pill.bad i{background:#ff6b5e}.pill.wait i{background:#6cc1ff}
#burger{display:none;align-self:center;background:none;border:0;color:#fff;width:42px;height:42px;border-radius:8px}
#burger:hover{background:rgba(255,255,255,.12)}
/* ---------------------------------------------------------------- layout + sidebar */
.layout{display:flex;min-height:calc(100% - 64px)}
nav{width:236px;flex:none;background:#dde6f1;padding:12px 10px;border-right:1px solid #c5d1e1}
nav a{display:flex;align-items:center;gap:12px;height:50px;padding:0 14px;margin-bottom:8px;border-radius:8px;background:#fff;color:#33415a;text-decoration:none;font-weight:600;border-left:5px solid transparent;box-shadow:0 1px 2px rgba(20,50,92,.08)}
nav a svg{width:24px;height:24px;flex:none;stroke:#5a6b86;fill:none;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
nav a:hover{background:#f6f9fd}
nav a.on{border-left-color:var(--amber);color:var(--navy);font-weight:800;background:#fff}
nav a.on svg{stroke:var(--navy)}
nav .navfoot{font-size:11.5px;color:var(--muted);padding:10px 6px 0;line-height:1.4}
main{flex:1;min-width:0;padding:22px 26px 40px}
.page{display:none;max-width:940px}
.page.on{display:block}
h2{font-size:22px;color:var(--navy);margin-bottom:4px}
.lead{color:var(--muted);font-size:14px;margin-bottom:16px}
.card{background:var(--card);border-radius:var(--rad);padding:18px;margin-bottom:16px;box-shadow:0 1px 3px rgba(20,50,92,.12)}
.card h3{font-size:15px;color:var(--navy);margin-bottom:10px}
/* ---------------------------------------------------------------- status */
.hero{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.badge{display:inline-block;padding:7px 18px;border-radius:99px;font-weight:800;font-size:15px;letter-spacing:.5px;background:#e9edf4;color:#4d5870}
.badge.ok{background:#e2f3e7;color:#1b7f3b}.badge.bad{background:#fde8e6;color:#b3261e}.badge.wait{background:#e1eefb;color:#1c5fa8}.badge.warn{background:#fff1d0;color:#9a5b00}
.why{color:var(--muted);font-size:14px;flex:1;min-width:200px}
.row{display:flex;justify-content:space-between;gap:14px;padding:8px 0;border-top:1px solid #edf1f6;font-size:14px}
.row:first-of-type{border-top:0}
.row span:first-child{color:var(--muted)}.row b{text-align:right;word-break:break-word}
/* ---------------------------------------------------------------- network map */
.map{display:flex;flex-direction:column;align-items:center;padding:6px 0 4px}
.top{display:grid;grid-template-columns:1fr auto 1fr;align-items:center;width:100%}.tl{display:flex;justify-content:flex-end;align-items:center}
.link{width:74px;height:2px;background:var(--line);flex:none;position:relative}
.link.bad{background:repeating-linear-gradient(90deg,var(--bad) 0 6px,transparent 6px 10px)}
.link.off{background:repeating-linear-gradient(90deg,var(--off) 0 6px,transparent 6px 10px)}
.node{position:relative;width:104px;text-align:center;font-size:12.5px;font-weight:700;color:#33415a;line-height:1.25}
.node svg{width:58px;height:58px;margin:0 auto 4px;stroke:var(--navy);fill:none;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.node small{display:block;font-weight:500;color:var(--muted);font-size:11.5px;word-break:break-word}
.dot{position:absolute;top:0;right:14px;width:17px;height:17px;border-radius:50%;border:3px solid #fff;background:var(--off);box-shadow:0 0 0 1px #d5dce8}
.dot.ok{background:var(--ok)}.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}.dot.wait{background:#3b8fe0}
.drop{width:2px;height:20px;background:var(--line)}.vl{display:none}
.groups{display:flex;gap:18px;position:relative;padding-top:20px;width:100%;justify-content:center;flex-wrap:wrap}
.group{position:relative;background:#fbfcfe;border:1px solid var(--line);border-radius:8px;padding:12px 10px 10px;flex:0 1 auto}.gitems{display:flex;gap:6px;flex-wrap:wrap;justify-content:center}
.group::before{content:"";position:absolute;left:50%;top:-21px;width:2px;height:20px;background:var(--line)}
.groups::before{content:"";position:absolute;top:0;left:var(--bl,24%);right:var(--br,24%);height:2px;background:var(--line)}
.gtitle{text-align:center;font-size:11.5px;letter-spacing:.5px;font-weight:800;color:var(--muted);text-transform:uppercase;margin-bottom:6px}
.legend{display:flex;gap:16px;flex-wrap:wrap;justify-content:center;margin-top:16px;font-size:12.5px;color:var(--muted)}
.legend span{display:flex;align-items:center;gap:6px}
.legend i{width:12px;height:12px;border-radius:50%;display:inline-block}
/* ---------------------------------------------------------------- forms */
label{display:block;font-weight:700;font-size:13.5px;margin:14px 0 5px;color:var(--navy)}
input[type=text],input[type=password],input[type=number],select{width:100%;font:inherit;padding:11px 12px;border-radius:8px;border:1px solid #c9d3e2;background:#fff;color:var(--text)}
input:focus,select:focus{outline:2px solid #9ec1ff;border-color:#6fa0f0}
.pw{position:relative}.pw button{position:absolute;right:6px;top:5px;background:none;border:0;color:var(--navy);font-weight:700;font-size:12.5px;padding:6px 8px}
.btn{border:0;border-radius:9px;padding:11px 20px;font-weight:800;background:var(--navy);color:#fff;margin-top:16px;letter-spacing:.2px}
.btn:hover{background:#1c4379}
.btn.alt{background:#fff;color:var(--navy);border:1px solid #c9d3e2;margin-left:8px}
.btn.danger{background:#fff;color:#b3261e;border:1px solid #efc4c0}
.btn.amber{background:var(--amber);color:#2b1c00}
.fine{font-size:12.8px;color:var(--muted);margin-top:7px}
.chk{display:flex;gap:10px;align-items:flex-start;font-weight:400;color:var(--text);margin:14px 0 0}
.chk input{width:auto;margin-top:4px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:0 16px}
.nets{margin-top:12px;display:grid;gap:6px}
.net{display:flex;align-items:center;gap:10px;padding:10px 12px;border-radius:8px;background:#f5f8fc;border:1px solid #e1e8f2;cursor:pointer}
.net:hover{background:#eaf1fb;border-color:#b8cdea}
.net b{flex:1;font-weight:600}.net small{color:var(--muted)}
.bars{display:flex;gap:2px;align-items:flex-end;height:16px;justify-content:center;margin:3px auto 0}.bars i{width:4px;background:#c3cddd;border-radius:1px}.bars i.on{background:var(--navy)}
.result{margin-top:14px;padding:11px 14px;border-radius:8px;font-size:14px;background:#eef3fa;border-left:5px solid var(--off)}
.result.ok{border-left-color:var(--ok);background:#e9f6ee}.result.bad{border-left-color:var(--bad);background:#fdeceb}.result.wait{border-left-color:#3b8fe0;background:#e7f1fc}
.quota{height:9px;background:#e3e9f2;border-radius:9px;overflow:hidden;margin-top:6px}.quota i{display:block;height:100%;background:var(--navy)}
#toast{position:fixed;left:50%;bottom:74px;transform:translateX(-50%) translateY(30px);background:#10233f;color:#fff;padding:12px 20px;border-radius:10px;font-weight:700;opacity:0;transition:.25s;z-index:60;pointer-events:none;max-width:90vw;text-align:center}
#toast.on{opacity:1;transform:translateX(-50%)}
/* ---------------------------------------------------------------- phone */
#back{display:none}
@media(max-width:860px){
  #burger{display:block}
  header{gap:6px;padding:0 10px}
  .tab{margin-left:4px;padding:0 16px}
  .brand small{display:none}
  .brand b{font-size:16px}
  .brand .logo{width:28px;height:28px}
  .pill{font-size:11.5px;padding:5px 10px}
  nav{position:fixed;top:64px;left:0;bottom:0;z-index:40;transform:translateX(-102%);transition:transform .22s;width:260px;box-shadow:6px 0 20px rgba(0,0,0,.25);overflow:auto}
  body.menu nav{transform:none}
  #back{display:block;position:fixed;inset:64px 0 0 0;background:rgba(10,20,40,.45);z-index:35;opacity:0;pointer-events:none;transition:.2s}
  body.menu #back{opacity:1;pointer-events:auto}
  main{padding:16px 12px 40px}
  .node{width:96px}.node svg{width:46px;height:46px}.dot{right:8px}
  .groups{padding-top:0;gap:20px;flex-direction:column;align-items:stretch}.groups::before{left:50%;right:auto;top:0;bottom:0;width:2px;height:auto;transform:translateX(-1px)}.group::before{display:none}.group{width:100%;z-index:1}.gitems{display:grid;grid-template-columns:repeat(3,1fr);gap:10px 0;justify-items:center}#gServers .gitems{grid-template-columns:1fr}#gMachines .gitems{grid-template-columns:repeat(2,1fr)}.top{display:flex;flex-direction:column;align-items:center}.tl{display:flex;flex-direction:column;align-items:center}#l2{display:none}.tr{display:none}.link{width:2px;height:22px;margin:12px 0}.link.bad{background:repeating-linear-gradient(180deg,var(--bad) 0 6px,transparent 6px 10px)}.link.off{background:repeating-linear-gradient(180deg,var(--off) 0 6px,transparent 6px 10px)}.vl{display:block;width:2px;height:22px;margin:12px 0;background:var(--line)}.vl.bad{background:repeating-linear-gradient(180deg,var(--bad) 0 6px,transparent 6px 10px)}.vl.off{background:repeating-linear-gradient(180deg,var(--off) 0 6px,transparent 6px 10px)}.drop{height:18px;margin-top:12px}
  .two{grid-template-columns:1fr}
  .pill span.t{display:none}
  html{scroll-padding-top:72px}header{height:58px}.brand{height:58px}.tab{height:38px;line-height:38px}nav{top:58px}#back{inset:58px 0 0 0}
}
@media(max-width:420px){.brand b{font-size:14px}.tab{padding:0 12px}}
#lost{display:none;background:#fff1d0;color:#9a5b00;border-radius:10px;padding:10px 14px;margin-bottom:14px;font-size:14px;max-width:940px}
</style></head>
<body>
<header>
  <button id="burger" aria-label="Menu" onclick="document.body.classList.toggle('menu')"><svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="#fff" stroke-width="2.4" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg></button>
  <div class="brand">
    <svg class="logo" viewBox="0 0 48 48"><circle cx="24" cy="24" r="22" fill="#f59e0b"/><path d="M9 28c0-9 6-16 15-16s15 7 15 16v5H9z" fill="#14325c"/><path d="M24 12v21" stroke="#f59e0b" stroke-width="2.4"/><rect x="25" y="23" width="14" height="9" rx="3" fill="#f59e0b"/></svg>
    <div><b>JOLLY HELMET</b><small>Online Server</small></div>
  </div>
  <div class="tab">Setup</div>
  <div class="pill" id="pill"><i></i><span class="t" id="pilltxt">...</span></div>
</header>
<div id="back" onclick="document.body.classList.remove('menu')"></div>
<div class="layout">
<nav id="nav">
  <a href="#map" data-p="map"><svg viewBox="0 0 24 24"><path d="M3 13l4-4 4 5 4-8 6 8"/><path d="M3 20h18"/></svg>Network Map</a>
  <a href="#internet" data-p="internet"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18"/></svg>Internet</a>
  <a href="#blynk" data-p="blynk"><svg viewBox="0 0 24 24"><circle cx="8" cy="15" r="4"/><path d="M11 12l9-9M16 7l3 3M14 9l2 2"/></svg>Blynk</a>
  <a href="#more" data-p="more"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1"/></svg>More settings</a>
  <div class="navfoot">It only listens to the machines and sends their status to Blynk. It never controls them.</div>
</nav>
<main>
<div id="lost">Lost contact with the board for a moment. This is normal while it tests your Wi-Fi: it moves to your router's channel. Keep this page open, it reconnects by itself (if not, join the Wi-Fi <b>JollyHelmet-Online</b> again).</div>

<!-- ======================================================= NETWORK MAP -->
<section class="page" id="p-map">
  <h2>Network Map</h2>
  <div class="lead">What the Online Server hears and where it sends it.</div>
  <div class="card"><div class="hero"><span id="big" class="badge">...</span><span id="why" class="why"></span></div></div>
  <div class="card">
    <div class="map">
      <div class="top">
        <div class="tl">
          <div class="node" id="nInternet"><span class="dot" id="dInternet"></span>
            <svg viewBox="0 0 64 64"><circle cx="32" cy="32" r="22"/><path d="M10 32h44M32 10c9 9 9 35 0 44M32 10c-9 9-9 35 0 44M14 20h36M14 44h36"/></svg>Internet<small id="tInternet">Blynk</small></div>
          <div class="link" id="l1"></div>
          <div class="node" id="nWifi"><span class="dot" id="dWifi"></span>
            <svg viewBox="0 0 64 64"><path d="M8 46h48v8H8zM14 46l6-14h24l6 14"/><path d="M44 26c5 0 8-3 8-3M46 18c6 0 10-4 10-4"/><circle cx="16" cy="50" r="1.5"/><circle cx="24" cy="50" r="1.5"/><path d="M32 32V18"/></svg>Wi-Fi<small id="tWifi">-</small></div>
          <div class="link" id="l2"></div>
        </div>
        <div class="vl" id="vl"></div>
        <div class="node" id="nOS"><span class="dot ok" id="dOS"></span>
          <svg viewBox="0 0 64 64"><rect x="10" y="16" width="44" height="32" rx="5"/><path d="M18 24h10M18 32h10M36 24h10"/><circle cx="44" cy="38" r="3"/><path d="M22 48v6M42 48v6M18 54h28"/></svg>Online Server<small id="tOS">listens only</small></div>
        <div class="tr"></div>
      </div>
      <div class="drop"></div>
      <div class="groups">
        <div class="group" id="gMachines">
          <div class="gtitle">Machines heard on the radio</div>
          <div class="gitems">
            <div class="node" id="nB1"><span class="dot" id="dB1"></span><svg viewBox="0 0 64 64"><rect x="14" y="8" width="36" height="48" rx="4"/><rect x="20" y="14" width="24" height="18" rx="2"/><path d="M22 40h20M22 46h12"/><circle cx="44" cy="46" r="2"/></svg> Box 1<small id="tB1">-</small></div>
            <div class="node" id="nB2"><span class="dot" id="dB2"></span><svg viewBox="0 0 64 64"><rect x="14" y="8" width="36" height="48" rx="4"/><rect x="20" y="14" width="24" height="18" rx="2"/><path d="M22 40h20M22 46h12"/><circle cx="44" cy="46" r="2"/></svg> Box 2<small id="tB2">-</small></div>
            <div class="node" id="nB3"><span class="dot" id="dB3"></span><svg viewBox="0 0 64 64"><rect x="14" y="8" width="36" height="48" rx="4"/><rect x="20" y="14" width="24" height="18" rx="2"/><path d="M22 40h20M22 46h12"/><circle cx="44" cy="46" r="2"/></svg> Box 3<small id="tB3">-</small></div>
            <div class="node" id="nRS"><span class="dot" id="dRS"></span><svg viewBox="0 0 64 64"><path d="M22 8h20l-2 14H24z"/><path d="M32 22v10M20 32h24M24 32v10h16V32"/><path d="M32 42v8M26 50h12M22 56h20"/><circle cx="32" cy="37" r="2"/></svg> Refilling Station<small id="tRS">-</small></div>
          </div>
        </div>
        <div class="group" id="gServers">
          <div class="gtitle">Server heard</div>
          <div class="gitems">
            <div class="node" id="nLS"><span class="dot" id="dLS"></span>
              <svg viewBox="0 0 64 64"><rect x="12" y="10" width="40" height="14" rx="3"/><rect x="12" y="28" width="40" height="14" rx="3"/><circle cx="20" cy="17" r="1.8"/><circle cx="20" cy="35" r="1.8"/><path d="M32 42v8M20 54h24M32 50v4"/></svg>LocalServer<small id="tLS">-</small></div>
          </div>
        </div>
      </div>
      <div class="legend"><span><i style="background:var(--ok)"></i>heard / working</span><span><i style="background:var(--bad)"></i>out of order / not heard</span><span><i style="background:var(--off)"></i>not used / unknown</span></div>
    </div>
  </div>
  <div class="card"><h3>Details</h3>
    <div class="row"><span>Wi-Fi network</span><b id="s_wifi">-</b></div>
    <div class="row"><span>Blynk upload</span><b id="s_blynk">-</b></div>
    <div class="row"><span>Public status device</span><b id="s_pub">-</b></div>
    <div class="row"><span>Machines heard on the radio</span><b id="s_radio">-</b></div>
    <div class="row"><span>Revenue totals (LocalServer)</span><b id="s_stats">-</b></div>
    <div class="row"><span>Sessions counted (per box and hour)</span><b id="s_use">-</b></div>
    <div class="row"><span>Clock</span><b id="s_time">-</b></div>
    <div class="row"><span>Next upload</span><b id="s_next">-</b></div>
    <div class="row"><span>Uploads this month</span><b id="s_mq">-</b></div>
    <div class="row"><span>Running for</span><b id="s_up">-</b></div>
  </div>
</section>

<!-- ======================================================= INTERNET -->
<section class="page" id="p-internet">
  <h2>Internet</h2>
  <div class="lead">The Wi-Fi the Online Server uses to reach Blynk (the school Wi-Fi or a phone hotspot). Only 2.4 GHz works.</div>
  <div class="card">
    <h3>Wi-Fi network</h3>
    <button class="btn alt" style="margin:0" id="scan" type="button">Search for networks</button><span id="scanmsg" class="fine" style="margin-left:10px"></span>
    <div class="nets" id="nets"></div>
    <label for="ssid">Network name (SSID)</label><input id="ssid" type="text" autocomplete="off" autocapitalize="none" spellcheck="false">
    <label for="pass">Password</label>
    <div class="pw"><input id="pass" type="password" autocomplete="off" placeholder="(leave empty for an open network)"><button type="button" data-show="pass">Show</button></div>
    <div id="inetResult" class="result">...</div>
    <button class="btn" type="button" data-save>Save and connect</button><button class="btn danger alt" type="button" id="forget">Forget Wi-Fi</button>
    <div class="fine">Saving makes the board test the network right away. The result appears above within about 20 seconds.</div>
  </div>
</section>

<!-- ======================================================= BLYNK -->
<section class="page" id="p-blynk">
  <h2>Blynk</h2>
  <div class="lead">Where the status is sent. The web page reads it from there.</div>
  <div class="card">
    <h3>Device tokens</h3>
    <label for="token">Blynk device Auth Token</label>
    <div class="pw"><input id="token" type="password" autocomplete="off" spellcheck="false" placeholder="paste the token of this device"><button type="button" data-show="token">Show</button></div>
    <label for="ptoken">Public status device token (optional)</label>
    <div class="pw"><input id="ptoken" type="password" autocomplete="off" spellcheck="false" placeholder="token of a SECOND Blynk device, for the customer page"><button type="button" data-show="ptoken">Show</button></div>
    <div class="fine">Not the same token as above. Type - (a minus sign) and Save to remove it.</div>
    <div id="blynkResult" class="result">...</div>
    <button class="btn" type="button" data-save>Save and connect</button>
  </div>
</section>

<!-- ======================================================= MORE -->
<section class="page" id="p-more">
  <h2>More settings</h2>
  <div class="lead">You normally never need these.</div>
  <div class="card">
    <h3>Upload</h3>
    <div class="two">
      <div><label for="iv">Send to Blynk every</label>
        <select id="iv"><option value="30">30 seconds</option><option value="60">1 minute</option><option value="120">2 minutes</option><option value="300">5 minutes</option></select></div>
      <div><label for="tz">Time zone (hours from UTC)</label><input id="tz" type="number" step="0.5" min="-12" max="14"></div>
    </div>
    <label class="chk"><input id="extra" type="checkbox"><span>Also send single numbers V1 to V4 for Blynk dashboards (revenue, customers, alerts, boxes running). The datastreams must exist in the template.</span></label>
    <label class="chk"><input id="events" type="checkbox"><span>Send a Blynk event with code <b>alert</b> when something needs attention (empty tank, box out of order...). The event must exist in the template.</span></label>
    <label for="mqmax">Blynk messages per month in your plan (0 = do not show)</label><input id="mqmax" type="number" min="0" step="1000">
    <div class="quota" id="quotaBar" style="display:none"><i id="quotaFill"></i></div>
  </div>
  <div class="card">
    <h3>Blynk server</h3>
    <label for="server">Server</label><input id="server" type="text" autocomplete="off" spellcheck="false">
    <label class="chk"><input id="insecure" type="checkbox"><span>Skip the TLS certificate check (only if every upload fails with a TLS error)</span></label>
  </div>
  <div class="card">
    <h3>Statistics</h3>
    <div class="fine" style="margin:0 0 4px">The per-box, busiest-hours and busiest-days counts kept in this board.</div>
    <button class="btn danger alt" style="margin:8px 0 0" type="button" id="resetuse">Reset the per-box and busiest-hours statistics</button>
  </div>
  <button class="btn" type="button" data-save>Save and connect</button>
</section>

</main></div>
<div id="toast"></div>

<script>
const $=id=>document.getElementById(id);
async function api(p){const r=await fetch(p,{cache:'no-store'});return r.json();}
function ago(s){s=Math.round(s);if(s<60)return s+' s';if(s<3600)return Math.round(s/60)+' min';return (s/3600).toFixed(1)+' h';}
function up(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m '+(s%60)+'s';}
function dot(id,cls){$(id).className='dot '+(cls||'');}
function bars(rssi){const q=rssi>-55?4:rssi>-65?3:rssi>-75?2:1;return '<span class="bars">'+[1,2,3,4].map(i=>'<i class="'+(i<=q?'on':'')+'" style="height:'+(4+i*3)+'px"></i>').join('')+'</span>';}
function wifiHint(e){e=e||'';if(/password/i.test(e))return 'Check the password (it is case-sensitive), then Save and connect again.';if(/not found/i.test(e))return 'The board cannot see that network. Only 2.4 GHz works: a network that is 5 GHz only (often named ...5G) cannot be used. Move the board closer and try again.';if(/timeout|no answer/i.test(e))return 'The router did not answer. Check that it is on and in range, then try again.';return 'Check the name and the password, then Save and connect again.';}
const GROUPS={B1:[0,1,2,3],B2:[4,5,6,12],B3:[7,8,9,10]};   /* Box 1 = A1 A2 A3 S3A, Box 2 = C1 C2 S3C + C3 (the relay board, last place), Box 3 = B1 B2 B3 S3B (same order as the V0 board string, 13 places) */
let filled=false,lastOk=Date.now(),saveAt=0;
function fillForm(s){$('ssid').value=s.ssid||'';$('server').value=s.server;$('iv').value=String(s.iv);$('tz').value=s.tz;$('mqmax').value=s.mqmax;
  $('extra').checked=!!s.extra;$('events').checked=!!s.events;$('insecure').checked=!!s.insecure;
  $('pass').placeholder=s.hasPass?'(unchanged)':'(leave empty for an open network)';$('token').placeholder=s.hasToken?'(unchanged)':'paste the token of this device';$('ptoken').placeholder=s.hasPtoken?'(unchanged)':'token of a SECOND Blynk device, for the customer page';}
function render(s){
  lastOk=Date.now();$('lost').style.display='none';
  const since=saveAt?(Date.now()-saveAt)/1000:1e9;
  const fresh=s.state!=='testing'&&s.wifi.age>=0&&s.wifi.age<since-1;           /* a result newer than the Save */
  const testing=s.state==='testing'||(saveAt&&since<45&&!fresh);
  let cls='badge',txt='',w='',pill='';
  if(testing){cls+=' wait';txt='TESTING THE CONNECTION...';w=s.hasToken?'Joining "'+s.ssid+'", then talking to Blynk.':'Joining "'+s.ssid+'" to test the Wi-Fi.';pill='wait';}
  else if(!s.ssid){cls+=' warn';txt='NOT SET UP';w='Choose a Wi-Fi network in Internet.';pill='warn';}
  else if(s.wifi.age>=0&&!s.wifi.ok){cls+=' bad';txt='NOT CONNECTED';w='Could not join the Wi-Fi "'+s.ssid+'": '+(s.wifi.err||'could not join')+'.';pill='bad';}
  else if(!s.hasToken){cls+=' warn';txt=s.wifi.age>=0?'WI-FI OK, TOKEN NEEDED':'NOT SET UP';w=s.wifi.age>=0?'The Wi-Fi works ('+s.wifi.ip+', '+s.wifi.rssi+' dBm). Now paste the Blynk device Auth Token in Blynk.':'Save and connect to test the Wi-Fi, then paste the Blynk token.';pill='warn';}
  else if(s.blynk.ok&&s.blynk.age<s.iv*3+30){cls+=' ok';txt='ONLINE';w='The last upload to Blynk worked '+ago(s.blynk.age)+' ago.';pill='ok';}
  else if(s.blynk.age<0&&s.wifi.age<0){cls+=' wait';txt='WAITING FOR THE FIRST UPLOAD';w='Next upload in '+s.next+' s.';pill='wait';}
  else{cls+=' bad';txt='NOT CONNECTED';w=(s.wifi.ok?'Wi-Fi joined, but Blynk failed: ':'Could not join the Wi-Fi: ')+(s.wifi.ok?s.blynk.err:s.wifi.err);pill='bad';}
  $('big').className=cls;$('big').textContent=txt;$('why').textContent=w;
  $('pill').className='pill '+pill;$('pilltxt').textContent=txt.replace('...','');
  /* the Internet and Blynk pages */
  const wifiTxt=!s.ssid?'No network chosen yet.':s.ssid+' - '+(s.wifi.age<0?'not tested yet (Save and connect)':s.wifi.ok?'joined ('+s.wifi.ip+', '+s.wifi.rssi+' dBm)':'FAILED: '+(s.wifi.err||'could not join'));
  const iR=$('inetResult');iR.className='result '+(testing?'wait':!s.ssid?'':s.wifi.age<0?'':s.wifi.ok?'ok':'bad');iR.textContent=wifiTxt;
  if(!testing&&s.ssid&&s.wifi.age>=0){const h=document.createElement('div');h.className='fine';h.style.marginTop='4px';h.textContent=s.wifi.ok?(s.hasToken?'The Wi-Fi works.':'The Wi-Fi works. Next: paste the Blynk Auth Token in Blynk.'):wifiHint(s.wifi.err);iR.appendChild(h);}
  const bTxt=testing?'Testing...':!s.hasToken?'No token saved yet. Paste the Auth Token of your Blynk device, then Save and connect (the Wi-Fi is tested first).':s.blynk.age<0?'Not tried yet.':(s.blynk.ok?'Blynk upload OK':'Blynk upload FAILED: '+s.blynk.err)+' ('+ago(s.blynk.age)+' ago)'+(s.blynk.note?' - '+s.blynk.note:'')+(s.hasPtoken?' · public status device: '+(s.pub.tried?(s.pub.ok?'OK':'FAILED: '+s.pub.err):'waiting for the next upload'):'');
  const bR=$('blynkResult');bR.className='result '+(testing?'wait':!s.hasToken||s.blynk.age<0?'':s.blynk.ok?'ok':'bad');bR.textContent=bTxt;
  /* the Network Map */
  const wifiUp=s.wifi.ok&&s.wifi.age>=0,blynkUp=s.blynk.ok&&s.blynk.age>=0,n=(s.radio&&s.radio.n)||'0000000000000';
  dot('dInternet',testing?'wait':!s.hasToken?'off':blynkUp?'ok':s.blynk.age<0?'wait':'bad');$('tInternet').textContent=!s.hasToken?'Blynk (no token)':blynkUp?'Blynk · OK':s.blynk.age<0?'Blynk':'Blynk · failed';
  dot('dWifi',testing?'wait':!s.ssid?'off':wifiUp?'ok':s.wifi.age<0?'wait':'bad');$('tWifi').innerHTML='';
  {const t=$('tWifi');t.textContent=!s.ssid?'none chosen':s.ssid;if(wifiUp)t.insertAdjacentHTML('beforeend','<br>'+bars(s.wifi.rssi));}
  $('l1').className='link '+(blynkUp?'':(!s.hasToken||s.blynk.age<0?'off':'bad'));$('l2').className='link '+(wifiUp?'':(s.wifi.age<0?'off':'bad'));$('vl').className='vl '+(wifiUp?'':(s.wifi.age<0?'off':'bad'));
  const grp=(k,idD,idT)=>{const ix=GROUPS[k],h=ix.filter(i=>n[i]==='1').length;dot(idD,h===ix.length?'ok':'bad');$(idT).innerHTML=(h===ix.length?'':'<b style="color:var(--bad)">OUT OF ORDER</b><br>')+h+' of '+ix.length+' boards';};
  grp('B1','dB1','tB1');grp('B2','dB2','tB2');grp('B3','dB3','tB3');
  dot('dRS',n[11]==='1'?'ok':'bad');$('tRS').textContent=n[11]==='1'?'heard':'not heard';
  dot('dLS',s.radio.stats?'ok':'bad');$('tLS').textContent=s.radio.stats?'totals '+ago(s.radio.statsAge)+' ago':'not heard';
  $('tOS').textContent=testing?'testing...':'listens only';dot('dOS',testing?'wait':'ok');
  /* the details */
  $('s_wifi').textContent=wifiTxt;
  $('s_blynk').textContent=s.blynk.age<0?(s.hasToken?'not tried yet':'no token yet'):(s.blynk.ok?'OK':'FAILED: '+s.blynk.err)+' ('+ago(s.blynk.age)+' ago)'+(s.blynk.note?' - '+s.blynk.note:'');
  $('s_pub').textContent=!s.hasPtoken?'not used':(s.pub.tried?(s.pub.ok?'OK':'FAILED: '+s.pub.err)+' ('+ago(s.pub.age)+' ago)':'waiting for the next upload');
  $('s_radio').textContent=n.slice(0,13).split('').filter(c=>c==='1').length+' of 13 boards';
  $('s_stats').textContent=s.radio.stats?'heard '+ago(s.radio.statsAge)+' ago':'not heard yet (is the LocalServer on?)';
  $('s_use').textContent=s.sessions;$('s_time').textContent=s.time;$('s_next').textContent=testing?'-':s.next+' s';
  $('s_mq').textContent=s.mq+(s.mqmax>0?' of '+s.mqmax:'');$('s_up').textContent=up(s.up)+' ('+s.rst+')';
  if(s.mqmax>0){$('quotaBar').style.display='block';$('quotaFill').style.width=Math.min(100,s.mq*100/s.mqmax)+'%';}else $('quotaBar').style.display='none';
  if(!filled){filled=true;fillForm(s);}
}
async function poll(){try{render(await api('/api/status'));}catch(e){if(Date.now()-lastOk>4000)$('lost').style.display='block';}}
setInterval(poll,2000);poll();
/* ---- pages + menu ---- */
function show(p){document.querySelectorAll('.page').forEach(e=>e.classList.toggle('on',e.id==='p-'+p));document.querySelectorAll('#nav a').forEach(a=>a.classList.toggle('on',a.dataset.p===p));document.body.classList.remove('menu');window.scrollTo(0,0);}
function route(){const p=(location.hash||'#map').slice(1);show(['map','internet','blynk','more'].includes(p)?p:'map');}
addEventListener('hashchange',route);route();
function bracket(){const g=document.querySelector('.groups');if(!g)return;const gs=[...g.querySelectorAll('.group')];if(gs.length<2)return;const r=g.getBoundingClientRect();const c=gs.map(e=>{const b=e.getBoundingClientRect();return b.left+b.width/2-r.left;});c.push(r.width/2);g.style.setProperty('--bl',Math.min(...c)+'px');g.style.setProperty('--br',(r.width-Math.max(...c))+'px');}
addEventListener('resize',bracket);addEventListener('load',bracket);setInterval(bracket,3000);
/* ---- buttons ---- */
let tt;function toast(t){const e=$('toast');e.textContent=t;e.classList.add('on');clearTimeout(tt);tt=setTimeout(()=>e.classList.remove('on'),2600);}
document.querySelectorAll('[data-show]').forEach(b=>b.onclick=()=>{const i=$(b.dataset.show);const h=i.type==='password';i.type=h?'text':'password';b.textContent=h?'Hide':'Show';});
$('scan').onclick=async()=>{
  $('scanmsg').textContent='searching...';$('nets').innerHTML='';
  try{await api('/api/scan?start=1');}catch(e){}
  for(let i=0;i<20;i++){
    await new Promise(r=>setTimeout(r,1000));
    let j;try{j=await api('/api/scan');}catch(e){continue;}
    if(j.scanning)continue;
    $('scanmsg').textContent=j.networks.length?j.networks.length+' found':'none found';
    j.networks.forEach(n=>{const d=document.createElement('div');d.className='net';d.innerHTML=bars(n.rssi)+'<b></b><small></small>';d.querySelector('b').textContent=n.ssid;d.querySelector('small').textContent=(n.open?'open · ':'')+n.rssi+' dBm';d.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus();};$('nets').append(d);});
    return;
  }
  $('scanmsg').textContent='no answer, try again';
};
document.querySelectorAll('[data-save]').forEach(b=>b.onclick=async()=>{
  const q=new URLSearchParams({ssid:$('ssid').value.trim(),iv:$('iv').value,server:$('server').value.trim(),tz:$('tz').value,mqmax:$('mqmax').value||'0',
    extra:$('extra').checked?1:0,events:$('events').checked?1:0,insecure:$('insecure').checked?1:0});
  if($('pass').value)q.set('pass',$('pass').value);if($('token').value.trim())q.set('token',$('token').value.trim());if($('ptoken').value.trim())q.set('ptoken',$('ptoken').value.trim());
  if(!$('pass').value&&!$('token').value.trim())q.set('keep','1');
  $('pass').value='';$('token').value='';$('ptoken').value='';
  saveAt=Date.now();toast('Saved. Testing now...');
  try{await api('/api/save?'+q.toString());}catch(e){}
  poll();
});
$('forget').onclick=async()=>{if(!confirm('Forget the saved Wi-Fi and token?'))return;try{await api('/api/forget');}catch(e){}saveAt=0;filled=false;$('ssid').value='';poll();toast('Wi-Fi and token forgotten');};
$('resetuse').onclick=async()=>{if(!confirm('Start the per-box and busiest-hours statistics again from zero?'))return;try{await api('/api/resetusage');}catch(e){}toast('Statistics reset');};
</script>
</body></html>)rawliteral";
