// Live checks against the DEPLOYED Worker, using tokens from .env. Never prints a token.
//   node scripts/api-test.mjs auth                 verify 401 without/with wrong tokens and 200 with the right ones
//   node scripts/api-test.mjs list | status
//   node scripts/api-test.mjs upload <file.ino>
//   node scripts/api-test.mjs play <animationId>
//   node scripts/api-test.mjs stop
//   node scripts/api-test.mjs command              what the ESP32 would receive (uses DEVICE_TOKEN)
import fs from 'node:fs';
import path from 'node:path';
import { loadEnv } from './lib/env.mjs';

const env = loadEnv({ required: ['API_BASE', 'ADMIN_TOKEN', 'DEVICE_TOKEN'] });
const base = env.API_BASE.replace(/\/$/, '');
const admin = { 'X-Admin-Token': env.ADMIN_TOKEN };
const device = { 'X-Device-Token': env.DEVICE_TOKEN };
const req = async (method, route, headers = {}, body) => {
  const res = await fetch(base + route, { method, headers, body });
  const text = await res.text();
  let data; try { data = JSON.parse(text); } catch { data = text.length > 200 ? `<${text.length} bytes>` : text; }
  return { status: res.status, data };
};
const show = r => console.log(r.status, typeof r.data === 'string' ? r.data : JSON.stringify(r.data, null, 2));

const [cmd, arg] = process.argv.slice(2);
if (cmd === 'list') show(await req('GET', '/api/animations'));
else if (cmd === 'status') show(await req('GET', '/api/device/status'));
else if (cmd === 'command') show(await req('GET', '/api/device/command', device));
else if (cmd === 'play') show(await req('POST', `/api/animations/${encodeURIComponent(arg)}/play`, admin));
else if (cmd === 'stop') show(await req('POST', '/api/animations/stop', admin));
else if (cmd === 'upload') {
  const fd = new FormData();
  fd.append('file', new File([fs.readFileSync(arg)], path.basename(arg)));
  show(await req('POST', '/api/animations/upload', admin, fd));
} else if (cmd === 'auth') {
  const list = (await req('GET', '/api/animations')).data.animations || [];
  const id = list[0]?.id || 'none';
  const rows = [
    ['device/command, no token', await req('GET', '/api/device/command'), 401],
    ['device/command, wrong token', await req('GET', '/api/device/command', { 'X-Device-Token': 'wrong' }), 401],
    ['device/command, ADMIN token (must not work)', await req('GET', '/api/device/command', { 'X-Device-Token': env.ADMIN_TOKEN }), 401],
    ['device/command, DEVICE token', await req('GET', '/api/device/command', device), 200],
    ['stop, no token', await req('POST', '/api/animations/stop'), 401],
    ['stop, DEVICE token (must not work)', await req('POST', '/api/animations/stop', { 'X-Admin-Token': env.DEVICE_TOKEN }), 401],
    ['stop, ADMIN token', await req('POST', '/api/animations/stop', admin), 200],
    ['frames, no token', await req('GET', `/api/animations/${id}/frames`), 401],
    ['frames, DEVICE token', await req('GET', `/api/animations/${id}/frames`, device), 200]
  ];
  let bad = 0;
  for (const [label, r, want] of rows) { const ok = r.status === want; if (!ok) bad++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}: HTTP ${r.status} (expected ${want})`); }
  process.exit(bad ? 1 : 0);
} else { console.log('usage: node scripts/api-test.mjs auth|list|status|command|upload <file>|play <id>|stop'); process.exit(2); }
