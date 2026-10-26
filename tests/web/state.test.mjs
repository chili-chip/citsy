// Runs the built citsy.wasm in Node, the way the creator's worker calls it.
// Usage: node --test tests/web/  (after scripts/build-web.sh or the CI build;
// CITSY_WEB_DIR points at the folder holding citsy.js and citsy.wasm).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const dir = path.resolve(process.env.CITSY_WEB_DIR ?? path.join(here, '../../build-web'));
const require = createRequire(import.meta.url);
const CitsyModule = require(path.join(dir, 'citsy.js'));
const game = readFileSync(path.join(here, '../data/state.bitsy'), 'utf8');

async function citsy() {
  const m = await CitsyModule();
  const fn = {
    load: m.cwrap('citsy_load', 'number', ['string']),
    frame: m.cwrap('citsy_frame', 'number', ['number']),
    state: m.cwrap('citsy_state_json', 'string', []),
    version: m.cwrap('citsy_state_version', 'number', []),
    setVariable: m.cwrap('citsy_set_variable', null, ['string', 'string']),
    setItem: m.cwrap('citsy_set_item', 'number', ['string', 'number']),
    validate: m.cwrap('citsy_validate', 'string', ['string']),
  };
  return fn;
}

test('citsy_state_json lists variables and held items', async () => {
  const c = await citsy();
  assert.equal(c.load(game), 1);
  c.frame(16);
  const state = JSON.parse(c.state());
  assert.deepEqual(state.inventory, {});
  assert.deepEqual(state.variables, { mood: 'happy', visits: 0 });
  assert.equal(state.version, c.version());
});

test('setters change the state, and the version moves', async () => {
  const c = await citsy();
  c.load(game);
  const before = c.version();
  assert.equal(c.setItem('coin', 2), 1);
  assert.equal(c.setItem('nope', 2), 0);
  c.setVariable('mood', 'grumpy');
  c.setVariable('visits', '7');
  const state = JSON.parse(c.state());
  assert.ok(state.version > before);
  assert.deepEqual(state.inventory, { 0: 2 });
  assert.deepEqual(state.variables, { mood: 'grumpy', visits: 7 });
});

test('citsy_validate gives the line of a parse error', async () => {
  const c = await citsy();
  assert.deepEqual(JSON.parse(c.validate(game)), { ok: true });
  const broken = game.replace('10111101', '1011110');
  const result = JSON.parse(c.validate(broken));
  assert.equal(result.ok, false);
  assert.equal(result.errors[0].line, 39);
  assert.match(result.errors[0].message, /8 characters/);
});

test('reading the state every frame for a minute of play is cheap', async () => {
  const c = await citsy();
  c.load(game);
  const start = performance.now();
  for (let i = 0; i < 3600; i++) {
    c.frame(16.667);
    JSON.parse(c.state());
  }
  const perFrame = (performance.now() - start) / 3600;
  assert.ok(perFrame < 2, `${perFrame.toFixed(3)} ms per frame`);
});
