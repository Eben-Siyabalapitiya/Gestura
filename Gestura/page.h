#pragma once
// Calibration page served at http://192.168.4.1

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gestura</title>
<style>
:root{--bg:#0f1115;--card:#181b22;--line:#2a2f3a;--text:#e8eaf0;--dim:#8a91a0;--acc:#4f8cff;--on:#33d17a;--warn:#f5c211;--bad:#ff5c5c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:16px}
h1{font-size:20px;margin:0 0 4px}
h2{font-size:15px;margin:0 0 10px;color:var(--dim);font-weight:600;text-transform:uppercase;letter-spacing:.05em}
.wrap{max-width:900px;margin:0 auto}
.status{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0 16px}
.pill{padding:4px 10px;border-radius:999px;background:var(--card);border:1px solid var(--line);font-size:13px;color:var(--dim)}
.pill.on{background:var(--on);color:#0b1a10;border-color:var(--on);font-weight:700}
.pill.bad{background:var(--bad);color:#fff;border-color:var(--bad)}
.pill.warn{background:var(--warn);color:#1a1500;border-color:var(--warn)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:640px){.grid{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.scene{height:170px;perspective:600px;display:flex;align-items:center;justify-content:center}
.box{position:relative;width:120px;height:20px;transform-style:preserve-3d;transition:transform 60ms linear}
.f{position:absolute;border:1px solid rgba(255,255,255,.25);display:flex;align-items:center;justify-content:center;font-size:11px;font-weight:700}
.top,.bot{width:120px;height:80px;top:-30px}
.top{transform:rotateX(90deg) translateZ(10px);background:rgba(79,140,255,.85)}
.bot{transform:rotateX(-90deg) translateZ(10px);background:rgba(79,140,255,.35)}
.fr,.bk{width:120px;height:20px}
.fr{transform:translateZ(40px);background:rgba(245,194,17,.9);color:#1a1500}
.bk{transform:rotateY(180deg) translateZ(40px);background:rgba(79,140,255,.6)}
.lf,.rt{width:80px;height:20px;left:20px}
.lf{transform:rotateY(-90deg) translateZ(60px);background:rgba(79,140,255,.6)}
.rt{transform:rotateY(90deg) translateZ(60px);background:rgba(79,140,255,.6)}
.nums{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;text-align:center;margin:8px 0}
.nums div{background:#11141a;border-radius:8px;padding:6px}
.nums b{display:block;font-size:17px}
.nums span{font-size:11px;color:var(--dim)}
.bar{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--dim);margin:4px 0}
.bar .t{flex:1;height:8px;background:#11141a;border-radius:4px;position:relative;overflow:hidden}
.bar .t::after{content:"";position:absolute;left:50%;top:0;bottom:0;width:1px;background:var(--line)}
.bar i{position:absolute;top:0;bottom:0;background:var(--acc)}
.keys{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
.key{min-width:38px;text-align:center;padding:6px 8px;border-radius:8px;background:#11141a;border:1px solid var(--line);font-weight:700;font-size:13px;color:var(--dim)}
.key.on{background:var(--on);color:#0b1a10;border-color:var(--on)}
button{background:var(--acc);color:#fff;border:0;border-radius:8px;padding:9px 14px;font-weight:600;font-size:14px;cursor:pointer}
button.sec{background:#2a2f3a}
button:active{transform:scale(.97)}
.row{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
.set{display:grid;grid-template-columns:140px 1fr 56px;align-items:center;gap:8px;margin:8px 0}
.set label{font-size:13px;color:var(--dim)}
.set output{font-size:13px;text-align:right}
.chk{display:flex;flex-wrap:wrap;gap:14px;margin:8px 0;font-size:13px;color:var(--dim)}
select{background:#11141a;color:var(--text);border:1px solid var(--line);border-radius:6px;padding:4px}
input[type=range]{width:100%}
input[type=text],input[type=password]{background:#11141a;color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px;width:100%;font-size:14px}
.hint{font-size:12px;color:var(--dim);margin-top:6px}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--on);color:#0b1a10;padding:8px 16px;border-radius:999px;font-weight:700;opacity:0;transition:opacity .3s}
</style>
</head>
<body>
<div class="wrap">
<h1>Gestura</h1>
<div class="status">
  <span class="pill" id="pBle">Bluetooth</span>
  <span class="pill" id="pH0">Mouse hand</span>
  <span class="pill" id="pH1">Move hand</span>
  <span class="pill" id="pMode">Active</span>
</div>

<div class="grid">
  <div class="card">
    <h2>Mouse hand</h2>
    <div class="scene"><div class="box" id="box0">
      <div class="f top">MOUSE</div><div class="f bot"></div>
      <div class="f fr">FRONT</div><div class="f bk"></div>
      <div class="f lf"></div><div class="f rt"></div>
    </div></div>
    <div class="nums">
      <div><b id="p0">0</b><span>pitch</span></div>
      <div><b id="r0">0</b><span>roll</span></div>
      <div><b id="y0">0</b><span>turn</span></div>
    </div>
    <div class="bar">X<div class="t"><i id="bx0"></i></div></div>
    <div class="bar">Y<div class="t"><i id="by0"></i></div></div>
    <div class="bar">Z<div class="t"><i id="bz0"></i></div></div>
    <div class="keys">
      <span class="key" id="kl">L CLICK</span><span class="key" id="kr">R CLICK</span>
      <span class="key" id="t0">NEXT</span><span class="key" id="t1">PREV</span><span class="key" id="t2">TAP 3</span>
    </div>
    <div class="row"><button onclick="zero(0)">Zero mouse hand</button></div>
  </div>

  <div class="card">
    <h2>Move hand</h2>
    <div class="scene"><div class="box" id="box1">
      <div class="f top">MOVE</div><div class="f bot"></div>
      <div class="f fr">FRONT</div><div class="f bk"></div>
      <div class="f lf"></div><div class="f rt"></div>
    </div></div>
    <div class="nums">
      <div><b id="p1">0</b><span>pitch</span></div>
      <div><b id="r1">0</b><span>roll</span></div>
      <div><b id="a1">1.00</b><span>accel g</span></div>
    </div>
    <div class="bar">X<div class="t"><i id="bx1"></i></div></div>
    <div class="bar">Y<div class="t"><i id="by1"></i></div></div>
    <div class="bar">Z<div class="t"><i id="bz1"></i></div></div>
    <div class="keys">
      <span class="key" id="kw">W</span><span class="key" id="ka">A</span><span class="key" id="ks">S</span>
      <span class="key" id="kd">D</span><span class="key" id="kj">JUMP</span><span class="key" id="kv">VOICE</span>
    </div>
    <div class="row"><button onclick="zero(1)">Zero move hand</button></div>
  </div>
</div>

<div class="card">
  <div class="row" style="margin-top:0">
    <button onclick="zero(2)">Zero both hands</button>
    <button class="sec" onclick="act('pause')">Pause / resume</button>
    <button onclick="act('save','Saved to glove')">Save settings</button>
    <button class="sec" onclick="act('defaults','Defaults loaded').then(loadSettings)">Reset to defaults</button>
  </div>
  <div class="hint">Hold your hands still while zeroing (about 1 second). Changes apply right away, and "Save settings" keeps them after power off.</div>
</div>

<div class="card">
  <h2>Mouse hand</h2>
  <div class="set"><label>Mouse speed</label><input type="range" id="sens" min="0.2" max="5" step="0.1"><output id="o_sens"></output></div>
  <div class="set"><label>Deadzone (deg/s)</label><input type="range" id="dead" min="0" max="40" step="0.5"><output id="o_dead"></output></div>
  <div class="set"><label>Swing click strength</label><input type="range" id="swTh" min="100" max="600" step="10"><output id="o_swTh"></output></div>
  <div class="chk">
    <span>Turn axis <select id="axX"><option value="0">X</option><option value="1">Y</option><option value="2">Z</option></select></span>
    <span>Look up/down axis <select id="axY"><option value="0">X</option><option value="1">Y</option><option value="2">Z</option></select></span>
    <span>Swing axis <select id="swAx"><option value="0">X</option><option value="1">Y</option><option value="2">Z</option></select></span>
  </div>
  <div class="chk">
    <label><input type="checkbox" id="invX"> Flip turn</label>
    <label><input type="checkbox" id="lookY"> Allow look up/down</label>
    <label><input type="checkbox" id="invY"> Flip up/down</label>
    <label><input type="checkbox" id="swInv"> Flip swing</label>
    <label><input type="checkbox" id="clickRight"> Swing = right click</label>
    <label><input type="checkbox" id="outBle"> Send over Bluetooth</label>
    <label><input type="checkbox" id="outUsb"> Send over USB cable</label>
  </div>
  <div class="hint">Swing your hand and watch the X/Y/Z bars: the one that jumps the most is your swing axis. Turn your hand the same way to find the turn axis.</div>
</div>

<div class="card">
  <h2>Move hand</h2>
  <div class="set"><label>Smoothing (both hands)</label><input type="range" id="smooth" min="0" max="0.9" step="0.05"><output id="o_smooth"></output></div>
  <div class="set"><label>Tilt for W/S (deg)</label><input type="range" id="tilt" min="5" max="40" step="1"><output id="o_tilt"></output></div>
  <div class="set"><label>Tilt for A/D (deg)</label><input type="range" id="sideTilt" min="5" max="45" step="1"><output id="o_sideTilt"></output></div>
  <div class="set"><label>Key release margin (deg)</label><input type="range" id="release" min="0" max="20" step="1"><output id="o_release"></output></div>
  <div class="set"><label>Jump drop (g)</label><input type="range" id="jumpTh" min="0.1" max="0.9" step="0.05"><output id="o_jumpTh"></output></div>
  <div class="chk">
    <label><input type="checkbox" id="jumpOn"> Jump on</label>
    <label><input type="checkbox" id="diag"> Allow diagonal (W+A)</label>
    <label><input type="checkbox" id="autoZero"> Auto re-centre when still</label>
    <label><input type="checkbox" id="swapTilt"> Swap forward/sideways</label>
    <label><input type="checkbox" id="invFB"> Flip forward/back</label>
    <label><input type="checkbox" id="invLR"> Flip left/right</label>
  </div>
  <div class="chk">
    <span>Touch type <select id="touchMode"><option value="0">Touch sensor (TTP223)</option><option value="1">Copper tape to GND</option></select></span>
  </div>
  <div class="hint">Walking picks the strongest tilt only, so leaning forward will not trigger A/D by accident. Raise the tilt angles or smoothing if keys still flicker. Lower "jump drop" = you need a harder drop to jump.</div>
</div>

<div class="card">
  <h2>Cloud (HiveMQ)</h2>
  <div class="status">
    <span class="pill" id="pWifi">WiFi</span>
    <span class="pill" id="pMqtt">Cloud</span>
  </div>
  <div class="set"><label>WiFi name</label><input type="text" id="n_ssid"><span></span></div>
  <div class="set"><label>WiFi password</label><input type="password" id="n_pass"><span></span></div>
  <div class="set"><label>Broker host</label><input type="text" id="n_host" placeholder="xxxx.s1.eu.hivemq.cloud"><span></span></div>
  <div class="set"><label>Broker port</label><input type="text" id="n_port" placeholder="8883"><span></span></div>
  <div class="set"><label>Broker username</label><input type="text" id="n_user"><span></span></div>
  <div class="set"><label>Broker password</label><input type="password" id="n_mpass"><span></span></div>
  <div class="set"><label>Topic</label><input type="text" id="n_topic" placeholder="gestura"><span></span></div>
  <div class="row"><button onclick="saveNet()">Save and restart</button></div>
  <div class="hint">Use your phone hotspot for WiFi. After saving, the glove restarts and connects to HiveMQ, and you can open the cloud dashboard from any network. Your details are stored on the glove, not in the code.</div>
</div>
</div>
<div id="msg"></div>

<script>
const $ = id => document.getElementById(id);
const ranges = ['sens','dead','swTh','tilt','jumpTh','smooth','sideTilt','release'];
const selects = ['axX','axY','swAx','touchMode'];
const checks = ['invX','lookY','invY','swInv','clickRight','jumpOn','swapTilt','invFB','invLR','diag','outBle','outUsb','autoZero'];

function toast(t){const m=$('msg');m.textContent=t;m.style.opacity=1;setTimeout(()=>m.style.opacity=0,1400)}
function act(path,t){return fetch('/'+path).then(()=>{if(t)toast(t)})}
function zero(h){toast('Hold still...');fetch('/zero?h='+h).then(()=>toast('Zeroed'))}
function send(k,v){fetch('/set?'+k+'='+v)}

function saveNet(){
  const q = ['ssid','pass','host','port','user','mpass','topic']
    .map(k=>k+'='+encodeURIComponent($('n_'+k).value)).join('&');
  toast('Saving, glove is restarting...');
  fetch('/net?'+q).catch(()=>{});
}
function loadNet(){
  return fetch('/netinfo').then(r=>r.json()).then(n=>{
    ['ssid','host','user','topic'].forEach(k=>{if(!$('n_'+k).value)$('n_'+k).value=n[k]||''});
    if(!$('n_port').value)$('n_port').value=n.port||8883;
    pill('pWifi',n.wifi?'on':'',n.wifi?('WiFi '+n.ip):'WiFi not connected');
    pill('pMqtt',n.mqtt?'on':'',n.mqtt?'Cloud connected':'Cloud off');
  }).catch(()=>{});
}
setInterval(loadNet,4000);

ranges.forEach(k=>{$(k).addEventListener('input',e=>{$('o_'+k).textContent=e.target.value;send(k,e.target.value)})});
selects.forEach(k=>$(k).addEventListener('change',e=>send(k,e.target.value)));
checks.forEach(k=>$(k).addEventListener('change',e=>send(k,e.target.checked?1:0)));

function fillSettings(s){
  ranges.forEach(k=>{$(k).value=s[k];$('o_'+k).textContent=s[k]});
  selects.forEach(k=>$(k).value=s[k]);
  checks.forEach(k=>$(k).checked=!!s[k]);
}
function loadSettings(){return fetch('/data').then(r=>r.json()).then(d=>fillSettings(d.s))}

function bar(el,v){const w=Math.min(Math.abs(v)/500,1)*50;el.style.width=w+'%';el.style.left=(v<0?50-w:50)+'%'}
function on(id,v){$(id).classList.toggle('on',!!v)}
function pill(id,cls,text){const e=$(id);e.className='pill '+cls;e.textContent=text}

function draw(d){
  pill('pBle',d.ble?'on':'warn',d.ble?'Bluetooth connected':'Bluetooth waiting');
  pill('pH0',d.h[0].ok?'on':'bad',d.h[0].ok?'Mouse hand OK':'Mouse hand missing');
  pill('pH1',d.h[1].ok?'on':'bad',d.h[1].ok?'Move hand OK':'Move hand missing');
  pill('pMode',d.zeroing?'warn':d.paused?'bad':'on',d.zeroing?'Zeroing':d.paused?'Paused':'Active');
  d.h.forEach((h,i)=>{
    $('box'+i).style.transform=`rotateX(-25deg) rotateY(${-h.y}deg) rotateX(${h.p}deg) rotateZ(${h.r}deg)`;
    $('p'+i).textContent=Math.round(h.p);
    $('r'+i).textContent=Math.round(h.r);
    bar($('bx'+i),h.gx);bar($('by'+i),h.gy);bar($('bz'+i),h.gz);
  });
  $('y0').textContent=Math.round(d.h[0].y);
  $('a1').textContent=d.h[1].a.toFixed(2);
  on('kw',d.k.w);on('ka',d.k.a);on('ks',d.k.s);on('kd',d.k.d);on('kj',d.k.j);on('kv',d.k.v);
  on('kl',d.k.l);on('kr',d.k.r);
  on('t0',d.t[0]);on('t1',d.t[1]);on('t2',d.t[2]);
}

function poll(){
  fetch('/data').then(r=>r.json()).then(draw).catch(()=>pill('pBle','bad','Lost connection to glove'))
    .finally(()=>setTimeout(poll,80));
}
loadSettings().then(loadNet).then(poll).catch(()=>setTimeout(()=>location.reload(),2000));
</script>
</body>
</html>
)rawliteral";
