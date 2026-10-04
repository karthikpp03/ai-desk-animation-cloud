// Single place where local configuration is read. Every script in /scripts uses this loader,
// so each secret lives exactly once: in the git-ignored .env file at the project root.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const ENV_PATH = path.join(ROOT, '.env');

// Names whose values are secrets and must never be printed.
export const SECRET_NAMES = new Set(['ADMIN_TOKEN', 'DEVICE_TOKEN', 'GITHUB_TOKEN', 'OPENAI_API_KEY', 'WIFI_PASSWORD', 'HOME_WIFI_PASSWORD', 'HOTSPOT_WIFI_PASSWORD']);

export function parseEnv(text) {
  const out = {};
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#')) continue;
    const m = /^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/.exec(line);
    if (!m) continue;
    let value = m[2].trim();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    } else {
      value = value.replace(/\s+#.*$/, '');   // trailing comment on an unquoted value
    }
    out[m[1]] = value;
  }
  return out;
}

// Loads .env. Variables already set in the real environment (CI, shell) win over the file.
export function loadEnv({ required = [], quiet = false } = {}) {
  let fileVars = {};
  if (fs.existsSync(ENV_PATH)) fileVars = parseEnv(fs.readFileSync(ENV_PATH, 'utf8'));
  else if (!quiet && !required.length) console.warn('No .env file found (copy .env.example to .env).');
  const env = { ...fileVars };
  for (const key of Object.keys(fileVars)) if (process.env[key]) env[key] = process.env[key];
  const missing = required.filter(name => !env[name] || /^(replace|your|changeme)/i.test(env[name]));
  if (missing.length) {
    console.error(`Missing in .env: ${missing.join(', ')}`);
    console.error('Copy .env.example to .env and fill in the values.');
    process.exit(1);
  }
  return env;
}

export function mask(name, value) {
  return SECRET_NAMES.has(name) ? (value ? `<set, ${String(value).length} chars>` : '<empty>') : value;
}
