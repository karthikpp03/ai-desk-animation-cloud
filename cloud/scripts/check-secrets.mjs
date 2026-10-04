// Verifies that .env is ignored and that no secret value from .env appears in a git-tracked file.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, SECRET_NAMES, loadEnv } from './lib/env.mjs';

const git = (...args) => execFileSync('git', args, { cwd: ROOT, encoding: 'utf8' });
let problems = 0;

for (const f of ['.env', 'esp32/animation_client/secrets.h', '.dev.vars']) {
  let ignored = true;
  try { git('check-ignore', '-q', f); } catch { ignored = false; }
  console.log(`${ignored ? 'ok  ' : 'FAIL'} ${f} is ${ignored ? 'git-ignored' : 'NOT ignored'}`);
  if (!ignored) problems++;
}
// tracked files plus new files that a `git add .` would pick up (anything not ignored)
const tracked = git('ls-files', '--cached', '--others', '--exclude-standard').split('\n').filter(Boolean);
for (const f of ['.env', 'esp32/animation_client/secrets.h', '.dev.vars']) if (tracked.includes(f)) { console.log(`FAIL ${f} is tracked by git`); problems++; }

// Any value stored in .env that is secret must not appear in tracked files (or in git history of them).
const env = loadEnv({ quiet: true });
const secrets = [...SECRET_NAMES].map(n => [n, env[n]]).filter(([, v]) => v && v.length >= 6);
const patterns = [[/ghp_[A-Za-z0-9]{30,}/, 'GitHub classic token'], [/github_pat_[A-Za-z0-9_]{30,}/, 'GitHub fine-grained token'], [/sk-[A-Za-z0-9_-]{30,}/, 'OpenAI-style key']];
for (const file of tracked) {
  const p = path.join(ROOT, file);
  if (!fs.existsSync(p) || fs.statSync(p).size > 2_000_000) continue;
  const text = fs.readFileSync(p, 'utf8');
  for (const [name, value] of secrets) if (text.includes(value)) { console.log(`FAIL ${file} contains the value of ${name}`); problems++; }
  for (const [re, label] of patterns) if (re.test(text)) { console.log(`FAIL ${file} looks like it contains a ${label}`); problems++; }
}
console.log(`Scanned ${tracked.length} tracked/uncommitted-but-not-ignored files for ${secrets.length} secret value(s) and token patterns.`);
console.log(problems ? `${problems} problem(s) found.` : 'No secrets found in tracked files.');
process.exit(problems ? 1 : 0);
