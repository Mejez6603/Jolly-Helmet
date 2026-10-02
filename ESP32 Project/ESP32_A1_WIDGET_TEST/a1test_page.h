// Web page for ESP32_A1_WIDGET_TEST. Lives in its own header ON PURPOSE: the Arduino IDE scans the .ino for C
// functions to auto-declare and mistakes the JavaScript "function ..." lines inside a raw string for C
// functions. Header files are not scanned.
#pragma once

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>A1 Widget Test</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:12px;max-width:680px;margin:auto}
 h1{font-size:18px;margin:4px 0 10px}
 h2{font-size:14px;margin:16px 0 6px;color:#9ad}
 .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
 button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:8px;padding:10px 12px;font-size:14px}
 button.on{background:#1b6e2e;border-color:#3c3}
 select,input{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:8px;font-size:14px}
 select{max-width:100%}
 input.n{width:78px}
 input.t{flex:1;min-width:140px}
 .box{border:1px solid #444;border-radius:8px;padding:8px;line-height:1.6}
 .note{color:#aaa;font-size:12px;margin:4px 0;line-height:1.5}
 pre{white-space:pre-wrap;word-break:break-all;margin:6px 0;font-size:12px;color:#9c9}
 .ok{color:#6d6}.warn{color:#fb4}.err{color:#f66}
</style></head><body>
<h1>A1 widget test <span id="net" class="warn"></span></h1>
<div class="note">This board plays the S3A for A1: it sends screens, the countdown, status lines and counters, and lists what A1 sends back. Keep the real S3A, LocalServer and the other nodes OFF.</div>

<h2>Screen</h2>
<div class="row"><select id="scr"></select></div>
<div class="row">
 <button onclick="q('/do?send='+sel())">Send to A1</button>
 <button onclick="q('/do?hard='+sel())">Send with hard refresh</button>
</div>
<pre id="payload"></pre>

<h2>Countdown and counters (like the S3A's once-a-second update)</h2>
<div class="row">
 <label for="timer" class="note">Time left (s)</label><input id="timer" class="n" type="number" min="0" max="5999" value="300">
 <button onclick="q('/do?timer='+val('timer'))">Set</button>
 <button id="run" onclick="q('/do?run='+(S.run?0:1))">Count down</button>
</div>
<div class="row">
 <label for="minc" class="note">{MINCOINS}</label><input id="minc" class="n" type="number" min="0" max="255" value="15">
 <label for="reqc" class="note">{REQUIREDCOIN}</label><input id="reqc" class="n" type="number" min="0" max="255" value="15">
 <button onclick="q('/do?minc='+val('minc')+'&reqc='+val('reqc'))">Send</button>
</div>
<div class="row">
 <label for="try" class="note">{TRY}</label><input id="try" class="n" type="number" min="0" max="99" value="3">
 <label for="tries" class="note">{TRIES}</label><input id="tries" class="n" type="number" min="0" max="99" value="10">
 <label for="maxc" class="note">{MAXCOINS}</label><input id="maxc" class="n" type="number" min="0" max="999" value="60">
 <button onclick="q('/do?try='+val('try')+'&tries='+val('tries')+'&maxc='+val('maxc'))">Send</button>
</div>

<h2>Status lines ({STATUS}, {STATUS2}, {STATUS3} - newest first)</h2>
<div class="row"><input id="st0" class="t" maxlength="31" value="HUMID ALC: Misting"></div>
<div class="row"><input id="st1" class="t" maxlength="31" value="UV: ON"></div>
<div class="row"><input id="st2" class="t" maxlength="31" value="COIN INSERTED"></div>
<div class="row">
 <button onclick="q('/do?st0='+enc('st0')+'&st1='+enc('st1')+'&st2='+enc('st2'))">Send lines</button>
 <button onclick="q('/do?st0=&st1=&st2=')">Clear lines</button>
</div>

<h2>Other</h2>
<div class="row">
 <button onclick="q('/do?resetcoins=1')">Reset A1's coin count</button>
 <button onclick="q('/do?beep=1')">Beep A1</button>
</div>

<h2>Heard from A1</h2>
<div class="box" id="a1box">...</div>
<div class="note" id="diag"></div>

<script>
let S={};
const scr=document.getElementById('scr');
function sel(){return scr.value;}
function val(id){return encodeURIComponent(document.getElementById(id).value);}
function enc(id){return encodeURIComponent(document.getElementById(id).value);}
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;');}
async function loadScreens(){
 try{const r=await fetch('/screens',{cache:'no-store'});const names=await r.json();
  names.forEach((n,i)=>{const o=document.createElement('option');o.value=i;o.textContent=(i+1)+'. '+n;scr.appendChild(o);});
  showPayload();
 }catch(e){setTimeout(loadScreens,1000);}
}
async function showPayload(){
 try{const r=await fetch('/payload?i='+scr.value,{cache:'no-store'});document.getElementById('payload').textContent=await r.text();}catch(e){}
}
scr.onchange=showPayload;
function render(){
 const run=document.getElementById('run');run.className=S.run?'on':'';run.textContent='Count down: '+(S.run?'ON':'OFF');
 const a=document.getElementById('a1box');
 let h='';
 if(!S.a1ok){h='<span class="warn">No telemetry from A1 yet</span> - is it powered, on channel 1, and running the new firmware?';}
 else{h='A1 last heard '+(S.a1age/1000).toFixed(1)+' s ago'+(S.a1age>3000?' <span class="err">(STALE)</span>':'')+' · coin pulses counted: <b>'+S.coins+'</b>';}
 h+='<br><span class="note">Touch actions A1 has sent (newest first). The number after "value" is the rating (rating screens) or the seconds ("NP_OK"):</span>';
 h+='<br>'+(S.log&&S.log.length?S.log.map(esc).join('<br>'):'<span class="note">none yet - tap something on A1</span>');
 a.innerHTML=h;
 document.getElementById('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
}
async function q(u){try{const r=await fetch(u,{cache:'no-store'});S=await r.json();render();document.getElementById('net').textContent='';}
 catch(e){document.getElementById('net').textContent='(no link)';}}
loadScreens();q('/state');setInterval(()=>q('/state'),700);
</script></body></html>)rawliteral";
