// Web page for ESP32_S3A_CONFIG_BENCH. Lives in its own header ON PURPOSE: the Arduino IDE scans the .ino for C
// functions to auto-declare and mistakes the JavaScript "function ..." lines inside a raw string for C
// functions. Header files are not scanned.
#pragma once

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>S3A Settings Bench</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:12px;max-width:720px;margin:auto}
 h1{font-size:18px;margin:4px 0 10px}
 h2{font-size:14px;margin:18px 0 6px;color:#9ad}
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
<h1>S3A settings bench <span id="net" class="warn"></span></h1>
<div class="note">A stand-in for the LocalServer's settings page. It changes the S3A's saved settings over the air; the S3A keeps them in its own memory. Keep the real LocalServer OFF while this is on.</div>

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
 <label><input type="checkbox" id="lv_acs"> send the request to the ACS</label></div>
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

<h2>Maintenance</h2>
<div class="note">Only works when the S3A is on the idle (TAPS) or OUT OF ORDER screen. While on, the relays below are switched by hand; the enclosure lock rests itself after 60 s.</div>
<div class="row"><button id="mt" onclick="q('/maint?on='+(S.state==25?0:1))">Maintenance</button></div>
<div class="grid" id="rel"></div>

<div class="note" id="diag"></div>

<details><summary class="note">Raw settings as the S3A reports them</summary><div class="box"><code id="raw"></code></div></details>

<script>
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
RN.forEach((n,i)=>{const b=document.createElement('button');b.id='r'+i;b.textContent=i+' '+n;b.onclick=()=>{mm^=(1<<i);q('/mask?v='+mm);};rel.appendChild(b);});
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
 L.innerHTML=h;
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
 if(S.state!=25)mm=0;
 $('diag').textContent='This board: up '+S.up+' s · last restart: '+S.rst+' · phones connected '+S.clients+' · joins '+S.joins+' · drops '+S.leaves+(S.leaves?(' (last reason '+S.why+')'):'');
 $('raw').textContent='rules  : '+(S.rules||'(not received yet)')+'\nlevels : '+(S.levels||'(not received yet)')+'\nphases : '+(S.phases||'(not received yet)');
}
async function q(u){try{const r=await fetch(u,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
q('/state');setInterval(()=>q('/state'),700);
</script></body></html>)rawliteral";
