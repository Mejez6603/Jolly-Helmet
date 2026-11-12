// Started on 2026-10-03 by tools\build_hub_pages.ps1, but EDITED BY HAND since (screen refresh, beep rows, ...): this file is now the master - do not run that script again.
// The look (CSS, fold-able cards, Live panel rows) is in hub_theme.h and reaches the pages as /hub.css and /hub.js.
// Each settings page talks to the LocalServer under /api/b1, /api/b2, /api/b3 and /api/acs (see settings_hub.h).
#pragma once
#include <Arduino.h>
const char PAGE_BOX1[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Box 1 Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b1"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a class="cur" href="/box1">Box 1</a><a href="/box2">Box 2</a><a href="/box3">Box 3</a><a href="/acs">ACS 1 / 2</a></div>
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
 <label><input type="checkbox" id="lv_acs"> send the request to ACS 1</label></div>
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
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. <b>The drain pumps and the coin power are not part of it</b> - they stay on until you switch them off. Switching a drain pump on asks you first. Ticking the box turns the safety off for everything until you untick it or leave maintenance.</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3A reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/b1';
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
const ST=["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1 (ALC)","UV 1","HUMIDIFIER 2 (SCN)","HUMIDIFIER 3 (BOTH)","UV 2","DRAIN PUMP 1","DRAIN PUMP 2","ACS DOOR LOCK","COIN POWER"];
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
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>relClick(i);rel.appendChild(b);});
// maintenance safety: each hand-set relay switches itself off after 2 s (the S3 does it); LATCH = relays that are exempt, DRAIN = relays that ask first
const MAINT_ST=25,LATCH=[8,9,11],DRAIN=[8,9];let lastClick=0,byTouch=0;
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
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b2"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a class="cur" href="/box2">Box 2</a><a href="/box3">Box 3</a><a href="/acs">ACS 1 / 2</a></div>
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
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-b3"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a href="/box2">Box 2</a><a class="cur" href="/box3">Box 3</a><a href="/acs">ACS 1 / 2</a></div>
<h1>Box 3 settings &middot; Cleaning + Drying <span id="net" class="warn"></span></h1>
<div class="note">Changes the S3B's saved settings over the air; the S3B keeps them in its own memory, so nothing is lost when this board is switched off.</div>

<h2>Live</h2>
<div class="box" id="live">...</div>

<div class="note"><b>Coin check:</b> on the INSERT COIN screen the acceptor power should read ON. Insert a coin: "pulses B1 has counted" should rise by the coin's value within a second or two, and B1 beeps. If it stays the same, the coin signal is not reaching B1. The "coin wire" line counts every pulse edge B1 sees BEFORE its filter: if a 20-peso coin shows 20 edges but only 1 pulse counted, B1's filter is eating them; if it shows 1 edge, the acceptor itself only sent one pulse for that coin.</div>

<h2>Containers and sensors (LEVELS)</h2>
<div class="row"><label><input type="checkbox" id="lv_checks"> Check the container levels before serving a customer</label></div>
<div class="note">Leave this OFF until the lines below are measured. With it ON, a container that is low and cannot be refilled greys CLEANING out (DRYING keeps working). Distances are what the ultrasonic on each Main Container reads, in cm from the sensor down to the liquid: <b>bigger = emptier</b>. Fill to the line you want, then tap "use live" next to it.</div>
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
 <label><input type="checkbox" id="lv_acs"> send the request to ACS 2</label></div>
<div class="row"><button class="go" onclick="sendLevels()">Send LEVELS to S3B</button><button onclick="loadFrom('levels')">Reload from S3B</button><span id="lvStatus" class="note"></span></div>

<h2>Rules (time, coins, free retry)</h2>
<div class="note">Box 3 starts with a copy of Box 1's prices - set Box 3's own here. Rating bands: each rating from 1 to 10 should fall in exactly one band. The free retry is offered once when EITHER rating after a cleaning (odor or dryness) is in the free range.</div>
<div id="bands"></div>
<div class="row"><label>Shortest session</label><input class="n" id="ru_min" type="number" min="1" max="120"><label>min</label>
 <label>Most coins</label><input class="n" id="ru_max" type="number" min="1" max="999"><span id="ru_maxnote" class="note"></span></div>
<div class="row"><input class="n" id="ru_coins" type="number" min="1"><label>coin(s) =</label><input class="n" id="ru_val" type="number" step="any" min="0.1">
 <select id="ru_unit"><option value="1">minute(s)</option><option value="0">second(s)</option></select></div>
<div class="row"><label><input type="checkbox" id="ru_free"> One free retry after the ratings</label>
 <label>from rating</label><input class="n" id="ru_freeR" type="number" min="1" max="10"><label>for</label><input class="n" id="ru_freeM" type="number" min="1" max="60"><label>min</label></div>
<div class="row"><button class="go" onclick="sendRules()">Send RULES to S3B</button><button onclick="loadFrom('rules')">Reload from S3B</button><span id="ruStatus" class="note"></span></div>

<h2>Cleaning phases (which relays run when)</h2>
<div class="note">The cleaning is cut into a START part, a MIDDLE part (whatever is left) and an END part, and each relay is ON or OFF in each. Defaults: UV the first and last minute, humidifiers in the middle, <b>heater + UV the last minute</b>. The heater is a hair dryer: keep it in the END part only, never together with the humidifiers. If phasing is switched off for a cleaning, the heater stays OFF. The parts count real cleaning time, so a pause does not use them up. If the two parts overlap, the start wins.</div>
<div class="row"><label><input type="checkbox" id="phP_on"> Phase the PAID cleaning</label>
 <label><input type="checkbox" id="phF_on"> Phase the FREE retry</label></div>
<div class="row"><b>PAID:</b> <label>Start part lasts</label><input class="n" id="phP_sv" type="number" min="0" step="any">
 <select id="phP_su"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select>
 <label>End part lasts</label><input class="n" id="phP_ev" type="number" min="0" step="any">
 <select id="phP_eu"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select></div>
<div id="phPRelays"></div>
<div class="row"><b>FREE retry:</b> <label>Start part lasts</label><input class="n" id="phF_sv" type="number" min="0" step="any">
 <select id="phF_su"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select>
 <label>End part lasts</label><input class="n" id="phF_ev" type="number" min="0" step="any">
 <select id="phF_eu"><option value="0">% of the cleaning</option><option value="1">seconds</option><option value="2">minutes</option></select></div>
<div id="phFRelays"></div>
<div class="note err" id="phWarn"></div>
<div class="row"><button class="go" onclick="sendPhases()">Send PHASES to S3B</button><button onclick="loadFrom('phases')">Reload from S3B</button><span id="phStatus" class="note"></span></div>

<h2>Drying</h2>
<div class="row"><label>Cool down lasts</label><input class="n" id="dr_cool" type="number" min="5" max="900"><label>seconds</label></div>
<div class="note">After every drying, and after an abort during a drying, nothing runs for this long before the door can be opened (Box 3 has no fan). Default 60. The drying length itself comes from the customer's rating and the Rules above.</div>
<div class="row"><button class="go" onclick="sendDrying()">Send DRYING to S3B</button><button onclick="loadFrom('drying')">Reload from S3B</button><span id="drStatus" class="note"></span></div>

<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div><div class="note">Restarts the box's touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages and when the heater switches on or off - this is the manual button for any other time.</div><div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div><div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div><div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div><div id="rfRows"></div><div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'')">Reload</button><span id="rfCfgStatus" class="note"></span></div><h2>OPEN A / OPEN B beep</h2><div class="row"><label>Beep for</label><input class="n" id="bp_len" type="number" min="20" max="1000"><label>ms, then wait</label><input class="n" id="bp_wait" type="number" min="0" max="10000"><label>ms</label><span class="note">(wait 0 = one beep only)</span><button class="go" onclick="sendBeep()">Send</button><span id="bpStatus" class="note"></span></div><div class="note">While the OPEN A or OPEN B step waits for the door, the box beeps like this (beep, wait, beep, wait ...) until the door opens. The box keeps the values in its own memory. Default: beep 100 ms, then wait 500 ms.</div><h2>Maintenance</h2>
<div class="note">Only works when the S3B is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand. With the safety on (default) each switches itself off after 2 s; with the bypass ticked the enclosure lock still rests itself after 60 s. <b>Careful with the heater:</b> with the bypass a hand-set heater switches itself off after 5 minutes, and leaving Maintenance switches everything off - keep the door shut and a helmet out of the way when testing it.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==25?0:1))">Maintenance</button></div>
<div class="row"><label class="by"><input type="checkbox" id="mbypass"> Bypass Safety 2 Seconds Relay</label><span id="mbNote" class="note warn"></span></div>
<div class="note"><b>Safety (on unless you tick the box above):</b> every relay you switch on here switches itself off again after <b>2 seconds</b>. The box does this by itself, so it also works when this page or the phone disconnects. <b>The drain pumps and the coin power are not part of it</b> - they stay on until you switch them off. Switching a drain pump on asks you first. Ticking the box turns the safety off for everything until you untick it or leave maintenance.</div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3B reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/b3';
async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}
const ST=["IDLE","OPEN A","CHECKING A","REFILLING A","SENSORS (old)","CLEANING","ABORT","OPEN B","FINISH","SAFETY PAUSE","TAPS","WELCOME","RATE A","TIME ALLOT","INSERT COIN","RATE B","CANCEL","METHOD","CHECKING S","REFILLING S","SENSORS A","CLOSE DOOR A","SENSORS B","CLOSE DOOR B","FREE RETRY","MAINTENANCE","OUT OF ORDER","HELMET NOTICE A","HELMET NOTICE B","SELECT MODE","SELECT DRY ONLY","DRYING","COOL DOWN","RATE B1"];
const RN=["ENCLOSURE LOCK","PANEL LOCK","BACKDOOR LOCK","HUMIDIFIER 1 (ALC)","UV 1","HUMIDIFIER 2 (SCN)","HUMIDIFIER 3 (BOTH)","UV 2","DRAIN PUMP 1","DRAIN PUMP 2","ACS DOOR LOCK","COIN POWER","HEATER"];
const LV=['lv_checks','lv_alcLow','lv_alcHigh','lv_sctLow','lv_sctHigh','lv_helm','lv_wsaIn','lv_wsaOut','lv_wssIn','lv_wssOut','lv_wsHigher','lv_tries','lv_interval','lv_acs'];
const RU_EXTRA=['ru_min','ru_coins','ru_val','ru_unit','ru_max','ru_free','ru_freeR','ru_freeM'];
const PHN=["Humidifier 1 (alcohol)","Humidifier 2 (scented)","Humidifier 3 (both)","UV 1","UV 2","HEATER (hair dryer)"];
let S={}, dirty={levels:false,rules:false,phases:false,drying:false}, pend={levels:null,rules:null,phases:null,drying:null}, loaded={levels:'',rules:'',phases:'',drying:''}, mm=0;
const $=id=>document.getElementById(id);
// band rows
const bandsBox=$('bands');
for(let i=0;i<4;i++){const d=document.createElement('div');d.className='row';
 d.innerHTML='<label>Rating</label><input class="n" id="ru_f'+i+'" type="number" min="1" max="10"><label>to</label><input class="n" id="ru_t'+i+'" type="number" min="1" max="10"><label>suggests</label><input class="n" id="ru_m'+i+'" type="number" min="1" max="120"><label>min</label>';bandsBox.appendChild(d);}
// phase rows: one line per relay with a Start / Middle / End tick each, for the PAID cleaning (P) and the FREE retry (F)
['P','F'].forEach(k=>{const box=$('ph'+k+'Relays');
 PHN.forEach((n,i)=>{const d=document.createElement('div');d.className='row';
  d.innerHTML='<b style="min-width:150px">'+n+'</b>'+[['1','Start'],['2','Middle'],['4','End']].map(p=>'<label><input type="checkbox" id="ph'+k+'_p'+i+'_'+p[0]+'"> '+p[1]+'</label>').join(' ');box.appendChild(d);});});
// relay buttons
const rel=$('rel');
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>relClick(i);rel.appendChild(b);});
// maintenance safety: each hand-set relay switches itself off after 2 s (the S3 does it); LATCH = relays that are exempt, DRAIN = relays that ask first
const MAINT_ST=25,LATCH=[8,9,11],DRAIN=[8,9];let lastClick=0,byTouch=0;
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
 if(id.startsWith('lv_')||id.startsWith('wsa_')||id.startsWith('wss_'))dirty.levels=true;else if(id.startsWith('ru_'))dirty.rules=true;else if(id.startsWith('ph'))dirty.phases=true;else if(id.startsWith('dr_'))dirty.drying=true;
 if(id.startsWith('ph'))phCheck();});});

function num(id){return parseFloat($(id).value);}
function setv(id,v){const el=$(id);if(el.type==='checkbox')el.checked=!!(+v);else el.value=v;}
function getv(id){const el=$(id);return el.type==='checkbox'?(el.checked?1:0):(el.value===''?'0':el.value);}
function levelsCsv(){return LV.map(getv).join(',');}
function updMaxNote(){const c=+getv('ru_coins'),v=+getv('ru_val'),u=+getv('ru_unit'),m=+getv('ru_max'),el=$('ru_maxnote');
 if(!(c>0&&v>0&&m>0)){el.textContent='';return;}
 const sec=m*v*(u?60:1)/c,mn=sec/60;el.textContent='= '+(mn>=1?(Math.round(mn*10)/10)+' min':Math.round(sec)+' s')+' of time, P'+m+' ('+m+' coins at 1 peso each)';}
setInterval(updMaxNote,400);
function rulesCsv(){const p=[];for(let i=0;i<4;i++){p.push(getv('ru_f'+i),getv('ru_t'+i),getv('ru_m'+i));}RU_EXTRA.forEach(id=>p.push(getv(id)));return p.join(',');}
function fillLevels(csv){const v=csv.split(',');if(v.length!==14)return;LV.forEach((id,i)=>setv(id,v[i]));dirty.levels=false;}
function fillRules(csv){const v=csv.split(',');if(v.length!==20)return;for(let i=0;i<4;i++){setv('ru_f'+i,v[i*3]);setv('ru_t'+i,v[i*3+1]);setv('ru_m'+i,v[i*3+2]);}RU_EXTRA.forEach((id,i)=>setv(id,v[12+i]));dirty.rules=false;}
function patOf(k){const pat=[];for(let i=0;i<6;i++){let v=0;[1,2,4].forEach(b=>{if($('ph'+k+'_p'+i+'_'+b).checked)v|=b;});pat.push(v);}return pat;}
function phasesCsv(){return [getv('phP_on'),getv('phF_on'),getv('phP_sv'),getv('phP_su'),getv('phP_ev'),getv('phP_eu'),getv('phF_sv'),getv('phF_su'),getv('phF_ev'),getv('phF_eu')].concat(patOf('P'),patOf('F')).join(',');}
function fillPhases(csv){const v=csv.split(',');if(v.length!==22)return;
 setv('phP_on',v[0]);setv('phF_on',v[1]);setv('phP_sv',v[2]);setv('phP_su',v[3]);setv('phP_ev',v[4]);setv('phP_eu',v[5]);
 setv('phF_sv',v[6]);setv('phF_su',v[7]);setv('phF_ev',v[8]);setv('phF_eu',v[9]);
 for(let i=0;i<6;i++){const a=+v[10+i],b=+v[16+i];[1,2,4].forEach(bit=>{$('phP_p'+i+'_'+bit).checked=!!(a&bit);$('phF_p'+i+'_'+bit).checked=!!(b&bit);});}
 dirty.phases=false;phCheck();}
// the heater must never run in a part where a humidifier is also on
function phCheck(){const bad=[];[['P','PAID'],['F','FREE']].forEach(x=>{const p=patOf(x[0]);const mist=p[0]|p[1]|p[2];if(p[5]&mist)bad.push(x[1]);});
 $('phWarn').textContent=bad.length?('Warning: in the '+bad.join(' and ')+' cleaning the heater shares a part with a humidifier - the mist and the hot air would run together. Untick one of them.'):'';}
function drCsv(){return getv('dr_cool');}
function fillDrying(csv){const v=csv.split(',');if(v.length!==1)return;setv('dr_cool',v[0]);dirty.drying=false;}
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function useLive(id,key){const v=S[key];if(!S.a2ok||!(v>0)){alert('No live reading from B2 (or no echo).');return;}setv(id,(+v).toFixed(1));dirty.levels=true;}
function capWater(w,which){
 if(!S.a2ok){alert('No live reading from B2.');return;}
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
async function send(kind,csv,statusId){pend[kind]=csv;$(statusId).textContent='sending...';$(statusId).className='note';await q('/send?kind='+kind+'&csv='+encodeURIComponent(csv));dirty[kind]=false;}
function sendLevels(){send('levels',levelsCsv(),'lvStatus');}
function sendRules(){send('rules',rulesCsv(),'ruStatus');}
function sendPhases(){send('phases',phasesCsv(),'phStatus');}
function sendDrying(){send('drying',drCsv(),'drStatus');}
const FILL={levels:fillLevels,rules:fillRules,phases:fillPhases,drying:fillDrying};
const ST_EL={levels:'lvStatus',rules:'ruStatus',phases:'phStatus',drying:'drStatus'};
function loadFrom(kind){if(S[kind])FILL[kind](S[kind]);}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function render(){
 const L=$('live');
 let h='';
 h+=S.s3aok?('S3B: <b class="ok">online</b> ('+age(S.s3aage)+') · step <b>'+(ST[S.state]||S.state)+'</b>'+(S.timer?(' · '+Math.floor(S.timer/60)+':'+String(S.timer%60).padStart(2,'0')+' left'):'')):'S3B: <b class="err">not heard</b> - is it on, on channel 1, running the new firmware?';
 h+='<br>B2 sensors: '+(S.a2ok?('<b class="ok">ok</b> ('+age(S.a2age)+')'):'<b class="err">silent</b>');
 h+=' · B3 relays: '+(S.a3ok?('<b class="ok">ok</b>'+(S.a3orph?' <span class="err">(B3 sees no S3B)</span>':'')+' · on: 0x'+S.a3mask.toString(16).toUpperCase().padStart(4,'0')):'<b class="err">silent</b>');
 h+='<br>Coins: B1 '+(S.a1ok?'<b class="ok">ok</b>':'<b class="err">silent</b>')+' · pulses B1 has counted: <b>'+S.coins+'</b>'+(S.coinage>=0?(' (last change '+age(S.coinage)+')'):' (none since B1 started)')+' · acceptor power from B3: <b>'+(((S.a3mask>>11)&1)?'ON':'off')+'</b>';
 h+='<br>Coin wire: B1 saw <b>'+S.raw+'</b> pulse edges in total · shortest gap between two <b>'+(S.gap>0?S.gap.toFixed(1)+' ms':'(none yet)')+'</b>'+(S.raw>S.coins?(' · <span class="warn">'+(S.raw-S.coins)+' edge(s) were filtered out or ignored</span>'):'');
 if(S.a2ok){
  h+='<br>Helmet <b>'+S.helm.toFixed(1)+'</b> cm · Alcohol container <b>'+S.als.toFixed(1)+'</b> cm · Scented container <b>'+S.sls.toFixed(1)+'</b> cm <span class="note">(0 = no echo)</span>';
  h+='<br>Water sensors: alcohol raw <b>'+S.raw0+'</b> · scented raw <b>'+S.raw1+'</b> · enclosure door <b>'+(S.door?'OPEN':'shut')+'</b>';
 }
 L.innerHTML=hubLines(h);
 // keep the forms in step with the S3B unless the user is typing in them
 ['levels','rules','phases','drying'].forEach(k=>{if(S[k]&&!dirty[k]&&S[k]!==loaded[k])FILL[k](S[k]);loaded[k]=S[k]||'';
  if(pend[k]){const el=$(ST_EL[k]);if(S[k]&&same(S[k],pend[k])){el.textContent='S3B has these values ✓';el.className='note ok';pend[k]=null;}else el.textContent='waiting for the S3B to confirm (or it refused - see its serial log)...';}});
 $('mt').className=S.state==25?'on':'';$('mt').textContent='Maintenance: '+(S.state==25?'ON':'OFF');
 for(let i=0;i<13;i++){const b=$('r'+i);b.className=((S.a3mask>>i)&1)?'on':'';b.disabled=S.state!=25;}
 if(S.state!=25)mm=0;else if(Date.now()-lastClick>1500)mm=S.a3mask&0x1FFF;
 bypassSync();
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules  : '+(S.rules||'(not received yet)')+'\nlevels : '+(S.levels||'(not received yet)')+'\nphases : '+(S.phases||'(not received yet)')+'\ndrying : '+(S.drying||'(not received yet)');
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
const char PAGE_ACS[] PROGMEM = R"hubpage(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ACS Settings</title>
<meta name="theme-color" content="#090d16"><link rel="stylesheet" href="/hub.css"><script src="/hub.js"></script></head><body class="t-acs"><div class="hubnav"><a href="/">Dashboard</a><a href="/#settings">&#9881; Settings</a><a href="/box1">Box 1</a><a href="/box2">Box 2</a><a href="/box3">Box 3</a><a class="cur" href="/acs">ACS 1 / 2</a></div>
<h1>ACS settings &middot; refill systems <span id="net" class="warn"></span></h1>
<div class="note">Changes the ACS's saved settings over the air; each ACS keeps them in its own memory. Pick ACS 1 (Box 1) or ACS 2 (Box 3) below - everything on this page then talks to that one.</div>
<div class="row"><button id="u1" onclick="pick(1)">ACS 1 (Box 1)</button><button id="u2" onclick="pick(2)">ACS 2 (Box 3)</button></div>

<h2>Live</h2>
<div class="box" id="live">...</div>
<div class="grid" id="conts" style="margin-top:8px"></div>
<div id="chips" style="margin-top:8px"></div>

<h2>Mixer container lines (LEVELS)</h2>
<div class="note">The ultrasonic reads the DISTANCE from the sensor down to the liquid, so a SMALLER number is a FULLER container. Fill the container to the line, tap "use live", then Send. <b>topup</b>: the refill recipe starts once the liquid is this far down. <b>low</b>: deliveries stop here and the refill is forced. <b>full</b>: the pumps stop here. <b>empty</b>: the distance to the bottom (only for the % read-out). Needs empty &gt; low &ge; topup &gt; full.</div>
<table id="lv"></table>
<div class="row"><button class="go" onclick="send('levels')">Send LEVELS</button><button onclick="loadFrom('levels')">Reload</button><span id="lvStatus" class="note"></span></div>

<h2>Mix (how much of each top-up is water)</h2>
<div class="row"><label>Alcohol container: water</label><input class="n" id="mx_waterA" type="number" step="1" min="1" max="99"><label>%</label>
 <label>Scented container: water</label><input class="n" id="mx_waterS" type="number" step="1" min="1" max="99"><label>%</label></div>
<div class="note">The water goes in first, up to this share of what still fits, then the alcohol (or the scented liquid) fills up to FULL. 30 % water in the alcohol container = 70/30. The scented share is small, so the scented container's FULL line and its level readings decide how exact it can be - the boss decides the number.</div>
<div class="row"><button class="go" onclick="send('mix')">Send MIX</button><button onclick="loadFrom('mix')">Reload</button><span id="mxStatus" class="note"></span></div>

<h2>Timing and water-level sensors (TIMING)</h2>
<table id="tm"></table>
<div class="note"><b>Water-level (WL) sensors:</b> dip the alcohol container's sensor in the liquid and tap WET, take it out and tap DRY (same for the scented one), then "Work out the thresholds". Alcohol conducts poorly, so a WL sensor may read much lower in the alcohol mix than in water - calibrate with the real liquid.</div>
<div class="row"><button class="sm" onclick="cap('A','dry')">Alc WL is DRY now</button><button class="sm" onclick="cap('A','wet')">Alc WL is WET now</button>
 <button class="sm" onclick="cap('S','dry')">Scn WL is DRY now</button><button class="sm" onclick="cap('S','wet')">Scn WL is WET now</button>
 <button class="sm go" onclick="calcWL()">Work out the thresholds</button><span id="capNote" class="note"></span></div>
<div class="row"><button class="go" onclick="send('timing')">Send TIMING</button><button onclick="loadFrom('timing')">Reload</button><span id="tmStatus" class="note"></span></div>

<h2>Try it (pretend to be the box)</h2>
<div class="note">The ACS pumps ONE bounded shot from the mixer container to the box per request ("deliver" seconds, or until LOW). A mixer container that is LOW or holds a half-made mix delivers nothing.</div>
<div class="row"><button onclick="q('/req?k=0&s=0')">Ask: alcohol</button><button onclick="q('/req?k=1&s=0')">Ask: scented</button>
 <button onclick="q('/req?k=0&s=1')">Stop: alcohol</button><button onclick="q('/req?k=1&s=1')">Stop: scented</button></div>
<div class="row"><button onclick="q('/retry')">Retry now (forget the EMPTY verdicts)</button><button onclick="q('/mixok')">Mixers are fine as they are (clear half-made flags)</button></div>

<h2>Maintenance</h2>
<div class="note">While Maintenance is on the automatic work is paused and the pumps below are switched by hand - one at a time. A hand-set pump switches itself off after 60 s, and Maintenance ends by itself after 30 minutes without a command. Never run a pump dry.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.step==10?0:1))">Maintenance</button></div>
<div class="grid" id="pp"></div>

<div class="note" id="diag"></div>
<details><summary class="note">Raw settings as the ACS reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
const API='/api/acs';let UNIT=1;
const ST=["IDLE","ALC WATER IN","ALC ALCOHOL IN","SCT WATER IN","SCT SCENTED IN","DELIVER ALCOHOL","DELIVER SCENTED","WATER EMPTY","ALCOHOL EMPTY","SCENTED EMPTY","MAINTENANCE"];
const PN=["ALCOHOL pump (supply &rarr; alc mixer)","WATER pump (&rarr; alc mixer)","WATER pump (&rarr; scn mixer)","SCENTED pump (supply &rarr; scn mixer)","MIXER ALCOHOL pump (&rarr; box)","MIXER SCENTED pump (&rarr; box)"];
const WARN=[[1,'no echo from the alcohol container\'s ultrasonic'],[2,'no echo from the scented container\'s ultrasonic'],[4,'alcohol container: WL is wet but the ultrasonic reads far from full'],[8,'scented container: WL is wet but the ultrasonic reads far from full'],
 [16,'alcohol container holds a half-made mix (not delivered until the recipe completes)'],[32,'scented container holds a half-made mix (not delivered until the recipe completes)'],[64,'a pump hit its time limit without reaching its line (blocked tube or a wrong line setting)']];
const LV=[['emptyA','Alc: empty (to the bottom)','usA'],['topupA','Alc: refill from','usA'],['lowA','Alc: LOW','usA'],['fullA','Alc: FULL','usA'],
          ['emptyS','Scn: empty (to the bottom)','usS'],['topupS','Scn: refill from','usS'],['lowS','Scn: LOW','usS'],['fullS','Scn: FULL','usS']];
const MX=['waterA','waterS'];
const TM=[['norise','No rise within','s','Pump running this long with no rise = that supply is empty.',1],['risecm','A rise is at least','cm','How far the distance must shrink to count (small rooms use half of what is left).',0.1],
 ['wlrise','or a WL move of','raw','How far the WL reading must move to count as a rise.',1],['phasemax','One pump runs at most','s','Hard limit; then the fills pause.',1],
 ['deliver','One delivery shot','s','How long a mixer pump runs per request from the box.',1],['emptyhold','Try an "empty" supply again after','s','Default 300.',1],
 ['reqhold','A request from the box stays valid','s','The box repeats it every ~10 s.',1],['holdoff','Fills pause after a time limit','s','',1],
 ['wlenter','WL means WET from','raw','',1],['wlleave','WL means DRY again below','raw','The gap is the hysteresis.',1]];
let S={}, dirty={levels:false,timing:false,mix:false}, pend={levels:null,timing:null,mix:null}, loaded={levels:'',timing:'',mix:''}, capd={A:{},S:{}};
const $=id=>document.getElementById(id);
// level rows
const lv=$('lv');
LV.forEach(r=>{const tr=document.createElement('tr');
 tr.innerHTML='<td>'+r[1]+'</td><td><input class="n" id="lv_'+r[0]+'" type="number" step="0.1" min="1"> cm</td><td><button class="sm" onclick="useLive(\'lv_'+r[0]+'\',\''+r[2]+'\')">use live</button></td>';lv.appendChild(tr);});
// timing rows
const tm=$('tm');
TM.forEach(r=>{const tr=document.createElement('tr');
 tr.innerHTML='<td>'+r[1]+'</td><td><input class="n" id="tm_'+r[0]+'" type="number" step="'+r[4]+'" min="0"> '+r[2]+'</td><td class="note">'+r[3]+'</td>';tm.appendChild(tr);});
{const tr=document.createElement('tr');tr.innerHTML='<td>WL reading goes UP in liquid</td><td><input type="checkbox" id="tm_wlhigher"></td><td class="note">Most water-level boards: yes.</td>';tm.appendChild(tr);}
// pump buttons
const pp=$('pp');
PN.forEach((n,i)=>{const b=document.createElement('button');b.id='p'+i;b.innerHTML=i+' '+n;b.onclick=()=>q('/pump?i='+i+'&on='+(((S.pumps>>i)&1)?0:1));pp.appendChild(b);});
// container cards
const conts=$('conts');
['A','S'].forEach(k=>{const d=document.createElement('div');d.className='cont';d.id='c'+k;conts.appendChild(d);});
document.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>{const id=el.id;
 if(id.startsWith('lv_'))dirty.levels=true;else if(id.startsWith('tm_'))dirty.timing=true;else if(id.startsWith('mx_'))dirty.mix=true;});});

function setv(id,v){const el=$(id);if(el.type==='checkbox')el.checked=!!(+v);else el.value=v;}
function getv(id){const el=$(id);return el.type==='checkbox'?(el.checked?1:0):(el.value===''?'0':el.value);}
function levelsCsv(){return LV.map(r=>getv('lv_'+r[0])).join(',');}
function mixCsv(){return MX.map(n=>getv('mx_'+n)).join(',');}
function timingCsv(){return TM.map(r=>getv('tm_'+r[0])).concat([getv('tm_wlhigher')]).join(',');}
function fillLevels(csv){const v=csv.split(',');if(v.length!==8)return;LV.forEach((r,i)=>setv('lv_'+r[0],v[i]));dirty.levels=false;}
function fillMix(csv){const v=csv.split(',');if(v.length!==2)return;MX.forEach((n,i)=>setv('mx_'+n,v[i]));dirty.mix=false;}
function fillTiming(csv){const v=csv.split(',');if(v.length!==11)return;TM.forEach((r,i)=>setv('tm_'+r[0],v[i]));setv('tm_wlhigher',v[10]);dirty.timing=false;}
const FILL={levels:fillLevels,mix:fillMix,timing:fillTiming}, CSV={levels:levelsCsv,mix:mixCsv,timing:timingCsv}, ST_EL={levels:'lvStatus',mix:'mxStatus',timing:'tmStatus'};
function same(a,b){const x=a.split(',').map(parseFloat),y=b.split(',').map(parseFloat);if(x.length!==y.length)return false;return x.every((v,i)=>Math.abs(v-y[i])<0.06);}
function useLive(id,key){const v=S[key];if(!S.ok||!(v>0)){alert('No live reading (ACS silent, or no echo).');return;}setv(id,(+v).toFixed(1));dirty.levels=true;}
function send(kind){const c=CSV[kind]();
 if(kind==='levels'){const v=c.split(',').map(parseFloat);for(let k=0;k<2;k++){const e=v[k*4],t=v[k*4+1],l=v[k*4+2],f=v[k*4+3];
  if(!(e>l&&l>=t&&t>f&&f>=1)){alert('Check the '+(k?'scented':'alcohol')+' lines: empty > low >= topup > full >= 1.');return;}}}
 pend[kind]=c;$(ST_EL[kind]).textContent='sending...';$(ST_EL[kind]).className='note';q('/send?kind='+kind+'&csv='+encodeURIComponent(c));dirty[kind]=false;}
function loadFrom(kind){if(S[kind])FILL[kind](S[kind]);}
function cap(k,w){const v=k==='A'?S.wlA:S.wlS;capd[k][w]=v;$('capNote').textContent='Alc: dry '+(capd.A.dry??'-')+' wet '+(capd.A.wet??'-')+' | Scn: dry '+(capd.S.dry??'-')+' wet '+(capd.S.wet??'-');}
function calcWL(){const ds=[],ws=[];['A','S'].forEach(k=>{if(capd[k].dry!=null&&capd[k].wet!=null){ds.push(capd[k].dry);ws.push(capd[k].wet);}});
 if(!ds.length){alert('Capture DRY and WET for at least one container first.');return;}
 const d=ds.reduce((a,b)=>a+b)/ds.length,w=ws.reduce((a,b)=>a+b)/ws.length;
 if(Math.abs(w-d)<60){alert('DRY and WET readings are too close ('+Math.round(d)+' / '+Math.round(w)+') - is the sensor really in the liquid?');return;}
 const hi=w>d;setv('tm_wlhigher',hi?1:0);setv('tm_wlenter',Math.round(d+0.6*(w-d)));setv('tm_wlleave',Math.round(d+0.4*(w-d)));setv('tm_wlrise',Math.max(20,Math.round(Math.abs(w-d)*0.25)));dirty.timing=true;}
function age(ms){return (ms/1000).toFixed(1)+' s ago';}
function chip(t,c){return '<span class="chip '+(c||'')+'">'+t+'</span>';}
function card(k){const pct=k==='A'?S.pctA:S.pctS,us=k==='A'?S.usA:S.usS,wl=k==='A'?S.wlA:S.wlS,fl=S.flags;
 const low=(fl>>(k==='A'?0:2))&1,full=(fl>>(k==='A'?1:3))&1,wet=(fl>>(k==='A'?4:5))&1;
 return '<b>'+(k==='A'?'ALCOHOL mixer container':'SCENTED mixer container')+'</b><div class="bar"><i style="width:'+pct+'%"></i></div>'
  +'Level <b>'+pct+' %</b> &middot; ultrasonic <b>'+(us>0?us.toFixed(1)+' cm':'no echo')+'</b><br>Water-level sensor raw <b>'+wl+'</b> &middot; '+(wet?'<b class="ok">WET</b>':'dry')+'<br>'
  +(low?chip('LOW','bad'):'')+(full?chip('FULL','good'):'');}
function render(){
 $('u1').className=S.unit==1?'sel':'';$('u2').className=S.unit==2?'sel':'';
 const L=$('live');let h='';
 h+=S.ok?('ACS '+S.unit+': <b class="ok">online</b> ('+age(S.age)+') &middot; step <b>'+(ST[S.step]||S.step)+'</b> &middot; '+(S.step>=7&&S.step<=9?('retry in '+S.sec+' s'):(S.step==0||S.step==10?'':(S.sec+' s in this step'))))
   :'ACS '+S.unit+': <b class="err">not heard</b> - is it on, on channel 1, running the new refill firmware (ESP32_ACS'+S.unit+')?';
 L.innerHTML=hubLines(h);
 if(S.ok){$('cA').innerHTML=card('A');$('cS').innerHTML=card('S');
  let c='';
  c+=S.empty&1?chip('WATER EMPTY','bad'):'';c+=S.empty&2?chip('ALCOHOL EMPTY','bad'):'';c+=S.empty&4?chip('SCENTED EMPTY','bad'):'';
  c+=(S.flags&128)?chip('the box is asking','run'):'';
  PN.forEach((n,i)=>{if((S.pumps>>i)&1)c+=chip('running: '+n,'run');});
  WARN.forEach(w=>{if(S.warn&w[0])c+='<div class="note warn">&#9888; '+w[1]+'</div>';});
  $('chips').innerHTML=c||'<span class="note">No empty supply, no pump running, no warnings.</span>';
 }else{$('cA').innerHTML='';$('cS').innerHTML='';$('chips').innerHTML='';}
 // keep the forms in step with the ACS unless the user is typing in them
 ['levels','mix','timing'].forEach(k=>{if(S[k]&&!dirty[k]&&S[k]!==loaded[k])FILL[k](S[k]);loaded[k]=S[k]||'';
  if(pend[k]){const el=$(ST_EL[k]);if(S[k]&&same(S[k],pend[k])){el.textContent='ACS has these values ✓';el.className='note ok';pend[k]=null;}else el.textContent='waiting for the ACS to confirm (or it refused - see the serial log)...';}});
 $('mt').className=S.step==10?'on':'';$('mt').textContent='Maintenance: '+(S.step==10?'ON':'OFF');
 for(let i=0;i<6;i++){const b=$('p'+i);b.className=((S.pumps>>i)&1)?'on':'';b.disabled=S.step!=10;}
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='levels: '+(S.levels||'(not received yet)')+'\nmix   : '+(S.mix||'(not received yet)')+'\ntiming: '+(S.timing||'(not received yet)');
}
async function q(u){try{const r=await fetch(API+u+(u.indexOf('?')>=0?'&':'?')+'u='+UNIT,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
function pick(n){UNIT=n;dirty={levels:false,timing:false,mix:false};loaded={levels:'',timing:'',mix:''};q('/state');}
q('/state');setInterval(()=>q('/state'),700);
</script></body></html>)hubpage";
