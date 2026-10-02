// Web page for ESP32_A3_BENCH_TEST. Lives in its own header ON PURPOSE: the Arduino IDE scans the .ino
// for C functions to auto-declare, and it mistakes the JavaScript "function ..." lines inside a raw
// string for C functions and injects broken prototypes above it. Header files are not scanned.
#pragma once

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>A3 Bench Test</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:12px;max-width:680px;margin:auto}
 h1{font-size:18px;margin:4px 0 10px}
 h2{font-size:14px;margin:16px 0 6px;color:#9ad}
 .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
 button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:8px;padding:10px 12px;font-size:14px}
 button.on{background:#1b6e2e;border-color:#3c3}
 button.bad{background:#7a1c1c;border-color:#e55}
 button:disabled{opacity:.45}
 select{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:8px;font-size:14px}
 .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
 .cell{border:1px solid #444;border-radius:8px;padding:6px}
 .cell small{display:block;margin-top:4px;color:#aaa}
 .mis{border-color:#e90}
 .box{border:1px solid #444;border-radius:8px;padding:8px;line-height:1.6}
 .note{color:#aaa;font-size:12px;margin:4px 0;line-height:1.5}
 input{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:8px;width:90px;font-size:14px}
 .ok{color:#6d6}.warn{color:#fb4}.err{color:#f66}
</style></head><body>
<h1>Node A3 bench test <span id="net" class="warn"></span></h1>

<h2>Fake S3A</h2>
<div class="row">
 <button id="hb" onclick="q('/set?hb='+(S.hb?0:1))">Heartbeat</button>
 <button id="auto" onclick="q('/set?auto='+(S.auto?0:1))">Re-send every 2 s</button>
 <button id="proto" onclick="q('/set?proto='+(S.old?'new':'old'))">Protocol</button>
</div>
<div class="note" id="protoNote"></div>

<div id="newbox">
 <div class="row">
  <button onclick="q('/set?preset=off')">ALL OFF</button>
  <button onclick="q('/set?preset=alc')">ALCOHOL set</button>
  <button onclick="q('/set?preset=scn')">SCENTED set</button>
  <button onclick="q('/set?preset=bal')">BALANCED set</button>
  <button onclick="if(confirm('Switch ALL 12 relays ON? Check the supply can carry it.'))q('/set?preset=all')">ALL 12 ON</button>
 </div>
</div>

<div id="oldbox" hidden>
 <div class="note">Talks exactly like the S3A on the box today: one command per relay, addressed to the old A2.</div>
 <div class="row" id="lrelays"></div>
 <div class="row">
  <label for="lstate" class="note">Pretend the S3A is in step:</label>
  <select id="lstate" onchange="q('/set?lstate='+this.value)"></select>
 </div>
 <div class="note">Expect: MIST = Humidifier 1 + 2 + 3, UV = UV 1 + UV 2, and COIN POWER on <b>only</b> for step 14 INSERT COIN and step 5 CLEANING (and off again if "none" is chosen).</div>
</div>

<h2>Relays - asked vs what A3 says</h2>
<div class="grid" id="grid"></div>

<h2>Fake A2 (door + helmet) - leave OFF if the real A2 is powered</h2>
<div class="row">
 <button id="a2" onclick="q('/set?a2='+(S.a2?0:1))">A2 sending</button>
 <button id="door" onclick="q('/set?door='+(S.door?0:1))">Door</button>
 <input id="dist" type="number" step="0.5" min="0" max="400" value="0">
 <button onclick="q('/set?dist='+document.getElementById('dist').value)">Set helmet cm</button>
</div>
<div class="box" id="a2box"></div>

<h2>Real A2 (heard over the air)</h2>
<div class="box" id="realbox">...</div>

<h2>What A3 reports</h2>
<div class="box" id="a3box">...</div>
<div class="note" id="diag"></div>

<script>
const NAMES=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1 (ALC)","UV 1","HUMIDIFIER 2 (SCN)","HUMIDIFIER 3 (BOTH)","UV 2","DRAIN PUMP 1","DRAIN PUMP 2","ACS DOOR LOCK","COIN POWER"];
const LREL=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","MIST","UV LIGHT"];
const LST=[[255,'(none - no status sent)'],[10,'TAPS'],[11,'WELCOME'],[12,'RATE A'],[13,'TIME ALLOT'],[14,'INSERT COIN'],[2,'CHECKING'],[1,'INSTRUCTIONS (door)'],[4,'SENSORS'],[5,'CLEANING'],[9,'PAUSED (SAFETY)'],[7,'RETRIEVE'],[8,'FINISH'],[15,'RATE B'],[16,'CANCEL CONFIRM'],[6,'ABORT CONFIRM']];
const WD=["normal","ORPHANED - everything forced OFF","ORPHANED - enclosure held UNLOCKED"];
let S={};
const g=document.getElementById('grid');
NAMES.forEach((n,i)=>{const d=document.createElement('div');d.className='cell';d.id='c'+i;
 d.innerHTML='<button style="width:100%" onclick="q(\'/set?toggle='+i+'\')">'+i+' '+n+'</button><small id="s'+i+'"></small>';g.appendChild(d);});
const lr=document.getElementById('lrelays');
LREL.forEach((n,i)=>{const b=document.createElement('button');b.id='l'+i;b.textContent=n;b.onclick=()=>q('/set?lrelay='+i);lr.appendChild(b);});
const ls=document.getElementById('lstate');
LST.forEach(p=>{const o=document.createElement('option');o.value=p[0];o.textContent=p[0]===255?p[1]:(p[0]+'  '+p[1]);ls.appendChild(o);});
function setBtn(id,on,label){const b=document.getElementById(id);b.className=on?'on':'';b.textContent=label+': '+(on?'ON':'OFF');}
function hex3(v){return '0x'+v.toString(16).toUpperCase().padStart(3,'0');}
function render(){
 setBtn('hb',S.hb,'Heartbeat');setBtn('auto',S.auto,'Re-send every 2 s');setBtn('a2',S.a2,'A2 sending');
 const pb=document.getElementById('proto');pb.className=S.old?'on':'';pb.textContent='Protocol: '+(S.old?'OLD (what the box runs today)':'NEW (relay mask)');
 document.getElementById('protoNote').textContent=S.old?'OLD: per-relay commands to "A2" + a state broadcast every second - this tests the legacy bridge.':'NEW: one 12-bit relay mask - this tests A3 as designed for the new S3A.';
 document.getElementById('newbox').hidden=!!S.old;document.getElementById('oldbox').hidden=!S.old;
 document.getElementById('door').textContent='Door: '+(S.door?'OPEN':'CLOSED');
 document.getElementById('door').className=S.door?'':'on';
 for(let i=0;i<12;i++){
  const want=(S.mask>>i)&1,got=(S.a3mask>>i)&1;
  const c=document.getElementById('c'+i);
  c.firstChild.className=want?'on':'';c.firstChild.disabled=!!S.old;
  c.className='cell'+((S.a3ok&&want!=got)?' mis':'');
  document.getElementById('s'+i).textContent='asked '+(want?'ON':'OFF')+' | A3 says '+(S.a3ok?(got?'ON':'OFF'):'?');
 }
 for(let i=0;i<5;i++){document.getElementById('l'+i).className=((S.lmask>>i)&1)?'on':'';}
 if(document.activeElement!==ls){ls.value=String(S.lstate);}
 document.getElementById('a2box').textContent='Sending: door '+(S.door?'OPEN':'CLOSED')+', helmet '+S.dist.toFixed(1)+' cm'+(S.a2?'':'  (fake A2 is silent)');
 const r=document.getElementById('realbox');
 if(!S.ra2ok){r.innerHTML='<span class="warn">No A2SensorPacket heard yet</span> - is the real A2 powered and on channel 1?';}
 else{
  const dn=['Enclosure','Panel','Backdoor','ACS side'];
  const doors=dn.map((n,i)=>n+' '+(((S.ra2doors>>i)&1)?'<b>OPEN</b>':'shut')).join(' · ');
  const wl=['ALS','SLS'].map((n,i)=>'WS '+n+' raw '+(i?S.ra2raw1:S.ra2raw0)+' ('+(((S.ra2wet>>i)&1)?'<b>WET</b>':'dry')+')').join(' · ');
  r.innerHTML='Last packet '+(S.ra2age/1000).toFixed(1)+' s ago'+(S.ra2age>3000?' <span class="err">(STALE)</span>':'')+
   '<br>Helmet '+S.ra2h.toFixed(1)+' cm · MC ALS '+S.ra2als.toFixed(1)+' cm · MC SLS '+S.ra2sls.toFixed(1)+' cm (0 = no echo)'+
   '<br>Doors: '+doors+'<br>'+wl;
  if(S.lgok){
   const rn=['enc','pan','bak','mist','uv'].map((n,i)=>n+':'+(((S.lgrs>>i)&1)?'ON':'off')).join(' ');
   r.innerHTML+='<br><span class="note">Old-format packet (legacy bridge): '+(S.lgage/1000).toFixed(1)+' s ago · alcohol distance '+S.lgalc.toFixed(1)+' cm (fixed on purpose) · relays mirrored '+rn+'</span>';
  } else {r.innerHTML+='<br><span class="note">No old-format packet (legacy bridge) heard.</span>';}
 }
 document.getElementById('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 const a=document.getElementById('a3box');
 if(!S.a3ok){a.innerHTML='<span class="err">No status from A3 yet</span> - is it powered and on channel 1?';}
 else{
  const stale=S.a3age>3000;
  a.innerHTML='Last status: '+(S.a3age/1000).toFixed(1)+' s ago'+(stale?' <span class="err">(STALE)</span>':'')+
   '<br>Applied mask: '+hex3(S.a3mask)+' (asked '+hex3(S.mask)+')'+
   '<br>Watchdog: <span class="'+(S.a3wd?'err':'ok')+'">'+(WD[S.a3wd]||S.a3wd)+'</span>';
 }
}
async function q(u){try{const r=await fetch(u,{cache:'no-store'});S=await r.json();render();document.getElementById('net').textContent='';}
 catch(e){document.getElementById('net').textContent='(no link)';}}
q('/state');setInterval(()=>q('/state'),500);
</script></body></html>)rawliteral";
