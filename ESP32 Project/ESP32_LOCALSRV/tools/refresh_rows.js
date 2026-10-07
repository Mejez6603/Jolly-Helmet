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
const render0=render;render=function(){render0();rfSync();arfSync();};
