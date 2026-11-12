#pragma once
// The control page of the fake Refilling Station (served at http://192.168.4.1/ from the sketch's own Wi-Fi). No internet, no external files:
// everything is inline. The page polls /api/state every 1.5 s, so the BOOT button and the serial console show up here too.
//   /api/state                      -> {"on":true,"seq":12,"lvl":[3,3,3],"valve":0,"queue":0,"fail":0}
//   /api/level?box=0|1|2|3&v=0..4|255   (box 0 = all three; 255 = sensor fault)
//   /api/flag?box=1..3&t=queue|valve|fail&v=1|0
//   /api/on?v=1|0                   (0 = stop broadcasting = the station is OFFLINE)
const char FAKE_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Fake Refilling Station</title>
<style>
:root{--bg:#10161d;--card:#18212b;--line:#2b3947;--fg:#e9eff5;--mut:#92a3b4;--ok:#2fbf71;--bad:#e5484d;
--l0:#e5484d;--l1:#f08c28;--l2:#e2c12a;--l3:#2fbf71;--l4:#2f9bd8;--l5:#8f6cf0}
*{box-sizing:border-box}
html,body{margin:0}
body{background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;padding:16px 16px 40px;max-width:560px;margin:0 auto}
h1{font-size:1.25rem;margin:0 0 2px}
.sub{color:var(--mut);font-size:.85rem;margin:0 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.bh{display:flex;align-items:baseline;gap:8px;margin-bottom:10px}
.bh b{font-size:1.05rem}
.tag{font-size:.72rem;letter-spacing:.06em;text-transform:uppercase;color:var(--mut)}
button{font:inherit;color:var(--fg);background:transparent;border:2px solid var(--line);border-radius:10px;min-height:48px;padding:6px 10px;cursor:pointer;-webkit-tap-highlight-color:transparent}
button:active{transform:scale(.97)}
.lv{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.lb{border-color:var(--c);font-weight:600;font-size:.92rem}
.lb.sel{background:var(--c);color:#10161d;font-weight:800}
.l0{--c:var(--l0)}.l1{--c:var(--l1)}.l2{--c:var(--l2)}.l3{--c:var(--l3)}.l4{--c:var(--l4)}.l5{--c:var(--l5)}
.fl{display:flex;gap:8px;margin-top:10px}
.chip{flex:1;min-height:40px;font-size:.8rem;letter-spacing:.06em;color:var(--mut)}
.chip.sel{background:var(--fg);color:var(--bg);border-color:var(--fg);font-weight:800}
.big{width:100%;min-height:58px;font-size:1.02rem;font-weight:800;letter-spacing:.02em}
.big.on{background:var(--ok);border-color:var(--ok);color:#08210f}
.big.off{background:var(--bad);border-color:var(--bad);color:#fff}
.note{color:var(--mut);font-size:.85rem;margin-top:10px}
.res{margin-top:10px;font-weight:700}
.res.y{color:var(--ok)}.res.n{color:var(--l1)}
.pre{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.lost{display:none;background:var(--bad);color:#fff;border-radius:10px;padding:10px 12px;margin-bottom:12px;font-weight:700}
.foot{color:var(--mut);font-size:.78rem;margin-top:16px}
</style></head><body>
<h1>Fake Refilling Station</h1>
<p class="sub">Test stand-in: the boxes, the LocalServer and the Online Server think this is the real station. <span id="seq"></span></p>
<div class="lost" id="lost">Connection to the fake station lost - check you are still on its Wi-Fi.</div>

<div class="card">
  <button class="big off" id="onoff">...</button>
  <div class="note">OFFLINE = nothing is broadcast. After the offline time (15 s by default) Box 3 offers DRYING only.</div>
</div>

<div id="boxes"></div>

<div class="card">
  <div class="bh"><b>All three boxes</b></div>
  <div class="pre">
    <button class="lb l3" data-all="3">All HIGH</button>
    <button class="lb l2" data-all="2">All MID</button>
    <button class="lb l0" data-all="0">All EMPTY</button>
  </div>
  <div class="note">QUEUE, VALVE and FAIL only change what the LocalServer and web page show (the "waiting for its turn", "valve open" and "last refill failed" flags). The boxes ignore them.</div>
</div>
<p class="foot">Same as the BOOT button and the serial console. Nothing is saved: after a restart every box is HIGH.</p>

<script>
var NAMES=['EMPTY','LOW','MID','HIGH','MAX','FAULT'],VALS=[0,1,2,3,4,255],S=null;
function $(id){return document.getElementById(id)}
function build(){
  var w=$('boxes');
  for(var b=1;b<=3;b++){
    var c=document.createElement('div');c.className='card';
    var h='<div class="bh"><b>Box '+b+'</b>'+(b==3?'<span class="tag">under test</span>':'')+'</div><div class="lv">';
    for(var i=0;i<6;i++)h+='<button class="lb l'+i+'" data-b="'+b+'" data-v="'+VALS[i]+'">'+NAMES[i]+'</button>';
    h+='</div><div class="fl">';
    ['queue','valve','fail'].forEach(function(t){h+='<button class="chip" data-b="'+b+'" data-t="'+t+'">'+t.toUpperCase()+'</button>'});
    h+='</div>';
    if(b==3)h+='<div class="res" id="n3"></div>';
    c.innerHTML=h;w.appendChild(c);
  }
}
function lost(x){$('lost').style.display=x?'block':'none'}
function call(p){
  return fetch(p,{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json()})
    .then(function(s){S=s;draw();lost(false)}).catch(function(){lost(true)});
}
function draw(){
  if(!S)return;
  var o=$('onoff');o.className='big '+(S.on?'on':'off');
  o.textContent=S.on?'BROADCASTING - tap to go OFFLINE':'OFFLINE - tap to start broadcasting';
  $('seq').textContent='Packet #'+S.seq+'.';
  var i;
  document.querySelectorAll('.lb[data-b]').forEach(function(b){i=+b.dataset.b-1;b.classList.toggle('sel',S.lvl[i]==+b.dataset.v)});
  document.querySelectorAll('.chip').forEach(function(b){i=+b.dataset.b-1;b.classList.toggle('sel',((S[b.dataset.t]>>i)&1)==1)});
  var l=S.lvl[2],ok=S.on&&l>=2&&l<=4,n=$('n3');
  n.className='res '+(ok?'y':'n');
  n.textContent=ok?'Box 3 offers CLEANING and DRYING.':(S.on?'Box 3 offers DRYING only (container '+(l==255?'FAULT':NAMES[l])+').':'Box 3 offers DRYING only once the offline time has passed (station not heard).');
}
document.addEventListener('click',function(e){
  var b=e.target.closest('button');if(!b)return;
  if(b.id=='onoff')call('/api/on?v='+(S&&S.on?0:1));
  else if(b.dataset.all!==undefined)call('/api/level?box=0&v='+b.dataset.all);
  else if(b.dataset.t){var i=+b.dataset.b-1,cur=S&&((S[b.dataset.t]>>i)&1)==1;call('/api/flag?box='+b.dataset.b+'&t='+b.dataset.t+'&v='+(cur?0:1))}
  else if(b.dataset.v!==undefined)call('/api/level?box='+b.dataset.b+'&v='+b.dataset.v);
});
build();call('/api/state');setInterval(function(){call('/api/state')},1500);
</script>
</body></html>)rawliteral";
