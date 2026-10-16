// The page of the price recalibration board (kept in a header so the Arduino prototype scanner does not read the JavaScript).
#pragma once
static const char RESTATE_HTML[] PROGMEM = R"JHR(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jolly Helmet - price recalibration</title>
<style>
:root{--bg:#f4f6fa;--card:#fff;--ink:#14213a;--mut:#5b6678;--line:#d9dfea;--navy:#0b1f3a;--gold:#f2a900;--ok:#1b7f3b;--bad:#b3261e;--hi:#fff6dd}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,Segoe UI,Arial,sans-serif}
header{background:var(--navy);color:#fff;padding:14px 16px;border-bottom:4px solid var(--gold)}
header h1{margin:0;font-size:19px}header p{margin:2px 0 0;color:#c8d3e6;font-size:13px}
main{max-width:860px;margin:0 auto;padding:14px 16px 40px;display:grid;gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
h2{margin:0 0 8px;font-size:16px}h2 span{color:var(--gold);margin-right:6px}
.pills{display:flex;flex-wrap:wrap;gap:8px}.pill{padding:4px 10px;border-radius:99px;font-size:13px;border:1px solid var(--line);background:#fff}
.pill.ok{border-color:var(--ok);color:var(--ok)}.pill.bad{border-color:var(--bad);color:var(--bad)}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.tile{border:1px solid var(--line);border-radius:8px;padding:8px 10px}.tile b{display:block;font-size:20px}.tile small{color:var(--mut)}
table{border-collapse:collapse;width:100%}th,td{padding:6px 8px;border-bottom:1px solid var(--line);text-align:right;vertical-align:middle}
th:first-child,td:first-child{text-align:left}th{font-size:12px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
tr.sec td{background:#eef2f9;font-weight:700;text-align:left;color:var(--navy)}tr.hl td{background:var(--hi);font-weight:700}
input[type=number]{width:74px;padding:5px;border:1px solid var(--line);border-radius:6px;font:inherit;text-align:right}
button,a.btn{font:inherit;border:0;border-radius:8px;padding:9px 14px;background:var(--navy);color:#fff;cursor:pointer;text-decoration:none;display:inline-block}
button.gold{background:var(--gold);color:#241a00;font-weight:700}button.ghost{background:#fff;color:var(--bad);border:1px solid var(--bad)}
button:disabled{opacity:.45;cursor:not-allowed}.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.note{color:var(--mut);font-size:13px}.warn{background:#fdecea;border:1px solid var(--bad);border-radius:8px;padding:8px 10px;color:#611a15;font-size:14px}
.prev{background:var(--hi);border:1px solid var(--gold);border-radius:8px;padding:8px 10px;font-size:14px}
.scroll{overflow-x:auto}
</style></head><body>
<header><h1>Price recalibration</h1><p>Jolly Helmet - the old data, restated at the new price list. It listens to the LocalServer; it only sends something after you press Update, and the coins really collected are never changed.</p></header>
<main>
<div class="card"><div class="pills"><span class="pill" id="pLive">LocalServer: ...</span><span class="pill" id="pBase">Old data: ...</span></div></div>

<div class="card"><h2>Live totals <small class="note">(from the LocalServer, every 10 s)</small></h2>
<div class="tiles" id="tiles"></div></div>

<div class="card"><h2><span>1</span>Price lists</h2>
<div class="scroll"><table id="tPrice"><tr><th></th><th>NEW price list</th><th>OLD price list</th><th>Share of minutes</th></tr></table></div>
<p class="note">The restated figures use the <b>new</b> list only (coins = minutes served x new coins per minute). The old list is only used to show how many coins the old price explains. If the boxes did not serve equal minutes, change the share (Box 1 : Box 2 : Box 3).</p>
<div class="row"><button id="bSave">Save price lists</button><span class="note" id="saveMsg"></span></div></div>

<div class="card"><h2><span>2</span>Capture the old data</h2>
<p class="note">Do this <b>after</b> the new rates are set on all three boxes. Everything the LocalServer has counted up to this moment becomes the OLD data and is copied into this board's flash (it survives power-off). Nothing in the LocalServer changes.</p>
<div class="row"><button class="gold" id="bCap">Capture the old data now</button><button class="ghost" id="bClr">Clear the capture</button><span class="note" id="capMsg"></span></div></div>

<div class="card"><h2><span>3</span>Apply the corrected figures to the LocalServer and the Online Server</h2>
<p class="note">The old price list charged more coins per minute than the new one, so the coin total of the old data is too high. <b>Apply</b> sends the corrected figures: the LocalServer <b>keeps the original figures</b> (in its flash, with a restore button) and replaces its coin total, so its revenue follows; the Online page follows at its next upload. Both show one small line saying the figures were corrected. Press Apply once after you saved the price lists. This board can be unplugged afterwards.</p>
<div class="row"><button class="gold" id="bPub">Apply now</button><button class="ghost" id="bWd">Stop sending</button><span class="note" id="pubMsg"></span></div>
<p class="note" id="pubState"></p></div>

<div class="card"><h2><span>4</span>Result</h2>
<div id="banner"></div>
<div class="scroll"><table id="tRes"></table></div>
<p class="note" id="reco"></p>
<div class="row"><a class="btn" href="/report.csv" download="JollyHelmet_price_recalibration.csv">Download report (CSV)</a><button id="bCopy">Copy report text</button><span class="note" id="copyMsg"></span></div></div>

<div class="card" id="cHist" hidden><h2>Last sessions at the capture</h2><div class="scroll"><table id="tHist"></table></div></div>

<div class="card"><h2>How the numbers are made</h2>
<p class="note">Customers served and minutes served do not depend on the price, so they are the same in both columns. <b>Restated coins</b> = minutes served x the new coins per minute. <b>Restated revenue</b> = restated coins x the coin value. The coins that were really collected are shown next to it and are never changed. Coins that the minutes served cannot explain (test or bench pulses, aborted runs, refunds, free-retry minutes) stay in "as collected" only. The LocalServer keeps only totals, not a record per sale, so this is an estimate by design: write it in the document as "restated", never as "revenue collected".</p></div>
</main>
<script>
var $=function(i){return document.getElementById(i)};
var S=null,loaded=false,dirty=false;
function n(x){return Number(x).toLocaleString('en-US')}
function m(x){return Number(x).toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2})}
function mmss(s){s=Math.round(s);return Math.floor(s/60)+':'+('0'+(s%60)).slice(-2)}
function hm(s){s=Math.round(s);var h=Math.floor(s/3600),mi=Math.round((s%3600)/60);if(mi===60){h++;mi=0}return (h?h+' h ':'')+mi+' min'}
function stamp(at,tz){var d=new Date((at-tz*60)*1000);return d.toISOString().slice(0,16).replace('T',' ')}
function fillPrices(s){
  var t=$('tPrice');while(t.rows.length>1)t.deleteRow(1);
  for(var i=0;i<3;i++){var r=t.insertRow();
    r.insertCell().textContent='Box '+(i+1);
    r.insertCell().innerHTML='<input type=number min=1 max=999 id=nc'+i+' value='+s.nc[i]+'> coins = <input type=number step=any min=0.1 id=nm'+i+' value='+s.nm[i]+'> min';
    r.insertCell().innerHTML='<input type=number min=1 max=999 id=oc'+i+' value='+s.oc[i]+'> coins = <input type=number step=any min=0.1 id=om'+i+' value='+s.om[i]+'> min';
    r.insertCell().innerHTML='<input type=number step=any min=0 id=sh'+i+' value='+s.sh[i]+'>';}
  var ins=t.getElementsByTagName('input');for(var k=0;k<ins.length;k++)ins[k].oninput=function(){dirty=true;$('saveMsg').textContent='not saved yet'};
}
function row(t,a,b,c,cls){var r=t.insertRow();if(cls)r.className=cls;r.insertCell().textContent=a;if(b!==undefined)r.insertCell().textContent=b;if(c!==undefined)r.insertCell().textContent=c;return r}
function sec(t,a){var r=t.insertRow();r.className='sec';var c=r.insertCell();c.colSpan=3;c.textContent=a}
function render(){
  var s=S,L=s.live,B=s.base,C=s.calc;
  $('pLive').className='pill '+(L.seen&&L.age<30000?'ok':'bad');
  $('pLive').textContent=L.seen?('LocalServer heard '+Math.round(L.age/1000)+' s ago (#'+L.seq+')'):'LocalServer not heard yet - is it on? (channel 1)';
  $('pBase').className='pill '+(B.valid?'ok':'');
  $('pBase').textContent=B.valid?('Old data captured '+stamp(B.at,B.tz)):'Old data not captured yet (preview only)';
  $('tiles').innerHTML=L.seen?('<div class=tile><b>'+n(L.ss)+'</b><small>Customers served</small></div><div class=tile><b>'+n(L.cn)+'</b><small>Coins counted</small></div><div class=tile><b>PHP '+m(L.cn*L.cv)+'</b><small>Revenue (coins x '+m(L.cv)+')</small></div><div class=tile><b>'+hm(L.du)+'</b><small>Time served</small></div><div class=tile><b>'+mmss(L.ss?L.du/L.ss:0)+'</b><small>Average session</small></div>'):'<p class=note>Waiting for the LocalServer...</p>';
  if(!loaded||(!dirty&&!document.activeElement.type)){fillPrices(s.set);loaded=true}
  $('bCap').disabled=B.valid||!(L.seen&&L.age<30000);
  $('bClr').disabled=!B.valid;
  var P=s.pub||{};
  $('bPub').disabled=!B.valid||P.on==1;
  $('bWd').disabled=!P.on;
  $('pubState').textContent=P.on?((P.mode==1?'Applied as update #':'Published as update #')+P.rev+', sent '+P.sent+' times'+(P.ago>=0?(', last '+Math.round(P.ago/1000)+' s ago'):'')+'.'+(P.stale?' The price lists changed since: on the LocalServer press Restore original, then Apply again.':'')+' Check the LocalServer Home page (Statistics) and the Online page.'):'Nothing sent.';
  var bn='';
  if(C.preview)bn='<div class=prev><b>PREVIEW.</b> Nothing is captured yet: the numbers below treat the live totals as the old data. Set the new rates on the boxes, then press Capture.</div>';
  if(C.down)bn+='<div class=warn><b>The LocalServer totals are lower than at the capture</b>: its statistics were reset after the capture. The figures since the capture are not valid.</div>';
  $('banner').innerHTML=bn;
  var t=$('tRes');t.innerHTML='';
  var h=t.insertRow();['','As collected','Restated at the new list'].forEach(function(x){var c=document.createElement('th');c.textContent=x;h.appendChild(c)});
  sec(t,C.preview?'OLD DATA (live totals, preview)':'OLD DATA (up to the capture)');
  row(t,'Customers served',n(C.oSs),n(C.oSs));
  row(t,'Time served',hm(C.oDu),hm(C.oDu));
  row(t,'Coins',n(C.oCn),n(C.oRestCn));
  row(t,'Revenue (PHP)',m(C.oCollRev),m(C.oRestRev),'hl');
  sec(t,'SINCE THE CAPTURE (sold at the new list)');
  row(t,'Customers served',n(C.nSs),n(C.nSs));
  row(t,'Time served',hm(C.nDu),hm(C.nDu));
  row(t,'Coins',n(C.nCn),n(C.nCn));
  row(t,'Revenue (PHP)',m(C.nRev),m(C.nRev),'hl');
  sec(t,'TOTAL (old + new)');
  row(t,'Customers served',n(C.tSs),n(C.tSs));
  row(t,'Coins',n(C.tCollCn),n(C.tRestCn));
  row(t,'Revenue (PHP)',m(C.tCollRev),m(C.tRestRev),'hl');
  $('reco').textContent='Old data check: the old price list explains '+n(C.oExpCn)+' coins from the minutes served; '+n(C.oCn)+' were collected, so '+n(C.oUnexpl)+' coins are not explained by paid minutes (test or bench pulses, aborted runs, refunds, free-retry minutes). They are in "as collected" only.';
  var H=$('tHist'),hv=B.valid&&B.hd&&B.hd.length;$('cHist').hidden=!hv;
  if(hv){H.innerHTML='<tr><th>#</th><th>Seconds</th><th>Coins paid</th><th>At the old list</th><th>At the new list</th></tr>';
    for(var i=0;i<B.hd.length;i++){var r=H.insertRow();r.insertCell().textContent=i+1;r.insertCell().textContent=B.hd[i];r.insertCell().textContent=B.hc[i];
      r.insertCell().textContent=Math.ceil(B.hd[i]*C.cpsOld-0.0001);r.insertCell().textContent=Math.ceil(B.hd[i]*C.cpsNew-0.0001)}}
}
function poll(){fetch('/api/state',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){S=j;render()}).catch(function(){$('pLive').className='pill bad';$('pLive').textContent='This board does not answer'})}
function q(id){return document.getElementById(id).value}
$('bSave').onclick=function(){
  var u='/api/settings?';for(var i=0;i<3;i++)u+='nc'+i+'='+q('nc'+i)+'&nm'+i+'='+q('nm'+i)+'&oc'+i+'='+q('oc'+i)+'&om'+i+'='+q('om'+i)+'&sh'+i+'='+q('sh'+i)+'&';
  fetch(u).then(function(r){return r.json()}).then(function(j){$('saveMsg').textContent=j.msg;if(j.ok){dirty=false;loaded=false}poll()});
};
$('bCap').onclick=function(){
  if(!confirm('Capture the old data now?\n\nUse this only after the new rates are set on all three boxes. The copy cannot be overwritten until you clear it.'))return;
  var d=new Date();
  fetch('/api/capture?at='+Math.floor(d.getTime()/1000)+'&tz='+d.getTimezoneOffset()).then(function(r){return r.json()}).then(function(j){$('capMsg').textContent=j.msg;poll()});
};
$('bClr').onclick=function(){
  if(!confirm('Clear the captured old data?\n\nDo this only to capture again. Download the CSV first if you need it.'))return;
  fetch('/api/clear?sure=YES').then(function(r){return r.json()}).then(function(j){$('capMsg').textContent=j.msg;poll()});
};
$('bCopy').onclick=function(){
  fetch('/report.csv',{cache:'no-store'}).then(function(r){return r.text()}).then(function(t){
    t=t.replace(/^﻿/,'');
    var done=function(){$('copyMsg').textContent='Copied. Paste it into Excel or Word.'};
    if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(t).then(done,function(){fallback(t)})}else fallback(t);
    function fallback(x){var a=document.createElement('textarea');a.value=x;document.body.appendChild(a);a.select();try{document.execCommand('copy');done()}catch(e){$('copyMsg').textContent='Copy failed - use Download instead'}a.remove()}
  });
};
$('bPub').onclick=function(){
  if(!S||!S.base.valid){$('pubMsg').textContent='Capture the old data first.';return}
  if(!confirm('Apply the corrected figures now?\n\nThe LocalServer keeps the original figures and replaces its coin total (so its revenue) with the corrected one; the Online page follows. Both show a small note that the figures were corrected. You can undo it with Restore original on the LocalServer Home page.'))return;
  fetch('/api/publish').then(function(r){return r.json()}).then(function(j){$('pubMsg').textContent=j.msg;poll()});
};
$('bWd').onclick=function(){
  if(!confirm('Stop sending? What the LocalServer already applied stays.'))return;
  fetch('/api/withdraw').then(function(r){return r.json()}).then(function(j){$('pubMsg').textContent=j.msg;poll()});
};
poll();setInterval(poll,3000);
</script></body></html>
)JHR";
