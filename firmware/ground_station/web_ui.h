// Phone dashboard served by the ground station's Wi-Fi access point (http://192.168.4.1).
// Offline by design: no internet or map tiles needed. Shows live telemetry, a home-centred
// track plot, coordinates with links to map apps, and the camera stream when a camera node
// is on the network.
#pragma once

static const char WEB_PAGE[] = R"WEBUI(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>蝴蝶地面站</title>
<style>
:root{--bg:#F1F4F2;--card:#fff;--ink:#17262A;--mute:#5E6E72;--line:#D6DEDB;--acc:#1F6F8B;--ok:#2E7D52;--bad:#B3402E;--warn:#9A6A00;--trk:#1F6F8B;--grid:#C9D3D0}
@media (prefers-color-scheme:dark){:root{--bg:#0F171A;--card:#162226;--ink:#E2ECEE;--mute:#93A6AB;--line:#2A3B40;--acc:#63B8D4;--ok:#5FC28A;--bad:#F07A63;--warn:#E3B54A;--trk:#63B8D4;--grid:#2E4247;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 -apple-system,"PingFang SC","Microsoft YaHei",system-ui,sans-serif;padding:max(12px,env(safe-area-inset-top)) 14px 24px}
h1{font-size:18px;margin:0 0 8px}.row{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px}
.chip{font-size:13px;padding:3px 10px;border-radius:999px;border:1px solid var(--line);background:var(--card)}
.ok{color:var(--ok);border-color:var(--ok)}.bad{color:var(--bad);border-color:var(--bad)}.warn{color:var(--warn);border-color:var(--warn)}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px}
.num{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.num b{display:block;font-size:22px;font-variant-numeric:tabular-nums}.num span{font-size:12px;color:var(--mute)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px;margin-bottom:10px}
canvas{width:100%;aspect-ratio:1;display:block;touch-action:none}
.btns{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
button,a.btn{font:inherit;font-size:14px;padding:7px 12px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--ink);text-decoration:none;cursor:pointer}
input{font:13px ui-monospace,Menlo,monospace;width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink)}
img{width:100%;border-radius:8px;background:#000;display:block}.mute{color:var(--mute);font-size:13px}
</style></head><body>
<h1>🦋 蝴蝶地面站</h1>
<div class="row" id="chips"><span class="chip">连接中…</span></div>
<div class="grid">
  <div class="num"><b id="dist">—</b><span>离家距离 m</span></div>
  <div class="num"><b id="alt">—</b><span>气压高度 m</span></div>
  <div class="num"><b id="spd">—</b><span>地速 m/s</span></div>
  <div class="num"><b id="vbat">—</b><span>电池 V</span></div>
  <div class="num"><b id="brg">—</b><span>方位 °</span></div>
  <div class="num"><b id="sats">—</b><span>卫星数</span></div>
</div>
<div class="card">
  <canvas id="map" width="600" height="600"></canvas>
  <div class="mute" id="scale">以“家”为中心，上方为正北</div>
  <div class="btns"><button id="sethome">把当前位置设为家</button><button id="clear">清除轨迹</button></div>
</div>
<div class="card">
  <div class="mute">坐标（WGS-84）</div>
  <input id="coord" readonly value="等待 GPS 定位…">
  <div class="btns"><button id="copy">复制坐标</button><a class="btn" id="amap" target="_blank" rel="noopener">高德地图</a><a class="btn" id="apple" target="_blank" rel="noopener">Apple 地图</a><a class="btn" id="gmap" target="_blank" rel="noopener">Google 地图</a></div>
  <div class="mute">打开地图需要手机能上网（移动数据）；定位和轨迹本身不需要。</div>
</div>
<div class="card">
  <div class="mute" id="camnote">摄像头：未连接（可选的摄像头节点接入后会自动显示）</div>
  <img id="cam" alt="摄像头画面" hidden>
</div>
<script>
const $=id=>document.getElementById(id);
const MODES=["MANUAL","STAB","HOLD","AUTO","返航"],STATES=["未解锁","已解锁","失控保护"];
let track=[],home=null,cur=null,camUrl="",camTry=0;
function outChina(lat,lon){return lon<72.004||lon>137.8347||lat<0.8293||lat>55.8271}
function tLat(x,y){let r=-100+2*x+3*y+.2*y*y+.1*x*y+.2*Math.sqrt(Math.abs(x));r+=(20*Math.sin(6*x*Math.PI)+20*Math.sin(2*x*Math.PI))*2/3;r+=(20*Math.sin(y*Math.PI)+40*Math.sin(y/3*Math.PI))*2/3;r+=(160*Math.sin(y/12*Math.PI)+320*Math.sin(y*Math.PI/30))*2/3;return r}
function tLon(x,y){let r=300+x+2*y+.1*x*x+.1*x*y+.1*Math.sqrt(Math.abs(x));r+=(20*Math.sin(6*x*Math.PI)+20*Math.sin(2*x*Math.PI))*2/3;r+=(20*Math.sin(x*Math.PI)+40*Math.sin(x/3*Math.PI))*2/3;r+=(150*Math.sin(x/12*Math.PI)+300*Math.sin(x/30*Math.PI))*2/3;return r}
function toGcj(lat,lon){if(outChina(lat,lon))return[lat,lon];const a=6378245,ee=.00669342162296594323;let dLat=tLat(lon-105,lat-35),dLon=tLon(lon-105,lat-35);const rl=lat/180*Math.PI;let m=Math.sin(rl);m=1-ee*m*m;const sm=Math.sqrt(m);dLat=dLat*180/((a*(1-ee))/(m*sm)*Math.PI);dLon=dLon*180/(a/sm*Math.cos(rl)*Math.PI);return[lat+dLat,lon+dLon]}
function enu(p){const k=Math.cos(home[0]*Math.PI/180);return[(p[1]-home[1])*111320*k,(p[0]-home[0])*110540]}
function chip(t,c){return '<span class="chip '+(c||"")+'">'+t+'</span>'}
function update(d){
  let h="";
  if(!d.ok){h+=chip("蝴蝶无信号","bad")}else{h+=chip(STATES[d.state]||"?",d.state==1?"ok":d.state==2?"bad":"");h+=chip(MODES[d.mode]||"?");h+=chip("链路 "+d.link+"/s",d.link>40?"ok":"warn");if(d.flags&2)h+=chip("低电量","bad");if(d.flags&16)h+=chip("充电中","warn");if(d.flags&64)h+=chip("返航中","warn")}
  h+=chip(d.pad?"手柄已连接":"手柄未连接",d.pad?"ok":"warn");h+=chip(d.fix?"GPS 已定位":"GPS 未定位",d.fix?"ok":"warn");
  $("chips").innerHTML=h;
  $("alt").textContent=d.ok?d.alt.toFixed(1):"—";$("vbat").textContent=d.ok?d.vbat.toFixed(2):"—";
  $("spd").textContent=d.fix?d.spd.toFixed(1):"—";$("sats").textContent=d.ok?d.sats:"—";
  if(d.home)home=d.home;
  if(d.fix){cur=[d.lat,d.lon,d.crs,d.spd];const l=track[track.length-1];if(home&&(!l||Math.hypot(...enu(l).map((v,i)=>v-enu(cur)[i]))>0.5)){track.push([d.lat,d.lon]);if(track.length>1200)track.shift()}
    $("coord").value=d.lat.toFixed(7)+", "+d.lon.toFixed(7);const g=toGcj(d.lat,d.lon);
    $("amap").href="https://uri.amap.com/marker?position="+d.lon+","+d.lat+"&coordinate=wgs84&name=%E8%9D%B4%E8%9D%B6&callnative=1";
    $("apple").href="https://maps.apple.com/?ll="+g[0].toFixed(7)+","+g[1].toFixed(7)+"&q=%E8%9D%B4%E8%9D%B6";
    $("gmap").href="https://www.google.com/maps/search/?api=1&query="+d.lat+","+d.lon}
  if(home&&cur){const e=enu(cur),dist=Math.hypot(e[0],e[1]);$("dist").textContent=dist.toFixed(0);$("brg").textContent=((Math.atan2(e[0],e[1])*180/Math.PI+360)%360).toFixed(0)}
  if(d.cam&&d.cam!==camUrl&&Date.now()>camTry){camUrl=d.cam;const im=$("cam");im.onload=()=>{im.hidden=false;$("camnote").textContent="摄像头画面"};im.onerror=()=>{im.hidden=true;$("camnote").textContent="摄像头：未连接（5 秒后重试）";camUrl="";camTry=Date.now()+5000};im.src=d.cam}
  draw();
}
function draw(){
  const c=$("map"),dpr=window.devicePixelRatio||1,w=c.clientWidth;c.width=w*dpr;c.height=w*dpr;const x=c.getContext("2d");x.scale(dpr,dpr);
  const cs=getComputedStyle(document.documentElement),col=n=>cs.getPropertyValue(n).trim();
  const r0=w/2-14;x.clearRect(0,0,w,w);
  if(!home){x.fillStyle=col("--mute");x.font="14px sans-serif";x.textAlign="center";x.fillText("等待 GPS 定位后自动设置“家”",w/2,w/2);return}
  let maxd=10;for(const p of track){const e=enu(p);maxd=Math.max(maxd,Math.hypot(e[0],e[1]))}if(cur){const e=enu(cur);maxd=Math.max(maxd,Math.hypot(e[0],e[1]))}
  const steps=[10,20,50,100,200,500,1000,2000,5000];const R=steps.find(s=>s>=maxd*1.1)||steps[steps.length-1];const k=r0/R;
  x.strokeStyle=col("--grid");x.fillStyle=col("--mute");x.font="11px sans-serif";x.lineWidth=1;
  for(let i=1;i<=4;i++){x.beginPath();x.arc(w/2,w/2,r0*i/4,0,2*Math.PI);x.stroke();x.fillText((R*i/4)+" m",w/2+4,w/2-r0*i/4+12)}
  x.beginPath();x.moveTo(w/2,w/2-r0);x.lineTo(w/2,w/2+r0);x.moveTo(w/2-r0,w/2);x.lineTo(w/2+r0,w/2);x.stroke();
  x.fillStyle=col("--ink");x.font="bold 13px sans-serif";x.textAlign="center";x.fillText("N",w/2,11);
  x.strokeStyle=col("--trk");x.lineWidth=2;x.beginPath();track.forEach((p,i)=>{const e=enu(p);const px=w/2+e[0]*k,py=w/2-e[1]*k;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();
  x.fillStyle=col("--ok");x.beginPath();x.arc(w/2,w/2,6,0,2*Math.PI);x.fill();x.fillStyle=col("--bg");x.font="bold 9px sans-serif";x.fillText("H",w/2,w/2+3);
  if(cur){const e=enu(cur),px=w/2+e[0]*k,py=w/2-e[1]*k;x.save();x.translate(px,py);x.fillStyle=col("--bad");
    if(cur[3]>1){x.rotate(cur[2]*Math.PI/180);x.beginPath();x.moveTo(0,-11);x.lineTo(7,8);x.lineTo(0,4);x.lineTo(-7,8);x.closePath();x.fill()}else{x.beginPath();x.arc(0,0,6,0,2*Math.PI);x.fill()}x.restore()}
  $("scale").textContent="以“家”为中心，上方为正北，外圈半径 "+R+" m";
}
async function poll(){try{const r=await fetch("/api",{cache:"no-store"});update(await r.json())}catch(e){$("chips").innerHTML=chip("与地面站断开，检查 Wi-Fi","bad")}setTimeout(poll,500)}
$("sethome").onclick=async()=>{try{const r=await fetch("/sethome",{cache:"no-store"});const d=await r.json();if(d.home){home=d.home;track=[]}}catch(e){}};
$("clear").onclick=()=>{track=[];draw()};
$("copy").onclick=()=>{const i=$("coord");i.select();i.setSelectionRange(0,99);try{document.execCommand("copy");$("copy").textContent="已复制"}catch(e){$("copy").textContent="请长按选中复制"}setTimeout(()=>$("copy").textContent="复制坐标",1500)};
window.addEventListener("resize",draw);draw();poll();
</script></body></html>)WEBUI";
