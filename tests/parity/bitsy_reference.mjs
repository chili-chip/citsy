#!/usr/bin/env node
// Plays each tests/parity/cases/*.case file in the Bitsy 8.15 reference engine
// (tests/parity/reference/bitsy-8.15) and writes the matching .golden trace.
// tests/unit/test_parity.cpp plays the same cases in citsy and compares.
//
//   node tests/parity/bitsy_reference.mjs           # rewrite every golden
//   node tests/parity/bitsy_reference.mjs --check   # fail if a golden is stale
//   node tests/parity/bitsy_reference.mjs name ...  # only these cases
//
// Case format and trace format: tests/parity/README.md.

import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const refDir = path.join(here, "reference", "bitsy-8.15");
const casesDir = path.join(here, "cases");

// Load order matches the editor's export template.
const engineFiles = [
  "world.js", "sound.js", "font.js", "transition.js",
  "script.js", "dialog.js", "renderer.js", "bitsy.js",
];
const engineSource = engineFiles
  .map((f) => fs.readFileSync(path.join(refDir, f), "utf8"))
  .join("\n;\n");
const fonts = {};
for (const f of fs.readdirSync(refDir)) {
  if (f.endsWith(".bitsyfont")) fonts[f] = fs.readFileSync(path.join(refDir, f), "utf8");
}

export const kFrameMs = 16;
export const kSettleMs = 6000;
export const kMaxReadPages = 200;

export const Btn = { up: 0, down: 1, left: 2, right: 3, ok: 4, menu: 5 };

// A deterministic Math.random so {shuffle} is repeatable (mulberry32).
function seededRandom(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// The two sound channels as a host would play them: bitsy.sound() starts a
// note that lasts `duration` ms of real time, and frequency() and volume()
// change the note that is playing. Frequencies are in hundredths of a hertz,
// volume is 0 to 15.
function createSynth() {
  const channels = {};
  const ch = (c) => (channels[c] ??= { left: 0, freq: 0, vol: 0, pulse: 0 });
  return {
    sound(c, duration, freq, vol, pulse) { Object.assign(ch(c), { left: duration, freq, vol, pulse }); },
    frequency(c, freq) { ch(c).freq = freq; },
    volume(c, vol) { ch(c).vol = vol; },
    advance(dt) { for (const c of Object.values(channels)) c.left = Math.max(0, c.left - dt); },
    // What a channel sounds like now, as one trace word.
    state(c) {
      const { left, freq, vol, pulse } = ch(c);
      // A NaN frequency (a blip with BEAT 0 0 and RPT 1) is silent.
      if (!(left > 0 && vol > 0 && freq > 0)) return "off";
      return `${Math.round(freq / 100)}hz v${vol} p${pulse}`;
    },
  };
}

// A headless Bitsy System API: enough of `bitsy.*` for the engine to run.
function createSystem(gameData) {
  const held = new Set();
  const synth = createSynth();
  let loopFn = null;
  let nextTile = 1000;
  let gfx = 1;
  let txt = 0;
  const sys = {
    TILE_SIZE: 8, MAP_SIZE: 16, VIDEO_SIZE: 128,
    GFX_VIDEO: 0, GFX_MAP: 1, TXT_HIREZ: 0, TXT_LOREZ: 1,
    BTN_UP: Btn.up, BTN_DOWN: Btn.down, BTN_LEFT: Btn.left,
    BTN_RIGHT: Btn.right, BTN_OK: Btn.ok, BTN_MENU: Btn.menu,
    VIDEO: 2, TEXTBOX: 3, MAP1: 4, MAP2: 5, SOUND1: 6, SOUND2: 7,
    PULSE_1_8: 0, PULSE_1_4: 1, PULSE_1_2: 2,
    log() {},
    button: (code) => held.has(code),
    getGameData: () => gameData,
    getFontData: () => fonts["ascii_small.bitsyfont"],
    graphicsMode(m) { if (m !== undefined) gfx = m; return gfx; },
    textMode(m) { if (m !== undefined) txt = m; return txt; },
    color() {}, tile: () => nextTile++, delete() {}, fill() {}, set() {},
    textbox() {},
    sound: synth.sound, frequency: synth.frequency, volume: synth.volume,
    loop(fn) { loopFn = fn; },
  };
  const update = (dt) => {
    synth.advance(dt);
    loopFn(dt);
  };
  return { sys, held, update, synth, gfx: () => gfx };
}

export function createEngine(gameData) {
  const { sys, held, update: step, synth, gfx } = createSystem(gameData);
  const log = { text: "", sound: [], soundOn: false, ms: 0, last: null };
  // With `trace output` on, note each frame where a sound channel or the
  // graphics mode (video during a transition) changes.
  const update = (dt) => {
    step(dt);
    log.ms += dt;
    if (!log.soundOn) return;
    const now = `s1 ${synth.state(sys.SOUND1)} | s2 ${synth.state(sys.SOUND2)} | ${gfx() === sys.GFX_VIDEO ? "video" : "map"}`;
    if (now !== log.last) log.sound.push(`@${log.ms} ${now}`);
    log.last = now;
  };
  const ctx = {
    bitsy: sys,
    Resources: { ...fonts },
    console,
    Math: Object.create(Math, { random: { value: seededRandom(0xc15a9) } }),
  };
  vm.createContext(ctx);
  vm.runInContext(engineSource, ctx, { filename: "bitsy-8.15" });

  // Record the text scripts put in the dialog box, apart from how it is laid
  // out in rows and pages (citsy keeps its own box layout).
  const buf = ctx.dialogBuffer;
  const wrap = (name, text) => {
    const orig = buf[name];
    buf[name] = function (...args) {
      log.text += typeof text === "function" ? text(...args) : text;
      return orig.apply(this, args);
    };
  };
  wrap("AddText", (s) => s);
  wrap("AddLinebreak", "\n");
  wrap("AddPagebreak", "\n");
  wrap("AddDrawing", " ");
  return { ctx, held, update, log };
}

function normalizeText(s) {
  return s.replace(/\s+/g, " ").trim();
}

// Mode, room and position always; inventory and variables only while no
// dialog is open, for the same reason as the text.
function snapshot(ctx) {
  const lines = [];
  const p = ctx.state.room != null && ctx.sprite[ctx.playerId] ? ctx.player() : null;
  lines.push(`room ${ctx.state.room ?? "-"} at ${p ? p.x : "-"},${p ? p.y : "-"}`);
  const mode = ctx.isEnding ? "ending" : ctx.isNarrating ? "narrating" : "playing";
  lines.push(`mode ${mode}`);
  if (ctx.dialogBuffer.IsActive()) {
    lines.push("dialog open");
    return lines;
  }
  lines.push("dialog -");
  const inv = p ? Object.keys(p.inventory).filter((k) => p.inventory[k] > 0).sort() : [];
  lines.push(`inventory ${inv.map((k) => `${k}=${p.inventory[k]}`).join(" ") || "-"}`);
  const names = ctx.scriptInterpreter.GetVariableNames().slice().sort();
  for (const n of names) {
    const v = ctx.scriptInterpreter.GetVariable(n);
    lines.push(`var ${n} = ${v === undefined ? "null" : JSON.stringify(v)}`);
  }
  return lines;
}

export function frames(eng, ms) {
  for (let t = 0; t < ms; t += kFrameMs) eng.update(kFrameMs);
}

export function press(eng, btn) {
  eng.held.add(btn);
  eng.update(kFrameMs);
  eng.held.delete(btn);
  eng.update(kFrameMs);
}

export function parseCase(text, caseFile) {
  const steps = [];
  let game = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, "").trim();
    if (!line) continue;
    const [cmd, ...rest] = line.split(/\s+/);
    if (cmd === "game") game = path.resolve(path.dirname(caseFile), rest.join(" "));
    else steps.push({ cmd, args: rest, line });
  }
  if (!game) throw new Error(`${caseFile}: no "game" line`);
  return { game, steps };
}

export function runCase(caseFile) {
  const { game, steps } = parseCase(fs.readFileSync(caseFile, "utf8"), caseFile);
  const eng = createEngine(fs.readFileSync(game, "utf8"));
  const out = [];
  // Text is printed once its dialog has closed: Bitsy runs a script as the
  // text types out, so mid-dialog it may not have produced all of it yet.
  const shown = () => {
    if (eng.ctx.dialogBuffer.IsActive()) return [];
    const text = normalizeText(eng.log.text);
    eng.log.text = "";
    return text ? [`text ${JSON.stringify(text)}`] : [];
  };
  const sounds = () => eng.log.sound.splice(0).map((l) => `output ${l}`);
  eng.log.soundOn = steps.some((s) => s.cmd === "trace" && s.args[0] === "output");
  frames(eng, kSettleMs);
  out.push("> start", ...sounds(), ...shown(), ...snapshot(eng.ctx));
  for (const step of steps) {
    if (step.cmd === "trace") continue;
    out.push(`> ${step.line}`);
    if (step.cmd === "press") {
      for (const name of step.args) {
        if (!(name in Btn)) throw new Error(`${caseFile}: unknown button ${name}`);
        press(eng, Btn[name]);
        frames(eng, kSettleMs);
      }
    } else if (step.cmd === "read") {
      for (let i = 0; i < kMaxReadPages && eng.ctx.dialogBuffer.IsActive(); i++) {
        press(eng, Btn.ok);
        frames(eng, kSettleMs);
      }
    } else if (step.cmd === "wait") {
      frames(eng, Number(step.args[0] ?? kSettleMs));
    } else {
      throw new Error(`${caseFile}: unknown command ${step.cmd}`);
    }
    out.push(...sounds(), ...shown(), ...snapshot(eng.ctx));
  }
  return out.join("\n") + "\n";
}

function main() {
  const args = process.argv.slice(2);
  const check = args.includes("--check");
  const only = args.filter((a) => !a.startsWith("--"));
  const cases = fs.readdirSync(casesDir)
    .filter((f) => f.endsWith(".case"))
    .filter((f) => only.length === 0 || only.includes(f.replace(/\.case$/, "")))
    .sort();
  let stale = 0;
  for (const f of cases) {
    const caseFile = path.join(casesDir, f);
    const goldenFile = caseFile.replace(/\.case$/, ".golden");
    const trace = runCase(caseFile);
    const old = fs.existsSync(goldenFile) ? fs.readFileSync(goldenFile, "utf8") : null;
    if (check) {
      if (old !== trace) { console.error(`stale: ${path.relative(process.cwd(), goldenFile)}`); stale++; }
    } else if (old !== trace) {
      fs.writeFileSync(goldenFile, trace);
      console.log(`wrote ${path.relative(process.cwd(), goldenFile)}`);
    }
  }
  if (stale) {
    console.error(`${stale} golden file(s) are out of date; run node tests/parity/bitsy_reference.mjs`);
    process.exit(1);
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main();
