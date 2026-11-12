// Started on 2026-10-03 by tools\build_hub_pages.ps1, but EDITED BY HAND since (screen refresh, beep rows, ...): this file is now the master - do not run that script again.
// The look (CSS, fold-able cards, Live panel rows) is in hub_theme.h and reaches the pages as /hub.css and /hub.js.
// Each settings page talks to the LocalServer under /api/b1, /api/b2, /api/b3 and /api/refill (see settings_hub.h).
#pragma once
#include <Arduino.h>
const char PAGE_BOX1[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Box 1 Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b1"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a class="cur" href="/box1">Box 1</a><a href="/box2">Box 2</a><a href="/box3">Box 3</a><a href="/refill">Refilling Station</a></div>
<h1>Box 1 settings <span id="net" class="warn"></span></h1>
<div class="note">Changes the S3A's saved settings over the air; the S3A keeps them in its own memory, so nothing is lost when this board is switched off.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>

<div class="note"><b>Coin check:</b> on the INSERT COIN screen the acceptor power should read ON. Insert a coin: "pulses A1 has counted" should rise by the coin's value within a second or two, and A1 beeps. If it stays the same, the coin signal is not reaching A1. The "coin wire" line counts every pulse edge A1 sees BEFORE its filter: if a 20-peso coin shows 20 edges but only 1 pulse counted, A1's filter is eating them; if it shows 1 edge, the acceptor itself only sent one pulse for that coin.</div>

<h2>Containers and sensors (LEVELS)</h2>
<div class="row"><label><input type="checkbox" id="lv_checks"> Check the container levels before serving a customer</label></div>
<div class="note">Distances are what the ultrasonic on each Main Container reads, in cm from the sensor down to the liquid: <b>bigger = emptier</b>. Fill to the line you want, then tap "use live" next to it.</div>
<div class="row"><b>Alcohol</b>
 <label>Low at</label><input class="n" id="lv_alcLow" type="number" step="0.1"><button class="sm" onclick="useLive('lv_alcLow','als')">use live</button>
 <label>High at</label><input class="n" id="lv_alcHigh" type="number" step="0.1"><button class="sm" onclick="useLive('lv_alcHigh','als')">use live</button></div>
<div class="row"><b>Scented</b>
 <label>Low at</label><input class="n" id="lv_sctLow" type="number" step="0.1"><button class="sm" onclick="useLive('lv_sctLow','sls')">use live</button>
 <label>High at</label><input class="n" id="lv_sctHigh" type="number" step="0.1"><button class="sm" onclick="useLive('lv_sctHigh','sls')">use live</button></div>
<div class="row"><label>Helmet counts as inside under</label><input class="n" id="lv_helm" type="number" step="0.5"><label>cm</label><button class="sm" onclick="useLive('lv_helm','helm')">use live</button></div>

<div class="note"><b>Water sensors</b> (analog). With the sensor DRY tap "dry now", then put it in the liquid and tap "wet now" - the two switch-over numbers are worked out for you. Test each with its real liquid: alcohol barely conducts and may hardly react.</div>
<div class="row"><b>Alcohol sensor</b> dry <input class="n" id="wsa_dry" type="number"><button class="sm" onclick="capWater('a','dry')">dry now</button>
 wet <input class="n" id="wsa_wet" type="number"><button class="sm" onclick="capWater('a','wet')">wet now</button></div>
<div class="row"><label>wet when</label><input class="n" id="lv_wsaIn" type="number"><label>dry again when</label><input class="n" id="lv_wsaOut" type="number"></div>
<div class="row"><b>Scented sensor</b> dry <input class="n" id="wss_dry" type="number"><button class="sm" onclick="capWater('s','dry')">dry now</button>
 wet <input class="n" id="wss_wet" type="number"><button class="sm" onclick="capWater('s','wet')">wet now</button></div>
<div class="row"><label>wet when</label><input class="n" id="lv_wssIn" type="number"><label>dry again when</label><input class="n" id="lv_wssOut" type="number"></div>
<div class="row"><label for="lv_wsHigher">The reading goes</label>
 <select id="lv_wsHigher"><option value="1">UP when wet</option><option value="0">DOWN when wet</option></select>
 <span class="note" id="wsNote"></span></div>

<div class="row"><label>Refill tries</label><input class="n" id="lv_tries" type="number" min="1" max="99">
 <label>every</label><input class="n" id="lv_interval" type="number" min="1" max="250"><label>s</label>
 <label><input type="checkbox" id="lv_acs"> send the request to the old ACS 1 (leave OFF - the Refilling Station replaced it)</label></div>
<div class="row"><button class="go" onclick="sendLevels()">Send LEVELS to S3A</button><button onclick="loadFrom('levels')">Reload from S3A</button><span id="lvStatus" class="note"></span></div>

<h2>Rules (time, coins, free retry)</h2>
<div class="note">Rating bands: each rating from 1 to 10 should fall in exactly one band.</div>
<div id="bands"></div>
<div class="row"><label>Shortest session</label><input class="n" id="ru_min" type="number" min="1" max="120"><label>min</label>
 <label>Most coins</label><input class="n" id="ru_max" type="number" min="1" max="999"><span id="ru_maxnote" class="note"></span></div>
<div class="row"><input class="n" id="ru_coins" type="number" min="1"><label>coin(s) =</label><input class="n" id="ru_val" type="number" step="any" min="0.1">
 <select id="ru_unit"><option value="1">minute(s)</option><option value="0">second(s)</option></select></div>
<div class="row"><label><input type="checkbox" id="ru_free"> One free retry after the second rating</label>
 <label>from rating</label><input class="n" id="ru_freeR" type="number" min="1" max="10"><label>for</label><input class="n" id="ru_freeM" type="number" min="1" max="60"><label>min</label></div>
<div class="row"><button class="go" onclick="sendRules()">Send RULES to S3A</button><button onclick="loadFrom('rules')">Reload from S3A</button><span id="ruStatus" class="note"></span></div>

<h2>Cleaning phases (which relays run when)</h2>
<div class="note">The cleaning is cut into a START part, a MIDDLE part (whatever is left) and an END part, and each cleaning relay is ON or OFF in each. Example: UV on for the first and last minute, the humidifiers on in between. The parts count real cleaning time, so a pause does not use them up, and coins added mid-clean keep the END part "the last minute". If the two parts overlap, the start wins.</div>
<div class="row"><label><input type="checkbox" id="ph_paid"> Phase the PAID cleaning</label>
 <label><input type="checkbox" id="ph_free"> Phase the FREE retry <span class="note">(a 2-minute retry with 1 + 1 minute parts would never run the humidifiers)</span></label></div>
<div class="row"><label>Start part lasts</label><input class="n" id="ph_sv" type="number" min="0" step="any">
 <select id="ph_su"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select>
 <label>End part lasts</label><input class="n" id="ph_ev" type="number" min="0" step="any">
 <select id="ph_eu"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select></div>
<div id="phRelays"></div>
<div class="row"><button class="go" onclick="sendPhases()">Send PHASES to S3A</button><button onclick="loadFrom('phases')">Reload from S3A</button><span id="phStatus" class="note"></span></div>

<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div><div class="note">Restarts the box's touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages - this is the manual button for any other time.</div><div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div><div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div><div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div><div id="rfRows"></div><div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'')">Reload</button><span id="rfCfgStatus" class="note"></span></div><h2>OPEN A / OPEN B beep</h2><div class="row"><label>Beep for</label><input class="n" id="bp_len" type="number" min="20" max="1000"><label>ms, then wait</label><input class="n" id="bp_wait" type="number" min="0" max="10000"><label>ms</label><span class="note">(wait 0 = one beep only)</span><button class="go" onclick="sendBeep()">Send</button><span id="bpStatus" class="note"></span></div><div class="note">While the OPEN A or OPEN B step waits for the door, the box beeps like this (beep, wait, beep, wait ...) until the door opens. The box keeps the values in its own memory. Default: beep 100 ms, then wait 500 ms.</div><h2>Maintenance</h2>
<div class="note">Only works when the S3A is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. With the safety on (default) each switches itself off after 2 s; with the bypass ticked the enclosure lock still rests itself after 60 s.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==25?0:1))">Maintenance</button></div>
<div class="row"><label class="by"><input type="checkbox" id="mbypass"> Bypass Safety 2 Seconds Relay</label><span id="mbNote" class="note warn"></span></div>
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. <b>The coin power is not part of it</b> - it stays on until you switch it off. Ticking the box turns the safety off for everything until you untick it or leave maintenance.</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3A reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/b1';
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
const ST=["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1 (ALC)","UV 1","HUMIDIFIER 2 (SCN)","HUMIDIFIER 3 (BOTH)","UV 2","(UNUSED)","(UNUSED)","SIDE LOCK","COIN POWER"];
const LV=['lv_checks','lv_alcLow','lv_alcHigh','lv_sctLow','lv_sctHigh','lv_helm','lv_wsaIn','lv_wsaOut','lv_wssIn','lv_wssOut','lv_wsHigher','lv_tries','lv_interval','lv_acs'];
const RU_EXTRA=['ru_min','ru_coins','ru_val','ru_unit','ru_max','ru_free','ru_freeR','ru_freeM'];
const PHN=["Humidifier 1 (alcohol)","Humidifier 2 (scented)","Humidifier 3 (both)","UV 1","UV 2"];
let S={}, dirty={levels:false,rules:false,phases:false}, pend={levels:null,rules:null,phases:null}, loaded={levels:'',rules:'',phases:''}, mm=0;
const $=id=>document.getElementById(id);
// band rows
const bandsBox=$('bands');
for(let i=0;i<4;i++){const d=document.createElement('div');d.className='row';
 d.innerHTML='<label>Rating</label><input class="n" id="ru_f'+i+'" type="number" min="1" max="10"><label>to</label><input class="n" id="ru_t'+i+'" type="number" min="1" max="10"><label>suggests</label><input class="n" id="ru_m'+i+'" type="number" min="1" max="120"><label>min</label>';bandsBox.appendChild(d);}
// phase rows: one line per cleaning relay with a Start / Middle / End tick each
const phBox=$('phRelays');
PHN.forEach((n,i)=>{const d=document.createElement('div');d.className='row';
 d.innerHTML='<b style="min-width:150px">'+n+'</b>'+[['1','Start'],['2','Middle'],['4','End']].map(p=>'<label><input type="checkbox" id="ph_p'+i+'_'+p[0]+'"> '+p[1]+'</label>').join(' ');phBox.appendChild(d);});
// relay buttons
const rel=$('rel');
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>relClick(i);if(n==='(UNUSED)')b.style.display='none';rel.appendChild(b);});
// maintenance safety: each hand-set relay switches itself off after 2 s (the S3 does it); LATCH = relays that are exempt, DRAIN = relays that ask first (none now: the drain pumps were removed 2026-11-03)
const MAINT_ST=25,LATCH=[11],DRAIN=[];let lastClick=0,byTouch=0;
const byEl=$('mbypass');
function sendMask(){return q('/mask?v='+mm+'&by='+(byEl.checked?1:0));}
async function relClick(i){
 if(!((mm>>i)&1)&&DRAIN.indexOf(i)>=0){
  const ok=await hubConfirm({title:'WARNING',head:'You are now toggling Drain Pump '+(i-7)+' ON',text:'It pumps liquid out until you switch it off again. Make sure the drain tube is connected and the container can take it, so nothing spills.',yes:'Yes, turn it ON',no:'No, cancel'});
  if(!ok)return;
 }
 mm^=(1<<i);lastClick=Date.now();sendMask();
}
byEl.addEventListener('change',()=>{byTouch=Date.now();byEl.parentElement.classList.toggle('active',byEl.checked);sendMask();});
function bypassSync(){const m=S.state==MAINT_ST;byEl.disabled=!m;
 if(!m)byEl.checked=false;else if(Date.now()-byTouch>1500)byEl.checked=!!S.by;
 byEl.parentElement.classList.toggle('active',byEl.checked);$('mbNote').textContent=byEl.checked?'safety OFF - relays stay on until you switch them off':'';}
// mark a form as edited so the live refresh does not overwrite typing
document.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>{const id=el.id;
 if(id.startsWith('lv_')||id.startsWith('wsa_')||id.startsWith('wss_'))dirty.levels=true;else if(id.startsWith('ru_'))dirty.rules=true;else if(id.startsWith('ph_'))dirty.phases=true;});});

function num(id){return parseFloat($(id).value);}
function setv(id,v){const el=$(id);if(el.type==='checkbox')el.checked=!!(+v);else el.value=v;}
function getv(id){const el=$(id);return el.type==='checkbox'?(el.checked?1:0):(el.value===''?'0':el.value);}
function levelsCsv(){return LV.map(getv).join(',');}
// "Most coins" shown as the longest session it allows, and the price, at the coin rate typed above it
function updMaxNote(){const c=+getv('ru_coins'),v=+getv('ru_val'),u=+getv('ru_unit'),m=+getv('ru_max'),el=$('ru_maxnote');
 if(!(c>0&&v>0&&m>0)){el.textContent='';return;}
 const sec=m*v*(u?60:1)/c,mn=sec/60;el.textContent='= '+(mn>=1?(Math.round(mn*10)/10)+' min':Math.round(sec)+' s')+' of time, P'+m+' ('+m+' coins at 1 peso each)';}
setInterval(updMaxNote,400);
function rulesCsv(){const p=[];for(let i=0;i<4;i++){p.push(getv('ru_f'+i),getv('ru_t'+i),getv('ru_m'+i));}RU_EXTRA.forEach(id=>p.push(getv(id)));return p.join(',');}
function fillLevels(csv){const v=csv.split(',');if(v.length!==14)return;LV.forEach((id,i)=>setv(id,v[i]));dirty.levels=false;}
function fillRules(csv){const v=csv.split(',');if(v.length!==20)return;for(let i=0;i<4;i++){setv('ru_f'+i,v[i*3]);setv('ru_t'+i,v[i*3+1]);setv('ru_m'+i,v[i*3+2]);}RU_EXTRA.forEach((id,i)=>setv(id,v[12+i]));dirty.rules=false;}
function phasesCsv(){const pat=[];for(let i=0;i<5;i++){let v=0;[1,2,4].forEach(b=>{if($('ph_p'+i+'_'+b).checked)v|=b;});pat.push(v);}
 return [getv('ph_paid'),getv('ph_free'),getv('ph_sv'),getv('ph_su'),getv('ph_ev'),getv('ph_eu')].concat(pat).join(',');}
function fillPhases(csv){const v=csv.split(',');if(v.length!==11)return;
 setv('ph_paid',v[0]);setv('ph_free',v[1]);setv('ph_sv',v[2]);setv('ph_su',v[3]);setv('ph_ev',v[4]);setv('ph_eu',v[5]);
 for(let i=0;i<5;i++){const p=+v[6+i];[1,2,4].forEach(b=>{$('ph_p'+i+'_'+b).checked=!!(p&b);});}
 dirty.phases=false;}
async function sendPhases(){const c=phasesCsv();pend.phases=c;$('phStatus').textContent='sending...';await q('/send?kind=phases&csv='+encodeURIComponent(c));dirty.phases=false;}
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function useLive(id,key){const v=S[key];if(!S.a2ok||!(v>0)){alert('No live reading from A2 (or no echo).');return;}setv(id,(+v).toFixed(1));dirty.levels=true;}
function capWater(w,which){
 if(!S.a2ok){alert('No live reading from A2.');return;}
 setv('ws'+w+'_'+which,w==='a'?S.raw0:S.raw1);dirty.levels=true;
 const d=num('ws'+w+'_dry'),t=num('ws'+w+'_wet');
 if(isNaN(d)||isNaN(t))return;
 const gap=t-d,note=$('wsNote');
 if(Math.abs(gap)<150){note.textContent='The '+(w==='a'?'alcohol':'scented')+' sensor barely reacts ('+Math.abs(gap)+') - not reliable for this liquid.';note.className='note err';}
 else{note.textContent='';note.className='note';}
 const enter=Math.round(d+0.6*gap),leave=Math.round(d+0.4*gap);
 setv('lv_ws'+w+'In',enter);setv('lv_ws'+w+'Out',leave);
 setv('lv_wsHigher',gap>0?1:0);
}
async function sendLevels(){const c=levelsCsv();pend.levels=c;$('lvStatus').textContent='sending...';await q('/send?kind=levels&csv='+encodeURIComponent(c));dirty.levels=false;}
async function sendRules(){const c=rulesCsv();pend.rules=c;$('ruStatus').textContent='sending...';await q('/send?kind=rules&csv='+encodeURIComponent(c));dirty.rules=false;}
function loadFrom(kind){if(kind==='levels'&&S.levels)fillLevels(S.levels);if(kind==='rules'&&S.rules)fillRules(S.rules);if(kind==='phases'&&S.phases)fillPhases(S.phases);}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function render(){
 const L=$('live');
 let h='';
 h+=S.s3aok?('S3A: <b class="ok">online</b> ('+age(S.s3aage)+') · step <b>'+(ST[S.state]||S.state)+'</b>'+(S.timer?(' · '+Math.floor(S.timer/60)+':'+String(S.timer%60).padStart(2,'0')+' left'):'')):'S3A: <b class="err">not heard</b> - is it on, on channel 1, running the new firmware?';
 h+='<br>A2 sensors: '+(S.a2ok?('<b class="ok">ok</b> ('+age(S.a2age)+')'):'<b class="err">silent</b>');
 h+=' · A3 relays: '+(S.a3ok?('<b class="ok">ok</b>'+(S.a3orph?' <span class="err">(A3 sees no S3A)</span>':'')+' · on: 0x'+S.a3mask.toString(16).toUpperCase().padStart(3,'0')):'<b class="err">silent</b>');
 h+='<br>Coins: A1 '+(S.a1ok?'<b class="ok">ok</b>':'<b class="err">silent</b>')+' · pulses A1 has counted: <b>'+S.coins+'</b>'+(S.coinage>=0?(' (last change '+age(S.coinage)+')'):' (none since A1 started)')+' · acceptor power from A3: <b>'+(((S.a3mask>>11)&1)?'ON':'off')+'</b>';
 h+='<br>Coin wire: A1 saw <b>'+S.raw+'</b> pulse edges in total · shortest gap between two <b>'+(S.gap>0?S.gap.toFixed(1)+' ms':'(none yet)')+'</b>'+(S.raw>S.coins?(' · <span class="warn">'+(S.raw-S.coins)+' edge(s) were filtered out or ignored</span>'):'');
 if(S.a2ok){
  h+='<br>Helmet <b>'+S.helm.toFixed(1)+'</b> cm · Alcohol container <b>'+S.als.toFixed(1)+'</b> cm · Scented container <b>'+S.sls.toFixed(1)+'</b> cm <span class="note">(0 = no echo)</span>';
  h+='<br>Water sensors: alcohol raw <b>'+S.raw0+'</b> · scented raw <b>'+S.raw1+'</b> · enclosure door <b>'+(S.door?'OPEN':'shut')+'</b>';
 }
 L.innerHTML=hubLines(h);
 // keep the form in step with the S3A unless the user is typing in it
 if(S.levels&&!dirty.levels&&S.levels!==loaded.levels){fillLevels(S.levels);}
 if(S.rules&&!dirty.rules&&S.rules!==loaded.rules){fillRules(S.rules);}
 if(S.phases&&!dirty.phases&&S.phases!==loaded.phases){fillPhases(S.phases);}
 loaded.levels=S.levels||'';loaded.rules=S.rules||'';loaded.phases=S.phases||'';
 // did the S3A accept what was sent?
 ['levels','rules','phases'].forEach(k=>{const el=$(k==='levels'?'lvStatus':(k==='rules'?'ruStatus':'phStatus'));
  if(pend[k]){if(S[k]&&same(S[k],pend[k])){el.textContent='S3A has these values ✓';el.className='note ok';pend[k]=null;}
   else el.textContent='waiting for the S3A to confirm...';}});
 $('mt').className=S.state==25?'on':'';$('mt').textContent='Maintenance: '+(S.state==25?'ON':'OFF');
 for(let i=0;i<12;i++){const b=$('r'+i);b.className=((S.a3mask>>i)&1)?'on':'';b.disabled=S.state!=25;}
 if(S.state!=25)mm=0;else if(Date.now()-lastClick>1500)mm=S.a3mask&0x0FFF;
 bypassSync();
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules  : '+(S.rules||'(not received yet)')+'\nlevels : '+(S.levels||'(not received yet)')+'\nphases : '+(S.phases||'(not received yet)');
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
</script></body></html>)hubpage";
const char PAGE_BOX2[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Box 2 Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b2"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a class="cur" href="/box2">Box 2</a><a href="/box3">Box 3</a><a href="/refill">Refilling Station</a></div>
<h1>Box 2 settings &middot; Heater <span id="net" class="warn"></span></h1>
<div class="note">Changes the S3C's saved settings over the air; the S3C keeps them in its own memory, so nothing is lost when this board is switched off.</div>

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

<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div><div class="note">Restarts the box's touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages - this is the manual button for any other time.</div><div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div><div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div><div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div><div id="rfRows"></div><div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'')">Reload</button><span id="rfCfgStatus" class="note"></span></div><h2>OPEN A / OPEN B beep</h2><div class="row"><label>Beep for</label><input class="n" id="bp_len" type="number" min="20" max="1000"><label>ms, then wait</label><input class="n" id="bp_wait" type="number" min="0" max="10000"><label>ms</label><span class="note">(wait 0 = one beep only)</span><button class="go" onclick="sendBeep()">Send</button><span id="bpStatus" class="note"></span></div><div class="note">While the OPEN A or OPEN B step waits for the door, the box beeps like this (beep, wait, beep, wait ...) until the door opens. The box keeps the values in its own memory. Default: beep 100 ms, then wait 500 ms.</div><h2>Maintenance</h2>
<div class="note">Only works when the S3C is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. With the safety on (default) each switches itself off after 2 s; with the bypass ticked the enclosure lock still rests itself after 60 s. <b>Careful with the heater:</b> with the bypass it is switched off by itself after 5 minutes on, and leaving Maintenance switches everything off - but keep the door shut and a helmet out of the way when testing it.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==21?0:1))">Maintenance</button></div>
<div class="row"><label class="by"><input type="checkbox" id="mbypass"> Bypass Safety 2 Seconds Relay</label><span id="mbNote" class="note warn"></span></div>
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. Ticking the box turns the safety off until you untick it or leave maintenance (the heater still switches itself off after 5 minutes).</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3C reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/b2';
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
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
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>relClick(i);rel.appendChild(b);});
// maintenance safety: each hand-set relay switches itself off after 2 s (the S3 does it); LATCH = relays that are exempt, DRAIN = relays that ask first
const MAINT_ST=21,LATCH=[],DRAIN=[];let lastClick=0,byTouch=0;
const byEl=$('mbypass');
function sendMask(){return q('/mask?v='+mm+'&by='+(byEl.checked?1:0));}
async function relClick(i){
 if(!((mm>>i)&1)&&DRAIN.indexOf(i)>=0){
  const ok=await hubConfirm({title:'WARNING',head:'You are now toggling Drain Pump '+(i-7)+' ON',text:'It pumps liquid out until you switch it off again. Make sure the drain tube is connected and the container can take it, so nothing spills.',yes:'Yes, turn it ON',no:'No, cancel'});
  if(!ok)return;
 }
 mm^=(1<<i);lastClick=Date.now();sendMask();
}
byEl.addEventListener('change',()=>{byTouch=Date.now();byEl.parentElement.classList.toggle('active',byEl.checked);sendMask();});
function bypassSync(){const m=S.state==MAINT_ST;byEl.disabled=!m;
 if(!m)byEl.checked=false;else if(Date.now()-byTouch>1500)byEl.checked=!!S.by;
 byEl.parentElement.classList.toggle('active',byEl.checked);$('mbNote').textContent=byEl.checked?'safety OFF - relays stay on until you switch them off':'';}
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
 L.innerHTML=hubLines(h);
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
 if(S.state!=21)mm=0;else if(Date.now()-lastClick>1500)mm=S.mask&0x3F;
 bypassSync();
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules : '+(S.rules||'(not received yet)')+'\nheat  : '+(S.heat||'(not received yet)');
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
</script></body></html>)hubpage";
const char PAGE_BOX3[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Box 3 Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b3"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a href="/box2">Box 2</a><a class="cur" href="/box3">Box 3</a><a href="/refill">Refilling Station</a></div>
<h1>Box 3 settings &middot; Cleaning + Drying <span id="net" class="warn"></span></h1>
<div class="note">Changes the S3B's saved settings over the air; the S3B keeps them in its own memory, so nothing is lost when this board is switched off.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>

<div class="note"><b>Coin check:</b> on the INSERT COIN screen the acceptor power should read ON. Insert a coin: "pulses B1 has counted" should rise by the coin's value within a second or two, and B1 beeps. If it stays the same, the coin signal is not reaching B1. The "coin wire" line counts every pulse edge B1 sees BEFORE its filter: if a 20-peso coin shows 20 edges but only 1 pulse counted, B1's filter is eating them; if it shows 1 edge, the acceptor itself only sent one pulse for that coin.</div>

<h2>Rules (coins and time)</h2>
<div class="note">Box 3 has no ratings and no time picker. On the INSERT COIN screen the customer pays the <b>required coins</b> (one step of the rate below, for example 15 coins), and the cleaning or the drying then runs for the time the coins bought. More coins can be added while it runs. <b>Most coins</b> is the most one session can take.</div>
<div class="row"><input class="n" id="ru_coins" type="number" min="1"><label>coin(s) =</label><input class="n" id="ru_val" type="number" step="any" min="0.1">
 <select id="ru_unit"><option value="1">minute(s)</option><option value="0">second(s)</option></select></div>
<div class="row"><label>Most coins</label><input class="n" id="ru_max" type="number" min="1" max="999"><span id="ru_maxnote" class="note"></span></div>
<div class="row"><button class="go" onclick="sendRules()">Send RULES to S3B</button><button onclick="loadFrom('rules')">Reload from S3B</button><span id="ruStatus" class="note"></span></div>

<h2>Sensors and the Refilling Station</h2>
<div class="row"><label>Helmet counts as inside under</label><input class="n" id="se_helm" type="number" step="0.5" min="1" max="200"><label>cm</label><button class="sm" onclick="useLive('se_helm','helm')">use live</button></div>
<div class="row"><label>The Refilling Station counts as offline after</label><input class="n" id="se_off" type="number" min="5" max="120"><label>seconds</label></div>
<div class="note">On the CHECKING screen (2 seconds after the first tap) the box reads its Main Container from the Refilling Station. If the station is not heard for this long, or the container is EMPTY or LOW, only DRYING is offered and CLEANING is greyed out. The refilling itself, the levels and the valve test are on the <a href="/refill" style="color:var(--accent)">Refilling Station</a> page. Distances are what the helmet ultrasonic reads, in cm from the sensor: tap "use live" with a helmet inside.</div>
<div class="row"><button class="go" onclick="sendSensors()">Send SENSORS to S3B</button><button onclick="loadFrom('sensors')">Reload from S3B</button><span id="seStatus" class="note"></span></div>

<h2>Auto-dry after a cleaning</h2>
<div class="row"><label>Drying lasts</label><input class="n" id="ad_sec" type="number" min="10" max="900"><label>seconds</label>
 <label>heater OFF for the last</label><input class="n" id="ad_tail" type="number" min="0" max="900"><label>seconds</label></div>
<div class="note">Every cleaning ends with this drying: UV 1 + UV 2 + the heater, and the heater switches OFF for its last seconds so the helmet is not handed over hot. A cancel during a cleaning (CONFIRM) jumps straight to it, because the helmet is still wet. Default 180 s with the heater off for the last 30 s.</div>
<div class="row"><button class="go" onclick="sendDry()">Send AUTO-DRY to S3B</button><button onclick="loadFrom('dry')">Reload from S3B</button><span id="adStatus" class="note"></span></div>

<h2>Drying</h2>
<div class="row"><label>Cool down lasts</label><input class="n" id="dr_cool" type="number" min="5" max="900"><label>seconds</label></div>
<div class="note">After every drying, and after a cancel during a drying, nothing runs for this long before the door can be opened (Box 3 has no fan). Default 60. The drying length itself is what the customer's coins bought (see Rules).</div>
<div class="row"><button class="go" onclick="sendDrying()">Send DRYING to S3B</button><button onclick="loadFrom('drying')">Reload from S3B</button><span id="drStatus" class="note"></span></div>

<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div><div class="note">Restarts the box's touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages and when the heater switches on or off - this is the manual button for any other time.</div><div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div><div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div><div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div><div id="rfRows"></div><div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'')">Reload</button><span id="rfCfgStatus" class="note"></span></div><h2>OPEN A / OPEN B beep</h2><div class="row"><label>Beep for</label><input class="n" id="bp_len" type="number" min="20" max="1000"><label>ms, then wait</label><input class="n" id="bp_wait" type="number" min="0" max="10000"><label>ms</label><span class="note">(wait 0 = one beep only)</span><button class="go" onclick="sendBeep()">Send</button><span id="bpStatus" class="note"></span></div><div class="note">While the OPEN A or OPEN B step waits for the door, the box beeps like this (beep, wait, beep, wait ...) until the door opens. The box keeps the values in its own memory. Default: beep 100 ms, then wait 500 ms.</div><h2>Maintenance</h2>
<div class="note">Only works when the S3B is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. With the safety on (default) each switches itself off after 2 s; with the bypass ticked the enclosure lock still rests itself after 60 s. <b>Careful with the heater:</b> with the bypass a hand-set heater switches itself off after 5 minutes, and leaving Maintenance switches everything off - keep the door shut and a helmet out of the way when testing it.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==25?0:1))">Maintenance</button></div>
<div class="row"><label class="by"><input type="checkbox" id="mbypass"> Bypass Safety 2 Seconds Relay</label><span id="mbNote" class="note warn"></span></div>
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. <b>The coin power is not part of it</b> - it stays on until you switch it off. Ticking the box turns the safety off for everything until you untick it or leave maintenance.</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3B reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/b3';
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
// the S3B's state numbers (a number the page does not know shows as the number; "(old)" = Box 1's steps, which Box 3 no longer uses)
const ST=["IDLE","OPEN A","CHECKING","REFILLING (old)","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A (old)","TIME ALLOT (old)","INSERT COIN","RATE B (old)","CANCEL","METHOD (old)","CHECKING S (old)","REFILLING S (old)","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY (old)","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1 (old)","AUTO DRY","START DRYING"];
const RFN=["EMPTY","LOW","MID","HIGH","MAX"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1","UV 1","HUMIDIFIER 2","HUMIDIFIER 3","UV 2","(UNUSED)","(UNUSED)","SIDE LOCK","COIN POWER","HEATER"];
const RU=['ru_coins','ru_val','ru_unit','ru_max'];           // S3B subIndex 1: rateCoins,rateValue,rateUnitMin,maxCoins
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
function useLive(id,key){const v=S[key];if(!S.a2ok||!(v>0)){alert('No live reading from B2 (or no echo).');return;}setv(id,(+v).toFixed(1));dirty.sensors=true;}
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
 h+=S.s3aok?('S3B: <b class="ok">online</b> ('+age(S.s3aage)+') · step <b>'+(ST[S.state]||S.state)+'</b>'+(S.timer?(' · '+Math.floor(S.timer/60)+':'+String(S.timer%60).padStart(2,'0')+' left'):'')):'S3B: <b class="err">not heard</b> - is it on, on channel 1, running the new firmware?';
 h+='<br>B2 sensors: '+(S.a2ok?('<b class="ok">ok</b> ('+age(S.a2age)+')'):'<b class="err">silent</b>');
 h+=' · B3 relays: '+(S.a3ok?('<b class="ok">ok</b>'+(S.a3orph?' <span class="err">(B3 sees no S3B)</span>':'')+' · on: 0x'+S.a3mask.toString(16).toUpperCase().padStart(4,'0')):'<b class="err">silent</b>');
 h+='<br>Coins: B1 '+(S.a1ok?'<b class="ok">ok</b>':'<b class="err">silent</b>')+' · pulses B1 has counted: <b>'+S.coins+'</b>'+(S.coinage>=0?(' (last change '+age(S.coinage)+')'):' (none since B1 started)')+' · acceptor power from B3: <b>'+(((S.a3mask>>11)&1)?'ON':'off')+'</b>';
 h+='<br>Coin wire: B1 saw <b>'+S.raw+'</b> pulse edges in total · shortest gap between two <b>'+(S.gap>0?S.gap.toFixed(1)+' ms':'(none yet)')+'</b>'+(S.raw>S.coins?(' · <span class="warn">'+(S.raw-S.coins)+' edge(s) were filtered out or ignored</span>'):'');
 if(S.a2ok)h+='<br>Helmet <b>'+S.helm.toFixed(1)+'</b> cm <span class="note">(0 = no echo)</span> · enclosure door <b>'+(S.door?'OPEN':'shut')+'</b>';
 const R=S.rf||[0,0,255,0,0,0];
 h+='<br>Refilling Station: '+(R[0]?('<b class="ok">online</b> · Box 3 container <b>'+(RFN[R[2]]||(R[2]===254?'NOT WATCHED':'SENSOR FAULT'))+'</b> (raw '+R[1]+')'+(R[3]?' · valve <b>OPEN</b> (refilling)':'')+(R[4]?' · waiting for its turn':'')+(R[5]?' · <span class="err">last refill FAILED</span>':'')):'<b class="err">not heard</b> - Box 3 offers DRYING only');
 L.innerHTML=hubLines(h);
 // keep the forms in step with the S3B unless the user is typing in them
 Object.keys(KEY).forEach(k=>{const v=S[KEY[k]];if(v&&!dirty[k]&&v!==loaded[k])FILL[k](v);loaded[k]=v||'';
  if(pend[k]){const el=$(ST_EL[k]);if(v&&same(v,pend[k])){el.textContent='S3B has these values ✓';el.className='note ok';pend[k]=null;}else el.textContent='waiting for the S3B to confirm (or it refused - see its serial log)...';}});
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
<div class="note"><b>For now the station watches and refills Box 3 only</b>; Box 1 and Box 2 show NOT WATCHED until they are ready. <b>One valve at a time</b> (the three boxes share one water supply). A container that is <b>EMPTY</b> or <b>LOW</b> joins the queue: EMPTY containers go first, LOW second, and inside each group the one that waited longest goes first. A refill that is running is never interrupted, and a refill stops when the container reaches MAX. If the level does not rise (nothing arrives), the refill is called FAILED, the valve closes, and the station tries again after the retry time, for ever. <b>SENSOR FAULT</b> means the station found the container's water-level sensor unusable (it reads the 3.3 V rail, or its reading jumps about wildly because the wire is loose or floating): that box is not refilled, its box offers drying only, and it clears by itself 10 seconds after the reading is normal. A dry sensor that simply reads 0 cannot be told from a dead one: that shows up as a refill that does not rise (FAILED).</div>

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
  h+='<div class="note">'+(off?'The station does not watch this box for now (it watches and refills Box 3 only).':'raw '+(ok?raw:'-')+' &middot; MID '+S.mid[i]+' &middot; HIGH '+S.high[i]+' &middot; MAX '+S.max[i])+'</div>';
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
