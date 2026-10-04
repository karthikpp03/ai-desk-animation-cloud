const API = 'https://api.github.com';
const API_VERSION = '2022-11-28';

function config(env) {
  const owner = String(env.GITHUB_OWNER || '').trim();
  const repo = String(env.GITHUB_REPO || '').trim();
  const branch = String(env.GITHUB_BRANCH || 'main').trim();
  if (!env.GITHUB_TOKEN || !owner || !repo || !branch) {
    throw new Error('GitHub storage is not configured. Set GITHUB_TOKEN, GITHUB_OWNER, GITHUB_REPO and GITHUB_BRANCH.');
  }
  return { owner, repo, branch };
}

function pathUrl(owner, repo, path) {
  return `${API}/repos/${encodeURIComponent(owner)}/${encodeURIComponent(repo)}/contents/${path.split('/').map(encodeURIComponent).join('/')}`;
}

function headers(env, accept = 'application/vnd.github+json') {
  return {
    Accept: accept,
    Authorization: `Bearer ${env.GITHUB_TOKEN}`,
    'X-GitHub-Api-Version': API_VERSION,
    'User-Agent': 'ai-desk-animation-cloud'
  };
}

async function githubResponse(response, fallback) {
  if (response.ok) return response;
  let detail = '';
  try {
    const data = await response.json();
    detail = data.message ? `: ${data.message}` : '';
  } catch {}
  throw new Error(`${fallback} (${response.status})${detail}`);
}

export async function getFile(env, path, { raw = false } = {}) {
  const { owner, repo, branch } = config(env);
  const url = `${pathUrl(owner, repo, path)}?ref=${encodeURIComponent(branch)}`;
  const response = await fetch(url, { headers: headers(env, raw ? 'application/vnd.github.raw+json' : 'application/vnd.github+json') });
  if (response.status === 404) return null;
  await githubResponse(response, `GitHub read failed for ${path}`);
  if (raw) return { body: response.body, contentType: response.headers.get('content-type') || 'application/octet-stream', sha: response.headers.get('etag') || '' };
  const data = await response.json();
  return {
    sha: data.sha,
    content: String(data.content || '').replace(/\s+/g, ''),
    encoding: data.encoding || 'base64',
    downloadUrl: data.download_url || null
  };
}

function bytesToBase64(bytes) {
  let out = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    out += String.fromCharCode(...bytes.subarray(i, Math.min(i + chunk, bytes.length)));
  }
  return btoa(out);
}

function textToBase64(text) {
  return bytesToBase64(new TextEncoder().encode(text));
}

export async function putFile(env, path, content, message, { sha = null, binary = false } = {}) {
  const { owner, repo, branch } = config(env);
  const payload = {
    message,
    content: binary ? bytesToBase64(content) : textToBase64(String(content)),
    branch
  };
  if (sha) payload.sha = sha;

  const response = await fetch(pathUrl(owner, repo, path), {
    method: 'PUT',
    headers: { ...headers(env), 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  await githubResponse(response, `GitHub write failed for ${path}`);
  return response.json();
}

export async function getJson(env, path, fallback) {
  const file = await getFile(env, path);
  if (!file) return fallback;
  try {
    const bytes = Uint8Array.from(atob(file.content), c => c.charCodeAt(0));
    return JSON.parse(new TextDecoder().decode(bytes));
  } catch {
    throw new Error(`GitHub JSON file is invalid: ${path}`);
  }
}

export async function putJson(env, path, value, message, retries = 3) {
  const content = JSON.stringify(value, null, 2) + '\n';
  for (let attempt = 0; attempt < retries; attempt++) {
    const current = await getFile(env, path);
    try {
      return await putFile(env, path, content, message, { sha: current?.sha || null });
    } catch (error) {
      if (!String(error.message).includes('(409)') || attempt === retries - 1) throw error;
    }
  }
  throw new Error(`Could not update ${path}`);
}


export async function updateJson(env, path, updater, message, retries = 3) {
  for (let attempt = 0; attempt < retries; attempt++) {
    const current = await getFile(env, path);
    let value;
    if (current) {
      const bytes = Uint8Array.from(atob(current.content), c => c.charCodeAt(0));
      value = JSON.parse(new TextDecoder().decode(bytes));
    } else {
      value = {};
    }
    const next = await updater(value);
    const content = JSON.stringify(next, null, 2) + '\n';
    try {
      return await putFile(env, path, content, message, { sha: current?.sha || null });
    } catch (error) {
      if (!String(error.message).includes('(409)') || attempt === retries - 1) throw error;
    }
  }
  throw new Error(`Could not update ${path}`);
}

export async function deleteFile(env, path, message) {
  const current = await getFile(env, path);
  if (!current) return false;
  const { owner, repo, branch } = config(env);
  const response = await fetch(pathUrl(owner, repo, path), {
    method: 'DELETE',
    headers: { ...headers(env), 'Content-Type': 'application/json' },
    body: JSON.stringify({ message, sha: current.sha, branch })
  });
  await githubResponse(response, `GitHub delete failed for ${path}`);
  return true;
}

export function animationPath(id, file) { return `animations/${id}/${file}`; }
export const ANIMATIONS_INDEX = 'animations.json';
export const DEVICE_STATE = 'device/state.json';
