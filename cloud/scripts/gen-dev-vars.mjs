// .env  ->  .dev.vars   (git-ignored; read automatically by `wrangler dev`)
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, loadEnv } from './lib/env.mjs';

const NAMES = ['ADMIN_TOKEN', 'DEVICE_TOKEN', 'GITHUB_TOKEN', 'GITHUB_OWNER', 'GITHUB_REPO', 'GITHUB_BRANCH', 'OPENAI_API_KEY', 'OPENAI_MODEL'];
const env = loadEnv({ required: ['ADMIN_TOKEN', 'DEVICE_TOKEN', 'GITHUB_TOKEN', 'GITHUB_OWNER', 'GITHUB_REPO'] });
const lines = NAMES.filter(n => env[n]).map(n => `${n}=${JSON.stringify(env[n])}`);
fs.writeFileSync(path.join(ROOT, '.dev.vars'), lines.join('\n') + '\n', { mode: 0o600 });
console.log(`Wrote .dev.vars with: ${NAMES.filter(n => env[n]).join(', ')}`);
