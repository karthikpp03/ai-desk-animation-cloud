// npm run test:parser  -  verifies .ino parsing and timing detection (no network, no secrets needed).
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { ROOT } from './lib/env.mjs';
import { parseIno } from '../worker/parser/inoParser.js';

let failures = 0;
const check = (label, fn) => { try { fn(); console.log(`ok   ${label}`); } catch (e) { failures++; console.log(`FAIL ${label}\n       ${e.message.split('\n')[0]}`); } };

// ---- real sketches ------------------------------------------------------------------------------
const real = [
  { file: ['sample-eye.ino'], frames: 14, ms: 100 },
  { file: ['animations/oled_display.ino', 'test-fixtures/oled_display.ino', 'oled_display.ino'], frames: 106, ms: 67 }
];
for (const spec of real) {
  const found = spec.file.map(f => path.join(ROOT, f)).find(p => fs.existsSync(p));
  const name = spec.file[0];
  if (!found) { console.log(`skip ${name} (file not present)`); continue; }
  const { metadata: m, frames, frameNames } = parseIno(fs.readFileSync(found, 'utf8'), path.basename(found));
  check(`${name}: ${spec.frames} frames detected`, () => assert.equal(m.frames, spec.frames));
  check(`${name}: every frame is 1024 bytes (total ${frames.length})`, () => { assert.equal(m.bytesPerFrame, 1024); assert.equal(frames.length, spec.frames * 1024); });
  check(`${name}: frame order frame0..frame${spec.frames - 1}`, () => assert.deepEqual(frameNames, Array.from({ length: spec.frames }, (_, i) => `frame${i}`)));
  check(`${name}: frameDurationMs = ${spec.ms}`, () => assert.equal(m.frameDurationMs, spec.ms));
  check(`${name}: fps = ${(1000 / spec.ms).toFixed(2)}`, () => assert.ok(Math.abs(m.fps - 1000 / spec.ms) < 0.01, `got ${m.fps}`));
  check(`${name}: loop = true`, () => assert.equal(m.loop, true));
  console.log(`     timing source: ${m.timingSource}`);
}

// ---- synthetic timing patterns --------------------------------------------------------------------
const zeros = Array(1024).fill('0x00').join(',');
const frame = n => `const uint8_t PROGMEM frame${n}[1024] = {${zeros}};\n`;
const base = frame(0) + frame(1);
const cases = [
  ['millis() - lastMs >= 67', 'void loop(){ if (millis() - lastMs >= 67) { lastMs = millis(); } }', 67],
  ['millis named interval', 'const unsigned long FRAME_INTERVAL = 80;\nvoid loop(){ if (millis() - lastMs > FRAME_INTERVAL) {} }', 80],
  ['now - last >= 50UL', 'void loop(){ unsigned long now = millis(); if (now - lastFrame >= 50UL) {} }', 50],
  ['nextFrame = millis() + 40', 'void loop(){ if (millis() >= nextFrame) { nextFrame = millis() + 40; } }', 40],
  ['delay(120) in loop()', 'void setup(){ delay(2000); }\nvoid loop(){ draw(); delay(120); }', 120],
  ['delay(CONST)', '#define FRAME_DELAY 90\nvoid loop(){ draw(); delay(FRAME_DELAY); }', 90],
  ['delay(1000 / FPS)', '#define FPS 20\nvoid loop(){ draw(); delay(1000 / FPS); }', 50],
  ['FPS constant only', 'const int fps = 25;\nvoid loop(){ draw(); }', 40],
  ['frameDelay variable only', 'int frameDelay = 70;\nvoid loop(){ draw(); }', 70],
  ['setup() delay is NOT frame timing', 'void setup(){ delay(2000); }\nvoid loop(){ draw(); }', 100],
  ['unrelated "> 5" ignored', 'void loop(){ if (x > 5) {} if (millis() - t >= 33) {} }', 33],
  ['no timing -> 100 ms default', 'void loop(){ draw(); }', 100]
];
for (const [label, code, ms] of cases) check(`timing: ${label} -> ${ms} ms`, () => assert.equal(parseIno(base + code, 't.ino').metadata.frameDurationMs, ms));
check('default fallback is flagged in timingSource', () => assert.match(parseIno(base + 'void loop(){}', 't.ino').metadata.timingSource, /^default/));

// ---- ordering ------------------------------------------------------------------------------------------
check('pointer table order wins over declaration order', () => {
  const a = frame(0).replace(zeros, Array(1024).fill('0x01').join(','));
  const r = parseIno(a + frame(1) + 'const uint8_t* frames[] = {frame1, frame0};\nvoid loop(){}', 'o.ino');
  assert.deepEqual(r.frameNames, ['frame1', 'frame0']);
  assert.equal(r.frames[0], 0); assert.equal(r.frames[1024], 1);
});
check('wrong-sized frame is rejected', () => assert.throws(() => parseIno(frame(0).replace('1024]', '1024]').replace(zeros, '0x00,0x00') + 'void loop(){}', 'bad.ino')));

console.log(failures ? `\n${failures} check(s) FAILED` : '\nAll parser checks passed');
process.exit(failures ? 1 : 0);
