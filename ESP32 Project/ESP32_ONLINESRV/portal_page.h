#pragma once
#include <Arduino.h>

// The Online Server's own setup page (captive portal). Served at http://192.168.4.1 after joining the Wi-Fi "JollyHelmet-Online".
// It shows whether the board is connected to the internet / Blynk and lets the user pick ANY Wi-Fi network. Plain HTML + JS, no libraries.
const char PORTAL_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jolly Helmet Online Server</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;background:#fff2c6;color:#1d2433;padding:14px;max-width:520px;margin:0 auto}
h1{font-size:19px;color:#14325c;margin:2px 0 2px}
.sub{color:#5b6678;font-size:13px;margin-bottom:12px}
.card{background:#fff;border-radius:14px;padding:14px;margin-bottom:12px;box-shadow:0 1px 3px rgba(20,50,92,.12)}
.badge{display:inline-block;padding:5px 14px;border-radius:99px;font-weight:800;font-size:14px;letter-spacing:.4px;background:#e9edf4;color:#4d5870}
.badge.ok{background:#e2f3e7;color:#1b7f3b}.badge.bad{background:#fde8e6;color:#b3261e}.badge.wait{background:#e1eefb;color:#1c5fa8}.badge.warn{background:#fff1d0;color:#9a5b00}
.row{display:flex;justify-content:space-between;gap:10px;padding:6px 0;border-top:1px solid #f0e8cf;font-size:14px}
.row span:first-child{color:#5b6678}.row b{text-align:right;word-break:break-word}
label{display:block;font-weight:700;font-size:13px;margin:12px 0 4px;color:#14325c}
input,select{width:100%;font:inherit;padding:10px;border-radius:9px;border:1px solid #d9cfae;background:#fff;color:#1d2433}
button{font:inherit;font-weight:700;border:0;border-radius:10px;padding:10px 16px;cursor:pointer;background:#14325c;color:#fff;margin-top:12px}
button.alt{background:#fff;color:#14325c;border:1px solid #d9cfae;margin-left:6px}
.nets{margin-top:8px;display:grid;gap:5px}
.net{display:flex;justify-content:space-between;padding:8px 10px;border-radius:9px;background:#fff8e1;cursor:pointer;font-size:14px}
.net small{color:#5b6678}
.fine{font-size:12.5px;color:#5b6678;margin-top:8px}
.chk{display:flex;gap:8px;align-items:flex-start;font-weight:400;color:#1d2433;margin:10px 0 0}.chk input{width:auto;margin-top:3px}
#lost{display:none;background:#fff1d0;color:#9a5b00;border-radius:10px;padding:9px 12px;margin-bottom:12px;font-size:13.5px}
</style></head><body>
<h1>Jolly Helmet Online Server</h1>
<div class="sub">Sends the machines' status to Blynk so you can watch them on the web. It only listens to the machines, it never controls them.</div>
<div id="lost">Lost contact with the board for a moment. This is normal while it tests your Wi-Fi: it moves to your router's channel. Keep this page open, it reconnects by itself (if not, join the Wi-Fi <b>JollyHelmet-Online</b> again).</div>

<div class="card">
  <div id="big" class="badge">...</div>
  <div id="why" class="fine"></div>
  <div style="margin-top:8px">
    <div class="row"><span>Wi-Fi network</span><b id="s_wifi">-</b></div>
    <div class="row"><span>Blynk upload</span><b id="s_blynk">-</b></div>
    <div class="row"><span>Machines heard on the radio</span><b id="s_radio">-</b></div>
    <div class="row"><span>Revenue totals (LocalServer)</span><b id="s_stats">-</b></div>
    <div class="row"><span>Clock</span><b id="s_time">-</b></div>
    <div class="row"><span>Next upload</span><b id="s_next">-</b></div>
    <div class="row"><span>Uploads this month</span><b id="s_mq">-</b></div>
    <div class="row"><span>Running for</span><b id="s_up">-</b></div>
  </div>
</div>

<div class="card">
  <b style="color:#14325c">Wi-Fi to use for the internet</b>
  <div><button id="scan" type="button">Search for networks</button><span id="scanmsg" class="fine" style="margin-left:8px"></span></div>
  <div class="nets" id="nets"></div>
  <label for="ssid">Network name (SSID)</label><input id="ssid" autocomplete="off" autocapitalize="none" spellcheck="false">
  <label for="pass">Password</label><input id="pass" type="password" autocomplete="off" placeholder="(leave empty for an open network)">
  <label for="token">Blynk device Auth Token</label><input id="token" type="password" autocomplete="off" spellcheck="false" placeholder="paste the token of this device">
  <label for="iv">Send to Blynk every</label>
  <select id="iv"><option value="30">30 seconds</option><option value="60">1 minute</option><option value="120">2 minutes</option><option value="300">5 minutes</option></select>
  <label class="chk"><input id="extra" type="checkbox"><span>Also send single numbers V1 to V4 for Blynk dashboards (revenue, customers, alerts, boxes running). The datastreams must exist in the template.</span></label>
  <label class="chk"><input id="events" type="checkbox"><span>Send a Blynk event with code <b>alert</b> when something needs attention (empty tank, box out of order...). The event must exist in the template.</span></label>
  <details style="margin-top:12px"><summary style="cursor:pointer;color:#14325c;font-weight:700">More settings</summary>
    <label for="server">Blynk server</label><input id="server" autocomplete="off" spellcheck="false">
    <label for="tz">Time zone (hours from UTC)</label><input id="tz" type="number" step="0.5" min="-12" max="14">
    <label for="mqmax">Blynk messages per month in your plan (0 = do not show)</label><input id="mqmax" type="number" min="0" step="1000">
    <label class="chk"><input id="insecure" type="checkbox"><span>Skip the TLS certificate check (only if every upload fails with a TLS error)</span></label>
  </details>
  <div><button id="save" type="button">Save and connect</button><button id="forget" type="button" class="alt">Forget Wi-Fi</button></div>
  <div class="fine">Saving makes the board test the network right away. The result appears above within about 20 seconds.</div>
</div>
<script>
const $=id=>document.getElementById(id);
async function api(p){const r=await fetch(p,{cache:'no-store'});return r.json();}
function ago(s){s=Math.round(s);if(s<60)return s+' s';if(s<3600)return Math.round(s/60)+' min';return (s/3600).toFixed(1)+' h';}
function up(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m '+(s%60)+'s';}
let filled=false,lastOk=Date.now();
function render(s){
  lastOk=Date.now();$('lost').style.display='none';
  const big=$('big'),why=$('why');
  let cls='badge',txt='',w='';
  if(s.state==='testing'){cls+=' wait';txt='TESTING THE CONNECTION...';w='Joining "'+s.ssid+'" and talking to Blynk.';}
  else if(!s.ssid||!s.hasToken){cls+=' warn';txt='NOT SET UP';w=!s.ssid?'Choose a Wi-Fi network below.':'Paste the Blynk device Auth Token below.';}
  else if(s.blynk.ok&&s.blynk.age<s.iv*3+30){cls+=' ok';txt='ONLINE';w='The last upload to Blynk worked '+ago(s.blynk.age)+' ago.';}
  else if(s.blynk.age<0&&s.wifi.age<0){cls+=' wait';txt='WAITING FOR THE FIRST UPLOAD';w='Next upload in '+s.next+' s.';}
  else{cls+=' bad';txt='NOT CONNECTED';w=(s.wifi.ok?'Wi-Fi joined, but Blynk failed: ':'Could not join the Wi-Fi: ')+(s.wifi.ok?s.blynk.err:s.wifi.err);}
  big.className=cls;big.textContent=txt;why.textContent=w;
  $('s_wifi').textContent=!s.ssid?'none chosen':s.ssid+' - '+(s.wifi.age<0?'not tried yet':s.wifi.ok?'joined ('+s.wifi.ip+', '+s.wifi.rssi+' dBm)':'FAILED: '+s.wifi.err);
  $('s_blynk').textContent=s.blynk.age<0?'not tried yet':(s.blynk.ok?'OK':'FAILED: '+s.blynk.err)+' ('+ago(s.blynk.age)+' ago)'+(s.blynk.note?' - '+s.blynk.note:'');
  $('s_radio').textContent=s.radio.heard+' of 13 boards';
  $('s_stats').textContent=s.radio.stats?'heard '+ago(s.radio.statsAge)+' ago':'not heard yet (is the LocalServer on?)';
  $('s_time').textContent=s.time;
  $('s_next').textContent=s.state==='testing'?'-':s.next+' s';
  $('s_mq').textContent=s.mq+(s.mqmax>0?' of '+s.mqmax:'');
  $('s_up').textContent=up(s.up)+' ('+s.rst+')';
  if(!filled){filled=true;$('ssid').value=s.ssid||'';$('server').value=s.server;$('iv').value=String(s.iv);$('tz').value=s.tz;$('mqmax').value=s.mqmax;
    $('extra').checked=!!s.extra;$('events').checked=!!s.events;$('insecure').checked=!!s.insecure;
    if(s.hasPass)$('pass').placeholder='(unchanged)';if(s.hasToken)$('token').placeholder='(unchanged)';}
}
async function poll(){try{render(await api('/api/status'));}catch(e){if(Date.now()-lastOk>4000)$('lost').style.display='block';}}
setInterval(poll,2000);poll();
$('scan').onclick=async()=>{
  $('scanmsg').textContent='searching...';$('nets').innerHTML='';
  try{await api('/api/scan?start=1');}catch(e){}
  for(let i=0;i<20;i++){
    await new Promise(r=>setTimeout(r,1000));
    let j;try{j=await api('/api/scan');}catch(e){continue;}
    if(j.scanning)continue;
    $('scanmsg').textContent=j.networks.length?j.networks.length+' found':'none found';
    j.networks.forEach(n=>{const d=document.createElement('div');d.className='net';const a=document.createElement('span');a.textContent=n.ssid;const b=document.createElement('small');b.textContent=(n.open?'open ':'')+n.rssi+' dBm';d.append(a,b);d.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus();};$('nets').append(d);});
    return;
  }
  $('scanmsg').textContent='no answer, try again';
};
$('save').onclick=async()=>{
  const q=new URLSearchParams({ssid:$('ssid').value.trim(),iv:$('iv').value,server:$('server').value.trim(),tz:$('tz').value,mqmax:$('mqmax').value||'0',
    extra:$('extra').checked?1:0,events:$('events').checked?1:0,insecure:$('insecure').checked?1:0});
  if($('pass').value)q.set('pass',$('pass').value);if($('token').value.trim())q.set('token',$('token').value.trim());
  if(!$('pass').value&&!$('token').value.trim())q.set('keep','1');
  $('pass').value='';$('token').value='';
  try{await api('/api/save?'+q.toString());}catch(e){}
  $('why').textContent='Saved. Testing now...';
};
$('forget').onclick=async()=>{if(!confirm('Forget the saved Wi-Fi and token?'))return;try{await api('/api/forget');}catch(e){}filled=false;$('ssid').value='';poll();};
</script></body></html>)rawliteral";
