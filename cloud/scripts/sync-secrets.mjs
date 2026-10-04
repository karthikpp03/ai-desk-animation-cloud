// .env  ->  Cloudflare Worker secrets (via `wrangler secret put`, value sent on stdin).
// Usage:  npm run sync:secrets            upload everything that is set in .env
//         npm run sync:secrets -- --dry-run   only list the names that would be uploaded
// Values are never printed or placed on a command line.
import { spawnSync } from 'node:child_process';
import { ROOT, loadEnv } from './lib/env.mjs';

const NAMES = ['ADMIN_TOKEN', 'DEVICE_TOKEN', 'GITHUB_TOKEN', 'OPENAI_API_KEY', 'GITHUB_OWNER', 'GITHUB_REPO', 'GITHUB_BRANCH'];
const dryRun = process.argv.includes('--dry-run');
const env = loadEnv({ required: ['ADMIN_TOKEN', 'DEVICE_TOKEN', 'GITHUB_TOKEN', 'GITHUB_OWNER', 'GITHUB_REPO'] });

const todo = NAMES.filter(n => env[n]);
const skipped = NAMES.filter(n => !env[n]);
if (skipped.length) console.log(`Skipping (empty in .env): ${skipped.join(', ')}`);

let failed = 0;
for (const name of todo) {
  if (dryRun) { console.log(`[dry-run] would upload ${name}`); continue; }
  const r = spawnSync('npx', ['wrangler', 'secret', 'put', name], {
    cwd: ROOT, input: env[name], encoding: 'utf8', shell: process.platform === 'win32'
  });
  if (r.status === 0) console.log(`uploaded ${name}`);
  else { failed++; console.error(`FAILED  ${name}: ${String(r.stderr || r.stdout || '').split('\n').filter(Boolean).slice(-2).join(' ').replaceAll(env[name], '***')}`); }
}
process.exit(failed ? 1 : 0);
