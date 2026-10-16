// The page of the history-file uploader (kept in a header so the Arduino prototype scanner does not read the JavaScript).
// The team never sees JSON: they fill in one card per customer (date, time, box, coins, status, minutes) and the page builds history.json itself.
#pragma once
static const char ENTRY_HTML[] PROGMEM = R"JHE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jolly Helmet - customer history entry</title>
<style>
:root{--bg:#f4f6fa;--card:#fff;--ink:#14213a;--mut:#5b6678;--line:#d9dfea;--navy:#0b1f3a;--gold:#f2a900;--ok:#1b7f3b;--bad:#b3261e;--hi:#fff6dd}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,Segoe UI,Arial,sans-serif}
header{background:var(--navy);color:#fff;padding:14px 16px;border-bottom:4px solid var(--gold)}
header h1{margin:0;font-size:20px}header p{margin:3px 0 0;color:#c8d3e6;font-size:13px}
main{max-width:900px;margin:0 auto;padding:14px 14px 60px;display:grid;gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
h2{margin:0 0 6px;font-size:17px}h2 span{display:inline-block;background:var(--gold);color:#241a00;border-radius:99px;min-width:24px;text-align:center;margin-right:8px;font-size:14px}
.pills{display:flex;flex-wrap:wrap;gap:8px}.pill{padding:4px 10px;border-radius:99px;font-size:13px;border:1px solid var(--line);background:#fff}
.pill.ok{border-color:var(--ok);color:var(--ok)}.pill.bad{border-color:var(--bad);color:var(--bad)}
button,a.btn,label.btn{font:inherit;border:0;border-radius:10px;padding:10px 14px;background:var(--navy);color:#fff;cursor:pointer;text-decoration:none;display:inline-block;min-height:44px}
.gold{background:var(--gold);color:#241a00;font-weight:700}.light{background:#e8edf6;color:var(--navy)}.red{background:#fdecea;color:var(--bad)}
button:disabled{opacity:.45;cursor:not-allowed}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
.note{color:var(--mut);font-size:14px}.err{background:#fdecea;border:1px solid var(--bad);border-radius:8px;padding:8px 10px;color:#611a15;font-size:14px;margin-top:8px}
.okb{background:#e8f5ec;border:1px solid var(--ok);border-radius:8px;padding:8px 10px;color:#14492a;font-size:14px;margin-top:8px}
.warnb{background:var(--hi);border:1px solid var(--gold);border-radius:8px;padding:8px 10px;font-size:14px;margin-top:8px}
.rc{display:grid;grid-template-columns:repeat(auto-fit,minmax(118px,1fr));gap:8px;align-items:end;border:2px solid var(--line);border-radius:12px;padding:10px;margin-top:10px;position:relative}
.rc.bad{border-color:var(--bad);background:#fff8f7}
.rc .rn{grid-column:1/-1;font-weight:700;color:var(--mut);font-size:13px}
.rc label{display:grid;gap:3px;font-size:12px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.03em}
.rc input,.rc select{font:inherit;font-size:16px;padding:9px 8px;border:1px solid var(--line);border-radius:8px;background:#fff;color:var(--ink);width:100%;text-transform:none;letter-spacing:0;font-weight:400}
.rc .rx{grid-column:1/-1;display:flex;gap:8px;flex-wrap:wrap}.rc .rx button{min-height:38px;padding:6px 12px;font-size:14px}
.rc .re{grid-column:1/-1;color:var(--bad);font-size:13px;min-height:0}
.rc input.calc{background:var(--hi);border-color:var(--gold)}.rc .rd{grid-column:1/-1;color:var(--mut);font-size:12px;min-height:0}
.rates{display:grid;gap:8px;margin-top:8px}.rates .rr{display:flex;flex-wrap:wrap;align-items:center;gap:6px}.rates .rr b{min-width:44px}.rates input{width:68px;font:inherit;font-size:16px;padding:8px 6px;border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:5px 8px;border-bottom:1px solid var(--line);text-align:right}
th:first-child,td:first-child{text-align:left}th{font-size:12px;color:var(--mut);font-weight:600;text-transform:uppercase}
.scroll{overflow-x:auto;max-height:240px;overflow-y:auto}
textarea{width:100%;min-height:150px;font:13px/1.35 ui-monospace,Consolas,monospace;border:1px solid var(--line);border-radius:8px;padding:8px;resize:vertical}
details{margin-top:8px}summary{cursor:pointer;color:var(--navy);font-weight:600}
select.mini{font:inherit;padding:8px;border:1px solid var(--line);border-radius:8px}
.daynav input{flex:1;min-width:140px;font:inherit;font-size:16px;padding:9px;border:1px solid var(--line);border-radius:8px}
.chips{display:flex;gap:6px;overflow-x:auto;padding:8px 0 4px}
.chip{flex:none;border:1px solid var(--line);background:#fff;color:var(--ink);border-radius:99px;padding:6px 12px;font-size:13px;min-height:36px;white-space:nowrap}
.chip.cur{background:var(--navy);color:#fff;border-color:var(--navy)}.chip.bad{border-color:var(--bad);color:var(--bad)}.chip.cur.bad{background:var(--bad);color:#fff}
.dh{margin-top:8px;font-size:15px}
</style></head><body>
<header><h1>Customer history entry</h1><p>For the days before the LocalServer kept dates: type in the previous customers from the team's own records. Never a name. The LocalServer chart shows these customers lighter, labelled "previous data collected by the team after the upgrade".</p></header>
<main>
<div class="card"><div class="pills"><span class="pill" id="pWifi">LocalServer Wi-Fi: ...</span><span class="pill" id="pSaved">Saved copy: ...</span></div>
<p class="note" id="wifiNote" style="margin:6px 0 0"></p>
<div id="draftBox"></div></div>

<div class="card"><h2><span>1</span>Start from</h2>
<div class="row"><button class="light" id="bPull">What is on the LocalServer now</button><button class="light" id="bSaved">The copy saved on this board</button><label class="btn light" style="margin:0">A file from this phone / PC<input type="file" id="file" accept=".json,application/json" style="display:none"></label></div>
<details><summary>Paste rows from Excel or Google Sheets</summary>
<p class="note">One customer per line, columns in this order: <b>date, time, box, coins, status, minutes</b> (minutes is optional). Status can be F / C or Finished / Cancelled.</p>
<div class="row"><label class="note">Dates are written as&nbsp;<select class="mini" id="dstyle"><option value="ymd">2026-09-17 (year-month-day)</option><option value="mdy">9/17/2026 (month/day/year)</option><option value="dmy">17/9/2026 (day/month/year)</option></select></label></div>
<textarea id="paste" placeholder="2026-09-17&#9;14:35&#9;1&#9;15&#9;Finished&#9;5&#10;2026-09-17&#9;15:10&#9;3&#9;15&#9;Cancelled&#9;2"></textarea>
<div class="row"><button class="light" id="bPaste">Add these rows</button><span class="note" id="pasteMsg"></span></div></details>
<p class="note" style="margin-bottom:0">Or just start typing below.</p></div>

<div class="card"><h2><span>2</span>One day at a time</h2>
<p class="note" style="margin:0">Pick the day, then add the customers of that day. The time is when the customer used the machine (see the form and the photo). Type the <b>coins</b> and the page works out the <b>minutes</b>, or type the <b>minutes</b> and it works out the <b>coins</b> (the box's rate is below). The number the page worked out is shaded; type over it if the form says something else.</p>
<details id="rateBox"><summary>Coins and minutes (the rates) - tap to see or change</summary>
<p class="note">These are the rates the LocalServer starts with: Box 1 and Box 3 take 15 coins = 5 minutes, Box 2 takes 10 coins = 3 minutes. If the price list was different on the day of a form, change it here (it is kept in this browser and used for the customers below).</p>
<div class="rates" id="rates"></div>
<div class="row"><button class="light" id="bRateDef">Use the LocalServer's starting rates</button></div></details>
<div class="row daynav"><button class="light" id="dPrev" aria-label="Previous day">&#9664;</button><input type="date" id="dayPick"><button class="light" id="dNext" aria-label="Next day">&#9654;</button><button class="light" id="dToday">Today</button></div>
<div class="chips" id="chips"></div>
<div id="dayHead"></div>
<div id="list"></div>
<div class="row"><button class="gold" id="bAdd">＋ Add a customer on this day</button><button class="light" id="bSort">Sort all days by time</button><button class="red" id="bClear">Clear everything</button></div>
<div id="sum"></div></div>

<div class="card"><h2><span>3</span>Send to the LocalServer</h2>
<p class="note" style="margin-top:0">This <b>replaces</b> the LocalServer's history with the customers above. The LocalServer checks every row again and keeps the old history if anything is wrong. The machine records and the totals are never touched.</p>
<div class="row"><button class="gold" id="bPush" disabled>Push to the LocalServer</button><a class="btn light" href="/saved.json" download="history.json">Download the saved file</a><span class="note" id="pushMsg"></span></div>
<div id="pushOut"></div>
<details><summary>Show the file (for people who like code)</summary>
<textarea id="json" spellcheck="false"></textarea>
<div class="row"><button class="light" id="bUseJson">Use this text</button><span class="note" id="jsonMsg"></span></div></details></div>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let rows=[];                 // {date,time,box,coins,cancel,min,src}  src = which of coins / min was typed (the other is worked out), '' = both as loaded
let busy=false;
function today(){const d=new Date();return d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0');}
// ---- coins <-> minutes. The rates are what the LocalServer starts with (Box 1 and 3: 20 s per coin, so 15 coins = 5 min; Box 2: 18 s per coin, so 10 coins = 3 min).
// They can be changed in the "rates" box (kept in this browser). Each customer remembers which number was TYPED (src) and which was worked out.
const RATE_DEFAULT={1:{c:15,m:5},2:{c:10,m:3},3:{c:15,m:5}};
let RATES=JSON.parse(JSON.stringify(RATE_DEFAULT));
try{const s=JSON.parse(localStorage.getItem('jh_entry_rates')||'null');if(s)[1,2,3].forEach(b=>{if(s[b]&&s[b].c>=1&&s[b].m>0)RATES[b]={c:+s[b].c,m:+s[b].m};});}catch(e){}
function rateOf(box){return RATES[box]||RATES[1];}
function coinsToMin(box,coins){coins=Number(coins);if(!(coins>=1))return'';const q=rateOf(box);return Math.round(coins*q.m/q.c*100)/100;}
function minToCoins(box,min){min=Number(min);if(!(min>0))return'';const q=rateOf(box);return Math.max(1,Math.round(min*q.c/q.m));}
function normRow(r){if(r.src===undefined)r.src=r.minAuto?'coins':'';delete r.minAuto;return r;}
function newRow(prev){const box=prev?prev.box:1,coins=prev?prev.coins:rateOf(box).c;return{date:prev?prev.date:today(),time:'',box,coins,cancel:0,min:coinsToMin(box,coins),src:'coins'};}
function rateText(box){const q=rateOf(box);return q.c+' coins = '+q.m+' min';}
function hintText(r){
  if(r.src==='coins'&&r.coins!=='')return'Minutes worked out from the coins (Box '+r.box+': '+rateText(r.box)+'). Type the minutes yourself to change it.';
  if(r.src==='min'&&r.min!=='')return'Coins worked out from the minutes (Box '+r.box+': '+rateText(r.box)+'). Type the coins yourself to change it.';
  return'';
}
function realDate(s){const m=/^(\d{4})-(\d{2})-(\d{2})$/.exec(String(s||'').trim());if(!m)return null;const y=+m[1],mo=+m[2],d=+m[3];if(y<2020||y>2099||mo<1||mo>12||d<1||d>31)return null;const dt=new Date(Date.UTC(y,mo-1,d));return(dt.getUTCFullYear()===y&&dt.getUTCMonth()===mo-1&&dt.getUTCDate()===d)?dt:null;}
function rowErr(r){
  if(!realDate(r.date))return'Pick the date.';
  if(!/^([01]?\d|2[0-3]):[0-5]\d$/.test(String(r.time||'')))return'Pick the time.';
  if(![1,2,3].includes(Number(r.box)))return'Choose the box.';
  const c=Number(r.coins);if(!(Number.isInteger(c)&&c>=1&&c<=999))return'Coins must be a whole number from 1 to 999.';
  if(r.min!==''&&r.min!==null&&r.min!==undefined){const m=Number(r.min);if(!(Number.isFinite(m)&&m>=0&&m<=720))return'Minutes must be 0 to 720, for example 5 or 2.5 (or empty).';}
  return'';
}
function pad2(n){return String(n).padStart(2,'0');}
function hhmm(t){const m=/^(\d{1,2}):(\d{2})$/.exec(String(t));return m?pad2(+m[1])+':'+m[2]:t;}
function makeJson(list){
  const lines=list.map(r=>' ['+JSON.stringify(r.date)+','+JSON.stringify(hhmm(r.time))+','+Number(r.box)+','+Number(r.coins)+','+JSON.stringify(r.cancel?'C':'F')+(r.min===''||r.min===null||r.min===undefined?'':','+Number(r.min))+']');
  return '{\n "source": "previous data collected after the upgrade",\n "rows": [\n'+lines.join(',\n')+'\n ]\n}\n';
}
let curDay=today();
const MON=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'],DOWN=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function dayLabel(s){const d=realDate(s);return d?DOWN[d.getUTCDay()]+' '+MON[d.getUTCMonth()]+' '+d.getUTCDate()+', '+d.getUTCFullYear():s;}
function setDay(s){if(!realDate(s))return;curDay=s;$('dayPick').value=s;buildList();}
function shiftDay(n){const d=realDate(curDay)||realDate(today());d.setUTCDate(d.getUTCDate()+n);setDay(d.toISOString().slice(0,10));}
function firstDay(){const ds=rows.map(r=>r.date).filter(realDate).sort();setDay(ds.length?ds[0]:today());}   // after loading rows: jump to the first day that has customers
function dayIdx(){return rows.map((r,i)=>i).filter(i=>rows[i].date===curDay);}
function cardHtml(r,i,k){
  return '<div class="rc" id="rc'+i+'" data-i="'+i+'"><div class="rn">Customer '+(k+1)+' of '+esc(dayLabel(curDay))+'</div>'+
   '<label>Time<input type="time" data-f="time" value="'+esc(r.time)+'"></label>'+
   '<label>Box<select data-f="box"><option value="1"'+(r.box==1?' selected':'')+'>Box 1 - Cleaner</option><option value="2"'+(r.box==2?' selected':'')+'>Box 2 - Dryer</option><option value="3"'+(r.box==3?' selected':'')+'>Box 3 - Cleaner + Dryer</option></select></label>'+
   '<label>Coins<input type="number" inputmode="numeric" min="1" max="999" data-f="coins" value="'+esc(r.coins)+'"></label>'+
   '<label>Status<select data-f="cancel"><option value="0"'+(r.cancel?'':' selected')+'>Finished</option><option value="1"'+(r.cancel?' selected':'')+'>Cancelled</option></select></label>'+
   '<label>Minutes<input type="number" inputmode="decimal" step="any" min="0" max="720" data-f="min" value="'+esc(r.min)+'"></label>'+
   '<label>Move to day<input type="date" data-f="move" value=""></label>'+
   '<div class="rd"></div><div class="rx"><button class="light" data-act="copy" title="Copies this customer; the new time is this time plus the minutes of this customer">Copy this customer</button><button class="red" data-act="del">Remove</button></div><div class="re" id="re'+i+'"></div></div>';
}
function buildList(){
  rows.forEach(normRow);
  const host=$('list'),ix=dayIdx();
  host.innerHTML=ix.length?ix.map((i,k)=>cardHtml(rows[i],i,k)).join(''):'<p class="note">No customers on this day yet. Press "Add a customer on this day".</p>';
  $('dayPick').value=curDay;
  ix.forEach(i=>markCalc($('rc'+i),rows[i]));
  refresh();
}
$('list').addEventListener('input',e=>{
  const f=e.target.dataset.f,c=e.target.closest('.rc');if(!f||!c)return;const r=rows[+c.dataset.i];
  if(f==='move'){if(realDate(e.target.value)){const to=e.target.value;r.date=to;buildList();$('dayHead').insertAdjacentHTML('beforeend','<div class=\"note\">Moved 1 customer to '+esc(dayLabel(to))+'.</div>');}return;}
  if(f==='time')r.time=e.target.value;
  else if(f==='box'){r.box=+e.target.value;derive(c,r);}
  else if(f==='cancel')r.cancel=+e.target.value;
  else if(f==='coins'){r.coins=e.target.value===''?'':+e.target.value;r.src='coins';derive(c,r);}
  else if(f==='min'){r.min=e.target.value===''?'':+e.target.value;r.src='min';derive(c,r);}
  refresh();
});
// fills the one that was NOT typed: the minutes from the coins, or the coins from the minutes (a customer loaded from a file keeps both numbers until one is edited)
function derive(c,r){
  if(r.src==='coins'){r.min=coinsToMin(r.box,r.coins);c.querySelector('[data-f=min]').value=r.min;}
  else if(r.src==='min'){const k=minToCoins(r.box,r.min);if(k!==''){r.coins=k;c.querySelector('[data-f=coins]').value=r.coins;}}
  markCalc(c,r);
}
function markCalc(c,r){
  if(!c)return;
  c.querySelector('[data-f=min]').classList.toggle('calc',r.src==='coins'&&r.min!=='');
  c.querySelector('[data-f=coins]').classList.toggle('calc',r.src==='min'&&r.coins!=='');
  const h=c.querySelector('.rd');if(h)h.textContent=hintText(r);
}
// the rates box
function renderRates(){
  $('rates').innerHTML=[1,2,3].map(b=>'<div class="rr"><b>Box '+b+'</b><input type="number" inputmode="numeric" min="1" max="999" data-rb="'+b+'" data-rf="c" value="'+RATES[b].c+'"> coins = <input type="number" inputmode="decimal" step="any" min="0.1" max="720" data-rb="'+b+'" data-rf="m" value="'+RATES[b].m+'"> minutes</div>').join('');
}
function recalcAll(){rows.forEach(r=>{if(r.src==='coins')r.min=coinsToMin(r.box,r.coins);else if(r.src==='min'){const k=minToCoins(r.box,r.min);if(k!=='')r.coins=k;}});buildList();}
$('rates').addEventListener('input',e=>{
  const b=e.target.dataset.rb,f=e.target.dataset.rf;if(!b)return;const v=Number(e.target.value);
  if(f==='c'&&v>=1)RATES[b].c=Math.round(v);else if(f==='m'&&v>0)RATES[b].m=v;else return;
  try{localStorage.setItem('jh_entry_rates',JSON.stringify(RATES));}catch(x){}
  recalcAll();
});
$('bRateDef').onclick=()=>{RATES=JSON.parse(JSON.stringify(RATE_DEFAULT));try{localStorage.removeItem('jh_entry_rates');}catch(x){}renderRates();recalcAll();};
$('list').addEventListener('click',e=>{
  const b=e.target.closest('[data-act]'),c=e.target.closest('.rc');if(!b||!c)return;const i=+c.dataset.i;
  if(b.dataset.act==='del'){rows.splice(i,1);if(!rows.length)clearDraft();buildList();}
  else if(b.dataset.act==='copy'){
    // the copy starts when this customer ends: its time = this time + this customer's minutes (decimals rounded to the nearest minute); nothing to add -> the time stays empty
    const s=rows[i],n=Object.assign({},s,{time:''});let note='';
    const tm=/^(\d{1,2}):(\d{2})$/.exec(hhmm(s.time)),mins=(s.min===''||s.min===null||s.min===undefined)?0:Math.round(Number(s.min));
    if(tm&&mins>0){
      let tot=(+tm[1])*60+(+tm[2])+mins;
      if(tot>=1440){tot-=1440;const d=realDate(s.date);if(d){d.setUTCDate(d.getUTCDate()+1);n.date=d.toISOString().slice(0,10);}}
      n.time=pad2(Math.floor(tot/60))+':'+pad2(tot%60);
      note='Copied: the new customer starts '+mins+' min after the first ('+hhmm(s.time)+' + '+mins+' min = '+n.time+')'+(n.date!==s.date?', which is already '+dayLabel(n.date)+' - the copy is on that day':'')+'. Change the time if the form says otherwise.';
    }
    rows.splice(i+1,0,n);buildList();
    if(note)$('dayHead').insertAdjacentHTML('beforeend','<div class="note">'+esc(note)+'</div>');
    const t=document.querySelector('#rc'+(i+1)+' [data-f=time]');if(t)t.focus();
  }
});
$('bAdd').onclick=()=>{const ix=dayIdx(),n=newRow(ix.length?rows[ix[ix.length-1]]:null);n.date=curDay;rows.push(n);buildList();const t=document.querySelector('#rc'+(rows.length-1)+' [data-f=time]');if(t){t.scrollIntoView({block:'center'});t.focus();}};
$('bSort').onclick=()=>{rows.sort((a,b)=>(a.date+' '+hhmm(a.time)).localeCompare(b.date+' '+hhmm(b.time)));buildList();};
$('bClear').onclick=()=>{if(rows.length&&!confirm('Remove all '+rows.length+' customers from this page? (Nothing on the LocalServer changes until you push.)'))return;rows=[];clearDraft();buildList();};
$('dayPick').addEventListener('change',e=>{if(e.target.value)setDay(e.target.value);});
$('dPrev').onclick=()=>shiftDay(-1);$('dNext').onclick=()=>shiftDay(1);$('dToday').onclick=()=>setDay(today());
$('chips').addEventListener('click',e=>{const b=e.target.closest('[data-day]');if(b)setDay(b.dataset.day);});
function renderChips(badDays){
  const cnt={};rows.forEach(r=>{cnt[r.date]=(cnt[r.date]||0)+1;});
  const days=Object.keys(cnt).filter(realDate).sort();
  $('chips').innerHTML=days.length?days.map(d=>'<button class=\"chip'+(d===curDay?' cur':'')+(badDays.has(d)?' bad':'')+'\" data-day=\"'+d+'\">'+esc(MON[+d.slice(5,7)-1]+' '+(+d.slice(8,10)))+' &middot; '+cnt[d]+'</button>').join(''):'';
}
function renderDayHead(){
  const ix=dayIdx();let coins=0,can=0,n=0;
  ix.forEach(i=>{if(!rowErr(rows[i])){n++;coins+=Number(rows[i].coins);if(rows[i].cancel)can++;}});
  $('dayHead').innerHTML='<div class=\"dh\"><b>'+esc(dayLabel(curDay))+'</b> - '+(ix.length?ix.length+' customer'+(ix.length===1?'':'s')+' ('+can+' cancelled), '+coins+' coins':'no customers yet: 0 customers, 0 coins')+'</div>';
}
function weekStart(s){const d=realDate(s);const k=(d.getUTCDay()+6)%7;d.setUTCDate(d.getUTCDate()-k);return d.toISOString().slice(0,10);}
function refresh(){
  let bad=0,first='';const badDays=new Set();
  rows.forEach((r,i)=>{const e=rowErr(r),c=$('rc'+i);if(c){c.classList.toggle('bad',!!e);$('re'+i).textContent=e;}if(e){bad++;badDays.add(r.date);if(!first)first=dayLabel(r.date)+': '+e;}});
  renderChips(badDays);renderDayHead();
  const good=rows.filter(r=>!rowErr(r));
  let h='';
  if(!rows.length)h='<div class="warnb">The list is empty. Pushing an empty list clears the history on the LocalServer.</div>';
  else if(bad)h='<div class="err"><b>'+bad+'</b> customer(s) still need attention. '+esc(first)+'</div>';
  else{
    const wk={};let can=0,coins=0;const bx=[0,0,0];
    good.forEach(r=>{const w=weekStart(r.date);(wk[w]=wk[w]||{n:0,c:0,x:0});wk[w].n++;wk[w].c+=Number(r.coins);if(r.cancel){wk[w].x++;can++;}coins+=Number(r.coins);bx[r.box-1]++;});
    const ds=good.map(r=>r.date).sort();
    h='<div class="okb"><b>'+good.length+'</b> customer'+(good.length===1?'':'s')+' ('+(good.length-can)+' finished, '+can+' cancelled), <b>'+coins+'</b> coins, '+ds[0]+' to '+ds[ds.length-1]+'. Box 1: '+bx[0]+' · Box 2: '+bx[1]+' · Box 3: '+bx[2]+'.</div>';
    h+='<div class="scroll"><table><tr><th>Week starting (Mon)</th><th>Customers</th><th>Cancelled</th><th>Coins</th></tr>'+Object.keys(wk).sort().map(k=>'<tr><td>'+k+'</td><td>'+wk[k].n+'</td><td>'+wk[k].x+'</td><td>'+wk[k].c+'</td></tr>').join('')+'</table></div>';
  }
  $('sum').innerHTML=h;
  $('bPush').disabled=busy||bad>0;
  if(document.activeElement!==$('json'))$('json').value=bad?'':makeJson(good);
  saveDraft();
}
// ---- loading ----
function rowsFromJson(text){
  let j;try{j=JSON.parse(text);}catch(e){return{err:'This is not valid JSON: '+e.message};}
  if(!j||!Array.isArray(j.rows))return{err:'No "rows" list found.'};
  const out=[];
  for(let i=0;i<j.rows.length;i++){const r=j.rows[i];if(!Array.isArray(r)||r.length<5)return{err:'Row '+(i+1)+' is not date, time, box, coins, F/C.'};
    const st=String(r[4]).trim().charAt(0).toUpperCase();
    out.push({date:String(r[0]).trim(),time:hhmm(String(r[1]).trim()),box:Number(r[2]),coins:Number(r[3]),cancel:st==='C'?1:0,min:(r.length>5&&String(r[5]).trim()!=='')?Number(r[5]):'',src:''});}
  return{rows:out};
}
function loadText(text,label){
  const p=rowsFromJson(text);
  if(p.err){$('pushOut').innerHTML='<div class="err">'+esc(p.err)+'</div>';return;}
  if(rows.length&&!confirm('Replace the '+rows.length+' customers on this page with the '+p.rows.length+' from '+label+'?'))return;
  rows=p.rows;$('pushOut').innerHTML='';firstDay();
}
$('file').addEventListener('change',e=>{const f=e.target.files[0];if(!f)return;const r=new FileReader();r.onload=()=>loadText(String(r.result),'the file');r.readAsText(f);e.target.value='';});
$('bUseJson').onclick=()=>{const p=rowsFromJson($('json').value);if(p.err){$('jsonMsg').textContent=p.err;return;}rows=p.rows;$('jsonMsg').textContent='';firstDay();};
$('bSaved').onclick=async()=>{try{const r=await fetch('/saved.json',{cache:'no-store'});if(!r.ok){$('pushOut').innerHTML='<div class="err">No copy is saved on this board yet (it keeps one after a successful push).</div>';return;}loadText(await r.text(),'the saved copy');}catch(e){}};
async function waitJob(label){
  for(let i=0;i<120;i++){await new Promise(r=>setTimeout(r,500));
    try{const s=await (await fetch('/api/state',{cache:'no-store'})).json();if(s.job.state==='done'||s.job.state==='fail')return s.job;$('pushMsg').textContent=label+'...';}catch(e){}}
  return{state:'fail',msg:'No answer from this board.'};
}
$('bPull').onclick=async()=>{
  $('pushOut').innerHTML='<p class="note">Asking the LocalServer...</p>';
  try{await fetch('/api/pull',{cache:'no-store'});const job=await waitJob('Reading from the LocalServer');
    if(job.state!=='done'){$('pushOut').innerHTML='<div class="err">'+esc(job.msg)+'</div>';return;}
    $('pushOut').innerHTML='';loadText(await (await fetch('/api/pulled',{cache:'no-store'})).text(),'the LocalServer');
  }catch(e){$('pushOut').innerHTML='<div class="err">No link to this board.</div>';}
};
// ---- paste from Excel ----
function parseDate(s,style){
  s=String(s).trim();let y,m,d,a;
  if(style==='ymd'){a=/^(\d{4})[-\/.](\d{1,2})[-\/.](\d{1,2})$/.exec(s);if(!a)return null;y=+a[1];m=+a[2];d=+a[3];}
  else{a=/^(\d{1,2})[-\/.](\d{1,2})[-\/.](\d{2,4})$/.exec(s);if(!a)return null;y=+a[3];if(y<100)y+=2000;if(style==='mdy'){m=+a[1];d=+a[2];}else{d=+a[1];m=+a[2];}}
  const iso=y+'-'+pad2(m)+'-'+pad2(d);return realDate(iso)?iso:null;
}
function parseTime(s){s=String(s).trim().toUpperCase();const a=/^(\d{1,2})[:.](\d{2})\s*(AM|PM)?$/.exec(s);if(!a)return null;let h=+a[1];const mi=+a[2];if(a[3]==='PM'&&h<12)h+=12;if(a[3]==='AM'&&h===12)h=0;if(h>23||mi>59)return null;return pad2(h)+':'+pad2(mi);}
$('bPaste').onclick=()=>{
  const style=$('dstyle').value,lines=$('paste').value.split(/\r?\n/).filter(l=>l.trim()),add=[],bad=[];
  lines.forEach((l,i)=>{
    const t=l.split(/\t|;|,/).map(x=>x.trim());
    const date=parseDate(t[0],style),time=parseTime(t[1]),box=Number(String(t[2]).replace(/[^0-9]/g,'')),st=String(t[4]||'').trim().charAt(0).toUpperCase();
    const hasC=Number(t[3])>=1,hasM=t[5]!==undefined&&String(t[5]).trim()!==''&&Number(t[5])>=0;
    let coins=Number(t[3]);if(!hasC&&hasM)coins=Number(minToCoins(box,Number(t[5])));         // the coins column is empty but the minutes are there: work the coins out
    if(!date||!time||![1,2,3].includes(box)||!(coins>=1&&coins<=999)||(st!=='F'&&st!=='C'&&st!=='')){bad.push(i+1);return;}
    const mn=hasM?Number(t[5]):coinsToMin(box,coins);
    add.push({date,time,box,coins:Math.round(coins),cancel:st==='C'?1:0,min:mn,src:hasM?(hasC?'':'min'):'coins'});
  });
  if(add.length){rows=rows.concat(add);curDay=add[0].date;buildList();}
  $('pasteMsg').textContent=add.length+' row(s) added.'+(bad.length?' Could not read line(s): '+bad.join(', ')+' (check the date style and the order of the columns).':'');
  if(add.length&&!bad.length)$('paste').value='';
};
// ---- push ----
$('bPush').onclick=async()=>{
  if(busy)return;
  const good=rows.filter(r=>!rowErr(r));if(good.length!==rows.length)return;
  if(!confirm('This replaces the LocalServer\'s history with '+good.length+' customer(s). Continue?'))return;
  busy=true;$('bPush').disabled=true;$('pushOut').innerHTML='';$('pushMsg').textContent='Sending...';
  try{
    const r=await fetch('/api/push',{method:'POST',headers:{'Content-Type':'text/plain'},body:makeJson(good)});const j=await r.json();
    if(!j.ok){$('pushOut').innerHTML='<div class="err">'+esc(j.msg||'Refused.')+'</div>';}
    else{const job=await waitJob('Sending to the LocalServer');
      if(job.state==='done'){$('pushOut').innerHTML='<div class="okb">'+esc(job.msg)+'<br>Open the LocalServer Home page: the chart now draws these customers lighter ("previous data collected by the team after the upgrade").</div>';clearDraft();}
      else $('pushOut').innerHTML='<div class="err">'+esc(job.msg)+'</div>';}
  }catch(e){$('pushOut').innerHTML='<div class="err">No link to this board.</div>';}
  $('pushMsg').textContent='';busy=false;refresh();refreshState();
};
// ---- an unsent draft survives closing the page (kept only in this browser) ----
function saveDraft(){try{if(rows.length)localStorage.setItem('jh_entry_draft',JSON.stringify({t:Date.now(),rows}));}catch(e){}}
function clearDraft(){try{localStorage.removeItem('jh_entry_draft');}catch(e){}}
function offerDraft(){try{const d=JSON.parse(localStorage.getItem('jh_entry_draft')||'null');if(d&&d.rows&&d.rows.length){
  $('draftBox').innerHTML='<div class="warnb">There is an unsent draft with <b>'+d.rows.length+'</b> customer(s) from '+esc(new Date(d.t).toLocaleString())+'. <button class="light" id="bRest">Restore it</button> <button class="light" id="bDisc">Discard</button></div>';
  $('bRest').onclick=()=>{rows=d.rows.map(normRow);$('draftBox').innerHTML='';firstDay();};$('bDisc').onclick=()=>{clearDraft();$('draftBox').innerHTML='';};}}catch(e){}}
async function refreshState(){
  try{const s=await (await fetch('/api/state',{cache:'no-store'})).json();
    const w=$('pWifi');w.textContent=s.sta?'LocalServer Wi-Fi: connected':'LocalServer Wi-Fi: not connected';w.className='pill '+(s.sta?'ok':'bad');
    const v=$('pSaved');v.textContent=s.saved.has?'Saved copy: '+s.saved.bytes+' bytes':'Saved copy: none yet';v.className='pill';
    $('wifiNote').textContent=s.sta?'':'The board is trying to join "'+s.ssid+'". The LocalServer must be on.';
  }catch(e){}
}
renderRates();buildList();offerDraft();refreshState();setInterval(refreshState,3000);
</script></body></html>)JHE";
