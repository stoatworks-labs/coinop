#!/usr/bin/env node
/**
 * Check the ported games against the plugin's own simulation harness.
 *
 * ## Why this exists
 *
 * Coinop's demo is not like the rest of the fleet's. On tinsel or orrery the
 * plugin *is* a fragment shader, so copying the shader across and checking it
 * character for character (`check_shaders.py`) means the demo runs the plugin's
 * actual work. Coinop's shader only decides what a *cell* looks like; the
 * substance is five game simulations, and those had to be re-implemented by hand
 * in `plugin.js`.
 *
 * A second implementation with nothing checking it is how a demo comes to run a
 * *plausible* game — one that looks right, plays right, and is not the game the
 * plugin plays. `coinoptest` asserts on the C++ games and cannot reach these.
 * Until this file existed, `demo/README.md` said plainly that nothing checked
 * the simulation port. This is that check.
 *
 * ## How it works
 *
 * `coinoptest --grid` runs every game under one fully specified configuration —
 * 32x24, seed 7, skill 0.70, autopilot, 400 frames of 20 ms, which is
 * `coinopgl`'s configuration so the two harnesses describe the same instant —
 * and prints an FNV-1a digest of the playfield bytes, the tick count, and a
 * census of cells by type. This drives the ported JavaScript through the same
 * sequence and diffs the same three things.
 *
 * The digest is exact, not approximate: the grid is 32x24 cells of four bytes,
 * and every one of them has to agree. A single cell in the wrong place, one
 * extra RNG draw, an off-by-one in a collision test — all of them change it.
 *
 *   node demo/tools/check_sim.mjs           compare against coinoptest --grid
 *   node demo/tools/check_sim.mjs --print   just print this side
 *
 * Exits non-zero on any mismatch, so tools/verify.sh can gate on it.
 *
 * ## What it does not prove
 *
 * One configuration and one instant. It says the ported game reaches the same
 * state as the C++ after 400 frames from seed 7 — which is a strong statement,
 * because reaching it requires every rule along the way to have agreed — but it
 * is not a sweep over seeds, skills or grid sizes, and it says nothing about the
 * interactive path, since it runs on autopilot with no input.
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import { Sim, Cell, Input, GAMES, GAME_NAMES } from '../plugin.js';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');

// coinoptest --grid's configuration, which is coinopgl's.
const CONFIG = {
  gridW: 32,
  gridH: 24,
  seed: 7n,
  speed: 0.5,
  skill: 0.7,
  autopilot: true,
  difficulty: 0.5,
  respawnTicks: 45,
};
const FRAMES = 400;
const FRAME_DT = 0.02;

const CELL_COUNT = Object.keys(Cell).filter((k) => k !== 'Count').length;

/**
 * FNV-1a, 64-bit, over the raw cell bytes — the same walk the C++ makes.
 *
 * `grid.cells` is the JavaScript equivalent of `Grid::Data()`: four bytes per
 * cell in the same order, which is what both the texture upload and this digest
 * depend on.
 */
function digest(grid) {
  let hash = 14695981039346656037n;
  const prime = 1099511628211n;
  const mask = (1n << 64n) - 1n;

  const bytes = grid.cells;
  for (let b = 0; b < bytes.length; b += 1) {
    hash = (hash ^ BigInt(bytes[b])) & mask;
    hash = (hash * prime) & mask;
  }
  return hash.toString(16).padStart(16, '0');
}

function measure(id) {
  const sim = new Sim();
  sim.setGame(id);
  sim.configure({ ...CONFIG });

  // A fresh Input, never touched: the C++ passes a default-constructed one, so
  // every game runs on its autopilot. A port that read a button here — or one
  // that left a latched press in the queue between games — would diverge.
  const input = new Input();

  let clock = 0;
  for (let f = 0; f < FRAMES; f += 1) {
    clock += FRAME_DT;
    sim.advance(clock, input);
  }

  const grid = sim.grid;

  const census = new Array(CELL_COUNT).fill(0);
  for (let y = 0; y < grid.h; y += 1) {
    for (let x = 0; x < grid.w; x += 1) {
      const t = grid.typeAt(x, y);
      if (t >= 0 && t < CELL_COUNT) census[t] += 1;
    }
  }

  // Per-plane sums, so a digest mismatch can be localised. See the note beside
  // the same calculation in tools/coinoptest/main.cpp.
  let planeShade = 0, planeTint = 0, planeFlash = 0;
  for (let b = 0; b < grid.cells.length; b += 4) {
    planeShade += grid.cells[b + 1];
    planeTint += grid.cells[b + 2];
    planeFlash += grid.cells[b + 3];
  }

  return {
    hash: digest(grid),
    ticks: sim.ticks,
    planes: [planeShade, planeTint, planeFlash],
    census,
  };
}

/** Parse a line of `coinoptest --grid` output. */
function parseReference(line) {
  const match = /^(\S+)\s+hash ([0-9a-f]{16})\s+ticks (\d+)\s+planes (\d+) (\d+) (\d+)\s+cells ((?:\d+\s*)+)$/.exec(line.trim());
  if (!match) return null;
  return {
    name: match[1],
    hash: match[2],
    ticks: Number(match[3]),
    planes: [Number(match[4]), Number(match[5]), Number(match[6])],
    census: match[7].trim().split(/\s+/).map(Number),
  };
}

const printOnly = process.argv.includes('--print');

const ported = Object.keys(GAMES).map(Number).sort((a, b) => a - b);

const mine = new Map();
for (const id of ported) {
  mine.set(GAME_NAMES[id], measure(id));
}

if (printOnly) {
  for (const [name, m] of mine) {
    console.log(`${name.padEnd(10)} hash ${m.hash}  ticks ${m.ticks}  planes ${m.planes.join(' ')}  cells ${m.census.join(' ')}`);
  }
  process.exit(0);
}

const harness = resolve(repoRoot, 'build', 'coinoptest');
if (!existsSync(harness)) {
  console.log('check_sim: build/coinoptest is not built — skipping.');
  console.log('  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build');
  process.exit(0);
}

const reference = new Map();
for (const line of execFileSync(harness, ['--grid'], { encoding: 'utf8' }).split('\n')) {
  const parsed = parseReference(line);
  if (parsed) reference.set(parsed.name, parsed);
}

if (reference.size === 0) {
  console.log('check_sim: coinoptest --grid produced nothing parseable.');
  console.log('  The harness may predate the --grid mode; rebuild it.');
  process.exit(1);
}

let failures = 0;
let checked = 0;

for (const [name, m] of mine) {
  const ref = reference.get(name);
  if (!ref) {
    console.log(`MISSING  ${name} — not in coinoptest --grid output`);
    failures += 1;
    continue;
  }

  const problems = [];
  if (m.hash !== ref.hash) problems.push(`hash ${m.hash} != ${ref.hash}`);
  if (m.ticks !== ref.ticks) problems.push(`ticks ${m.ticks} != ${ref.ticks}`);

  // The census is printed even when the hash already failed: a hash that
  // differs says nothing about HOW, and the first question on a mismatch is
  // always "is it one cell or is it a different game".
  if (m.census.join(' ') !== ref.census.join(' ')) {
    problems.push(`cells [${m.census.join(' ')}] != [${ref.census.join(' ')}]`);
  }

  const planeNames = ['shade', 'tint', 'flash'];
  for (let i = 0; i < 3; i += 1) {
    if (m.planes[i] !== ref.planes[i]) {
      problems.push(`${planeNames[i]} plane sums ${m.planes[i]} != ${ref.planes[i]}`);
    }
  }

  checked += 1;
  if (problems.length) {
    failures += 1;
    console.log(`DIFFERS  ${name}`);
    for (const problem of problems) console.log(`           ${problem}`);
  } else {
    console.log(`ok       ${name.padEnd(10)} hash ${m.hash}  ticks ${m.ticks}`);
  }
}

const total = GAME_NAMES.length;
console.log(`\n${checked - failures}/${checked} checked games match coinoptest (${ported.length}/${total} games ported).`);

process.exit(failures === 0 ? 0 : 1);
