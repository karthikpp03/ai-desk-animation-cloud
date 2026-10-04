// npm run test:worker  -  runs the real Worker code in-process against an in-memory fake of the GitHub
// Contents API. Uses throw-away random tokens, so it needs no secrets and touches no real service.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { ROOT } from './lib/env.mjs';
import worker from '../worker/index.js';

const rnd = () => crypto.randomBytes(16).toString('hex');
const env = { ADMIN_TOKEN: rnd(), DEVICE_TOKEN: rnd(), GITHUB_TOKEN: 'ghp_' + rnd(), OPENAI_API_KEY: 'sk-' + rnd(), GITHUB_OWNER: 'o', GITHUB_REPO: 'r', GITHUB_BRANCH: 'main' };

// ---- fake GitHub ---------------------------------------------------------------------------------
const files = new Map();
const realFetch = globalThis.fetch;
globalThis.fetch = async (url, init = {}) => {
  const u = new URL(url);
  assert.equal(u.host, 'api.github.com', `unexpected outbound request to ${u.host}`);
  const p = decodeURIComponent(u.pathname.replace(/^\/repos\/o\/r\/contents\//, ''));
  const method = init.method || 'GET';
  if (method === 'GET') {
    const f = files.get(p);
    if (!f) return new Response('{"message":"Not Found"}', { status: 404 });
    if (String(init.headers?.Accept).includes('raw')) return new Response(f.bytes, { headers: { etag: f.sha } });
    return new Response(JSON.stringify({ sha: f.sha, content: f.bytes.toString('base64'), encoding: 'base64' }));
  }
  if (method === 'PUT') {
    const body = JSON.parse(init.body);
    const cur = files.get(p);
    if (cur && body.sha !== cur.sha) return new Response('{"message":"conflict"}', { status: 409 });
    files.set(p, { bytes: Buffer.from(body.content, 'base64'), sha: rnd() });
    return new Response('{}');
  }
  if (method === 'DELETE') { files.delete(p); return new Response('{}'); }
  return new Response('{}', { status: 400 });
};

const call = (method, route, { headers = {}, body } = {}) => worker.fetch(new Request('https://w.test' + route, { method, headers, body }), env);
const admin = { 'X-Admin-Token': env.ADMIN_TOKEN };
const device = { 'X-Device-Token': env.DEVICE_TOKEN };
let failures = 0;
const check = async (label, fn) => { try { await fn(); console.log(`ok   ${label}`); } catch (e) { failures++; console.log(`FAIL ${label}\n       ${e.message.split('\n')[0]}`); } };

const upload = async (file, token = admin) => {
  const fd = new FormData();
  fd.append('file', new File([fs.readFileSync(file)], path.basename(file)));
  return call('POST', '/api/animations/upload', { headers: token, body: fd });
};
const candidates = [['sample-eye.ino'], ['animations/oled_display.ino', 'test-fixtures/oled_display.ino']];
const sources = candidates.map(c => c.map(f => path.join(ROOT, f)).find(p => fs.existsSync(p))).filter(Boolean);
const ids = {};

await check('upload without admin token -> 401', async () => assert.equal((await upload(sources[0], {})).status, 401));
await check('upload with wrong admin token -> 401', async () => assert.equal((await upload(sources[0], { 'X-Admin-Token': 'nope' })).status, 401));
for (const src of sources) {
  await check(`upload ${path.basename(src)} with admin token -> 201`, async () => {
    const res = await upload(src); const j = await res.json();
    assert.equal(res.status, 201, JSON.stringify(j)); ids[path.basename(src)] = j.animation;
  });
}
await check('re-upload is detected as duplicate', async () => assert.equal((await (await upload(sources[0])).json()).duplicate, true));
if (ids['oled_display.ino']) await check('oled_display metadata: 106 frames, 67 ms, ~15 FPS', async () => {
  const m = await (await call('GET', `/api/animations/${ids['oled_display.ino'].id}/metadata`)).json();
  assert.equal(m.frames, 106); assert.equal(m.frameDurationMs, 67); assert.ok(Math.abs(m.fps - 14.925) < 0.01); assert.equal(m.timingSource, 'millis() interval');
});
const first = Object.values(ids)[0];
await check('frames without device token -> 401', async () => assert.equal((await call('GET', `/api/animations/${first.id}/frames`)).status, 401));
await check('frames with ADMIN token (not device) -> 401', async () => assert.equal((await call('GET', `/api/animations/${first.id}/frames`, { headers: admin })).status, 401));
await check('frames with device token -> frames*1024 bytes', async () => {
  const res = await call('GET', `/api/animations/${first.id}/frames`, { headers: device });
  assert.equal(res.status, 200); assert.equal((await res.arrayBuffer()).byteLength, first.frames * 1024);
});
await check('play without admin token -> 401', async () => assert.equal((await call('POST', `/api/animations/${first.id}/play`)).status, 401));
await check('play with admin token -> 200, device sees play command', async () => {
  assert.equal((await call('POST', `/api/animations/${first.id}/play`, { headers: admin })).status, 200);
  const cmd = await (await call('GET', '/api/device/command', { headers: device })).json();
  assert.equal(cmd.command, 'play'); assert.equal(cmd.animationId, first.id);
});
await check('device command without/with wrong device token -> 401', async () => {
  assert.equal((await call('GET', '/api/device/command')).status, 401);
  assert.equal((await call('GET', '/api/device/command', { headers: { 'X-Device-Token': 'nope' } })).status, 401);
});
await check('stop without admin token -> 401; with admin -> device sees stop', async () => {
  assert.equal((await call('POST', '/api/animations/stop')).status, 401);
  assert.equal((await call('POST', '/api/animations/stop', { headers: admin })).status, 200);
  assert.equal((await (await call('GET', '/api/device/command', { headers: device })).json()).command, 'stop');
});
await check('no response body or header leaks any configured secret', async () => {
  const routes = [['GET', '/api/animations'], ['GET', '/api/device/status'], ['GET', '/api/import-failures'], ['GET', `/api/animations/${first.id}`], ['GET', `/api/animations/${first.id}/metadata`], ['GET', '/api/device/command'], ['POST', '/api/animations/upload'], ['GET', '/nope']];
  for (const [m, r] of routes) {
    const res = await call(m, r); const dump = JSON.stringify([...res.headers]) + await res.text();
    for (const [name, secret] of Object.entries(env)) if (name.endsWith('TOKEN') || name.endsWith('KEY')) assert.ok(!dump.includes(secret), `${r} leaked ${name}`);
  }
});

globalThis.fetch = realFetch;
console.log(failures ? `\n${failures} check(s) FAILED` : '\nAll Worker checks passed');
process.exit(failures ? 1 : 0);
