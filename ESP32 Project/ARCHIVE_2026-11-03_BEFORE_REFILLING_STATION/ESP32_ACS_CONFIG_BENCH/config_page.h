// Web page for ESP32_ACS_CONFIG_BENCH. Lives in its own header ON PURPOSE: the Arduino IDE scans the .ino for C
// functions to auto-declare and mistakes the JavaScript "function ..." lines inside a raw string for C
// functions. Header files are not scanned.
#pragma once

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ACS Settings Bench</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:12px;max-width:760px;margin:auto}
 h1{font-size:18px;margin:4px 0 10px}
 h2{font-size:14px;margin:18px 0 6px;color:#e9a65b}
 .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:7px 0}
 label{font-size:13px;color:#ccc}
 button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:8px;padding:9px 11px;font-size:14px}
 button.on{background:#1b6e2e;border-color:#3c3}
 button.go{background:#1e4f8a;border-color:#59f}
 button.sel{background:#7a4a10;border-color:#e9a65b}
 button.sm{padding:6px 8px;font-size:12px}
 button:disabled{opacity:.4}
 input,select{background:#222;color:#eee;border:1px solid #555;border-radius:6px;padding:7px;font-size:14px}
 input.n{width:78px}
 .box{border:1px solid #444;border-radius:8px;padding:8px;line-height:1.65}
 .note{color:#aaa;font-size:12px;margin:4px 0;line-height:1.5}
 .ok{color:#6d6}.warn{color:#fb4}.err{color:#f66}
 .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
 .cont{border:1px solid #444;border-radius:8px;padding:8px}
 .bar{height:14px;background:#222;border:1px solid #555;border-radius:7px;overflow:hidden;margin:6px 0}
 .bar i{display:block;height:100%;background:#2a8fd6}
 .chip{display:inline-block;border-radius:10px;padding:2px 9px;margin:2px 3px 2px 0;font-size:12px;background:#333}
 .chip.bad{background:#7a1f1f}.chip.good{background:#1b6e2e}.chip.run{background:#1e4f8a}
 code{color:#9c9;word-break:break-all;font-size:12px}
 details{margin-top:12px}
 table{border-collapse:collapse;width:100%}
 td{padding:3px 4px;font-size:13px}
</style></head><body>
<h1>ACS settings bench &middot; refill system <span id="net" class="warn"></span></h1>
<div class="note">A stand-in for the LocalServer's refill page. It changes the ACS's saved settings over the air; the ACS keeps them in its own memory. Keep the real LocalServer OFF while this is on.</div>
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
 L.innerHTML=h;
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
async function q(u){try{const r=await fetch(u,{cache:'no-store'});S=await r.json();render();$('net').textContent='';}
 catch(e){$('net').textContent='(no link)';}}
function pick(n){dirty={levels:false,timing:false,mix:false};loaded={levels:'',timing:'',mix:''};q('/unit?v='+n);}
q('/state');setInterval(()=>q('/state'),700);
</script></body></html>)rawliteral";
