# demo/ — the browser demo

Live at **https://coinop-demo.stoatworks-labs.com**, linked from the
[project page](https://stoatworks-labs.com/software/coinop/) and from the
[video plugins page](https://stoatworks-labs.com/video-plugins/).

**All thirteen games are ported** — Snake, Bricks, Marchers, Rally, Drift,
Stacker, Chase, Girders, Swarm, Trails, Reflex, Rafters and Duel — and every one
of them agrees with the C++ byte for byte under `check_sim.mjs`.

The dropdown is built from whichever keys `GAMES` carries, and the page prints
an `INCOMPLETE` notice naming the gap if that list ever falls behind
`GAME_NAMES` again. A demo that offers a game it has not ported is worse than
one that admits it has not.

**This is not the plugin.** The two shaders are the text from
[`source/Shaders.cpp`](../source/Shaders.cpp), copied across unedited —
`#ifdef COINOP_OVER_INPUT` branches included — and run in WebGL2 with the
parameters the plugin's constructor declares.

## What is a port rather than a copy

Almost all of it. Unlike the other demos in this set, Coinop's substance is not
in the shader: the shader only decides what a *cell* looks like. Ported into
`plugin.js` are `Rng.h` (xorshift64\*, in BigInt, because "same seed, same game"
has to hold exactly), `Grid.h`, `Input.h`, `Controls.cpp`, `Sim.cpp`'s
accumulator with all three of its defences, and one class per game.

### What checks the port

`tools/check_shaders.py` covers the shaders, character for character.

**`tools/check_sim.mjs` covers the simulation**, which is the part that actually
matters here. `coinoptest --grid` runs every game under one fully specified
configuration — 32×24, seed 7, skill 0.70, autopilot, 400 frames of 20 ms, which
is `coinopgl`'s configuration — and prints an FNV-1a digest of the playfield
bytes, the tick count, per-plane sums and a census of cells by type.
`check_sim.mjs` drives the ported JavaScript through the same sequence and diffs
all four.

The digest is exact rather than approximate: 32×24 cells of four bytes, every one
of which has to agree. A single cell out of place, one extra RNG draw, an
off-by-one in a collision test — all of them change it.

```bash
node demo/tools/check_sim.mjs        # against the C++
node demo/tools/check_sim.mjs --print
build/coinoptest --grid-map          # the whole playfield, for eyeballing a diff
```

It earned its place immediately: the snake's tail gradient was one shade level
bright on two cells, because the C++ casts to `uint8_t` (which truncates toward
zero) and the port used `Math.round`. Invisible on screen, and exactly the drift
that stops a demo being evidence about the plugin.

**What it does not prove.** One configuration and one instant. It says the ported
game reaches the same state as the C++ after 400 frames from seed 7 — a strong
statement, because getting there requires every rule along the way to have agreed
— but it is not a sweep over seeds, skills or grid sizes, and it says nothing
about the interactive path, since it runs on autopilot with no input.

## Why Step and Restart mean something different here

Every other page in this set renders any frame on its own, because every other
plugin is a pure function of (something, phase). This one has real state. Step
advances the game by one frame's worth of time and cannot go back; Restart begins
a new game rather than rewinding this one. That is the plugin's own behaviour,
and the reason `Sim.h` opens by admitting it breaks the fleet's rule knowingly.

## Porting another game

One class implementing `reset / step / draw / tickHz / score / finished /
intensity`, ported from `source/games/`, then one line in the `GAMES` map.
`GAME_NAMES` already lists all thirteen in `GameId` order; the dropdown is built
from whichever keys `GAMES` has and the notice counts the difference itself, so
nothing else needs touching.

Port it against `check_sim.mjs` rather than against the screen: add the game to
`GAMES`, run the checker, and fix until the digest matches. Marchers and Drift
were both done that way and both matched on the first run, which is the point —
a game that *looks* right is not evidence.

What the second batch of eight turned out to show: **every one of them matched
on its first honest run**, including Swarm, which carries a sine wobble per
diver, and Rafters and Duel, which carry float physics with sub-cell collision
thresholds. Stacker is entirely integer by design, so it had nothing to disagree
about.

That is a weaker result than it sounds and the reason is worth keeping. The
check is one configuration at one instant: 400 frames from seed 7. The C++
computes in 32-bit float where JavaScript has only 64-bit doubles, so a
threshold like `abs(f.x - shipX) < 0.9` can in principle fall on opposite sides
in the two implementations, and everything after that point is a different game.
Nothing here proves it does not happen at tick 4000 — only that it does not
happen by tick 400.

## Editing it

- `plugin.js` — parameters, shaders, and the simulation port. **When a shader in
  `source/Shaders.cpp` changes, change it here too**, and when a game's rules
  change, mirror that as well.
- `vendor/` — the shared kit, vendored from `stoatworks-backend/resolume-demo/`.
  **Do not edit these.** Fix the master and re-run `./sync.sh`.

## Deploying

From the repo root:

```bash
cf-run npx wrangler deploy
```

Verify **by content, never by status code**:

```bash
curl -s 'https://coinop-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```
