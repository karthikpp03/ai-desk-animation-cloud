const MAX_FRAMES = 600;
const FRAME_BYTES = 128 * 64 / 8;

function numberValue(v) {
  const n = v.trim();
  if (/^0x[0-9a-f]+$/i.test(n)) return parseInt(n, 16);
  if (/^\d+$/.test(n)) return Number(n);
  return NaN;
}

function parseByteList(body) {
  const tokens = body.match(/(?:0x[0-9a-f]+|\b\d+\b)/gi) || [];
  const bytes = [];
  for (const token of tokens) {
    const n = numberValue(token);
    if (!Number.isInteger(n) || n < 0 || n > 255) continue;
    bytes.push(n);
  }
  return bytes;
}

function detectDimensions(source) {
  const width = source.match(/#define\s+SCREEN_WIDTH\s+(\d+)/i)?.[1];
  const height = source.match(/#define\s+SCREEN_HEIGHT\s+(\d+)/i)?.[1];
  const display = source.match(/(?:drawBitmap|drawXBitmap)\s*\([^;]*?,\s*[^;]*?,\s*[^;]*?,\s*(\d+)\s*,\s*(\d+)/i);
  return {
    width: Number(width || display?.[1] || 128),
    height: Number(height || display?.[2] || 64)
  };
}

// ---------------------------------------------------------------------------
// Timing detection
//
// Order of preference (the first clear match wins; the default is a last resort):
//   1. millis() interval        if (millis() - lastMs >= 67)   /  next = millis() + 67
//   2. delay(...) in loop() or a helper (never the delay()s that live in setup())
//   3. FPS constant             #define FPS 15
//   4. frame-duration variable  const int frameDelay = 67;
// Numbers may be literals (67, 67UL), named constants, or `1000 / FPS` expressions.
// ---------------------------------------------------------------------------
const DEFAULT_FRAME_MS = 100;
const MIN_FRAME_MS = 1;
const MAX_FRAME_MS = 60000;

function stripComments(source) {
  return source.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

// Drop the (huge) bitmap initialisers so timing searches only look at code.
function stripBitmapData(source) {
  return source.replace(/=\s*\{[^{}]*\}\s*;/g, '= {};');
}

function functionBody(source, name) {
  const m = new RegExp(`\\b(?:void|int)\\s+${name}\\s*\\(\\s*(?:void)?\\s*\\)\\s*\\{`).exec(source);
  if (!m) return null;
  let depth = 1;
  let i = m.index + m[0].length;
  const start = i;
  for (; i < source.length && depth > 0; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}') depth--;
  }
  return { start: m.index, end: i, body: source.slice(start, i - 1) };
}

function collectConstants(code) {
  const consts = new Map();
  const defRe = /#define\s+([A-Za-z_]\w*)\s+([^\n]+)/g;
  let m;
  while ((m = defRe.exec(code))) consts.set(m[1], m[2].trim());
  const varRe = /\b(?:const|constexpr|static|volatile|unsigned|long|int|uint\d+_t|float|double)(?:\s+(?:const|constexpr|static|volatile|unsigned|long|int|short|uint\d+_t|float|double))*\s+([A-Za-z_]\w*)\s*=\s*([^;,]+);/g;
  while ((m = varRe.exec(code))) if (!consts.has(m[1])) consts.set(m[1], m[2].trim());
  return consts;
}

// Evaluate: 67, 67UL, 0x43, NAME, (expr), 1000 / 15, 1000UL / FPS, 1000 / (FPS)
function evalExpr(expr, consts, depth = 0) {
  if (depth > 6 || expr == null) return NaN;
  let e = String(expr).trim().replace(/^\((.*)\)$/, '$1').trim();
  e = e.replace(/\b(\d+(?:\.\d+)?|0x[0-9a-f]+)[uUlLfF]+\b/g, '$1');
  if (/^0x[0-9a-f]+$/i.test(e)) return parseInt(e, 16);
  if (/^\d+(?:\.\d+)?$/.test(e)) return Number(e);
  if (/^[A-Za-z_]\w*$/.test(e)) return consts.has(e) ? evalExpr(consts.get(e), consts, depth + 1) : NaN;
  const div = /^(.+?)\s*([\/*])\s*(.+)$/.exec(e);
  if (div) {
    const a = evalExpr(div[1], consts, depth + 1);
    const b = evalExpr(div[3], consts, depth + 1);
    if (!Number.isFinite(a) || !Number.isFinite(b)) return NaN;
    return div[2] === '/' ? (b === 0 ? NaN : a / b) : a * b;
  }
  return NaN;
}

function timingResult(ms, source) {
  if (!Number.isFinite(ms) || ms < MIN_FRAME_MS || ms > MAX_FRAME_MS) return null;
  return { frameDurationMs: ms, fps: 1000 / ms, source };
}

function detectTiming(rawSource) {
  const code = stripBitmapData(stripComments(rawSource));
  const consts = collectConstants(code);
  const loopFn = functionBody(code, 'loop');
  const setupFn = functionBody(code, 'setup');
  // Everything except setup(): delays used for splash screens / error halts must not set the frame rate.
  const withoutSetup = setupFn ? code.slice(0, setupFn.start) + code.slice(setupFn.end) : code;
  const scopes = [loopFn?.body, withoutSetup].filter(Boolean);
  const operand = '([A-Za-z_]\\w*|\\d+(?:\\.\\d+)?[uUlL]*|\\(?\\s*\\d+[uUlL]*\\s*\\/\\s*[A-Za-z_\\w]+\\s*\\)?)';

  for (const scope of scopes) {
    // Variables that hold a millis() timestamp, e.g. `unsigned long now = millis();`
    const stampVars = new Set(['millis\\s*\\(\\s*\\)']);
    for (const m of scope.matchAll(/\b([A-Za-z_]\w*)\s*=\s*millis\s*\(\s*\)\s*;/g)) stampVars.add(m[1]);
    const stamp = `(?:${[...stampVars].join('|')})`;

    // 1a. if (millis() - lastMs >= 67)   (also `>` and named intervals)
    const diff = new RegExp(`${stamp}\\s*-\\s*[A-Za-z_]\\w*(?:\\[[^\\]]*\\])?\\s*(?:>=|>)\\s*${operand}`).exec(scope);
    if (diff) {
      const r = timingResult(evalExpr(diff[1], consts), 'millis() interval');
      if (r) return r;
    }
    // 1b. nextFrame = millis() + 67;   nextFrame += 67;
    const next = new RegExp(`\\b[A-Za-z_]\\w*\\s*=\\s*${stamp}\\s*\\+\\s*${operand}|\\b(?:next|deadline)\\w*\\s*\\+=\\s*${operand}`, 'i').exec(scope);
    if (next) {
      const r = timingResult(evalExpr(next[1] ?? next[2], consts), 'millis() schedule');
      if (r) return r;
    }
    // 2. delay(67) / delay(FRAME_DELAY) / delay(1000 / FPS)
    const delayCall = /\bdelay\s*\(\s*([^)]+?)\s*\)\s*;/.exec(scope);
    if (delayCall) {
      const r = timingResult(evalExpr(delayCall[1], consts), 'delay()');
      if (r) return r;
    }
  }

  // 3. FPS constants
  for (const [name, value] of consts) {
    if (/^(?:target_?)?(?:fps|frame_?rate|frames_?per_?second)$/i.test(name)) {
      const fps = evalExpr(value, consts);
      if (fps > 0) {
        const r = timingResult(1000 / fps, `FPS constant (${name})`);
        if (r) return r;
      }
    }
  }
  // 4. frame-duration / interval variables
  for (const [name, value] of consts) {
    if (/^(?:frame_?(?:delay|interval|duration|time|period|ms)|(?:delay|interval|duration|period)_?(?:ms)?|(?:anim|animation)_?(?:delay|interval|ms)|frame_?delay_?ms)$/i.test(name)) {
      const r = timingResult(evalExpr(value, consts), `frame-duration variable (${name})`);
      if (r) return r;
    }
  }
  return { frameDurationMs: DEFAULT_FRAME_MS, fps: 1000 / DEFAULT_FRAME_MS, source: `default (${DEFAULT_FRAME_MS} ms - no timing found in source)` };
}

function detectLoop(source) {
  const code = stripBitmapData(stripComments(source));
  const body = functionBody(code, 'loop')?.body || code;
  return /%\s*(?:numFrames|frameCount|frame_count|NUM_FRAMES|FRAME_COUNT|\d+)/i.test(body)
    || /\b(?:frameIdx|frameIndex|frame|idx|index|currentFrame|f|i)\s*=\s*0\s*;/.test(body)
    || /\bfor\s*\(\s*(?:int|uint\w*|size_t|byte)?\s*\w+\s*=\s*0\s*;/.test(body)
    || /\b(?:while\s*\(\s*(?:true|1)\s*\)|for\s*\(\s*;\s*;\s*\))/.test(body);
}

function extractFrameArrays(source, preferredNames = null) {
  const declarations = [];
  const re = /(?:const\s+|static\s+|volatile\s+)*(?:uint8_t|unsigned\s+char)(?:\s+PROGMEM)?\s+([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]\s*=\s*\{([\s\S]*?)\};/gi;
  let m;
  while ((m = re.exec(source))) {
    const name = m[1];
    const declared = Number(m[2]);
    const bytes = parseByteList(m[3]);
    declarations.push({ name, declared, bytes, start: m.index });
  }

  let found;
  if (preferredNames?.length) {
    const wanted = new Set(preferredNames);
    found = declarations.filter(x => wanted.has(x.name));
    if (found.length !== preferredNames.length) throw new Error('The AI identified frame arrays that could not be extracted exactly from the source.');
    found.sort((a,b) => preferredNames.indexOf(a.name) - preferredNames.indexOf(b.name));
  } else {
    const numbered = declarations.filter(x => /^frame\d+$/i.test(x.name));
    found = numbered.length ? numbered : declarations.filter(x => /(?:frame|bitmap|animation|image)/i.test(x.name) && x.declared === FRAME_BYTES && x.bytes.length === FRAME_BYTES);
    found.sort((a,b) => {
      const an = /^frame(\d+)$/i.exec(a.name), bn = /^frame(\d+)$/i.exec(b.name);
      if (an && bn) return Number(an[1]) - Number(bn[1]);
      return a.start - b.start;
    });
  }
  return found;
}

// If the sketch plays frames through a pointer table (`const uint8_t* frames[] = {frame0, frame1, ...}`),
// that table is the real playback order. Only used when every entry is a known frame array.
function applyPlaybackOrder(source, arrays) {
  if (!arrays.length) return arrays;
  const byName = new Map(arrays.map(a => [a.name, a]));
  const code = stripComments(source);
  const re = /(?:uint8_t|unsigned\s+char)\s*\*\s*(?:const\s+)?[A-Za-z_]\w*\s*\[\s*\w*\s*\]\s*(?:PROGMEM\s*)?=\s*\{([^}]*)\}/g;
  let m;
  while ((m = re.exec(code))) {
    const names = m[1].split(',').map(x => x.trim().replace(/^&/, '')).filter(Boolean);
    if (names.length && names.length <= MAX_FRAMES && names.every(n => byName.has(n))) return names.map(n => byName.get(n));
  }
  return arrays;
}

export function parseIno(source, filename = 'animation.ino', hints = null) {
  if (typeof source !== 'string' || !source.trim()) throw new Error('The .ino file is empty.');
  const { width, height } = detectDimensions(source);
  const timing = detectTiming(source);
  const loop = detectLoop(source);
  const preferred = hints?.frame_arrays?.length ? hints.frame_arrays : null;
  let arrays = extractFrameArrays(source, preferred);
  if (!preferred) arrays = applyPlaybackOrder(source, arrays);

  if (!arrays.length) throw new Error('No frameN bitmap arrays were found. Expected arrays such as frame0[1024], frame1[1024], ...');
  if (arrays.length > MAX_FRAMES) throw new Error(`Animation has too many frames (${arrays.length}); maximum is ${MAX_FRAMES}.`);
  if (width !== 128 || height !== 64) throw new Error(`Unsupported dimensions ${width}x${height}; V1 requires 128x64.`);

  const expected = FRAME_BYTES;
  for (const frame of arrays) {
    if (frame.declared !== expected || frame.bytes.length !== expected) {
      throw new Error(`${frame.name} is ${frame.bytes.length} bytes (declared ${frame.declared}); expected exactly ${expected} bytes.`);
    }
  }

  const out = new Uint8Array(arrays.length * expected);
  arrays.forEach((frame, i) => out.set(frame.bytes, i * expected));

  const baseName = filename.replace(/\.ino$/i, '').replace(/[^a-zA-Z0-9._-]+/g, '_').replace(/^[_-]+|[_-]+$/g, '') || 'animation';
  const metadata = {
    id: '', name: baseName, originalFilename: filename,
    width, height, monochrome: true, bitsPerPixel: 1,
    bytesPerFrame: expected, frames: arrays.length,
    fps: Number(timing.fps.toFixed(3)), frameDurationMs: Math.round(timing.frameDurationMs),
    loop, format: 'raw-ssd1306-row-major-1bpp-v1', timingSource: timing.source
  };
  return { metadata, frames: out, parser: 'deterministic', frameNames: arrays.map(x => x.name) };
}
