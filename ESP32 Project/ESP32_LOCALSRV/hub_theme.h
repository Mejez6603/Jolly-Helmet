// Look and feel of the four settings pages (/box1 /box2 /box3 /acs), served as /hub.css and /hub.js (routes in settings_hub.h).
// The pages themselves (settings_pages.h) only hold the content and their own logic; this file decides how it looks:
//   - a sticky navigation bar, one accent colour per page (Box 1 blue, Box 2 orange, Box 3 violet, ACS teal - the same icons as the Control Center)
//   - every <h2> section becomes a card that folds open / shut (the fold state is remembered for this browser tab), with a row of jump chips on top
//   - the "Live" panel is split into one row per line (hubLines), relay / pump buttons become tiles with a lit dot when ON, tables stack on a phone
// The script never changes an id or an event handler of the page: it only moves the existing elements into the cards.
#pragma once
#include <Arduino.h>

const char HUB_CSS[] PROGMEM = R"hubtheme(
:root{color-scheme:dark;--bg:#090d16;--card:#111827;--well:#0b1220;--line:#1f293d;--line2:#2c3b57;--text:#e8eef7;--muted:#93a3ba;
 --accent:#38bdf8;--tint:rgba(56,189,248,.13);--tint2:rgba(56,189,248,.26);--onacc:#04121c;--ok:#4ade80;--warn:#fbbf24;--err:#f87171}
body.t-b2{--accent:#fb923c;--tint:rgba(251,146,60,.13);--tint2:rgba(251,146,60,.26)}
body.t-b3{--accent:#a78bfa;--tint:rgba(167,139,250,.14);--tint2:rgba(167,139,250,.28)}
body.t-acs{--accent:#2dd4bf;--tint:rgba(45,212,191,.13);--tint2:rgba(45,212,191,.26)}
*{box-sizing:border-box}
html{background:var(--bg);-webkit-text-size-adjust:100%}
body{margin:0;padding:0 14px calc(56px + env(safe-area-inset-bottom,0px));color:var(--text);
 font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif}
body::before{content:"";position:fixed;left:0;right:0;top:0;height:360px;z-index:-1;pointer-events:none;
 background:radial-gradient(800px 320px at 50% -110px,var(--tint2),transparent)}
body>*:not(.hubnav){width:100%;max-width:860px;margin-left:auto;margin-right:auto}

/* navigation bar */
.hubnav{position:sticky;top:0;z-index:40;display:flex;gap:6px;overflow-x:auto;margin:0 -14px 14px;
 padding:10px max(14px,calc((100% - 860px)/2 + 28px));background:rgba(9,13,22,.9);
 -webkit-backdrop-filter:blur(8px);backdrop-filter:blur(8px);border-bottom:1px solid var(--line);scrollbar-width:none}
.hubnav::-webkit-scrollbar{display:none}
.hubnav a{flex:none;color:var(--muted);text-decoration:none;border:1px solid var(--line2);border-radius:99px;padding:7px 14px;
 font-size:.82rem;font-weight:600;white-space:nowrap;transition:.15s}
.hubnav a:hover{color:var(--text);border-color:var(--accent)}
.hubnav a.cur{background:var(--tint2);color:#fff;border-color:var(--accent)}

/* title */
h1{margin:6px 0 4px;display:flex;align-items:center;flex-wrap:wrap;gap:4px 10px;font-size:1.45rem;font-weight:800;letter-spacing:-.01em}
h1::before{font-size:1.55rem;line-height:1}
body.t-b1 h1::before{content:"\1F4E6"}
body.t-b2 h1::before{content:"\1F525"}
body.t-b3 h1::before{content:"\1F9F4"}
body.t-acs h1::before{content:"\1F9EA"}
h1 #net{font-size:.7rem;font-weight:700;color:var(--err);background:rgba(239,68,68,.15);border-radius:99px;padding:2px 10px}
h1 #net:empty{display:none}

/* section cards */
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;margin-top:14px;margin-bottom:14px;overflow:hidden;scroll-margin-top:72px}
.card>h2{margin:0;padding:13px 16px;display:flex;align-items:baseline;gap:8px;cursor:pointer;user-select:none;-webkit-user-select:none;
 font-size:.95rem;font-weight:700;color:#fff;background:linear-gradient(90deg,var(--tint),transparent 75%);border-left:4px solid var(--accent)}
.card>h2 small{color:var(--muted);font-weight:500;font-size:.78rem}
.card>h2::after{content:"\25BE";margin-left:auto;color:var(--accent);font-size:1.05rem;transition:transform .2s}
.card>h2:focus-visible{outline:2px solid var(--accent);outline-offset:-2px}
.card.shut>h2::after{transform:rotate(-90deg)}
.card.shut>.cb{display:none}
.cb{padding:4px 16px 16px}
body>h2{margin:18px 0 6px;font-size:.95rem;color:var(--accent)}   /* only until the script has made cards of them */

/* jump chips */
.jump{display:flex;gap:6px;overflow-x:auto;padding:4px 0 6px;scrollbar-width:none}
.jump::-webkit-scrollbar{display:none}
.jump a,.jump button{flex:none;color:var(--muted);text-decoration:none;font-size:.76rem;font-weight:600;white-space:nowrap;
 background:var(--card);border:1px solid var(--line);border-radius:99px;padding:5px 12px;cursor:pointer}
.jump a:hover,.jump button:hover{color:#fff;border-color:var(--accent)}

/* text */
.note{color:var(--muted);font-size:.82rem;line-height:1.55}
div.note{margin:10px 0;padding:8px 12px;background:var(--well);border-left:3px solid var(--line2);border-radius:0 10px 10px 0}
div.note b{color:#cbd5e1}
body>div.note{background:none;border:0;padding:0 0 6px;margin:2px auto 10px}
div.note.warn{border-left-color:var(--warn);background:rgba(251,191,36,.07)}
.ok{color:var(--ok)}.warn{color:var(--warn)}.err{color:var(--err)}
code{color:#86efac;font-size:.78rem;word-break:break-all}
#raw{white-space:pre-wrap;display:block}
label{font-size:.88rem;color:#cbd5e1}
label>input[type=checkbox]{margin-right:6px;vertical-align:-3px}
b{font-weight:700}

/* rows and fields */
.row{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px;margin:11px 0}
.fld{display:inline-flex;flex-wrap:wrap;align-items:center;gap:6px 8px}
#bands .row,[id^=ph][id$=Relays] .row,#rfRows .row{background:var(--well);border:1px solid var(--line);border-radius:12px;padding:9px 12px;margin:7px 0}
input,select{background:#0a101d;color:var(--text);border:1px solid var(--line2);border-radius:10px;padding:8px 10px;font:inherit;font-size:.95rem;min-height:38px;transition:.15s}
input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--tint2)}
input.n{width:84px;text-align:center;font-variant-numeric:tabular-nums}
input[type=checkbox]{width:18px;height:18px;min-height:0;accent-color:var(--accent);padding:0}
select{padding-right:6px}

/* buttons */
button{font:inherit;font-size:.88rem;font-weight:600;color:var(--text);background:#1a2336;border:1px solid var(--line2);border-radius:10px;
 padding:9px 15px;cursor:pointer;transition:.15s}
button:hover:not(:disabled){border-color:var(--accent);background:#202c44}
button:active:not(:disabled){transform:scale(.98)}
button.go{background:var(--accent);border-color:var(--accent);color:var(--onacc)}
button.go:hover:not(:disabled){filter:brightness(1.1);background:var(--accent)}
button.sm{padding:5px 10px;font-size:.76rem;border-radius:8px;color:var(--accent);background:transparent}
button.sm.go{background:var(--accent);color:var(--onacc)}
button.on{background:rgba(34,197,94,.18);border-color:#22c55e;color:#bbf7d0}
button.sel,#u1.sel,#u2.sel{background:var(--accent);border-color:var(--accent);color:var(--onacc)}
button:disabled{opacity:.35;cursor:not-allowed}
.row>#u1,.row>#u2{flex:1 1 150px;padding:12px}
.row>#mt{min-width:190px}

/* live panel */
.box{background:var(--well);border:1px solid var(--line);border-radius:12px;padding:2px 14px;line-height:1.6}
#live .ln{padding:9px 0;border-bottom:1px solid #16213a;font-size:.9rem;line-height:1.65}
#live .ln:last-child{border-bottom:0}
#live b{color:#fff}
#live b.ok{color:var(--ok)}#live b.err{color:var(--err)}
.box>code{display:block;padding:8px 0}

/* relay and pump tiles */
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:8px}
#rel button,#pp button{display:flex;align-items:center;gap:10px;text-align:left;padding:12px 13px}
#rel button::before,#pp button::before{content:"";flex:none;width:9px;height:9px;border-radius:50%;background:#475569}
#rel button.on::before,#pp button.on::before{background:#22c55e;box-shadow:0 0 9px #22c55e}

/* maintenance safety tick box */
label.by{display:inline-flex;align-items:center;gap:10px;padding:10px 15px;border:1px solid var(--line2);border-radius:12px;background:var(--well);
 color:var(--text);font-weight:600;font-size:.9rem;cursor:pointer;transition:.15s}
label.by.active{border-color:var(--warn);background:rgba(251,191,36,.12);color:#fde68a}
label.by input[type=checkbox]{margin:0;accent-color:var(--warn)}
label.by input:disabled{cursor:not-allowed}

/* confirmation window (hubConfirm) */
.mod{position:fixed;inset:0;z-index:100;display:flex;align-items:center;justify-content:center;padding:16px;background:rgba(2,6,14,.78);
 -webkit-backdrop-filter:blur(3px);backdrop-filter:blur(3px)}
.mbox{width:100%;max-width:440px;background:var(--card);border:1px solid var(--warn);border-radius:16px;padding:20px 20px 16px;box-shadow:0 18px 60px rgba(0,0,0,.6)}
.mbox .mt{margin:0 0 6px;font-size:.78rem;font-weight:800;letter-spacing:.12em;color:var(--warn)}
.mbox .mh{margin:0 0 8px;font-size:1.12rem;font-weight:800;line-height:1.3}
.mbox .mp{margin:0 0 16px;color:#cbd5e1;font-size:.9rem;line-height:1.55}
.mbox .mrow{display:flex;gap:10px;flex-wrap:wrap}
.mbox .mrow button{flex:1 1 140px;padding:12px 14px}
.mbox .myes{background:#b91c1c;border-color:#ef4444;color:#fff}
.mbox .myes:hover:not(:disabled){background:#dc2626;border-color:#f87171}

/* ACS containers, bars, chips, tables */
#conts{grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px}
.cont{background:var(--well);border:1px solid var(--line);border-radius:12px;padding:12px 14px;line-height:1.7;font-size:.9rem}
.bar{height:12px;background:#0a101d;border:1px solid var(--line2);border-radius:99px;overflow:hidden;margin:8px 0}
.bar i{display:block;height:100%;border-radius:99px;background:linear-gradient(90deg,#0ea5e9,#2dd4bf);transition:width .4s}
.chip{display:inline-block;margin:3px 5px 3px 0;padding:3px 11px;border-radius:99px;font-size:.76rem;font-weight:700;background:#1c2740;color:#cbd5e1}
.chip.bad{background:rgba(239,68,68,.18);color:#fca5a5}
.chip.good{background:rgba(34,197,94,.18);color:#86efac}
.chip.run{background:var(--tint2);color:#fff}
table{width:100%;border-collapse:collapse}
td{padding:8px 4px;border-bottom:1px solid #18223a;font-size:.9rem;vertical-align:middle}
tr:last-child>td{border-bottom:0}
td.note{font-size:.76rem}
#lv td:nth-child(2),#lv td:nth-child(3),#tm td:nth-child(2){width:1%;white-space:nowrap}
#lv td:nth-child(3){padding-left:8px}
#tm td.note{padding-left:14px}

/* footer */
#diag{background:none;border:0;text-align:center;font-size:.74rem;padding:4px 0;margin:6px auto}
details{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:11px 16px;margin-top:14px;margin-bottom:14px}
summary{cursor:pointer;color:var(--muted);font-size:.82rem}
details[open]>summary{margin-bottom:8px}

@media(max-width:560px){
 body{padding-left:10px;padding-right:10px}
 .hubnav{margin-left:-10px;margin-right:-10px;padding:9px 10px}
 .cb{padding:2px 12px 14px}
 .card>h2{padding:12px}
 .row>button:not(.sm){flex:1 1 140px}
 input.n{width:76px}
 .row>b[style*="min-width"]{flex:1 1 100%;min-width:0!important}
 #lv tr,#tm tr{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:9px 0;border-bottom:1px solid #18223a}
 #lv td,#tm td{border:0;padding:0}
 #lv td:first-child,#tm td:first-child{flex:1 1 100%;font-weight:600}
 #tm td.note{flex:1 1 100%}
}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
)hubtheme";

const char HUB_JS[] PROGMEM = R"hubtheme(
(function(){
'use strict';
function all(s,r){return Array.prototype.slice.call((r||document).querySelectorAll(s));}
// the pages call this on the Live panel: one row per line
window.hubLines=function(h){return '<div class="ln">'+String(h).split('<br>').join('</div><div class="ln">')+'</div>';};
// A yes / no window for risky buttons: hubConfirm({title,head,text,yes,no}) -> Promise(true = yes). "No" is the default (Enter / Esc / a tap outside = no).
window.hubConfirm=function(o){return new Promise(function(res){
 var ov=document.createElement('div'),bx=document.createElement('div'),prev=document.activeElement;
 ov.className='mod';bx.className='mbox';bx.setAttribute('role','alertdialog');bx.setAttribute('aria-modal','true');
 function el(t,c,x){var e=document.createElement(t);e.className=c;e.textContent=x||'';return e;}
 var row=el('div','mrow'),no=el('button','mno',o.no||'No'),yes=el('button','myes',o.yes||'Yes');
 no.type='button';yes.type='button';
 bx.appendChild(el('div','mt',o.title||'WARNING'));bx.appendChild(el('div','mh',o.head||''));
 if(o.text)bx.appendChild(el('div','mp',o.text));
 row.appendChild(no);row.appendChild(yes);bx.appendChild(row);ov.appendChild(bx);document.body.appendChild(ov);
 function done(v){document.removeEventListener('keydown',key,true);ov.remove();if(prev&&prev.focus)try{prev.focus();}catch(e){}res(v);}
 function key(e){if(e.key==='Escape'){e.preventDefault();done(false);}}
 document.addEventListener('keydown',key,true);
 no.addEventListener('click',function(){done(false);});yes.addEventListener('click',function(){done(true);});
 ov.addEventListener('click',function(e){if(e.target===ov)done(false);});
 no.focus();
});};
var KEY='hubfold:'+location.pathname;
function loadFold(){try{return JSON.parse(sessionStorage.getItem(KEY)||'{}');}catch(e){return {};}}
function saveFold(o){try{sessionStorage.setItem(KEY,JSON.stringify(o));}catch(e){}}
// keep "label + field (+ its small button, + a short unit like min or cm)" together, so a phone never splits "High at" from its box
function isField(e){return e&&((e.tagName==='INPUT'&&e.type!=='checkbox')||e.tagName==='SELECT');}
function isPlainLabel(e){return e&&e.tagName==='LABEL'&&!e.querySelector('input');}
function groupRows(){
 all('.row').forEach(function(row){
  var kids=Array.prototype.slice.call(row.children),i=0;
  while(i<kids.length){
   if(isPlainLabel(kids[i])&&isField(kids[i+1])){
    var g=[kids[i]],j=i+1;
    while(j<kids.length&&(isField(kids[j])||(kids[j].tagName==='BUTTON'&&kids[j].classList.contains('sm')))){g.push(kids[j]);j++;}
    if(isPlainLabel(kids[j])&&kids[j].textContent.trim().length<=8&&!isField(kids[j+1])){g.push(kids[j]);j++;
     if(kids[j]&&kids[j].tagName==='BUTTON'&&kids[j].classList.contains('sm')){g.push(kids[j]);j++;}}
    var s=document.createElement('span');s.className='fld';row.insertBefore(s,g[0]);g.forEach(function(x){s.appendChild(x);});
    i=j;
   }else i++;
  }
 });
}
function build(){
 groupRows();
 var fold=loadFold(),cards=[],stop={H2:1,DETAILS:1,SCRIPT:1};
 all('body>h2').forEach(function(h,i){
  var sec=document.createElement('section'),cb=document.createElement('div'),title=h.textContent.replace(/\s+/g,' ').trim(),m=/^(.*?)\s*\((.*)\)$/.exec(title);
  sec.className='card';sec.id='sec'+i;cb.className='cb';
  h.parentNode.insertBefore(sec,h);
  sec.appendChild(h);sec.appendChild(cb);
  if(m){h.textContent=m[1];var s=document.createElement('small');s.textContent=m[2];h.appendChild(s);}
  var n=sec.nextSibling;
  while(n&&!(n.nodeType===1&&(stop[n.tagName]||n.id==='diag'))){var nx=n.nextSibling;cb.appendChild(n);n=nx;}
  h.setAttribute('role','button');h.tabIndex=0;
  var key=m?m[1]:title;
  function set(shut,keep){sec.classList.toggle('shut',shut);h.setAttribute('aria-expanded',shut?'false':'true');if(!keep){fold[key]=shut?1:0;saveFold(fold);}}
  h.addEventListener('click',function(){set(!sec.classList.contains('shut'));syncAll();});
  h.addEventListener('keydown',function(e){if(e.key==='Enter'||e.key===' '){e.preventDefault();h.click();}});
  set(!!fold[key],true);
  cards.push({sec:sec,key:key,set:set});
 });
 if(!cards.length)return;
 var nav=document.createElement('nav');nav.className='jump';
 cards.forEach(function(c){
  var a=document.createElement('a');a.href='#'+c.sec.id;a.textContent=c.key;
  a.addEventListener('click',function(e){e.preventDefault();c.set(false);syncAll();c.sec.scrollIntoView({behavior:'smooth',block:'start'});});
  nav.appendChild(a);});
 var fb=document.createElement('button');fb.type='button';
 function syncAll(){var anyOpen=cards.some(function(c){return !c.sec.classList.contains('shut');});fb.textContent=anyOpen?'Fold all':'Unfold all';}
 fb.addEventListener('click',function(){var anyOpen=cards.some(function(c){return !c.sec.classList.contains('shut');});cards.forEach(function(c){c.set(anyOpen);});syncAll();});
 nav.appendChild(fb);syncAll();
 cards[0].sec.parentNode.insertBefore(nav,cards[0].sec);
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',build);else build();
})();
)hubtheme";
