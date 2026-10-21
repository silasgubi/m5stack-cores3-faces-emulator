#pragma once

// Single-page UI of the file transfer mode (wifi_transfer.cpp), served at "/".
// No external resources: the phone has no internet while it is on the console network.
// The page only talks to the JSON/form API of wifi_transfer.cpp and fills every text from
// the T table below, which follows the console language (UI_LANG_PT_BR). Non-ASCII
// letters are written as \u escapes so this file stays ASCII.
// File names are always inserted with textContent, never as HTML.

#if UI_LANG_PT_BR
#define WEB_PAGE_TEXT \
    R"js({lang:"pt-BR",title:"Transfer\u00eancia de arquivos",free:"Cart\u00e3o: %1 livres de %2",)js" \
    R"js(roms:"Jogos (/roms)",nes:"Saves do NES (/nes_saves)",gb:"Saves do Game Boy (/gb_saves)",)js" \
    R"js(addRoms:"Enviar jogos (.nes .gb .gbc)",addSave:"Restaurar save (.sav)",del:"Apagar",)js" \
    R"js(dl:"Baixar",empty:"Vazio",confirmDel:"Apagar %s do cart\u00e3o?",)js" \
    R"js(confirmRep:"%s j\u00e1 existe. Substituir?",sending:"Enviando %s%n",done:"Pronto.",)js" \
    R"js(deleted:"%s apagado.",many:"O menu de jogos mostra at\u00e9 %d jogos.",)js" \
    R"js(saveHint:"O save tem o nome do jogo: Mario.nes \u2192 Mario.sav.",)js" \
    R"js(exit:"Ao terminar, aperte B no console.",)js" \
    R"js(cna:"Se os bot\u00f5es ou o download n\u00e3o funcionarem nesta janela, abra http://10.10.0.1 )js" \
    R"js(no navegador (Safari ou Chrome).",)js" \
    R"js(err:{dir:"pasta desconhecida",name:"nome de arquivo inv\u00e1lido",)js" \
    R"js(ext:"tipo de arquivo errado",space:"sem espa\u00e7o no cart\u00e3o",)js" \
    R"js(write:"n\u00e3o consegui gravar no cart\u00e3o",missing:"arquivo n\u00e3o encontrado",)js" \
    R"js(net:"conex\u00e3o perdida",http:"erro no console"}})js"
#else
#define WEB_PAGE_TEXT \
    R"js({lang:"en",title:"File transfer",free:"Card: %1 free of %2",)js" \
    R"js(roms:"Games (/roms)",nes:"NES saves (/nes_saves)",gb:"Game Boy saves (/gb_saves)",)js" \
    R"js(addRoms:"Add games (.nes .gb .gbc)",addSave:"Restore a save (.sav)",del:"Delete",)js" \
    R"js(dl:"Download",empty:"Empty",confirmDel:"Delete %s from the card?",)js" \
    R"js(confirmRep:"%s already exists. Replace it?",sending:"Sending %s%n",done:"Done.",)js" \
    R"js(deleted:"Deleted %s.",many:"The game menu shows up to %d games.",)js" \
    R"js(saveHint:"A save has the name of its game: Mario.nes \u2192 Mario.sav.",)js" \
    R"js(exit:"When you are done, press B on the console.",)js" \
    R"js(cna:"If the buttons or downloads do nothing in this window, open http://10.10.0.1 )js" \
    R"js(in your browser (Safari or Chrome).",)js" \
    R"js(err:{dir:"unknown folder",name:"invalid file name",ext:"wrong file type",)js" \
    R"js(space:"not enough space on the card",write:"could not write to the card",)js" \
    R"js(missing:"file not found",net:"connection lost",http:"console error"}})js"
#endif

constexpr char WEB_PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Console</title>
<style>
:root{--bg:#f3f4f6;--card:#fff;--ink:#1d2330;--muted:#687080;--line:#e2e5e9;--pri:#1f5fbf;--bad:#b3261e;--good:#1e7b45}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px 16px 96px}
h1{font-size:1.4em;margin:4px 0 2px}
h2{font-size:1.05em;margin:0 0 6px}
.sub{color:var(--muted);margin:0 0 14px;font-size:.9em}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}
ul{list-style:none;margin:0 0 10px;padding:0}
li{display:flex;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid var(--line)}
li:last-child{border-bottom:0}
.n{flex:1;min-width:0;overflow-wrap:anywhere}
.s{color:var(--muted);font-size:.85em;white-space:nowrap}
.empty{color:var(--muted);font-style:italic}
button,.btn{font:inherit;font-size:.95em;border-radius:8px;border:1px solid var(--line);background:#fff;color:var(--ink);padding:8px 12px;cursor:pointer;text-decoration:none;display:inline-block}
.btn.pri{background:var(--pri);border-color:var(--pri);color:#fff}
button.del{color:var(--bad)}
/* Invisible input over the whole button: some mobile browsers ignore taps on a label whose
   file input is display:none. */
label.btn{position:relative;overflow:hidden}
input[type=file]{position:absolute;left:0;top:0;width:100%;height:100%;opacity:0;cursor:pointer}
.hint{color:var(--muted);font-size:.85em;margin:8px 0 0}
.busy label,.busy button{opacity:.45;pointer-events:none}
#msg{position:fixed;left:0;right:0;bottom:0;padding:14px 16px;background:var(--ink);color:#fff;text-align:center;white-space:pre-line}
#msg.err{background:var(--bad)}
#msg.ok{background:var(--good)}
</style></head><body><main>
<h1 id="title"></h1>
<p class="sub" id="free">&nbsp;</p>
<section><h2 id="h_roms"></h2><ul id="roms"></ul>
<label class="btn pri"><span id="b_roms"></span><input type="file" multiple data-dir="roms"></label>
<p class="hint" id="many" hidden></p></section>
<section><h2 id="h_nes"></h2><ul id="nes_saves"></ul>
<label class="btn"><span class="b_save"></span><input type="file" multiple data-dir="nes_saves"></label></section>
<section><h2 id="h_gb"></h2><ul id="gb_saves"></ul>
<label class="btn"><span class="b_save"></span><input type="file" multiple data-dir="gb_saves"></label>
<p class="hint" id="n_save"></p></section>
<p class="hint" id="n_exit"></p>
<p class="hint" id="n_cna"></p>
</main><div id="msg" hidden></div>
<script>
const T=)html" WEB_PAGE_TEXT R"html(;
const RX={roms:/\.(nes|gb|gbc)$/i,nes_saves:/\.sav$/i,gb_saves:/\.sav$/i};
const $=i=>document.getElementById(i);
let list={},busy=false,hideT;
document.documentElement.lang=T.lang;
document.title=T.title;
for(const[k,v]of Object.entries({title:T.title,h_roms:T.roms,h_nes:T.nes,h_gb:T.gb,b_roms:T.addRoms,
  n_save:T.saveHint,n_exit:T.exit,n_cna:T.cna}))$(k).textContent=v;
document.querySelectorAll('.b_save').forEach(e=>e.textContent=T.addSave);
// No "accept" filter: iOS greys out files with extensions it does not know.
document.querySelectorAll('input[type=file]').forEach(i=>i.onchange=()=>{
  const f=[...i.files];i.value='';upload(i.dataset.dir,f)});

function sz(b){
  if(b<1024)return b+' B';
  if(b<1048576)return Math.round(b/1024)+' KB';
  if(b<1073741824)return (b/1048576).toFixed(1)+' MB';
  return (b/1073741824).toFixed(1)+' GB';
}
function msg(t,c){
  const m=$('msg');m.textContent=t;m.className=c||'';m.hidden=!t;
  clearTimeout(hideT);if(c=='ok')hideT=setTimeout(()=>{m.hidden=true},4000);
}
// The console font is ASCII only and FAT forbids some characters: drop accents, replace
// the rest, keep the extension when shortening to the 63 characters the menu holds.
function clean(name){
  let n=name.normalize('NFD').replace(/[\u0300-\u036f]/g,'')
    .replace(/[^\x20-\x7e]|[\\/:*?"<>|]/g,'_').trim().replace(/^[. ]+/,'');
  const dot=n.lastIndexOf('.');
  if(n.length>63&&dot>0){const e=n.slice(dot);n=n.slice(0,63-e.length).trim()+e}
  return n.replace(/\.{2,}/g,'.');
}
async function load(){
  try{list=await(await fetch('/api/list',{cache:'no-store'})).json()}
  catch(e){msg(T.err.net,'err');return}
  $('free').textContent=T.free.replace('%1',sz(list.free)).replace('%2',sz(list.total));
  for(const d in RX)render(d);
  const m=$('many');m.hidden=list.roms.length<=list.max;m.textContent=T.many.replace('%d',list.max);
}
function render(d){
  const ul=$(d),files=(list[d]||[]).sort((a,b)=>a.n.localeCompare(b.n,undefined,{sensitivity:'base'}));
  ul.textContent='';
  if(!files.length){const li=document.createElement('li');li.className='empty';li.textContent=T.empty;ul.append(li);return}
  for(const f of files){
    const li=document.createElement('li'),n=document.createElement('span'),s=document.createElement('span');
    n.className='n';n.textContent=f.n;s.className='s';s.textContent=sz(f.s);li.append(n,s);
    if(d=='roms'){
      const b=document.createElement('button');b.className='del';b.textContent=T.del;b.onclick=()=>del(d,f.n);li.append(b);
    }else{
      const a=document.createElement('a');a.className='btn';a.textContent=T.dl;a.download=f.n;
      a.href='/download?dir='+d+'&name='+encodeURIComponent(f.n);li.append(a);
    }
    ul.append(li);
  }
}
function send(dir,f,name,n){
  return new Promise(done=>{
    const x=new XMLHttpRequest(),fd=new FormData();
    fd.append('file',f,name);
    x.open('POST','/upload?dir='+dir);
    x.upload.onprogress=e=>{if(e.lengthComputable)
      msg(T.sending.replace('%s',()=>name).replace('%n',n)+' '+Math.floor(100*e.loaded/e.total)+'%')};
    x.onload=()=>done(x.status==200?'ok':(x.responseText.trim()||'http'));
    x.onerror=()=>done('net');
    msg(T.sending.replace('%s',()=>name).replace('%n',n));
    x.send(fd);
  });
}
// One request per file: simple progress, and one bad file does not cancel the others.
async function upload(dir,files){
  if(busy||!files.length)return;
  busy=true;document.body.classList.add('busy');
  const errs=[];
  for(let i=0;i<files.length;i++){
    const f=files[i],name=clean(f.name);
    const bad=!name?'name':!RX[dir].test(name)?'ext':f.size+65536>list.free?'space':'';
    if(bad){errs.push(f.name+' - '+T.err[bad]);continue}
    if((list[dir]||[]).some(x=>x.n.toLowerCase()==name.toLowerCase())&&!confirm(T.confirmRep.replace('%s',()=>name)))continue;
    const r=await send(dir,f,name,files.length>1?' ('+(i+1)+'/'+files.length+')':'');
    if(r=='ok')list.free-=f.size;else errs.push(name+' - '+(T.err[r]||r));
  }
  busy=false;document.body.classList.remove('busy');
  await load();
  msg(errs.length?errs.join('\n'):T.done,errs.length?'err':'ok');
}
async function del(d,name){
  if(busy||!confirm(T.confirmDel.replace('%s',()=>name)))return;
  let r;
  try{const x=await fetch('/delete',{method:'POST',body:new URLSearchParams({dir:d,name})});r=x.ok?'ok':(await x.text()).trim()}
  catch(e){r='net'}
  await load();
  msg(r=='ok'?T.deleted.replace('%s',()=>name):name+' - '+(T.err[r]||r),r=='ok'?'ok':'err');
}
load();
</script></body></html>
)html";
