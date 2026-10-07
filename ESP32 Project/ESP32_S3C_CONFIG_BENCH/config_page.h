// Web page for ESP32_S3C_CONFIG_BENCH. Lives in its own header ON PURPOSE: the Arduino IDE scans the .ino for C
// functions to auto-declare and mistakes the JavaScript "function ..." lines inside a raw string for C
// functions. Header files are not scanned.
#pragma once

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>S3C Settings Bench</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:12px;max-width:720px;margin:auto}
 h1{font-size:18px;margin:4px 0 10px}
 h2{font-size:14px;margin:18px 0 6px;color:#e9a65b}
 .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:7px 0}
 label{font-size:13px;color:#ccc}
 button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:8px;padding:9px 11px;font-size:14px}
 button.on{background:#1b6e2e;border-color:#3c3}
 button.go{background:#1e4f8a;border-color:#59f}
 button.sm{padding:6px 8px;font-size:12px}
 button:disabled{opacity:.4}
 input,select{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:7px;font-size:14px}
 input.n{width:74px}
 .box{border:1px solid #444;border-radius:8px;padding:8px;line-height:1.65}
 .note{color:#aaa;font-size:12px;margin:4px 0;line-height:1.5}
 .ok{color:#6d6}.warn{color:#fb4}.err{color:#f66}
 .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
 code{color:#9c9;word-break:break-all;font-size:12px}
 details{margin-top:12px}
</style></head><body>
<h1>S3C settings bench &middot; Box 2 (Heater) <span id="net" class="warn"></span></h1>
<div class="note">A stand-in for the LocalServer's Box 2 settings page. It changes the S3C's saved settings over the air; the S3C keeps them in its own memory. Keep the real LocalServer OFF while this is on.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>

<div class="note"><b>Coin check:</b> on the INSERT COIN screen insert a coin: "pulses C1 has counted" should rise by the coin's value within a second or two, and C1 beeps. Box 2 has no relay to cut the acceptor's power, so it always takes coins - only INSERT COIN and a paid heating credit them. If the count stays the same, the coin signal is not reaching C1. The "coin wire" line counts every pulse edge C1 sees BEFORE its filter: if a 20-peso coin shows 20 edges but only 1 pulse counted, C1's filter is eating them; if it shows 1 edge, the acceptor itself only sent one pulse for that coin.</div>

<h2>Heating (HEAT)</h2>
<div class="row"><label>Helmet counts as inside under</label><input class="n" id="ht_helm" type="number" step="0.5"><label>cm</label><button class="sm" onclick="useLive('ht_helm','helm')">use live</button></div>
<div class="note">Put the helmet in the chamber and tap "use live" to read the distance; set the number a little above it.</div>
<div class="row"><label>Cool down lasts</label><input class="n" id="ht_cool" type="number" min="5" max="900"><label>seconds</label></div>
<div class="note">After every heating, and after an abort, only the fan runs for this long before the door can be opened. Default 60.</div>
<div class="row"><button class="go" onclick="sendHeat()">Send HEAT to S3C</button><button onclick="loadFrom('heat')">Reload from S3C</button><span id="htStatus" class="note"></span></div>

<h2>Rules (time, coins, free retry)</h2>
<div class="note">Rating bands: each rating from 1 to 10 should fall in exactly one band.</div>
<div id="bands"></div>
<div class="row"><label>Shortest session</label><input class="n" id="ru_min" type="number" min="1" max="120"><label>min</label>
 <label>Most coins</label><input class="n" id="ru_max" type="number" min="1" max="999"><span id="ru_maxnote" class="note"></span></div>
<div class="row"><input class="n" id="ru_coins" type="number" min="1"><label>coin(s) =</label><input class="n" id="ru_val" type="number" step="any" min="0.1">
 <select id="ru_unit"><option value="1">minute(s)</option><option value="0">second(s)</option></select></div>
<div class="row"><label><input type="checkbox" id="ru_free"> One free retry after the second rating</label>
 <label>from rating</label><input class="n" id="ru_freeR" type="number" min="1" max="10"><label>for</label><input class="n" id="ru_freeM" type="number" min="1" max="60"><label>min</label></div>
<div class="row"><button class="go" onclick="sendRules()">Send RULES to S3C</button><button onclick="loadFrom('rules')">Reload from S3C</button><span id="ruStatus" class="note"></span></div>

<h2>Maintenance</h2>
<div class="note">Only works when the S3C is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. The enclosure lock rests itself after 60 s. <b>Careful with the heater:</b> it is switched off by itself after 5 minutes on, and leaving Maintenance switches everything off - but keep the door shut and a helmet out of the way when testing it.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==21?0:1))">Maintenance</button></div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3C reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const ST=["IDLE","OPEN A","SENSORS (old)","HEATING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","COOL DOWN","RATE B","CANCEL","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","HELMET NOTICE A","HELMET NOTICE B","OUT OF ORDER"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HEATER","UV LIGHT","FAN"];
const HT=['ht_helm','ht_cool'];
const RU_EXTRA=['ru_min','ru_coins','ru_val','ru_unit','ru_max','ru_free','ru_freeR','ru_freeM'];
let S={}, dirty={heat:false,rules:false}, pend={heat:null,rules:null}, loaded={heat:'',rules:''}, mm=0;
const $=id=>document.getElementById(id);
// band rows
const bandsBox=$('bands');
for(let i=0;i<4;i++){const d=document.createElement('div');d.className='row';
 d.innerHTML='<label>Rating</label><input class="n" id="ru_f'+i+'" type="number" min="1" max="10"><label>to</label><input class="n" id="ru_t'+i+'" type="number" min="1" max="10"><label>suggests</label><input class="n" id="ru_m'+i+'" type="number" min="1" max="120"><label>min</label>';bandsBox.appendChild(d);}
// relay buttons
const rel=$('rel');
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>{mm^=(1<<i);q('/mask?v='+mm);};rel.appendChild(b);});
// mark a form as edited so the live refresh does not overwrite typing
document.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>{const id=el.id;
 if(id.startsWith('ht_'))dirty.heat=true;else if(id.startsWith('ru_'))dirty.rules=true;});});

function setv(id,v){const el=$(id);if(el.type==='checkbox')el.checked=!!(+v);else el.value=v;}
function getv(id){const el=$(id);return el.type==='checkbox'?(el.checked?1:0):(el.value===''?'0':el.value);}
function heatCsv(){return HT.map(getv).join(',');}
// "Most coins" shown as the longest session it allows, and the price, at the coin rate typed above it
function updMaxNote(){const c=+getv('ru_coins'),v=+getv('ru_val'),u=+getv('ru_unit'),m=+getv('ru_max'),el=$('ru_maxnote');
 if(!(c>0&&v>0&&m>0)){el.textContent='';return;}
 const sec=m*v*(u?60:1)/c,mn=sec/60;el.textContent='= '+(mn>=1?(Math.round(mn*10)/10)+' min':Math.round(sec)+' s')+' of time, P'+m+' ('+m+' coins at 1 peso each)';}
setInterval(updMaxNote,400);
function rulesCsv(){const p=[];for(let i=0;i<4;i++){p.push(getv('ru_f'+i),getv('ru_t'+i),getv('ru_m'+i));}RU_EXTRA.forEach(id=>p.push(getv(id)));return p.join(',');}
function fillHeat(csv){const v=csv.split(',');if(v.length!==2)return;HT.forEach((id,i)=>setv(id,v[i]));dirty.heat=false;}
function fillRules(csv){const v=csv.split(',');if(v.length!==20)return;for(let i=0;i<4;i++){setv('ru_f'+i,v[i*3]);setv('ru_t'+i,v[i*3+1]);setv('ru_m'+i,v[i*3+2]);}RU_EXTRA.forEach((id,i)=>setv(id,v[12+i]));dirty.rules=false;}
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function useLive(id,key){const v=S[key];if(!S.c2ok||!(v>0)){alert('No live reading from C2 (or no echo).');return;}setv(id,(+v).toFixed(1));dirty.heat=true;}
async function sendHeat(){const c=heatCsv();pend.heat=c;$('htStatus').textContent='sending...';await q('/send?kind=heat&csv='+encodeURIComponent(c));dirty.heat=false;}
async function sendRules(){const c=rulesCsv();pend.rules=c;$('ruStatus').textContent='sending...';await q('/send?kind=rules&csv='+encodeURIComponent(c));dirty.rules=false;}
function loadFrom(kind){if(kind==='heat'&&S.heat)fillHeat(S.heat);if(kind==='rules'&&S.rules)fillRules(S.rules);}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function render(){
 const L=$('live');
 let h='';
 h+=S.s3cok?('S3C: <b class="ok">online</b> ('+age(S.s3cage)+') &middot; step <b>'+(ST[S.state]||S.state)+'</b>'+(S.timer?(' &middot; '+Math.floor(S.timer/60)+':'+String(S.timer%60).padStart(2,'0')+' left'):'')):'S3C: <b class="err">not heard</b> - is it on, on channel 1, running the new firmware?';
 h+='<br>C2 sensors and relays: '+(S.c2ok?('<b class="ok">ok</b> ('+age(S.c2age)+')'):'<b class="err">silent</b>');
 if(S.c2ok){
  h+='<br>Helmet <b>'+S.helm.toFixed(1)+'</b> cm <span class="note">(0 = no echo)</span> &middot; enclosure door <b>'+(S.door?'OPEN':'shut')+'</b> &middot; panel door '+(S.pan?'open':'shut')+' &middot; backdoor '+(S.bak?'open':'shut');
  h+='<br>Relays on now: '+(RN.filter((n,i)=>(S.mask>>i)&1).join(', ')||'none');
 }
 h+='<br>Coins: C1 '+(S.c1ok?'<b class="ok">ok</b>':'<b class="err">silent</b>')+' &middot; pulses C1 has counted: <b>'+S.coins+'</b>'+(S.coinage>=0?(' (last change '+age(S.coinage)+')'):' (none since C1 started)');
 h+='<br>Coin wire: C1 saw <b>'+S.raw+'</b> pulse edges in total &middot; shortest gap between two <b>'+(S.gap>0?S.gap.toFixed(1)+' ms':'(none yet)')+'</b>'+(S.raw>S.coins?(' &middot; <span class="warn">'+(S.raw-S.coins)+' edge(s) were filtered out or ignored</span>'):'');
 L.innerHTML=h;
 // keep the form in step with the S3C unless the user is typing in it
 if(S.heat&&!dirty.heat&&S.heat!==loaded.heat){fillHeat(S.heat);}
 if(S.rules&&!dirty.rules&&S.rules!==loaded.rules){fillRules(S.rules);}
 loaded.heat=S.heat||'';loaded.rules=S.rules||'';
 // did the S3C accept what was sent?
 ['heat','rules'].forEach(k=>{const el=$(k==='heat'?'htStatus':'ruStatus');
  if(pend[k]){if(S[k]&&same(S[k],pend[k])){el.textContent='S3C has these values ✓';el.className='note ok';pend[k]=null;}
   else el.textContent='waiting for the S3C to confirm...';}});
 $('mt').className=S.state==21?'on':'';$('mt').textContent='Maintenance: '+(S.state==21?'ON':'OFF');
 for(let i=0;i<6;i++){const b=$('r'+i);b.className=((S.mask>>i)&1)?'on':'';b.disabled=S.state!=21;}
 if(S.state!=21)mm=0;
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules : '+(S.rules||'(not received yet)')+'\nheat  : '+(S.heat||'(not received yet)');
}
async function q(u){try{const r=await fetch(u,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
q('/state');setInterval(()=>q('/state'),700);
</script></body></html>)rawliteral";
