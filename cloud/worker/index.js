let DurableObject;
try {
  ({ DurableObject } = await import('cloudflare:workers'));
} catch {
  // Local Node-based worker tests do not provide the Cloudflare runtime module.
  DurableObject = class {};
}
import { parseIno } from './parser/inoParser.js';
import { validateAnimation } from './validator/animation.js';
import { analyzeWithOpenAI } from './ai/analyze.js';
import { getFile, getJson, putFile, updateJson, deleteFile, animationPath, ANIMATIONS_INDEX, DEVICE_STATE } from './storage/github.js';

const MAX_UPLOAD = 2 * 1024 * 1024;
const MAX_CONVERTED = 8 * 1024 * 1024;
const MAX_FRAMES = 600;
const JSON_HEADERS = { 'Content-Type': 'application/json; charset=utf-8' };
const DEFAULT_STATE = { command: 'stop', animationId: null, slideshow: false, updatedAt: 0, currentAnimationId: null, deviceOnline: false, lastSeenAt: 0, mode: 'unknown', requestedMode: null, modeRequestId: 0, localIp: '', drawPad: { active: false, clients: 0, eventId: 0, event: '' } };
// Online/offline: the ESP32 sends a heartbeat every few seconds, but device/state.json lives in GitHub (one commit per
// write), so lastSeenAt is only persisted when it is older than HEARTBEAT_PERSIST_MS (or the mode/animation changed).
// ONLINE_WINDOW_MS must stay comfortably larger than HEARTBEAT_PERSIST_MS so one missed write never shows "offline".
const HEARTBEAT_PERSIST_MS = 40000;
const ONLINE_WINDOW_MS = 100000;
const DEVICE_MODES = ['normal', 'animation'];
const HEARTBEAT_MODES = ['normal', 'animation', 'draw_pad'];
const DRAW_PAD_SESSION_MS = 15 * 60 * 1000;

function json(data, status = 200, extra = {}) { return new Response(JSON.stringify(data), { status, headers: { ...JSON_HEADERS, ...extra } }); }
function cors(request, response) {
  const origin = request.headers.get('Origin');
  const headers = new Headers(response.headers);
  if (origin) headers.set('Access-Control-Allow-Origin', origin);
  headers.set('Access-Control-Allow-Headers', 'Content-Type, X-Admin-Token, X-Device-Token');
  headers.set('Access-Control-Allow-Methods', 'GET,POST,DELETE,OPTIONS');
  headers.set('Vary', 'Origin');
  return new Response(response.body, { status: response.status, headers });
}
async function requireAdmin(request, env) {
  if (!env.ADMIN_TOKEN) return false;
  return request.headers.get('X-Admin-Token') === env.ADMIN_TOKEN;
}
async function requireDevice(request, env) {
  if (!env.DEVICE_TOKEN) return false;
  return request.headers.get('X-Device-Token') === env.DEVICE_TOKEN;
}
function sanitizeSegment(value, fallback = 'animation') {
  return String(value || '').toLowerCase().replace(/\.ino$/i, '').replace(/[^a-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 48) || fallback;
}
async function sourceId(source) {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(source));
  return Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
}
function toUint8Array(value) {
  return value instanceof Uint8Array ? value : new Uint8Array(value);
}
async function getAnimations(env) {
  const data = await getJson(env, ANIMATIONS_INDEX, { version: 1, animations: [] });
  if (!data || !Array.isArray(data.animations)) throw new Error('animations.json is invalid.');
  return data.animations;
}
async function saveAnimations(env, animations, mode = 'replace') {
  let result = animations;
  await updateJson(env, ANIMATIONS_INDEX, current => {
    const currentList = Array.isArray(current.animations) ? current.animations : [];
    const nextList = mode === 'append'
      ? [...currentList.filter(x => x.id !== animations[0]?.id), ...animations]
      : animations;
    result = [...nextList].sort((a, b) => String(a.name).localeCompare(String(b.name)));
    return { version: 1, animations: result };
  }, 'Update animation index');
  return result;
}
async function listFailures(env) {
  // Failures are intentionally kept out of animations.json; playable animations remain the source of truth.
  const index = await getJson(env, 'failed.json', { failures: [] });
  return Array.isArray(index.failures) ? index.failures.slice(0, 50) : [];
}
async function recordFailureIndex(env, filename, reason) {
  await updateJson(env, 'failed.json', current => {
    const failures = Array.isArray(current.failures) ? current.failures : [];
    failures.unshift({ id: `${Date.now()}-${crypto.randomUUID().slice(0, 8)}`, filename, reason, createdAt: new Date().toISOString() });
    return { version: 1, failures: failures.slice(0, 50) };
  }, 'Update failed import history');
}

async function importAnimation(request, env) {
  const contentLength = Number(request.headers.get('Content-Length') || 0);
  if (contentLength > MAX_UPLOAD + 100_000) return json({ error: 'File too large. Maximum is 2 MB.' }, 413);

  const form = await request.formData().catch(() => null);
  if (!form) return json({ error: 'Upload must be multipart form data containing a .ino file.' }, 400);
  const file = form.get('file');
  if (!(file instanceof File)) return json({ error: 'Missing .ino file.' }, 400);
  if (!file.name.toLowerCase().endsWith('.ino')) return json({ error: 'Only .ino files are supported.' }, 400);
  if (file.size === 0) return json({ error: 'The .ino file is empty.' }, 400);
  if (file.size > MAX_UPLOAD) return json({ error: 'File too large. Maximum is 2 MB.' }, 413);

  const source = await file.text();
  const digest = await sourceId(source);
  const animations = await getAnimations(env);
  const duplicate = animations.find(a => a.sourceSha256 === digest);
  if (duplicate) return json({ success: true, duplicate: true, animation: duplicate, parser: duplicate.parser || 'existing' }, 200);

  let parsed;
  let ai = null;
  let deterministicError = null;
  try {
    parsed = parseIno(source, file.name);
  } catch (error) {
    deterministicError = error;
    try {
      ai = await analyzeWithOpenAI(source, env);
      if (!ai.convertible) throw new Error(ai.reason || 'OpenAI could not reliably identify the animation.');
      if (ai.width !== 128 || ai.height !== 64 || !Array.isArray(ai.frame_arrays) || ai.frame_arrays.length < 1 || ai.frame_arrays.length > MAX_FRAMES) {
        throw new Error('OpenAI did not identify a valid 128x64 frame-array set.');
      }
      parsed = parseIno(source, file.name, ai);
      // Timing the parser read directly from the source always wins; the AI value is only a fallback for the default.
      if (String(parsed.metadata.timingSource || '').startsWith('default')) {
        parsed.metadata.fps = ai.fps > 0 && ai.fps <= 60 ? ai.fps : parsed.metadata.fps;
        parsed.metadata.frameDurationMs = ai.frame_duration_ms > 0 && ai.frame_duration_ms <= 60000 ? ai.frame_duration_ms : parsed.metadata.frameDurationMs;
      }
      parsed.metadata.loop = Boolean(ai.loop);
      parsed.parser = 'deterministic-after-ai-hint';
    } catch (error) {
      const reason = error?.message || deterministicError?.message || 'Could not reliably convert animation.';
      await recordFailureIndex(env, file.name, reason).catch(() => {});
      return json({ error: `Import failed: ${reason}`, stage: 'conversion', filename: file.name }, 422);
    }
  }

  parsed.frames = toUint8Array(parsed.frames);
  if (parsed.frames.byteLength > MAX_CONVERTED) return json({ error: 'Converted animation exceeds the configured 8 MB limit.', stage: 'validation', filename: file.name }, 413);
  const id = `${sanitizeSegment(parsed.metadata.name)}-${digest.slice(0, 12)}`;
  parsed.metadata.id = id;
  parsed.metadata.sourceSha256 = digest;
  parsed.metadata.importedAt = new Date().toISOString();
  parsed.metadata.parser = parsed.parser;
  validateAnimation(parsed.metadata, parsed.frames);

  // Write animation files first. The index is updated last so the browser never sees a half-created animation.
  await putFile(env, animationPath(id, 'metadata.json'), JSON.stringify(parsed.metadata, null, 2) + '\n', `Add animation metadata: ${parsed.metadata.name}`);
  await putFile(env, animationPath(id, 'frames.bin'), parsed.frames, `Add animation frames: ${parsed.metadata.name}`, { binary: true });
  if (env.STORE_ORIGINAL_INO === 'true') {
    await putFile(env, animationPath(id, 'original.ino'), source, `Store original source: ${parsed.metadata.name}`);
  }

  const entry = {
    id,
    name: parsed.metadata.name,
    originalFilename: parsed.metadata.originalFilename,
    width: parsed.metadata.width,
    height: parsed.metadata.height,
    frames: parsed.metadata.frames,
    fps: parsed.metadata.fps,
    frameDurationMs: parsed.metadata.frameDurationMs,
    loop: parsed.metadata.loop,
    format: parsed.metadata.format,
    sourceSha256: digest,
    parser: parsed.parser
  };
  try {
    await saveAnimations(env, [entry], 'append');
  } catch (error) {
    return json({ error: `Animation files were stored, but animations.json could not be updated: ${error.message}`, stage: 'index-update', animation: entry }, 502);
  }
  return json({ success: true, duplicate: false, animation: entry, parser: parsed.parser, aiFallback: Boolean(ai) }, 201);
}

async function readState(env) { return getJson(env, DEVICE_STATE, DEFAULT_STATE); }

async function deviceCommand(request, env) {
  if (!(await requireDevice(request, env))) return json({ error: 'Unauthorized' }, 401);
  return json(await readState(env), 200, { 'Cache-Control': 'no-store' });
}
// Shared by /api/device/ack (animation mode) and /api/device/heartbeat (any mode).
async function recordSeen(env, { mode, animationId = null, localIp = '', drawPad = null }) {
  const before = await readState(env);
  const now = Date.now();
  const requestedAnimation = typeof animationId === 'string' && animationId ? animationId : before.currentAnimationId || null;
  const normalizedIp = typeof localIp === 'string' && localIp ? localIp.slice(0, 64) : (before.localIp || '');
  const normalizedDrawPad = drawPad && typeof drawPad === 'object' ? {
    active: !!drawPad.active,
    clients: Math.max(0, Math.min(255, Number(drawPad.clients || 0))),
    eventId: Math.max(0, Number(drawPad.eventId || 0)),
    event: typeof drawPad.event === 'string' ? drawPad.event.slice(0, 32) : ''
  } : (before.drawPad || DEFAULT_STATE.drawPad);
  const unchanged = before.currentAnimationId === requestedAnimation && (before.mode || 'unknown') === mode &&
    (before.localIp || '') === normalizedIp && JSON.stringify(before.drawPad || DEFAULT_STATE.drawPad) === JSON.stringify(normalizedDrawPad);
  if (unchanged && (now - Number(before.lastSeenAt || 0)) < HEARTBEAT_PERSIST_MS) {
    return { persisted: false, state: before };
  }
  await updateJson(env, DEVICE_STATE, current => ({ ...DEFAULT_STATE, ...current, currentAnimationId: requestedAnimation, mode, localIp: normalizedIp, drawPad: normalizedDrawPad, deviceOnline: true, lastSeenAt: now }), 'Update device heartbeat');
  return { persisted: true, state: before };
}
async function deviceAck(request, env) {
  if (!(await requireDevice(request, env))) return json({ error: 'Unauthorized' }, 401);
  const body = await request.json().catch(() => ({}));
  const { persisted } = await recordSeen(env, { mode: 'animation', animationId: body.animationId });
  return json({ success: true, persisted });
}
async function deviceHeartbeat(request, env) {
  if (!(await requireDevice(request, env))) return json({ error: 'Unauthorized' }, 401);
  const body = await request.json().catch(() => ({}));
  if (!HEARTBEAT_MODES.includes(body.mode)) return json({ error: 'invalid heartbeat mode.' }, 400);
  const { persisted, state } = await recordSeen(env, { mode: body.mode, animationId: body.animationId, localIp: body.localIp, drawPad: body.drawPad });
  return json({ success: true, persisted, requestedMode: state.requestedMode || null, modeRequestId: Number(state.modeRequestId || 0) }, 200, { 'Cache-Control': 'no-store' });
}
// Website "Mode Change" button: records a one-shot request that the ESP32 picks up on its next heartbeat/command poll.
async function requestMode(request, env) {
  const body = await request.json().catch(() => ({}));
  if (!DEVICE_MODES.includes(body.mode)) return json({ error: 'mode must be "normal" or "animation".' }, 400);
  const state = await readState(env);
  if (Date.now() - Number(state.lastSeenAt || 0) >= ONLINE_WINDOW_MS) return json({ error: 'ESP32 is offline, so the mode cannot be changed right now.' }, 409);
  const requestId = Date.now();
  await updateJson(env, DEVICE_STATE, current => ({ ...DEFAULT_STATE, ...current, requestedMode: body.mode, modeRequestId: requestId }), 'Request device mode change');
  return json({ success: true, mode: body.mode, requestId });
}

async function commandFromAdmin(request, env, command, animationId = null, slideshow = false) {
  await updateJson(env, DEVICE_STATE, current => ({ ...DEFAULT_STATE, ...current, command, animationId, slideshow, updatedAt: Date.now() }), 'Update device command state');
  return json({ success: true });
}


function drawPadStub(env) {
  return env.DRAW_PAD_RELAY.getByName('main');
}

export class DrawPadRelay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
  }

  sockets(role) {
    return this.ctx.getWebSockets().filter(ws => ws.deserializeAttachment()?.role === role);
  }

  sendToRole(role, message) {
    for (const ws of this.sockets(role)) {
      if (ws.readyState === WebSocket.OPEN) {
        try { ws.send(message); } catch {}
      }
    }
  }

  async fetch(request) {
    const url = new URL(request.url);

    if (request.method === 'POST' && url.pathname === '/api/drawpad/session') {
      const token = `${crypto.randomUUID()}-${crypto.randomUUID()}`;
      const expiresAt = Date.now() + DRAW_PAD_SESSION_MS;
      await this.ctx.storage.put('session', { token, expiresAt });
      // A new session is exclusive: release any older browser session.
      this.sendToRole('browser', JSON.stringify({ type: 'session-replaced' }));
      for (const ws of this.sockets('browser')) {
        try { ws.close(1000, 'Replaced by a new Draw Pad session'); } catch {}
      }
      return json({ success: true, session: token, expiresAt });
    }

    if (request.headers.get('Upgrade')?.toLowerCase() !== 'websocket') {
      return json({ error: 'WebSocket upgrade required.' }, 426);
    }

    let role;
    if (url.pathname === '/api/drawpad/device') {
      role = 'device';
    } else if (url.pathname === '/api/drawpad/browser') {
      role = 'browser';
      const token = url.searchParams.get('session') || '';
      const session = await this.ctx.storage.get('session');
      if (!session || session.token !== token || Date.now() >= Number(session.expiresAt || 0)) {
        return json({ error: 'Draw Pad session expired. Open Draw Pad again.' }, 401);
      }
      await this.ctx.storage.put('browserLastSeenAt', Date.now());
    } else {
      return json({ error: 'Not found' }, 404);
    }

    // Only one device connection and one browser session are allowed.
    for (const ws of this.sockets(role)) {
      try { ws.close(1000, 'Replaced by a new connection'); } catch {}
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ role, connectedAt: Date.now() });

    if (role === 'browser') {
      await this.ctx.storage.setAlarm(Date.now() + 30000);
      this.sendToRole('device', JSON.stringify({ type: 'open' }));
    } else {
      // A fresh ESP32 connection is treated as a clean boot/recovery state.
      // Do not automatically re-enter DRAW_PAD just because an old browser
      // tab is still open after the device rebooted.
      this.sendToRole('browser', JSON.stringify({ type: 'device-online' }));
    }

    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, message) {
    const state = ws.deserializeAttachment() || {};
    if (state.role === 'browser') {
      const text = typeof message === 'string' ? message : new TextDecoder().decode(message);
      if (text === 'KEEPALIVE' || text === '{"type":"keepalive"}') {
        await this.ctx.storage.put('browserLastSeenAt', Date.now());
        await this.ctx.storage.setAlarm(Date.now() + 30000);
        if (ws.readyState === WebSocket.OPEN) ws.send('KEEPALIVE_ACK');
        return;
      }
      this.sendToRole('device', message);
    } else if (state.role === 'device') {
      this.sendToRole('browser', message);
    }
  }

  async webSocketClose(ws) {
    const state = ws.deserializeAttachment() || {};
    if (state.role === 'browser') {
      this.sendToRole('device', JSON.stringify({ type: 'release' }));
    } else if (state.role === 'device') {
      this.sendToRole('browser', JSON.stringify({ type: 'device-offline' }));
    }
  }

  async webSocketError(ws) {
    const state = ws.deserializeAttachment() || {};
    if (state.role === 'browser') {
      this.sendToRole('device', JSON.stringify({ type: 'release' }));
    } else if (state.role === 'device') {
      this.sendToRole('browser', JSON.stringify({ type: 'device-offline' }));
    }
  }

  async alarm() {
    const browsers = this.sockets('browser');
    if (!browsers.length) return;
    const session = await this.ctx.storage.get('session');
    const lastSeen = Number(await this.ctx.storage.get('browserLastSeenAt') || 0);
    if (!session || Date.now() >= Number(session.expiresAt || 0) || !lastSeen || Date.now() - lastSeen >= 30000) {
      for (const ws of browsers) {
        try { ws.close(1000, 'Draw Pad session timed out'); } catch {}
      }
      this.sendToRole('device', JSON.stringify({ type: 'release' }));
      return;
    }
    await this.ctx.storage.setAlarm(Date.now() + 30000);
  }
}

export default {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') return cors(request, new Response(null, { status: 204 }));
    const url = new URL(request.url);
    try {
      if (url.pathname === '/api/animations' && request.method === 'GET') return cors(request, json({ animations: await getAnimations(env) }));
      if (url.pathname === '/api/import-failures' && request.method === 'GET') return cors(request, json({ failures: await listFailures(env) }));
      if (url.pathname === '/api/animations/upload' && request.method === 'POST') return cors(request, await importAnimation(request, env));

      // Must be matched before the /api/animations/:id pattern below, which would treat "stop" as an animation id.
      if (url.pathname === '/api/animations/stop' && request.method === 'POST') return cors(request, await commandFromAdmin(request, env, 'stop', null, false));

      const match = url.pathname.match(/^\/api\/animations\/([^/]+)(?:\/(metadata|frames|play|preview))?$/);
      if (match) {
        const id = decodeURIComponent(match[1]);
        const action = match[2] || 'detail';
        const animations = await getAnimations(env);
        const animation = animations.find(x => x.id === id);
        if (!animation) return cors(request, json({ error: 'Animation not found.' }, 404));

        if (action === 'play' && request.method === 'POST') return cors(request, await commandFromAdmin(request, env, 'play', id, false));
        if (action === 'metadata' && request.method === 'GET') {
          const file = await getFile(env, animationPath(id, 'metadata.json'));
          if (!file) return cors(request, json({ error: 'Not found' }, 404));
          const bytes = Uint8Array.from(atob(file.content), c => c.charCodeAt(0));
          return cors(request, new Response(bytes, { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=60' } }));
        }
        // "preview" serves the same normalized frames.bin the ESP32 plays, but to the website (no device token) so it can
        // draw thumbnails. The animation library is already publicly listable, so this exposes nothing new.
        if ((action === 'frames' || action === 'preview') && request.method === 'GET') {
          if (action === 'frames' && !(await requireDevice(request, env))) return cors(request, json({ error: 'Unauthorized' }, 401));
          const raw = await getFile(env, animationPath(id, 'frames.bin'), { raw: true });
          if (!raw) return cors(request, json({ error: 'Not found' }, 404));
          return cors(request, new Response(raw.body, { headers: { 'Content-Type': 'application/octet-stream', 'Cache-Control': 'public, max-age=31536000, immutable', 'Accept-Ranges': 'bytes' } }));
        }
        if (request.method === 'DELETE') {
          if (!(await requireAdmin(request, env))) return cors(request, json({ error: 'Unauthorized' }, 401));
          for (const file of ['metadata.json', 'frames.bin', 'original.ino']) {
            await deleteFile(env, animationPath(id, file), `Delete animation: ${animation.name}`);
          }
          await updateJson(env, ANIMATIONS_INDEX, current => ({ version: 1, animations: (Array.isArray(current.animations) ? current.animations : []).filter(x => x.id !== id).sort((a, b) => String(a.name).localeCompare(String(b.name))) }), 'Update animation index');
          return cors(request, json({ success: true }));
        }
        if (request.method === 'GET') return cors(request, json(animation));
      }

      if (url.pathname === '/api/device/play' && request.method === 'POST') {
        const body = await request.json().catch(() => ({}));
        if (typeof body.animationId !== 'string' || !body.animationId) return cors(request, json({ error: 'animationId is required.' }, 400));
        const animations = await getAnimations(env);
        if (!animations.some(a => a.id === body.animationId)) return cors(request, json({ error: 'Animation not found.' }, 404));
        return cors(request, await commandFromAdmin(request, env, 'play', body.animationId, false));
      }
      if (url.pathname === '/api/slideshow/start' && request.method === 'POST') return cors(request, await commandFromAdmin(request, env, 'slideshow', null, true));
      if (url.pathname === '/api/slideshow/stop' && request.method === 'POST') return cors(request, await commandFromAdmin(request, env, 'stop', null, false));
      if (url.pathname === '/api/device/command' && request.method === 'GET') return cors(request, await deviceCommand(request, env));
      // Global Draw Pad relay. The browser receives a short-lived session token;
      // the ESP32 authenticates its outbound WebSocket with the existing DEVICE_TOKEN.
      if (url.pathname === '/api/drawpad/session' && request.method === 'POST') {
        return cors(request, await drawPadStub(env).fetch(new Request(request.url, { method: 'POST' })));
      }
      if ((url.pathname === '/api/drawpad/browser' || url.pathname === '/api/drawpad/device') &&
          request.headers.get('Upgrade')?.toLowerCase() === 'websocket') {
        if (url.pathname.endsWith('/device') && !(await requireDevice(request, env))) {
          return json({ error: 'Unauthorized' }, 401);
        }
        // Do not wrap the 101 WebSocket response in a normal Response/CORS
        // helper; the upgrade response must preserve its webSocket endpoint.
        return env.DRAW_PAD_RELAY.getByName('main').fetch(request);
      }

      if (url.pathname === '/api/device/status' && request.method === 'GET') {
        const state = await readState(env);
        const online = Date.now() - Number(state.lastSeenAt || 0) < ONLINE_WINDOW_MS;
        return cors(request, json({ online, lastSeenAt: state.lastSeenAt || 0, serverTime: Date.now(), mode: online ? (state.mode || 'unknown') : 'unknown', localIp: online ? (state.localIp || '') : '', drawPad: online ? (state.drawPad || DEFAULT_STATE.drawPad) : DEFAULT_STATE.drawPad, requestedMode: state.requestedMode || null, modeRequestId: Number(state.modeRequestId || 0), command: state.command, animationId: state.animationId, currentAnimationId: state.currentAnimationId, slideshow: !!state.slideshow }, 200, { 'Cache-Control': 'no-store' }));
      }
      if (url.pathname === '/api/device/ack' && request.method === 'POST') return cors(request, await deviceAck(request, env));
      if (url.pathname === '/api/device/heartbeat' && request.method === 'POST') return cors(request, await deviceHeartbeat(request, env));
      if (url.pathname === '/api/device/mode' && request.method === 'POST') return cors(request, await requestMode(request, env));

      return cors(request, json({ error: 'Not found' }, 404));
    } catch (error) {
      console.error(error);
      return cors(request, json({ error: error?.message || 'Internal server error' }, 500));
    }
  }
};
