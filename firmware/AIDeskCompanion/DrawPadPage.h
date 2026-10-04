#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(

<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>OLED Draw Pad</title>
<style>
  :root{
    --bg:#0a0d10;
    --panel:#12171c;
    --panel-2:#161c22;
    --border:#232b32;
    --accent:#6ee7d8;
    --accent-dim:#3a5a56;
    --warn:#ffb454;
    --text:#e6edf0;
    --text-dim:#7c8b94;
    --mono: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", Menlo, monospace;
    --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  }
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
  html,body{margin:0;height:100%;}
  body{
    background:
      radial-gradient(circle at 20% -10%, #10181c 0%, transparent 40%),
      var(--bg);
    color:var(--text);
    font-family:var(--sans);
    display:flex;
    flex-direction:column;
    align-items:center;
    min-height:100%;
    padding:20px 16px 32px;
    -webkit-user-select:none;
    user-select:none;
  }
  .topbar{
    width:100%;
    max-width:640px;
    display:flex;
    align-items:center;
    justify-content:space-between;
    margin-bottom:18px;
  }
  .brand{display:flex;align-items:baseline;gap:8px;font-family:var(--mono);letter-spacing:.5px;}
  .brand b{font-size:15px;font-weight:700;color:var(--text);}
  .brand span{font-size:11px;color:var(--text-dim);}
  .status{
    display:flex;align-items:center;gap:7px;
    font-family:var(--mono);font-size:11px;color:var(--text-dim);
    padding:5px 10px;border:1px solid var(--border);border-radius:20px;background:var(--panel);
  }
  .dot{width:7px;height:7px;border-radius:50%;background:#4a555c;transition:background .2s;}
  .status.live .dot{background:var(--accent);box-shadow:0 0 6px var(--accent);animation:pulse 1.6s infinite;}
  .status.live{color:var(--accent);}
  @keyframes pulse{0%,100%{opacity:1;}50%{opacity:.4;}}

  .device{
    width:100%;max-width:640px;
    background:linear-gradient(180deg,var(--panel) 0%,var(--panel-2) 100%);
    border:1px solid var(--border);border-radius:18px;
    padding:20px 20px 16px;
    box-shadow:0 20px 50px -20px rgba(0,0,0,.6), inset 0 1px 0 rgba(255,255,255,.02);
  }
  .screen-wrap{
    position:relative;border-radius:6px;background:#000;padding:14px;
    border:1px solid #1c2329;
    box-shadow:inset 0 0 0 1px rgba(255,255,255,.02), 0 0 40px -12px var(--accent-dim);
  }
  canvas#pad{
    display:block;width:100%;height:auto;aspect-ratio:2/1;
    background:#000;
    image-rendering:pixelated;
    image-rendering:crisp-edges;
    touch-action:none;
    cursor:crosshair;
    border-radius:2px;
  }
  .screen-wrap::after{
    content:"";position:absolute;inset:14px;pointer-events:none;
    background:repeating-linear-gradient(180deg, rgba(255,255,255,.025) 0px, rgba(255,255,255,.025) 1px, transparent 1px, transparent 3px);
    border-radius:2px;mix-blend-mode:screen;
  }
  .pinrow{display:flex;justify-content:center;gap:22px;margin-top:12px;font-family:var(--mono);font-size:9px;letter-spacing:1px;color:var(--text-dim);}
  .pinrow span{display:flex;align-items:center;gap:5px;}
  .pinrow i{width:5px;height:5px;border-radius:50%;background:var(--text-dim);display:block;}
  .label-tag{text-align:center;font-family:var(--mono);font-size:10px;color:var(--text-dim);letter-spacing:1.5px;margin-top:10px;text-transform:uppercase;}

  .controls{
    width:100%;max-width:640px;margin-top:16px;
    background:var(--panel);border:1px solid var(--border);border-radius:14px;
    padding:16px 18px;display:flex;flex-direction:column;gap:14px;
  }
  .row{display:flex;align-items:center;gap:14px;}
  .row label{font-family:var(--mono);font-size:11px;color:var(--text-dim);letter-spacing:1px;min-width:64px;}
  input[type=range]{flex:1;-webkit-appearance:none;height:4px;border-radius:2px;background:var(--border);outline:none;}
  input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none;width:20px;height:20px;border-radius:50%;
    background:var(--accent);border:3px solid #0a0d10;box-shadow:0 0 0 1px var(--accent);
    cursor:pointer;margin-top:-8px;
  }
  input[type=range]::-webkit-slider-runnable-track{height:4px;border-radius:2px;}
  input[type=range]::-moz-range-thumb{
    width:14px;height:14px;border-radius:50%;
    background:var(--accent);border:3px solid #0a0d10;box-shadow:0 0 0 1px var(--accent);cursor:pointer;
  }
  .pensize-val{font-family:var(--mono);font-size:13px;color:var(--text);min-width:34px;text-align:right;}
  .pen-preview{width:26px;height:26px;border-radius:50%;background:#000;border:1px solid var(--border);display:flex;align-items:center;justify-content:center;flex-shrink:0;}
  .pen-preview i{border-radius:50%;background:var(--accent);display:block;}

  .btnrow{display:flex;gap:10px;}
  button{
    font-family:var(--mono);font-size:12px;letter-spacing:1px;text-transform:uppercase;
    border-radius:9px;border:1px solid var(--border);padding:12px 16px;cursor:pointer;
    transition:transform .08s, background .15s, border-color .15s;
  }
  button:active{transform:scale(.96);}
  .btn-clear{flex:1;background:transparent;color:var(--warn);border-color:#4a3a24;}
  .btn-clear:active{background:rgba(255,180,84,.08);}
  .btn-exit{flex:1;background:transparent;color:var(--text-dim);border-color:var(--border);}
  .btn-exit:active{background:rgba(110,231,216,.08);}

  footer{margin-top:18px;font-family:var(--mono);font-size:10px;color:#4a555c;letter-spacing:.5px;}
</style>
</head>
<body>

  <div class="topbar">
    <div class="brand"><b>DRAW&middot;PAD</b><span>SSD1306 &middot; 128&times;64</span></div>
    <div class="status" id="status"><span class="dot"></span><span id="statusText">connecting</span></div>
  </div>

  <div class="device">
    <div class="screen-wrap">
      <canvas id="pad" width="128" height="64"></canvas>
    </div>
    <div class="pinrow">
      <span><i></i>VCC</span>
      <span><i></i>GND</span>
      <span><i></i>SDA</span>
      <span><i></i>SCL</span>
    </div>
    <div class="label-tag">live mirror &mdash; tech by nandhu</div>
  </div>

  <div class="controls">
    <div class="row">
      <label>PEN SIZE</label>
      <input type="range" id="penSize" min="1" max="8" value="2" step="1">
      <div class="pen-preview"><i id="penDot"></i></div>
      <div class="pensize-val" id="penVal">2px</div>
    </div>
    <div class="btnrow">
      <button class="btn-clear" id="clearBtn">Clear panel</button>
      <button class="btn-exit" id="exitBtn">Exit Draw Pad</button>
    </div>
  </div>

  <footer>ESP32 &middot; WebSocket live draw &middot; no-lag mode</footer>

<script>
(function(){
  const canvas = document.getElementById('pad');
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#000';
  ctx.fillRect(0,0,canvas.width,canvas.height);

  const statusEl = document.getElementById('status');
  const statusText = document.getElementById('statusText');
  const penSizeInput = document.getElementById('penSize');
  const penVal = document.getElementById('penVal');
  const penDot = document.getElementById('penDot');
  const clearBtn = document.getElementById('clearBtn');
  const exitBtn = document.getElementById('exitBtn');

  let penSize = parseInt(penSizeInput.value, 10);

  function updatePenPreview(){
    penVal.textContent = penSize + 'px';
    const px = Math.max(2, Math.min(18, penSize * 2));
    penDot.style.width = px + 'px';
    penDot.style.height = px + 'px';
  }
  updatePenPreview();

  penSizeInput.addEventListener('input', () => {
    penSize = parseInt(penSizeInput.value, 10);
    updatePenPreview();
  });

  // ---------------- WebSocket, with auto-reconnect ----------------
  let ws = null;
  let wsReady = false;
  let reconnectTimer = null;

  function setStatus(connected, text){
    statusEl.classList.toggle('live', connected);
    statusText.textContent = text;
  }

  function connect(){
    setStatus(false, 'connecting');
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(proto + '://' + location.host + '/ws');
    ws.onopen = () => { wsReady = true; setStatus(true, 'live'); };
    ws.onclose = () => {
      wsReady = false;
      setStatus(false, 'reconnecting');
      clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(connect, 1200);
    };
    ws.onerror = () => {
      if (!window.__drawPadFailureSent) { window.__drawPadFailureSent = true; fetch('/event?type=failed', { keepalive: true }).catch(()=>{}); }
      try{ ws.close(); }catch(e){}
    };
  }
  connect();

  // ------- batched sender: coalesces fast pointermove into one WS frame -------
  let queue = [];
  function queueSend(cmd){ queue.push(cmd); }
  function flush(){
    if (queue.length && wsReady && ws.readyState === WebSocket.OPEN) {
      ws.send(queue.join(';'));
      queue = [];
    }
    requestAnimationFrame(flush);
  }
  requestAnimationFrame(flush);
  function flushNow(){
    if (queue.length && wsReady && ws.readyState === WebSocket.OPEN) {
      ws.send(queue.join(';'));
      queue = [];
    }
  }

  // ---------------------------- drawing ----------------------------
  let drawing = false;
  let lastX = 0, lastY = 0;

  function toCanvasPoint(clientX, clientY){
    const rect = canvas.getBoundingClientRect();
    let x = Math.round((clientX - rect.left) / rect.width  * canvas.width);
    let y = Math.round((clientY - rect.top)  / rect.height * canvas.height);
    x = Math.max(0, Math.min(canvas.width  - 1, x));
    y = Math.max(0, Math.min(canvas.height - 1, y));
    return [x, y];
  }

  function localDot(x, y, size){
    ctx.fillStyle = '#fff';
    ctx.beginPath();
    ctx.arc(x, y, Math.max(0.5, size/2), 0, Math.PI*2);
    ctx.fill();
  }
  function localLine(x0,y0,x1,y1,size){
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = size;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.beginPath();
    ctx.moveTo(x0,y0);
    ctx.lineTo(x1,y1);
    ctx.stroke();
    localDot(x1,y1,size);
  }

  function pointerDown(e){
    canvas.setPointerCapture(e.pointerId);
    const [x,y] = toCanvasPoint(e.clientX, e.clientY);
    drawing = true;
    lastX = x; lastY = y;
    localDot(x, y, penSize);
    queueSend('P,' + x + ',' + y + ',' + penSize);
  }
  function pointerMove(e){
    if (!drawing) return;
    const [x,y] = toCanvasPoint(e.clientX, e.clientY);
    if (x === lastX && y === lastY) return;
    localLine(lastX, lastY, x, y, penSize);
    queueSend('L,' + lastX + ',' + lastY + ',' + x + ',' + y + ',' + penSize);
    lastX = x; lastY = y;
  }
  function pointerUp(e){
    if (!drawing) return;
    drawing = false;
    try{ canvas.releasePointerCapture(e.pointerId); }catch(err){}
  }

  canvas.addEventListener('pointerdown', pointerDown);
  canvas.addEventListener('pointermove', pointerMove);
  canvas.addEventListener('pointerup', pointerUp);
  canvas.addEventListener('pointercancel', pointerUp);
  canvas.addEventListener('pointerleave', (e)=>{ if(drawing) pointerUp(e); });
  canvas.addEventListener('contextmenu', (e)=> e.preventDefault());

  clearBtn.addEventListener('click', () => {
    ctx.fillStyle = '#000';
    ctx.fillRect(0,0,canvas.width,canvas.height);
    queue = ['CLR'];
    flushNow();
  });

  exitBtn.addEventListener('click', () => {
    try { if (ws) ws.close(); } catch(e) {}
    window.close();
    setTimeout(() => { window.location.href = 'about:blank'; }, 50);
  });
})();
</script>
</body>
</html>

)HTMLPAGE";
