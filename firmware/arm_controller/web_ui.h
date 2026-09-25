// Phone page: join Wi-Fi "RobotArm" (password robotarm123), open http://192.168.4.1
// Live joint angles, tool position, current / voltage, and buttons that send text commands.
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>机械臂</title>
<style>
:root{--bg:#f4f6f7;--card:#fff;--ink:#1d2a2e;--muted:#667;--line:#dde3e5;--acc:#1f7a8c;--bad:#b3261e;--ok:#2e7d32}
@media(prefers-color-scheme:dark){:root{--bg:#111a1d;--card:#1a262a;--ink:#e3ecee;--muted:#9ab;--line:#2c3b40;--acc:#5fb3c4;--bad:#ef8a80;--ok:#8fd694}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,sans-serif}
main{max-width:560px;margin:0 auto;padding:12px 16px 40px}h1{font-size:20px;margin:6px 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px;margin:10px 0}
.row{display:flex;flex-wrap:wrap;gap:8px}.st{font-weight:700}.on{color:var(--ok)}.off{color:var(--muted)}.bad{color:var(--bad)}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}td{padding:3px 4px;border-bottom:1px solid var(--line)}
td:last-child{text-align:right}button{font:inherit;padding:9px 12px;border-radius:10px;border:1px solid var(--line);
background:var(--card);color:var(--ink);min-width:64px}button.p{background:var(--acc);color:#fff;border-color:var(--acc)}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}input{flex:1;min-width:0;font:inherit;padding:9px;border-radius:10px;
border:1px solid var(--line);background:var(--card);color:var(--ink)}#log{font:12px/1.4 ui-monospace,monospace;white-space:pre-wrap;
color:var(--muted);max-height:160px;overflow:auto}small{color:var(--muted)}
</style></head><body><main>
<h1>6 自由度机械臂</h1>
<div class="card"><div class="row"><span>状态 <span id="st" class="st">…</span></span><span>动作 <b id="act">…</b></span>
<span>模式 <b id="mode">…</b></span><span>速度 <b id="spd">…</b></span></div>
<div class="row"><span>手柄 <b id="pad">…</b></span><span>电流 <b id="amp">…</b></span><span>电压 <b id="volt">…</b></span>
<span>路点 <b id="wp">…</b></span></div><small id="ev"></small></div>
<div class="card"><table id="tb"></table></div>
<div class="card"><div class="row">
<button class="p" data-c="ON">上电 ON</button><button data-c="OFF">停放断电</button><button data-c="HOME">回原位</button>
<button data-c="STOP">停止</button></div></div>
<div class="card"><div class="grid">
<button data-c="UP 20">上 +20</button><button data-c="FORWARD 20">前 +20</button><button data-c="DOWN 20">下 −20</button>
<button data-c="LEFT 20">左 +20</button><button data-c="BACK 20">后 −20</button><button data-c="RIGHT 20">右 −20</button>
<button data-c="OPEN">张开夹爪</button><button data-c="CLOSE">闭合夹爪</button><button data-c="REC">记录路点</button>
<button data-c="PLAY">播放</button><button data-c="PLAY LOOP">循环播放</button><button data-c="SAVE">保存路点</button>
</div></div>
<div class="card"><div class="row"><input id="cmd" placeholder="例：MOVE 180 0 60 -90" autocapitalize="characters">
<button class="p" id="send">发送</button></div><div id="log"></div></div>
<small>单位 mm / 度；夹爪 0 = 张开，100 = 闭合。手柄一动，文字指令立即让出控制权。</small>
</main><script>
const $=id=>document.getElementById(id);
const names=["J1 底座","J2 大臂","J3 小臂","J4 手腕俯仰","J5 手腕旋转","J6 夹爪 %"];
function log(t){const l=$("log");l.textContent=t+"\n"+l.textContent.slice(0,2000)}
async function send(c){try{const r=await fetch("/cmd?c="+encodeURIComponent(c));log("> "+c+"\n"+await r.text())}catch(e){log("! "+e)}}
document.querySelectorAll("[data-c]").forEach(b=>b.onclick=()=>send(b.dataset.c));
$("send").onclick=()=>{const c=$("cmd").value.trim();if(c)send(c)};
$("cmd").onkeydown=e=>{if(e.key==="Enter")$("send").click()};
async function poll(){try{const d=await(await fetch("/api")).json();
const st=$("st");st.textContent=d.state;st.className="st "+(d.state==="ON"?"on":d.state==="OFF"?"off":"bad");
$("act").textContent=d.act;$("mode").textContent=d.mode==="CART"?"XYZ":"关节";$("spd").textContent=d.speed;
$("pad").textContent=d.pad?"已连接":"未连接";$("wp").textContent=d.wp;
$("amp").textContent=d.sensor?d.amps.toFixed(2)+" A":"—";$("volt").textContent=d.sensor?d.volts.toFixed(2)+" V":"—";
if(d.event)$("ev").textContent=d.event;
let h="";d.q.forEach((v,i)=>h+=`<tr><td>${names[i]}</td><td>${v.toFixed(1)}</td></tr>`);
const p=d.pose;h+=`<tr><td>末端 X / Y / Z (mm)</td><td>${p[0].toFixed(0)} / ${p[1].toFixed(0)} / ${p[2].toFixed(0)}</td></tr>`;
h+=`<tr><td>俯仰 / 旋转 (°)</td><td>${p[3].toFixed(0)} / ${p[4].toFixed(0)}</td></tr>`;$("tb").innerHTML=h;
}catch(e){$("st").textContent="连接中…";$("st").className="st off"}setTimeout(poll,300)}
poll();
</script></body></html>)HTML";
