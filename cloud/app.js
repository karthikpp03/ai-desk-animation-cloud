const $ = id => document.getElementById(id);

// Worker API. To test against a local `wrangler dev`, change this one line.
const API_BASE = 'https://ai-desk-animation-api.zeus-karthik11.workers.dev';

// The old Connection settings stored an admin token in this browser. It is no longer used: remove it.
try { for (const k of ['animationApiBase', 'animationAdminToken']) { localStorage.removeItem(k); sessionStorage.removeItem(k); } } catch {}

/* ------------------------------------------------------------------ logs */
const LOG_KEY = 'animationCloudLogs';
const LOG_MAX = 100;
let logs = [];
try { logs = JSON.parse(localStorage.getItem(LOG_KEY) || '[]').slice(0, LOG_MAX); } catch { logs = []; }

// Logs must never contain secrets: redact anything token-shaped and cap the length.
function redact(text) {
  return String(text)
    .replace(/\b(ghp_|gho_|github_pat_|sk-)[A-Za-z0-9_-]+/g, '[redacted]')
    .replace(/\b[a-f0-9]{32,}\b/gi, '[redacted]')
    .slice(0, 240);
}
function log(text, level = 'info') {
  logs.unshift({ t: Date.now(), level, text: redact(text) });
  logs.length = Math.min(logs.length, LOG_MAX);
  try { localStorage.setItem(LOG_KEY, JSON.stringify(logs)); } catch {}
  renderLogs();
}
function renderLogs() {
  const root = $('logs');
  if (!logs.length) { root.innerHTML = '<div class="log-empty">No events yet.</div>'; return; }
  root.innerHTML = logs.map(l => `<div class="log-row ${l.level}"><time>${new Date(l.t).toLocaleTimeString([], { hour12: false })}</time><span>${escapeHtml(l.text)}</span></div>`).join('');
}
function escapeHtml(s) { return String(s).replace(/[&<>"']/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m])); }

/* ------------------------------------------------------------------- api */
async function api(path, options = {}) {
  let res;
  try {
    res = await fetch(API_BASE + path, options);
  } catch {
    const err = new Error('Network error: could not reach the Animation API. Check your internet connection.');
    err.kind = 'network';
    throw err;
  }
  const type = res.headers.get('content-type') || '';
  let data = null;
  try { data = type.includes('json') ? await res.json() : await res.arrayBuffer(); } catch {}
  if (!res.ok) {
    const message = (data && data.error) || `HTTP ${res.status}`;
    const err = new Error(message);
    err.status = res.status;
    err.stage = data && data.stage;
    err.kind = /github/i.test(message) ? 'github' : res.status >= 500 ? 'worker' : 'api';
    throw err;
  }
  return data;
}
// Human-readable text for a failed request (never includes request headers or tokens).
function describe(err) {
  if (err.kind === 'github') return `GitHub storage error: ${err.message}`;
  if (err.kind === 'worker') return `Worker error (HTTP ${err.status}): ${err.message}`;
  return err.message;
}
function msg(text, bad = false) { $('message').textContent = text; $('message').style.color = bad ? '#ef8d8d' : ''; }
function fail(prefix, err) { msg(err.message, true); log(`${prefix}: ${describe(err)}`, 'error'); }

/* -------------------------------------------------- animation previews */
// Previews are drawn from the SAME normalized frames.bin the ESP32 plays (1024 bytes per 128x64 frame,
// row-major, MSB first = Adafruit drawBitmap layout), so they match the OLED. Nothing is parsed twice.
const FRAME_BYTES = 1024;
const ON = 0xFFFFF4E8;   // little-endian ABGR: soft white, like lit OLED pixels
const OFF = 0xFF000000;
const frameCache = new Map();   // id -> Promise<Uint8Array>
const reduceMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
let animators = [];
let observer = null;
let rafId = 0;

function loadFrames(a) {
  if (!frameCache.has(a.id)) {
    const p = api(`/api/animations/${encodeURIComponent(a.id)}/preview`).then(buf => {
      const bytes = new Uint8Array(buf);
      if (bytes.length !== a.frames * FRAME_BYTES) throw new Error(`unexpected frame data size (${bytes.length} bytes)`);
      return bytes;
    });
    p.catch(() => frameCache.delete(a.id));
    frameCache.set(a.id, p);
  }
  return frameCache.get(a.id);
}
function makeRenderer(canvas) {
  const ctx = canvas.getContext('2d');
  const image = ctx.createImageData(128, 64);
  const px = new Uint32Array(image.data.buffer);
  return (bytes, frame) => {
    const base = frame * FRAME_BYTES;
    for (let y = 0; y < 64; y++) {
      for (let x = 0; x < 128; x++) {
        px[y * 128 + x] = (bytes[base + y * 16 + (x >> 3)] & (0x80 >> (x & 7))) ? ON : OFF;
      }
    }
    ctx.putImageData(image, 0, 0);
  };
}
function setupPreview(card, a) {
  const box = card.querySelector('.preview');
  const canvas = box.querySelector('canvas');
  const render = makeRenderer(canvas);
  const anim = { a, bytes: null, render, frame: 0, last: 0, visible: false, started: false };
  animators.push(anim);
  card._anim = anim;
  anim.start = async () => {
    if (anim.started) return;
    anim.started = true;
    try {
      anim.bytes = await loadFrames(a);
      render(anim.bytes, 0);               // representative first frame right away
      kick();
    } catch (e) {
      anim.started = false;
      box.insertAdjacentHTML('beforeend', '<div class="preview-note">Preview unavailable</div>');
      log(`Preview failed for ${a.name}: ${describe(e)}`, 'warn');
    }
  };
  observer.observe(card);
}
function tick(now) {
  rafId = 0;
  let any = false;
  for (const an of animators) {
    if (!an.bytes || !an.visible || an.a.frames < 2) continue;
    any = true;
    const interval = an.a.frameDurationMs || 100;
    if (now - an.last >= interval) {
      an.last = now - ((now - an.last) % interval || 0);
      an.frame = (an.frame + 1) % an.a.frames;
      an.render(an.bytes, an.frame);
    }
  }
  if (any && !document.hidden) rafId = requestAnimationFrame(tick);
}
function kick() { if (!reduceMotion && !rafId) rafId = requestAnimationFrame(tick); }
document.addEventListener('visibilitychange', () => { if (!document.hidden) kick(); });

/* ------------------------------------------------------------ animations */
const animationsById = new Map();
async function loadAnimations() {
  const data = await api('/api/animations');
  const root = $('animations');
  root.innerHTML = '';
  animators = [];
  if (observer) observer.disconnect();
  observer = 'IntersectionObserver' in window ? new IntersectionObserver(entries => {
    for (const e of entries) {
      const an = e.target._anim;
      if (!an) continue;
      an.visible = e.isIntersecting;
      if (e.isIntersecting) { an.start(); kick(); }
    }
  }, { rootMargin: '150px' }) : { observe: el => { el._anim.visible = true; el._anim.start(); }, disconnect() {} };

  animationsById.clear();
  $('count').textContent = `${data.animations.length}`;
  $('empty').style.display = data.animations.length ? 'none' : 'block';
  for (const a of data.animations) {
    animationsById.set(a.id, a);
    const card = document.createElement('article');
    card.className = 'anim card';
    card.innerHTML = `<div class="preview"><canvas width="128" height="64" aria-label="Preview of ${escapeHtml(a.name)}"></canvas></div><h3 title="${escapeHtml(a.name)}">${escapeHtml(a.name)}</h3><div class="meta">${a.frames} frames · ${a.fps} FPS · ${a.frameDurationMs} ms</div><div class="actions"><button class="play">▶ Play</button></div>`;
    card.querySelector('.play').onclick = () => play(a.id, a.name);
    root.appendChild(card);
    setupPreview(card, a);
  }
}
async function play(id, name) {
  log(`Animation selected: ${name}`);
  try {
    await api(`/api/animations/${encodeURIComponent(id)}/play`, { method: 'POST' });
    msg(`Play command sent: ${name}`);
    log(`Animation playback started (command sent): ${name}`);
  } catch (e) { fail(`Could not start ${name}`, e); }
}
async function failures() {
  try {
    const d = await api('/api/import-failures');
    $('failures').innerHTML = d.failures.length ? d.failures.map(f => `<div class="failure"><strong>${escapeHtml(f.filename)}</strong><span>${escapeHtml(f.reason)}</span><br><small>${escapeHtml(f.createdAt)}</small></div>`).join('') : '<div class="empty">No failed imports.</div>';
  } catch (e) { $('failures').innerHTML = '<div class="empty">Could not load failures.</div>'; }
}

/* ---------------------------------------------------------- device status */
const MODE_LABEL = { normal: 'Normal', animation: 'Animation Display', unknown: '—' };
const POLL_MS = 10000;      // normal status poll
const POLL_FAST_MS = 2000;  // while waiting for a mode change to be confirmed
const MODE_CONFIRM_MS = 25000;
let statusTimer = 0;
let statusFails = 0;
let lastStatus = null;
let prev = null;            // { online, mode, anim } for transition logging
let pendingMode = null;     // { target, at }

function renderStatus(s) {
  const pill = $('dot').parentElement;
  const mode = s.online ? (s.mode || 'unknown') : 'unknown';
  if (s.online) {
    const anim = mode === 'animation' && s.currentAnimationId ? ` · ${(animationsById.get(s.currentAnimationId) || {}).name || s.currentAnimationId}` : '';
    $('deviceStatus').textContent = `ESP32 Online${mode !== 'unknown' ? ' · ' + MODE_LABEL[mode] + ' mode' : ''}${anim}`;
  } else {
    $('deviceStatus').textContent = 'ESP32 Offline';
  }
  pill.className = 'status ' + (s.online ? 'online' : 'offline');
  pill.title = s.lastSeenAt ? `Last heartbeat ${Math.max(0, Math.round(((s.serverTime || Date.now()) - s.lastSeenAt) / 1000))} s ago` : 'No heartbeat received yet';
  renderModeButton(s, mode);
}
function renderModeButton(s, mode) {
  const btn = $('modeBtn');
  $('modeState').textContent = pendingMode ? 'switching…' : MODE_LABEL[mode];
  btn.disabled = !!pendingMode || !s.online || mode === 'unknown';
  btn.title = !s.online ? 'ESP32 is offline' : 'Switch the ESP32 between normal mode and Animation Display Mode';
}
function trackTransitions(s) {
  const mode = s.online ? (s.mode || 'unknown') : 'unknown';
  const anim = s.online && mode === 'animation' ? (s.currentAnimationId || null) : null;
  if (!prev) {
    log(s.online ? `ESP32 connected${mode !== 'unknown' ? ` (${MODE_LABEL[mode]} mode)` : ''}` : 'ESP32 offline (no recent heartbeat)', s.online ? 'info' : 'warn');
  } else {
    if (s.online && !prev.online) log(`ESP32 connected${mode !== 'unknown' ? ` (${MODE_LABEL[mode]} mode)` : ''}`);
    if (!s.online && prev.online) log('ESP32 disconnected: heartbeat lost', 'warn');
    if (s.online && prev.online && mode !== prev.mode && mode !== 'unknown') {
      if (!pendingMode || pendingMode.target !== mode) log(`Mode changed: ${MODE_LABEL[mode]} mode (changed on the device)`);
    }
    if (anim && anim !== prev.anim) log(`Playback started on ESP32: ${(animationsById.get(anim) || {}).name || anim}`);
  }
  if (pendingMode) {
    if (s.online && mode === pendingMode.target) {
      log(`Mode changed: ESP32 is now in ${MODE_LABEL[mode]} mode`);
      msg(`ESP32 switched to ${MODE_LABEL[mode]} mode.`);
      pendingMode = null;
    } else if (Date.now() - pendingMode.at > MODE_CONFIRM_MS) {
      log(`Mode change to ${MODE_LABEL[pendingMode.target]} was not confirmed by the ESP32 (it may be busy, have a locked screen, or have lost Wi-Fi).`, 'warn');
      msg('Mode change not confirmed by the ESP32.', true);
      pendingMode = null;
    }
  }
  prev = { online: s.online, mode, anim };
}
async function pollStatus() {
  clearTimeout(statusTimer);
  if (!document.hidden) {
    try {
      const s = await api('/api/device/status');
      if (statusFails) log('Status check recovered.');
      statusFails = 0;
      lastStatus = s;
      trackTransitions(s);
      renderStatus(s);
    } catch (e) {
      statusFails++;
      if (statusFails === 1) log(`ESP32 status check failed: ${describe(e)} (will retry)`, 'warn');
      if (statusFails === 3) log('ESP32 status is still unavailable; retrying less often.', 'error');
      const pill = $('dot').parentElement;
      if (statusFails >= 3 || !lastStatus) {
        $('deviceStatus').textContent = 'Status unavailable';
        pill.className = 'status unknown';
        $('modeBtn').disabled = true;
      } else {
        $('deviceStatus').textContent += ' (retrying…)';
      }
    }
  }
  const delay = pendingMode ? POLL_FAST_MS : statusFails <= 1 ? POLL_MS : Math.min(60000, POLL_MS * 2 ** (statusFails - 1));
  statusTimer = setTimeout(pollStatus, delay);
}
document.addEventListener('visibilitychange', () => { if (!document.hidden) pollStatus(); });

/* ---------------------------------------------------------------- actions */
$('fileInput').onchange = async e => {
  const file = e.target.files[0];
  if (!file) return;
  msg('Converting .ino…');
  log(`Upload started: ${file.name} (${Math.max(1, Math.round(file.size / 1024))} KB)`);
  log(`Animation conversion started: ${file.name}`);
  const fd = new FormData();
  fd.append('file', file);
  try {
    const d = await api('/api/animations/upload', { method: 'POST', body: fd });
    msg(d.duplicate ? `Already exists: ${d.animation.name}` : `Imported ${d.animation.name} (${d.animation.frames} frames${d.aiFallback ? ' · AI fallback' : ''}).`);
    log(d.duplicate ? `Upload completed: ${d.animation.name} already exists (no changes)` : `Animation conversion completed: ${d.animation.name} (${d.animation.frames} frames${d.aiFallback ? ', AI fallback' : ''})`);
    if (!d.duplicate) log(`Upload completed: ${d.animation.name}`);
    await loadAnimations();
    await failures();
  } catch (err) {
    msg(err.message, true);
    log(err.stage === 'conversion' ? `Animation conversion failed: ${err.message}` : `Upload failed: ${describe(err)}`, 'error');
    await failures();
  } finally { e.target.value = ''; }
};
$('slideshowBtn').onclick = async () => {
  try { await api('/api/slideshow/start', { method: 'POST' }); msg('Slideshow started.'); log('Slideshow started (command sent)'); }
  catch (e) { fail('Could not start slideshow', e); }
};
$('stopBtn').onclick = async () => {
  try { await api('/api/animations/stop', { method: 'POST' }); msg('Stop command sent.'); log('Animation stopped (stop command sent; slideshow stopped too)'); }
  catch (e) { fail('Could not send stop', e); }
};
$('modeBtn').onclick = async () => {
  if (!lastStatus || !lastStatus.online || pendingMode) return;
  const current = lastStatus.mode === 'animation' ? 'animation' : 'normal';
  const target = current === 'animation' ? 'normal' : 'animation';
  try {
    await api('/api/device/mode', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ mode: target }) });
    pendingMode = { target, at: Date.now() };
    msg(`Mode change requested: ${MODE_LABEL[target]}`);
    log(`Mode change requested: ${MODE_LABEL[current]} → ${MODE_LABEL[target]}`);
    renderModeButton(lastStatus, current);
    pollStatus();
  } catch (e) { fail('Mode change failed', e); }
};
$('refreshBtn').onclick = async () => {
  try { await loadAnimations(); await failures(); await pollStatus(); msg('Refreshed.'); }
  catch (e) { fail('Refresh failed', e); }
};
$('clearLogsBtn').onclick = () => { logs = []; try { localStorage.removeItem(LOG_KEY); } catch {} renderLogs(); };

renderLogs();
log('Page loaded');
loadAnimations().catch(e => fail('Could not load animations', e));
failures();
pollStatus();
