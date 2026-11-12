// Started on 2026-10-03 by tools\build_hub_pages.ps1, but EDITED BY HAND since (screen refresh, beep rows, ...): this file is now the master - do not run that script again.
// 2026-11-12: the three box pages (Box 1, Box 2, Box 3) are now ONE page, PAGE_BOX - all three boxes run the same program. It reads its box number from its address
// (/box1, /box2, /box3) and fills in the box's board names ({S3} {TERM} {SENS} {REL}) itself. The Refilling Station page is unchanged.
// The look (CSS, fold-able cards, Live panel rows) is in hub_theme.h and reaches the pages as /hub.css and /hub.js.
// Each settings page talks to the LocalServer under /api/b1, /api/b2, /api/b3 and /api/refill (see settings_hub.h).
#pragma once
#include <Arduino.h>
const char PAGE_BOX[] PROGMEM = R"hubpage(<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Box Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body><div id="pg"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a href="/box2">Box 2</a><a href="/box3">Box 3</a><a href="/refill">Refilling Station</a></div>
<h1>{BOX} settings &middot; Cleaning + Drying <span id="net" class="warn"></span></h1>
<div class="note">Changes the {S3}'s saved settings over the air; the {S3} keeps them in its own memory, so nothing is lost when this board is switched off.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>

<div class="note"><b>Coin check:</b> on the INSERT COIN screen the acceptor power should read ON. Insert a coin: "pulses {TERM} has counted" should rise by the coin's value within a second or two, and {TERM} beeps. If it stays the same, the coin signal is not reaching {TERM}. The "coin wire" line counts every pulse edge {TERM} sees BEFORE its filter: if a 20-peso coin shows 20 edges but only 1 pulse counted, {TERM}'s filter is eating them; if it shows 1 edge, the acceptor itself only sent one pulse for that coin.</div>

<h2>Rules (coins and time)</h2>
<div class="note">{BOX} has no ratings and no time picker. On the INSERT COIN screen the customer pays the <b>required coins</b> (one step of the rate below, for example 15 coins), and the cleaning or the drying then runs for the time the coins bought. More coins can be added while it runs. <b>Most coins</b> is the most one session can take.</div>
<div class="row"><input class="n" id="ru_coins" type="number" min="1"><label>coin(s) =</label><input class="n" id="ru_val" type="number" step="any" min="0.1">
 <select id="ru_unit"><option value="1">minute(s)</option><option value="0">second(s)</option></select></div>
<div class="row"><label>Most coins</label><input class="n" id="ru_max" type="number" min="1" max="999"><span id="ru_maxnote" class="note"></span></div>
<div class="row"><button class="go" onclick="sendRules()">Send RULES to {S3}</button><button onclick="loadFrom('rules')">Reload from {S3}</button><span id="ruStatus" class="note"></span></div>

<h2>Sensors and the Refilling Station</h2>
<div class="row"><label>Helmet counts as inside under</label><input class="n" id="se_helm" type="number" step="0.5" min="1" max="200"><label>cm</label><button class="sm" onclick="useLive('se_helm','helm')">use live</button></div>
<div class="row"><label>The Refilling Station counts as offline after</label><input class="n" id="se_off" type="number" min="5" max="120"><label>seconds</label></div>
<div class="note">On the CHECKING screen (2 seconds after the first tap) the box reads its Main Container from the Refilling Station. If the station is not heard for this long, or the container is EMPTY or LOW, only DRYING is offered and CLEANING is greyed out. The refilling itself, the levels and the valve test are on the <a href="/refill" style="color:var(--accent)">Refilling Station</a> page. Distances are what the helmet ultrasonic reads, in cm from the sensor: tap "use live" with a helmet inside.</div>
<div class="row"><button class="go" onclick="sendSensors()">Send SENSORS to {S3}</button><button onclick="loadFrom('sensors')">Reload from {S3}</button><span id="seStatus" class="note"></span></div>

<h2>Auto-dry after a cleaning</h2>
<div class="row"><label>Drying lasts</label><input class="n" id="ad_sec" type="number" min="10" max="900"><label>seconds</label>
 <label>heater OFF for the last</label><input class="n" id="ad_tail" type="number" min="0" max="900"><label>seconds</label></div>
<div class="note">Every cleaning ends with this drying: UV 1 + UV 2 + the heater, and the heater switches OFF for its last seconds so the helmet is not handed over hot. A cancel during a cleaning (CONFIRM) jumps straight to it, because the helmet is still wet. Default 180 s with the heater off for the last 30 s.</div>
<div class="row"><button class="go" onclick="sendDry()">Send AUTO-DRY to {S3}</button><button onclick="loadFrom('dry')">Reload from {S3}</button><span id="adStatus" class="note"></span></div>

<h2>Drying</h2>
<div class="row"><label>Cool down lasts</label><input class="n" id="dr_cool" type="number" min="5" max="900"><label>seconds</label></div>
<div class="note">After every drying, and after a cancel during a drying, nothing runs for this long before the door can be opened ({BOX} has no fan). Default 60. The drying length itself is what the customer's coins bought (see Rules).</div>
<div class="row"><button class="go" onclick="sendDrying()">Send DRYING to {S3}</button><button onclick="loadFrom('drying')">Reload from {S3}</button><span id="drStatus" class="note"></span></div>

<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div><div class="note">Restarts the box's touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages and when the heater switches on or off - this is the manual button for any other time.</div><div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div><div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div><div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div><div id="rfRows"></div><div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'')">Reload</button><span id="rfCfgStatus" class="note"></span></div><h2>OPEN A / OPEN B beep</h2><div class="row"><label>Beep for</label><input class="n" id="bp_len" type="number" min="20" max="1000"><label>ms, then wait</label><input class="n" id="bp_wait" type="number" min="0" max="10000"><label>ms</label><span class="note">(wait 0 = one beep only)</span><button class="go" onclick="sendBeep()">Send</button><span id="bpStatus" class="note"></span></div><div class="note">While the OPEN A or OPEN B step waits for the door, the box beeps like this (beep, wait, beep, wait ...) until the door opens. The box keeps the values in its own memory. Default: beep 100 ms, then wait 500 ms.</div><h2>Maintenance</h2>
<div class="note">Only works when the {S3} is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. With the safety on (default) each switches itself off after 2 s; with the bypass ticked the enclosure lock still rests itself after 60 s. <b>Careful with the heater:</b> with the bypass a hand-set heater switches itself off after 5 minutes, and leaving Maintenance switches everything off - keep the door shut and a helmet out of the way when testing it.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==25?0:1))">Maintenance</button></div>
<div class="row"><label class="by"><input type="checkbox" id="mbypass"> Bypass Safety 2 Seconds Relay</label><span id="mbNote" class="note warn"></span></div>
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. <b>The coin power is not part of it</b> - it stays on until you switch it off. Ticking the box turns the safety off for everything until you untick it or leave maintenance.</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the {S3} reports them</summary><div class="box"><code id="raw"></code></div></details>
</div>
<script>
// ONE PAGE FOR ALL THREE BOXES (2026-11-12): /box1, /box2 and /box3 serve this same page; it reads its box number from the address. {BOX} {S3} {TERM} {SENS} {REL} in the text = this box's names.
const BN=parseInt((location.pathname.match(/box(\d)/)||[0,3])[1],10);                  // 1, 2 or 3
const BX=[['S3A','A1','A2','A3'],['S3C','C1','C2','C3'],['S3B','B1','B2','B3']][BN-1];
const T=s=>String(s).split('{BOX}').join('Box '+BN).split('{S3}').join(BX[0]).split('{TERM}').join(BX[1]).split('{SENS}').join(BX[2]).split('{REL}').join(BX[3]);
const API='/api/b'+BN;
document.title=T('{BOX} Settings');document.body.classList.add('t-b'+BN);
(function(){const p=document.getElementById('pg');p.innerHTML=T(p.innerHTML);const a=p.querySelector('.hubnav a[href="/box'+BN+'"]');if(a)a.className='cur';})();
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
// the box's state numbers (the same in all three boxes; a number the page does not know shows as the number; "(unused)" = steps the 36-step flow no longer has)
const ST=["IDLE","OPEN A","CHECKING","REFILLING (unused)","SENSORS (unused)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A (unused)","TIME ALLOT (unused)","INSERT COIN","RATE B (unused)","CANCEL","METHOD (unused)","CHECKING S (unused)","REFILLING S (unused)","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY (unused)","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1 (unused)","AUTO DRY","START DRYING"];
const RFN=["EMPTY","LOW","MID","HIGH","MAX"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1","UV 1","HUMIDIFIER 2","HUMIDIFIER 3","UV 2","(UNUSED)","(UNUSED)","SIDE LOCK","COIN POWER","HEATER"];
const RU=['ru_coins','ru_val','ru_unit','ru_max'];           // {S3} subIndex 1: rateCoins,rateValue,rateUnitMin,maxCoins
const SE=['se_helm','se_off'];                                // subIndex 2: helmetCm,offlineSec
const AD=['ad_sec','ad_tail'];                                // subIndex 3: autoDrySec,heaterOffTailSec
const KEY={rules:'rules',sensors:'levels',dry:'phases',drying:'drying'};   // which field of the answer carries each list (the hub keeps the old names)
let S={}, dirty={rules:false,sensors:false,dry:false,drying:false}, pend={rules:null,sensors:null,dry:null,drying:null}, loaded={rules:'',sensors:'',dry:'',drying:''}, mm=0;
const $=id=>document.getElementById(id);
// relay buttons (the two unused drain-pump slots stay hidden)
const rel=$('rel');
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>relClick(i);if(n==='(UNUSED)')b.style.display='none';rel.appendChild(b);});
// maintenance safety: each hand-set relay switches itself off after 2 s (the S3 does it)
const MAINT_ST=25;let lastClick=0,byTouch=0;
const byEl=$('mbypass');
function sendMask(){return q('/mask?v='+mm+'&by='+(byEl.checked?1:0));}
function relClick(i){mm^=(1<<i);lastClick=Date.now();sendMask();}
byEl.addEventListener('change',()=>{byTouch=Date.now();byEl.parentElement.classList.toggle('active',byEl.checked);sendMask();});
function bypassSync(){const m=S.state==MAINT_ST;byEl.disabled=!m;
 if(!m)byEl.checked=false;else if(Date.now()-byTouch>1500)byEl.checked=!!S.by;
 byEl.parentElement.classList.toggle('active',byEl.checked);$('mbNote').textContent=byEl.checked?'safety OFF - relays stay on until you switch them off':'';}
// mark a form as edited so the live refresh does not overwrite typing
document.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>{const id=el.id;
 if(id.startsWith('ru_'))dirty.rules=true;else if(id.startsWith('se_'))dirty.sensors=true;else if(id.startsWith('ad_'))dirty.dry=true;else if(id.startsWith('dr_'))dirty.drying=true;});});

function setv(id,v){const el=$(id);if(el.type==='checkbox')el.checked=!!(+v);else el.value=v;}
function getv(id){const el=$(id);return el.type==='checkbox'?(el.checked?1:0):(el.value===''?'0':el.value);}
function csvOf(ids){return ids.map(getv).join(',');}
function fillIds(ids,csv,kind){const v=csv.split(',');if(v.length!==ids.length)return;ids.forEach((id,i)=>setv(id,v[i]));dirty[kind]=false;}
function updMaxNote(){const c=+getv('ru_coins'),v=+getv('ru_val'),u=+getv('ru_unit'),m=+getv('ru_max'),el=$('ru_maxnote');
 if(!(c>0&&v>0&&m>0)){el.textContent='';return;}
 const sec=m*v*(u?60:1)/c,mn=sec/60;el.textContent='= '+(mn>=1?(Math.round(mn*10)/10)+' min':Math.round(sec)+' s')+' of time, P'+m+' ('+m+' coins at 1 peso each)';}
setInterval(updMaxNote,400);
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function useLive(id,key){const v=S[key];if(!S.a2ok||!(v>0)){alert(T('No live reading from {SENS} (or no echo).'));return;}setv(id,(+v).toFixed(1));dirty.sensors=true;}
async function send(kind,csv,statusId){pend[kind]=csv;$(statusId).textContent='sending...';$(statusId).className='note';await q('/send?kind='+kind+'&csv='+encodeURIComponent(csv));dirty[kind]=false;}
function sendRules(){send('rules',csvOf(RU),'ruStatus');}
function sendSensors(){send('sensors',csvOf(SE),'seStatus');}
function sendDry(){send('dry',csvOf(AD),'adStatus');}
function sendDrying(){send('drying',getv('dr_cool'),'drStatus');}
const FILL={rules:c=>fillIds(RU,c,'rules'),sensors:c=>fillIds(SE,c,'sensors'),dry:c=>fillIds(AD,c,'dry'),drying:c=>fillIds(['dr_cool'],c,'drying')};
const ST_EL={rules:'ruStatus',sensors:'seStatus',dry:'adStatus',drying:'drStatus'};
function loadFrom(kind){if(S[KEY[kind]])FILL[kind](S[KEY[kind]]);}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function render(){
 const L=$('live');
 let h='';
 h+=S.s3ok?('{S3}: <b class="ok">online</b> ('+age(S.s3age)+') · step <b>'+(ST[S.state]||S.state)+'</b>'+(S.timer?(' · '+Math.floor(S.timer/60)+':'+String(S.timer%60).padStart(2,'0')+' left'):'')):'{S3}: <b class="err">not heard</b> - is it on, on channel 1, running the new firmware?';
 h+='<br>{SENS} sensors: '+(S.a2ok?('<b class="ok">ok</b> ('+age(S.a2age)+')'):'<b class="err">silent</b>');
 h+=' · {REL} relays: '+(S.a3ok?('<b class="ok">ok</b>'+(S.a3orph?' <span class="err">({REL} sees no {S3})</span>':'')+' · on: 0x'+S.a3mask.toString(16).toUpperCase().padStart(4,'0')):'<b class="err">silent</b>');
 h+='<br>Coins: {TERM} '+(S.a1ok?'<b class="ok">ok</b>':'<b class="err">silent</b>')+' · pulses {TERM} has counted: <b>'+S.coins+'</b>'+(S.coinage>=0?(' (last change '+age(S.coinage)+')'):' (none since {TERM} started)')+' · acceptor power from {REL}: <b>'+(((S.a3mask>>11)&1)?'ON':'off')+'</b>';
 h+='<br>Coin wire: {TERM} saw <b>'+S.raw+'</b> pulse edges in total · shortest gap between two <b>'+(S.gap>0?S.gap.toFixed(1)+' ms':'(none yet)')+'</b>'+(S.raw>S.coins?(' · <span class="warn">'+(S.raw-S.coins)+' edge(s) were filtered out or ignored</span>'):'');
 if(S.a2ok)h+='<br>Helmet <b>'+S.helm.toFixed(1)+'</b> cm <span class="note">(0 = no echo)</span> · doors: enclosure <b>'+(S.door?'OPEN':'shut')+'</b>, panel <b>'+(S.pan?'OPEN':'shut')+'</b>, back <b>'+(S.bak?'OPEN':'shut')+'</b>, side <b>'+(S.side?'OPEN':'shut')+'</b>';
 const R=S.rf||[0,0,255,0,0,0];
 h+='<br>Refilling Station: '+(R[0]?('<b class="ok">online</b> · {BOX} container <b>'+(RFN[R[2]]||(R[2]===254?'NOT WATCHED':'SENSOR FAULT'))+'</b> (raw '+R[1]+')'+(R[3]?' · valve <b>OPEN</b> (refilling)':'')+(R[4]?' · waiting for its turn':'')+(R[5]?' · <span class="err">last refill FAILED</span>':'')):'<b class="err">not heard</b> - {BOX} offers DRYING only');
 L.innerHTML=hubLines(T(h));
 // keep the forms in step with the {S3} unless the user is typing in them
 Object.keys(KEY).forEach(k=>{const v=S[KEY[k]];if(v&&!dirty[k]&&v!==loaded[k])FILL[k](v);loaded[k]=v||'';
  if(pend[k]){const el=$(ST_EL[k]);if(v&&same(v,pend[k])){el.textContent=T('{S3} has these values ✓');el.className='note ok';pend[k]=null;}else el.textContent=T('waiting for the {S3} to confirm (or it refused - see its serial log)...');}});
 $('mt').className=S.state==25?'on':'';$('mt').textContent='Maintenance: '+(S.state==25?'ON':'OFF');
 for(let i=0;i<13;i++){const b=$('r'+i);b.className=((S.a3mask>>i)&1)?'on':'';b.disabled=S.state!=25;}
 if(S.state!=25)mm=0;else if(Date.now()-lastClick>1500)mm=S.a3mask&0x1FFF;
 bypassSync();
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules   : '+(S.rules||'(not received yet)')+'\nsensors : '+(S.levels||'(not received yet)')+'\nauto-dry: '+(S.phases||'(not received yet)')+'\ndrying  : '+(S.drying||'(not received yet)');
}
async function q(u){try{const r=await fetch(API+u,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
q('/state');setInterval(()=>q('/state'),700);
let rfDirty=false,rfPend=null,rfLoaded='';
function buildRefreshRows(){const box=$('rfRows');RN.forEach((n,i)=>{const d=document.createElement('div');d.className='row';
 d.innerHTML='<b style="min-width:170px">'+n+'</b><label><input type="checkbox" id="rf_on'+i+'"> when it turns ON</label> <label><input type="checkbox" id="rf_off'+i+'"> when it turns OFF</label>';box.appendChild(d);});
 document.querySelectorAll('#rfRows input').forEach(el=>el.addEventListener('input',()=>{rfDirty=true;}));}
function rfCsv(){let on=0,off=0;RN.forEach((n,i)=>{if($('rf_on'+i).checked)on|=(1<<i);if($('rf_off'+i).checked)off|=(1<<i);});return on+','+off;}
function rfFill(csv){const v=(csv||'').split(',');if(v.length!==2)return;const on=+v[0],off=+v[1];RN.forEach((n,i)=>{$('rf_on'+i).checked=!!((on>>i)&1);$('rf_off'+i).checked=!!((off>>i)&1);});rfDirty=false;}
async function sendRefreshCfg(){rfPend=rfCsv();const el=$('rfCfgStatus');el.className='note';el.textContent='sending...';await q('/send?kind=refresh&csv='+encodeURIComponent(rfPend));rfDirty=false;}
function rfSync(){if(S.refresh&&!rfDirty&&S.refresh!==rfLoaded)rfFill(S.refresh);rfLoaded=S.refresh||'';
 if(rfPend){const el=$('rfCfgStatus');if(S.refresh&&S.refresh===rfPend){el.textContent='the box has these values ✓';el.className='note ok';rfPend=null;}else el.textContent='waiting for the box to confirm (or it refused - see its serial log)...';}}
let arfDirty=false,arfPend=null,arfLoaded='';
function arfSync(){const el=$('arf_sec');
 if(S.autorf!==undefined&&S.autorf!==''&&!arfDirty&&S.autorf!==arfLoaded&&document.activeElement!==el)el.value=S.autorf;
 arfLoaded=S.autorf||'';
 if(arfPend!==null){const st=$('arfStatus');if(S.autorf===String(arfPend)){st.textContent='the box has this value ✓';st.className='note ok';arfPend=null;}else st.textContent='waiting for the box to confirm (or it refused - see its serial log)...';}}
async function sendAutoRefresh(){const el=$('arf_sec');let v=parseInt(el.value||'0',10);if(isNaN(v)||v<0)v=0;if(v>60)v=60;el.value=v;arfPend=v;
 const st=$('arfStatus');st.className='note';st.textContent='sending...';await q('/send?kind=autorefresh&csv='+v);arfDirty=false;}
$('arf_sec').addEventListener('input',()=>{arfDirty=true;});
buildRefreshRows();
let bpDirty=false,bpPend=null,bpLoaded='';
function beepSync(){const a=$('bp_len'),b=$('bp_wait');
 if(S.beep!==undefined&&S.beep!==''&&!bpDirty&&S.beep!==bpLoaded&&document.activeElement!==a&&document.activeElement!==b){const v=S.beep.split(',');if(v.length===2){a.value=v[0];b.value=v[1];}}
 bpLoaded=S.beep||'';
 if(bpPend!==null){const st=$('bpStatus');if(S.beep===bpPend){st.textContent='the box has these values ✓';st.className='note ok';bpPend=null;}else st.textContent='waiting for the box to confirm (or it refused - see its serial log)...';}}
async function sendBeep(){const a=$('bp_len'),b=$('bp_wait');let l=parseInt(a.value||'100',10),w=parseInt(b.value||'0',10);if(isNaN(l)||l<20)l=20;if(l>1000)l=1000;if(isNaN(w)||w<0)w=0;if(w>10000)w=10000;a.value=l;b.value=w;bpPend=l+','+w;
 const st=$('bpStatus');st.className='note';st.textContent='sending...';await q('/send?kind=beep&csv='+encodeURIComponent(bpPend));bpDirty=false;}
$('bp_len').addEventListener('input',()=>{bpDirty=true;});$('bp_wait').addEventListener('input',()=>{bpDirty=true;});
const render0=render;render=function(){render0();rfSync();arfSync();beepSync();};
</script></body></html>
)hubpage";

const char PAGE_REFILL[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Refilling Station</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-refill"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a href="/box2">Box 2</a><a href="/box3">Box 3</a><a class="cur" href="/refill">Refilling Station</a></div>
<h1>Refilling Station <span id="net" class="warn"></span></h1>
<div class="note">One ESP32 with three water-level sensors and three valves, one pair for each box's Main Container. It works <b>alone</b>: the boxes never ask it for anything, they only read its levels. This page changes the station's saved settings over the air; the station keeps them in its own memory.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>
<div class="grid" id="conts"></div>
<div class="note"><b>The station watches and refills all three boxes</b> (a box shows NOT WATCHED only if its entry in the station firmware's WATCH_BOX is set to false, for example while its sensor or valve is not wired yet). <b>One valve at a time</b> (the three boxes share one water supply). A container that is <b>EMPTY</b> or <b>LOW</b> joins the queue: EMPTY containers go first, LOW second, and inside each group the one that waited longest goes first. A refill that is running is never interrupted, and a refill stops when the container reaches MAX. If the level does not rise (nothing arrives), the refill is called FAILED, the valve closes, and the station tries again after the retry time, for ever. <b>SENSOR FAULT</b> means the station found the container's water-level sensor unusable (it reads the 3.3 V rail, or its reading jumps about wildly because the wire is loose or floating): that box is not refilled, its box offers drying only, and it clears by itself 10 seconds after the reading is normal. A dry sensor that simply reads 0 cannot be told from a dead one: that shows up as a refill that does not rise (FAILED).</div>

<h2>Levels and refilling (per box)</h2>
<div class="note">The sensor's raw value is 0 when dry and grows as the liquid rises. <b>EMPTY</b> = at or under "empty up to"; <b>LOW</b> = above that, up to "MID starts"; <b>MID</b> = MID starts .. HIGH starts; <b>HIGH</b> = HIGH starts .. MAX starts; <b>MAX</b> = MAX starts and above. Defaults: 0 / 500 / 1000 / 1200. The refill starts at EMPTY or LOW and stops at MAX. "Waits for a rise" is how long the level may stay flat before the refill is called failed: 30 s when the container is EMPTY (the liquid has to reach the sensor first), only 5 s when it is LOW (it already reads, so a flat level means nothing is coming). "Counts as rising" is the smallest step in the raw value that counts as a rise.</div>
<div id="boxforms"></div>
<div class="row"><button onclick="copyFrom1()">Copy Box 1's numbers into Box 2 and Box 3</button><span class="note">(only fills the boxes below - tap Send on each)</span></div>

<h2>Manual valve test</h2>
<div class="row"><label>A test opens a valve for</label><input class="n" id="ts_sec" type="number" min="1" max="30"><label>seconds</label><button class="go" onclick="sendTest()">Send</button><span id="tsStatus" class="note"></span></div>
<div class="note">Use the <b>Test valve</b> button of a container above to open its valve by hand, for example to check the plumbing. The valve closes by itself after this many seconds. It only works while the station is idle: when a refill is running the station refuses (the button is greyed out and says busy).</div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the Refilling Station reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/refill';
const RFN=["EMPTY","LOW","MID","HIGH","MAX"];
const F=[['em','Empty up to',0,4095],['mid','MID starts',1,4095],['high','HIGH starts',1,4095],['max','MAX starts',1,4095],['se','Waits for a rise when EMPTY',1,600],['sl','Waits for a rise when LOW',1,600],['cap','Valve open at most',10,1800],['rt','Tries again after',5,3600],['rise','Counts as rising',1,500]];
const FU=['','','','','s','s','s','s',''];                       // units shown after each field
const UNITS=['raw','raw','raw','raw','seconds','seconds','seconds','seconds','raw'];
let S={}, dirty=[false,false,false], pend=[null,null,null], loaded=['','',''], tsDirty=false, tsPend=null, tsLoaded='';
const $=id=>document.getElementById(id);
// one form per box
const forms=$('boxforms');
for(let n=1;n<=3;n++){
 const d=document.createElement('div');d.className='cont';d.id='bf'+n;
 let h='<b>Box '+n+'</b> <span class="note">(sensor '+n+' &middot; valve '+n+')</span>';
 F.forEach((f,i)=>{h+='<div class="row"><label style="min-width:210px">'+f[1]+'</label><input class="n" id="b'+n+'_'+f[0]+'" type="number" min="'+f[2]+'" max="'+f[3]+'"><label>'+UNITS[i]+'</label></div>';});
 h+='<div class="note err" id="b'+n+'_warn"></div>';
 h+='<div class="row"><button class="go" onclick="sendBox('+n+')">Send Box '+n+' to the station</button><button onclick="loadBox('+n+')">Reload</button><span id="b'+n+'_st" class="note"></span></div>';
 d.innerHTML=h;forms.appendChild(d);
}
document.querySelectorAll('#boxforms input').forEach(el=>el.addEventListener('input',()=>{const n=+el.id.charAt(1);dirty[n-1]=true;checkBox(n);}));
$('ts_sec').addEventListener('input',()=>{tsDirty=true;});
function getv(id){const el=$(id);return el.value===''?'0':el.value;}
function csvBox(n){return F.map(f=>getv('b'+n+'_'+f[0])).join(',');}
function fillBox(n,csv){const v=csv.split(',');if(v.length!==F.length)return;F.forEach((f,i)=>{$('b'+n+'_'+f[0]).value=v[i];});dirty[n-1]=false;checkBox(n);}
function checkBox(n){const v=F.map(f=>+getv('b'+n+'_'+f[0]));let w='';
 if(!(v[0]<v[1]&&v[1]<v[2]&&v[2]<v[3]))w='The levels must go up: empty up to < MID starts < HIGH starts < MAX starts.';
 $('b'+n+'_warn').textContent=w;}
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function sendBox(n){const c=csvBox(n);pend[n-1]=c;const el=$('b'+n+'_st');el.textContent='sending...';el.className='note';q('/send?kind=box'+n+'&csv='+encodeURIComponent(c));dirty[n-1]=false;}
function loadBox(n){const c=S['b'+n];if(c)fillBox(n,c);}
function copyFrom1(){const c=csvBox(1);for(let n=2;n<=3;n++){fillBox(n,c);dirty[n-1]=true;}}
function sendTest(){let v=parseInt($('ts_sec').value||'2',10);if(isNaN(v)||v<1)v=1;if(v>30)v=30;$('ts_sec').value=v;tsPend=String(v);
 const el=$('tsStatus');el.textContent='sending...';el.className='note';q('/send?kind=test&csv='+v);tsDirty=false;}
function testValve(i,on){q('/test?i='+i+'&on='+(on?1:0));}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function stName(s){return s===254?'NOT WATCHED':(s===255?'SENSOR FAULT':(RFN[s]||'?'));}
function stClass(s){return s>=254?'sx':'s'+s;}
function renderConts(){
 const box=$('conts');
 if(!box.children.length){for(let i=0;i<3;i++){const d=document.createElement('div');d.className='cont';d.id='ct'+i;box.appendChild(d);}}
 for(let i=0;i<3;i++){
  const ok=S.ok,st=S.st[i],raw=S.raw[i],valve=(S.valve>>i)&1,queued=(S.queue>>i)&1,failed=(S.fail>>i)&1,test=(S.test>>i)&1,mx=S.max[i]||1;
  const off=ok&&st===254;
  const pct=ok&&st<254?Math.max(2,Math.min(100,Math.round(100*raw/mx))):0;
  let h='<b>Box '+(i+1)+'</b> &middot; '+(ok?'<span class="chip lv '+stClass(st)+'">'+stName(st)+'</span>':'<span class="chip">NOT HEARD</span>');
  h+='<div class="bar lv '+stClass(st)+'"><i style="width:'+pct+'%"></i></div>';
  h+='<div class="note">'+(off?'The station does not watch this box (its WATCH_BOX entry in the station firmware is false, e.g. the sensor or valve is not wired yet).':'raw '+(ok?raw:'-')+' &middot; MID '+S.mid[i]+' &middot; HIGH '+S.high[i]+' &middot; MAX '+S.max[i])+'</div>';
  let c='';
  if(ok&&!off){
   if(valve)c+='<span class="chip run">'+(test?'valve OPEN (test)':'valve OPEN (refilling)')+'</span>';
   if(queued)c+='<span class="chip">waiting for its turn</span>';
   if(failed)c+='<span class="chip bad">last refill FAILED</span>';
   if(!valve&&!queued&&!failed)c+=(st===0||st===1)?'<span class="chip">needs a refill</span>':'<span class="chip good">'+(st===4?'full':'no refill needed')+'</span>';
  }
  h+='<div>'+c+'</div>';
  const busy=S.valve!==0,stopping=!!(valve&&test);
  h+='<div class="row"><button '+((!ok||off||(busy&&!stopping))?'disabled':'')+' onclick="testValve('+i+','+(stopping?0:1)+')">'+(off?'Not watched':(stopping?'Stop the test':(busy?(valve?'Busy (refilling)':'Busy (another valve is open)'):'Test valve '+(i+1))))+'</button></div>';
  $('ct'+i).innerHTML=h;
 }
}
function render(){
 const L=$('live');
 let h=S.ok?('Refilling Station: <b class="ok">online</b> ('+age(S.age)+') &middot; '+(S.valve?('valve <b>'+[1,2,3].filter(n=>(S.valve>>(n-1))&1).join(', ')+'</b> open'+((S.test)?' (manual test)':'')):'<b>idle</b> - no valve open')+(S.queue?(' &middot; waiting: Box '+[1,2,3].filter(n=>(S.queue>>(n-1))&1).join(', ')):'')):'Refilling Station: <b class="err">not heard</b> - is it on, on channel 1, running the ESP32_REFILL firmware? The boxes offer DRYING only while it is silent.';
 L.innerHTML=hubLines(h);
 renderConts();
 for(let n=1;n<=3;n++){const v=S['b'+n];
  if(v&&!dirty[n-1]&&v!==loaded[n-1])fillBox(n,v);loaded[n-1]=v||'';
  if(pend[n-1]){const el=$('b'+n+'_st');if(v&&same(v,pend[n-1])){el.textContent='the station has these values ✓';el.className='note ok';pend[n-1]=null;}else el.textContent='waiting for the station to confirm (or it refused - see its serial log)...';}}
 const t=$('ts_sec');
 if(S.tsec&&!tsDirty&&S.tsec!==tsLoaded&&document.activeElement!==t)t.value=S.tsec;
 tsLoaded=S.tsec||'';
 if(tsPend!==null){const st=$('tsStatus');if(S.tsec===tsPend){st.textContent='the station has this value ✓';st.className='note ok';tsPend=null;}else st.textContent='waiting for the station to confirm (or it refused - see its serial log)...';}
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='Box 1: '+(S.b1||'(not received yet)')+'\nBox 2: '+(S.b2||'(not received yet)')+'\nBox 3: '+(S.b3||'(not received yet)')+'\ntest : '+(S.tsec||'(not received yet)')+'\n(order: empty up to, MID, HIGH, MAX, wait EMPTY s, wait LOW s, valve max s, retry s, rise)';
}
async function q(u){try{const r=await fetch(API+u,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
q('/state');setInterval(()=>q('/state'),700);
</script></body></html>
)hubpage";
