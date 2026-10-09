// Pages web servies par le capteur (stockees en flash)
#pragma once
#include <Arduino.h>

static const char CSS_PAGE[] PROGMEM = R"==(
:root{--bg:#f3f0e9;--card:#fffdf8;--t:#1e1c19;--m:#77716a;--b:#e4ded3;--acc:#d9632b;--c1:#d9632b;--c2:#9a948b}
@media(prefers-color-scheme:dark){:root{--bg:#121110;--card:#1c1b19;--t:#f2eee7;--m:#9d978e;--b:#2f2d29;--c2:#77726b}}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--t);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:760px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:16px}
h1{font-size:22px;margin:0;letter-spacing:-.01em}h2{font-size:15px;margin:0 0 10px;font-weight:600}
a{color:var(--acc)}.m{color:var(--m);font-size:14px}
.card,.hero{background:var(--card);border:1px solid var(--b);border-radius:16px;padding:16px;margin-bottom:12px}
.hero{padding:20px}
.lab{font-size:12px;color:var(--m);text-transform:uppercase;letter-spacing:.07em}.nu{text-transform:none}
.top{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;flex-wrap:wrap}
.big{font-size:76px;font-weight:700;line-height:1;margin:6px 0 4px;font-variant-numeric:tabular-nums;letter-spacing:-.03em}
.big small{font-size:18px;font-weight:500;color:var(--m);margin-left:6px;letter-spacing:0}
.badge{display:inline-block;padding:6px 14px;border-radius:999px;color:#fff;font-weight:600;font-size:15px;background:#8a857d}
.scale{position:relative;margin:20px 0 30px}
.bar{height:10px;border-radius:5px;background:linear-gradient(90deg,#2f9e6b 0 12%,#d99a1f 12% 46.7%,#e0662f 46.7% 73.3%,#c0283f 73.3%)}
.cur{position:absolute;top:-6px;width:6px;height:22px;margin-left:-3px;border-radius:3px;background:var(--t);border:2px solid var(--card);transition:left .6s}
.tk{position:absolute;top:16px;font-size:12px;color:var(--m);transform:translateX(-50%)}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:12px}
.grid .card{margin:0}
.v{font-size:28px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.2;margin-top:4px}
.v small{font-size:13px;color:var(--m);font-weight:500;margin-left:3px}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:8px}
svg{width:100%;height:230px;display:block;touch-action:pan-y}
.gr{stroke:var(--b);stroke-width:1}.ax{fill:var(--m);font-size:11px}
.oms{stroke:#2f9e6b;stroke-dasharray:6 5;stroke-width:1.5}
.p1{fill:none;stroke:var(--c1);stroke-width:2.5;stroke-linejoin:round;stroke-linecap:round}
.p2{fill:none;stroke:var(--c2);stroke-width:1.5;stroke-linejoin:round}
.a1{fill:var(--c1);opacity:.12}
.tl{stroke:var(--t);stroke-width:1;opacity:.5}.tb{fill:var(--card);stroke:var(--b)}.tt{fill:var(--t);font-size:12px}
.leg{display:flex;gap:16px;flex-wrap:wrap;font-size:13px;color:var(--m);margin-top:10px;align-items:center}
.leg i{display:inline-block;width:16px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}
.l1{background:var(--c1)}.l2{background:var(--c2)}.l3{background:repeating-linear-gradient(90deg,#2f9e6b 0 5px,transparent 5px 9px)}
dl{display:grid;grid-template-columns:auto 1fr;gap:8px 16px;margin:0;font-size:15px}
dt{color:var(--m)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.btns{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}
button,.btn{appearance:none;border:1px solid var(--b);background:var(--card);color:var(--t);padding:11px 16px;border-radius:12px;font:inherit;font-weight:600;cursor:pointer;text-decoration:none;display:inline-block}
.pri{background:var(--acc);border-color:var(--acc);color:#fff}.dan{color:#c0283f}
form label{display:block;margin:14px 0 6px;font-weight:600;font-size:14px}
input,select{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--b);background:var(--bg);color:var(--t);font:inherit}
input[type=checkbox]{width:auto;margin-right:8px;transform:scale(1.2)}
.ck{display:flex;align-items:center;font-weight:600;margin:14px 0 6px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.hint{font-size:13px;color:var(--m);margin-top:6px}
.ok{background:#2f9e6b22;border:1px solid #2f9e6b66;border-radius:12px;padding:10px 14px;margin-bottom:12px}
footer{text-align:center;margin-top:20px}
@media(max-width:480px){.big{font-size:62px}.grid{gap:8px}.grid .card{padding:12px}.v{font-size:22px}}
)==";

static const char PAGE_INDEX[] PROGMEM = R"==(<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Capteur d'air</title>
<link rel="stylesheet" href="/style.css"></head><body><main>
<header><h1>Capteur d'air</h1><span id="heure" class="m"></span></header>
<section class="hero">
 <div class="top">
  <div><div class="lab">Particules fines PM2.5</div>
   <div class="big"><span id="pm25">--</span><small>µg/m³</small></div></div>
  <span id="badge" class="badge">Chargement…</span>
 </div>
 <div class="scale"><div class="bar"></div><div id="cur" class="cur" hidden></div>
  <span class="tk" style="left:0;transform:none">0</span><span class="tk" style="left:12%">9</span>
  <span class="tk" style="left:46.7%">35</span><span class="tk" style="left:73.3%">55</span>
  <span class="tk" style="left:100%;transform:translateX(-100%)">75+</span></div>
 <div id="msg" class="m"></div>
</section>
<div class="grid">
 <div class="card"><div class="lab">PM1</div><div class="v"><span id="pm1">--</span><small>µg/m³</small></div></div>
 <div class="card"><div class="lab">PM10</div><div class="v"><span id="pm10">--</span><small>µg/m³</small></div></div>
 <div class="card"><div class="lab">&gt;0,3 <span class="nu">µm</span></div><div class="v"><span id="p03">--</span><small>/0,1 L</small></div></div>
</div>
<section class="card"><div class="row"><h2>Historique</h2><span id="span" class="m"></span></div>
 <svg id="ch" viewBox="0 0 600 230"></svg>
 <div class="leg"><span><i class="l1"></i>PM2.5</span><span><i class="l2"></i>PM10</span><span><i class="l3"></i>Repère OMS 24 h (15)</span></div>
</section>
<section class="card"><h2>État</h2><dl id="st"></dl></section>
<div class="btns"><button class="pri" onclick="act('/mesure')">Mesurer maintenant</button>
 <button onclick="act('/ecran')">Écran on/off</button><a class="btn" href="/config">Paramètres</a>
 <a class="btn" id="tsl" target="_blank" hidden>ThingSpeak ↗</a></div>
<footer class="m">Capteur d'air v<span id="ver"></span> · capteur-air.local</footer>
</main><script>
const C=[[9,'Bon','#2f9e6b'],[35,'Moyen','#d99a1f'],[55,'Mauvais','#e0662f'],[125,'Très mauvais','#c0283f'],[1e9,'Dangereux','#7b2d8b']];
const $=i=>document.getElementById(i);
const q=v=>C.find(c=>v<=c[0]);
const f=v=>v<10?v.toFixed(1).replace('.',','):String(Math.round(v));
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ago=s=>s<90?'maintenant':s<3600?Math.round(s/60)+' min':(s/3600).toFixed(1).replace('.',',')+' h';
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' j ':'')+(d||h?h+' h ':'')+m+' min'}
async function load(){try{show(await(await fetch('/api')).json())}catch(e){$('msg').textContent='Connexion au capteur perdue…'}}
function act(u){fetch(u,{method:'POST'}).then(()=>setTimeout(load,300))}
function show(d){
 $('ver').textContent=d.version;$('heure').textContent=d.heure;
 if(d.valide){const c=q(d.pm25);$('pm25').textContent=f(d.pm25);$('pm1').textContent=f(d.pm1);$('pm10').textContent=f(d.pm10);
  $('p03').textContent=Math.round(d.p03);$('badge').textContent=c[1];$('badge').style.background=c[2];
  $('cur').hidden=false;$('cur').style.left=Math.min(d.pm25/75*100,100)+'%'}
 else{$('badge').textContent=d.capteur?'Première mesure…':'Capteur non détecté';$('badge').style.background=d.capteur?'#8a857d':'#c0283f'}
 const p=d.prochaine_s;
 $('msg').textContent={CHAUFFE:'Chauffe du capteur en cours',MESURE:'Mesure en cours',VEILLE:'Prochaine mesure dans '+Math.floor(p/60)+' min '+String(p%60).padStart(2,'0')+' s'}[d.etat]||'';
 const ts=!d.ts_actif?'Non configuré':(d.ts_attente?'Envoi en attente':d.ts_code==200?'OK':d.ts_code?'Erreur '+d.ts_code:'—')+' · '+d.ts_ok+' envoi(s)';
 const r=[['Wi-Fi',d.wifi_ok?d.ssid+' · '+d.rssi+' dBm':'Déconnecté'],['Adresse IP',d.ip],['ThingSpeak',ts],
  ['Intervalle de mesure',d.cycle_min+' min'],['Écran',d.ecran?'Allumé':'Éteint'],['En marche depuis',dur(d.uptime_s)],['Mémoire libre',d.heap+' octets']];
 $('st').innerHTML=r.map(x=>'<dt>'+x[0]+'</dt><dd>'+esc(x[1])+'</dd>').join('');
 $('tsl').hidden=!d.ts_actif;$('tsl').href='https://thingspeak.mathworks.com/channels/'+d.ts_channel;
 chart(d.histo)}
let CH=null;
let HL=null;addEventListener('resize',()=>{if(HL)chart(HL)});
function chart(h){
 HL=h;const s=$('ch'),W=Math.round(s.clientWidth)||600,H=230,L=34,R=8,T=10,B=24;s.setAttribute('viewBox','0 0 '+W+' '+H);
 if(h.length<2){CH=null;s.innerHTML=`<text x="${W/2}" y="115" text-anchor="middle" class="ax">Historique en cours de constitution…</text>`;$('span').textContent='';return}
 const tm=Math.max(h[0][0],60);let vm=20;for(const p of h)vm=Math.max(vm,p[1],p[2]);vm*=1.1;
 const X=a=>L+(W-L-R)*(1-a/tm),Y=v=>H-B-(H-B-T)*v/vm;
 let g='';const sv=vm>200?100:vm>100?50:vm>40?20:vm>20?10:5;
 for(let v=0;v<=vm;v+=sv)g+=`<line x1="${L}" x2="${W-R}" y1="${Y(v)}" y2="${Y(v)}" class="gr"/><text x="${L-6}" y="${Y(v)+4}" text-anchor="end" class="ax">${v}</text>`;
 const st=[900,1800,3600,7200,10800,21600,43200].find(x=>tm/x<=(W<500?4:7))||43200;
 for(let a=0;a<=tm;a+=st)if(!a||X(a)<W-R-90)g+=`<text x="${X(a)}" y="${H-6}" text-anchor="${a?'middle':'end'}" class="ax">${a?'-'+(a<3600?a/60+' min':a/3600+' h'):'maintenant'}</text>`;
 g+=`<line x1="${L}" x2="${W-R}" y1="${Y(15)}" y2="${Y(15)}" class="oms"/>`;
 const pl=i=>h.map(p=>X(p[0]).toFixed(1)+','+Y(p[i]).toFixed(1)).join(' ');
 g+=`<polygon points="${X(h[0][0])},${Y(0)} ${pl(1)} ${X(h[h.length-1][0])},${Y(0)}" class="a1"/><polyline points="${pl(2)}" class="p2"/><polyline points="${pl(1)}" class="p1"/><g id="tip"></g>`;
 s.innerHTML=g;CH={h,X,Y,W,H,T,B};$('span').textContent='dernières '+ago(h[0][0])}
$('ch').addEventListener('pointermove',e=>{if(!CH)return;const r=e.currentTarget.getBoundingClientRect(),x=(e.clientX-r.left)/r.width*CH.W;
 let b=CH.h[0],bd=1e9;for(const p of CH.h){const d=Math.abs(CH.X(p[0])-x);if(d<bd){bd=d;b=p}}
 const px=CH.X(b[0]),t='PM2.5 '+f(b[1])+' · PM10 '+f(b[2])+' · '+(b[0]<90?'maintenant':'il y a '+ago(b[0])),w=t.length*6.3+16,tx=Math.min(Math.max(px-w/2,0),CH.W-w);
 $('tip').innerHTML=`<line x1="${px}" x2="${px}" y1="${CH.T}" y2="${CH.H-CH.B}" class="tl"/><circle cx="${px}" cy="${CH.Y(b[1])}" r="4" fill="var(--c1)"/><rect x="${tx}" y="0" width="${w}" height="22" rx="6" class="tb"/><text x="${tx+8}" y="15" class="tt">${t}</text>`});
$('ch').addEventListener('pointerleave',()=>{const t=$('tip');if(t)t.innerHTML=''});
load();setInterval(load,10000);
</script></body></html>)==";
