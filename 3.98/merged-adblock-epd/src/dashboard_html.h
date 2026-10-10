#pragma once
// The dashboard page lives in flash as a raw string.
// Modern, self-contained (no external assets) and entirely in Chinese.
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b0e13">
<title>AdBlock DNS 控制台</title>
<style>
:root{--bg:#0b0e13;--card:#141922;--card2:#1b2330;--line:#242e3c;--tx:#e7eef8;--mut:#8697ab;--ac:#4d8dff;--ok:#3fb950;--bad:#f0645c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;-webkit-font-smoothing:antialiased}
header{position:sticky;top:0;z-index:9;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 20px;background:rgba(11,14,19,.88);backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:9px;font-weight:650;letter-spacing:.01em}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 4px rgba(63,185,80,.16)}
.dot.off{background:var(--bad);box-shadow:0 0 0 4px rgba(240,100,92,.16)}
.meta{display:flex;align-items:center;gap:8px;color:var(--mut);font-size:13px;font-variant-numeric:tabular-nums}
main{max-width:1080px;margin:0 auto;padding:18px 16px 44px;display:flex;flex-direction:column;gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px 18px}
.card h2{margin:0 0 14px;font-size:15px;font-weight:600;display:flex;align-items:baseline;gap:8px}
.card h2 small{font-weight:400;font-size:12px;color:var(--mut)}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:14px 16px}
.tile .k{font-size:12px;color:var(--mut)}
.tile .v{font-size:26px;font-weight:650;letter-spacing:-.02em;margin-top:4px;font-variant-numeric:tabular-nums}
.tile .v u{font-size:13px;font-weight:500;color:var(--mut);text-decoration:none;margin-left:3px}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:16px}
table{width:100%;border-collapse:collapse;font-size:13px}
th{text-align:left;font-weight:500;color:var(--mut);font-size:12px;padding:0 8px 8px;border-bottom:1px solid var(--line)}
td{padding:9px 8px;border-bottom:1px solid rgba(36,46,60,.65);font-variant-numeric:tabular-nums;vertical-align:middle}
tbody tr:last-child td{border-bottom:0}
tbody tr:hover{background:var(--card2)}
.n{text-align:right}
.sub{color:var(--mut);font-size:11.5px}
.axis{display:flex;justify-content:space-between;color:var(--mut);font-size:11px;padding:6px 2px 0;font-variant-numeric:tabular-nums}
input,select,button{font:inherit;color:var(--tx);background:var(--card2);border:1px solid var(--line);border-radius:10px;padding:9px 12px;outline:none;max-width:100%}
input:focus,select:focus{border-color:var(--ac)}
input[type=file]{padding:7px 10px;font-size:12.5px;flex:1}
button{cursor:pointer;white-space:nowrap}
button:hover{border-color:#3a4860}
button.primary{background:var(--ac);border-color:var(--ac);color:#fff;font-weight:600}
button.primary:hover{filter:brightness(1.09)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.chip{display:inline-flex;align-items:center;gap:8px;background:var(--card2);border:1px solid var(--line);border-radius:999px;padding:5px 6px 5px 12px;font-size:12.5px}
.chip b{cursor:pointer;color:var(--mut);font-weight:400;background:#26303e;border-radius:50%;width:18px;height:18px;display:grid;place-items:center;font-size:13px}
.chip b:hover{color:var(--bad);background:#2e1d1f}
.mut{color:var(--mut)}
.tag{display:inline-block;font-size:12px;padding:2px 9px;border-radius:999px;background:rgba(77,141,255,.13);color:#8ab4ff}
.tag.ok{background:rgba(63,185,80,.14);color:#5ccd78}
.tag.off{background:rgba(134,151,171,.14);color:var(--mut)}
.kv{display:flex;justify-content:space-between;gap:10px;padding:7px 0;border-bottom:1px solid rgba(36,46,60,.65);font-size:13px}
.kv:last-child{border-bottom:0}
.kv span:last-child{font-variant-numeric:tabular-nums;text-align:right}
.hint{color:var(--mut);font-size:12px;margin-top:10px}
.toast{position:fixed;left:50%;bottom:26px;transform:translate(-50%,20px);opacity:0;transition:.22s;background:#1d2634;border:1px solid var(--line);padding:10px 18px;border-radius:12px;pointer-events:none;font-size:13px}
.toast.on{opacity:1;transform:translate(-50%,0)}
@media(max-width:780px){.stats{grid-template-columns:repeat(2,1fr)}.cols{grid-template-columns:1fr}}
</style></head><body>
<header>
  <div class="brand"><span id="sd" class="dot"></span>AdBlock DNS</div>
  <div class="meta"><span id="ip">--</span><span>·</span><span id="wifi">--</span></div>
</header>
<main>
  <section class="stats" id="stats"></section>

  <section class="card">
    <h2>每小时拦截 <small>最近 24 小时</small></h2>
    <div id="chart"></div>
    <div class="axis"><span>00</span><span>06</span><span>12</span><span>18</span><span>23</span></div>
  </section>

  <div class="cols">
    <section class="card">
      <h2>域名排行榜 <small>今日</small></h2>
      <table id="top"></table>
    </section>
    <section class="card">
      <h2>客户端 <small>最近活跃</small></h2>
      <table id="clients"></table>
    </section>
  </div>

  <section class="card">
    <h2>自定义拦截域名</h2>
    <div class="row">
      <input id="d2" placeholder="例如 ads.example.com" onkeydown="if(event.key==='Enter')addB()">
      <button class="primary" onclick="addB()">添加</button>
    </div>
    <div class="chips" id="custom"></div>
  </section>

  <div class="cols">
    <section class="card">
      <h2>规则更新</h2>
      <div class="row" style="margin-bottom:10px">
        <input id="upurl" placeholder="规则文件地址（.bin）">
        <select id="upiv">
          <option value="0">不自动更新</option>
          <option value="6">每 6 小时</option>
          <option value="12">每 12 小时</option>
          <option value="24">每天</option>
          <option value="72">每 3 天</option>
          <option value="168">每周</option>
        </select>
        <button class="primary" onclick="saveUp()">保存</button>
      </div>
      <div id="upinfo"></div>
      <div class="hint">远程地址需指向本固件格式的 .bin 规则文件；也可以直接上传本地文件。</div>
      <form method="post" action="/blocklist-upload" enctype="multipart/form-data" class="row" style="margin-top:10px">
        <input type="file" name="bl" accept=".bin">
        <button>上传规则</button>
      </form>
    </section>
    <section class="card">
      <h2>设备</h2>
      <div id="dev"></div>
      <div class="row" style="margin-top:12px">
        <button onclick="refreshScreen()">立即刷新屏幕</button>
      </div>
      <div class="hint">刷新一次约需 15 秒，期间屏幕会闪烁几秒。</div>
      <form method="post" action="/update" enctype="multipart/form-data" class="row" style="margin-top:12px">
        <input type="file" name="fw" accept=".bin">
        <button>升级固件</button>
      </form>
    </section>
  </div>

  <section class="card">
    <h2>网络</h2>
    <form id="wform" method="post" action="/setwifi" class="row">
      <input name="ssid" placeholder="WiFi 名称" required>
      <input name="pass" type="password" placeholder="WiFi 密码">
      <button class="primary">保存并重启</button>
    </form>
    <div class="hint">保存后设备会重启并连接新的网络。若一直连不上，长按开发板 BOOT 键 3 秒可重新开启配网热点。</div>
  </section>

  <section class="card">
    <h2>管理员密码 <small>用户名 admin</small></h2>
    <form id="pform" class="row" onsubmit="return false">
      <input id="np1" type="password" placeholder="新密码（6-64 位，不含空格）" autocomplete="new-password">
      <input id="np2" type="password" placeholder="再次输入新密码" autocomplete="new-password">
      <button class="primary" onclick="setPass()">修改密码</button>
    </form>
    <div class="hint">修改成功后浏览器会要求用新密码重新登录。密码保存在设备上，重启后依然有效；忘记密码可在串口日志中查看。</div>
  </section>
</main>
<div class="toast" id="t"></div>
<script>
var $=function(i){return document.getElementById(i)};
var TK="",upInit=false;
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return {"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]})}
function fmt(n){return (n==null?0:n).toLocaleString("en-US")}
function toast(m){var t=$("t");t.textContent=m;t.classList.add("on");setTimeout(function(){t.classList.remove("on")},1900)}
function tile(k,v,u){return '<div class="tile"><div class="k">'+k+'</div><div class="v">'+v+(u?'<u>'+u+'</u>':'')+'</div></div>'}
function kv(k,v){return '<div class="kv"><span>'+k+'</span><span>'+v+'</span></div>'}
async function load(){
  var d;
  try{ var r=await fetch("/stats",{cache:"no-store"}); d=await r.json(); }catch(e){ $("sd").className="dot off"; return }
  TK=d.token;
  $("sd").className="dot"; $("ip").textContent=d.ip; $("wifi").textContent=d.wifi||"未连接";
  var tot=d.todayBlocked+d.todayAllowed;
  var rate=tot?(d.todayBlocked*100/tot).toFixed(1):"0.0";
  $("stats").innerHTML=tile("今日拦截",fmt(d.todayBlocked))+
    tile("今日查询",fmt(tot))+tile("拦截率",rate,"%")+tile("累计拦截",fmt(d.blocked))+
    tile("规则数量",fmt(d.domains))+tile("剩余内存",(d.heap/1024|0),"KB")+
    tile("信号强度",d.rssi,"dBm")+tile("芯片温度",d.temp,"°C");
  var mx=Math.max(1,Math.max.apply(null,d.hours));
  var slot=480/24,bars="";
  for(var i=0;i<d.hours.length;i++){
    var v=d.hours[i],h=v/mx*86;
    if(v>0) bars+='<rect x="'+(i*slot+3)+'" y="'+(94-h)+'" width="'+(slot-6)+'" height="'+h+'" rx="2" fill="#4d8dff"><title>'+i+' 时  '+v+' 次</title></rect>';
  }
  $("chart").innerHTML='<svg viewBox="0 0 480 96" preserveAspectRatio="none" style="width:100%;height:96px;display:block">'+
    '<line x1="0" y1="95" x2="480" y2="95" stroke="#242e3c" stroke-width="1"/>'+bars+'</svg>';
  $("top").innerHTML='<thead><tr><th>#</th><th>域名</th><th class="n">次数</th></tr></thead><tbody>'+
    (d.top.length?d.top.map(function(t,i){return '<tr><td class="mut">'+(i+1)+'</td><td>'+esc(t.d)+'</td><td class="n">'+t.n+'</td></tr>'}).join(""):
     '<tr><td colspan="3" class="mut">暂无拦截记录</td></tr>')+'</tbody>';
  $("clients").innerHTML='<thead><tr><th>IP / MAC</th><th class="n">拦截</th><th class="n">放行</th><th></th></tr></thead><tbody>'+
    (d.clients.length?d.clients.map(function(c){return '<tr><td>'+esc(c.ip)+'<div class="sub">'+esc(c.mac)+'</div></td><td class="n">'+c.blocked+
      '</td><td class="n">'+c.allowed+'</td><td class="n"><button onclick="ban(\''+esc(c.ip)+'\')">'+(c.banned?"解禁":"拉黑")+'</button></td></tr>'}).join(""):
     '<tr><td colspan="4" class="mut">暂无客户端</td></tr>')+'</tbody>';
  $("custom").innerHTML=d.custom.length?d.custom.map(function(x){return '<span class="chip">'+esc(x)+'<b onclick="unb(\''+esc(x)+'\')">×</b></span>'}).join(""):
    '<span class="mut">暂无自定义规则</span>';
  $("dev").innerHTML=kv("运行时间",d.uptime)+kv("布隆过滤器",d.bloom)+kv("待处理查询",d.pending)+
    kv("累计查询",fmt(d.allowed+d.blocked))+kv("累计放行",fmt(d.allowed))+kv("设备地址",d.ip);
  $("upinfo").innerHTML=kv("最近更新",esc(d.upstat))+
    kv("自动更新",(d.upiv>0?'<span class="tag ok">每 '+d.upiv+' 小时</span>':'<span class="tag off">已关闭</span>'));
  if(!upInit){ $("upurl").value=d.upurl||""; $("upiv").value=String(d.upiv); upInit=true; }
  $("wform").action="/setwifi?token="+encodeURIComponent(TK);
}
async function ban(ip){ await fetch("/ban?token="+TK+"&ip="+encodeURIComponent(ip)); toast("已更新"); load() }
async function addB(){ var d=$("d2").value.trim(); if(!d) return; await fetch("/addblock?token="+TK+"&d="+encodeURIComponent(d)); $("d2").value=""; toast("已添加 "+d); load() }
async function unb(d){ await fetch("/unblock?token="+TK+"&d="+encodeURIComponent(d)); toast("已移除"); load() }
async function saveUp(){ await fetch("/setupdate?token="+TK+"&url="+encodeURIComponent($("upurl").value.trim())+"&iv="+$("upiv").value); toast("已保存"); load() }
async function refreshScreen(){ await fetch("/refresh-screen?token="+TK); toast("正在刷新屏幕"); }
async function setPass(){
  var a=$("np1").value,b=$("np2").value;
  if(a.length<6||a.length>64){ toast("密码长度需为 6-64 位"); return }
  if(a!==b){ toast("两次输入不一致"); return }
  var r=await fetch("/setpass",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"token="+encodeURIComponent(TK)+"&new="+encodeURIComponent(a)});
  if(r.ok){ $("np1").value="";$("np2").value=""; toast("密码已修改，请重新登录"); return }
  var msg=await r.text();
  var map={length:"密码长度需为 6-64 位",charset:"密码只能使用可见 ASCII 字符且不含空格",same:"新密码与当前密码相同","bad token":"会话已过期，请刷新页面"};
  toast(map[msg]||"修改失败");
}
load(); setInterval(load,5000);
</script></body></html>
)HTML";
