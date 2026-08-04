# demo/ — the browser demo

Intended for **https://coinop-demo.stoatworks-labs.com**, to be linked from the
[project page](https://stoatworks-labs.com/software/coinop/) and from the
[video plugins page](https://stoatworks-labs.com/video-plugins/).

> **INCOMPLETE — do not deploy yet.** The plugin has five games; this page
> carries **Snake and Bricks**. Marchers, Rally and Drift are not in the dropdown
> rather than being there and doing nothing. See "Finishing it" below.

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

**Nothing checks the simulation port.** `coinoptest` asserts on the C++ games and
cannot reach these. `tools/check_shaders.py` covers the shaders and nothing else.

## Why Step and Restart mean something different here

Every other page in this set renders any frame on its own, because every other
plugin is a pure function of (something, phase). This one has real state. Step
advances the game by one frame's worth of time and cannot go back; Restart begins
a new game rather than rewinding this one. That is the plugin's own behaviour,
and the reason `Sim.h` opens by admitting it breaks the fleet's rule knowingly.

## Finishing it

Each remaining game is one class implementing `reset / step / draw / tickHz /
finished / intensity`, ported from `source/games/`, then added to the `GAMES` map:

| Game | Source | Notes |
|---|---|---|
| Marchers | `Marchers.{h,cpp}` | Formation steps down and shoots back, speeding up as it thins out. |
| Rally | `Rally.{h,cpp}` | Two paddles, one ball. Axis drives one, two buttons the other. |
| Drift | `Drift.{h,cpp}` | The vector game — continuous rotation and sub-cell drift, rasterised into the same grid. |

`GAME_NAMES` already holds all five in the plugin's order, and the dropdown is
built from whichever keys `GAMES` has, so adding one is a single line. The
`differences` entry that admits the gap counts them itself and will shrink on its
own.

## Editing it

- `plugin.js` — parameters, shaders, and the simulation port. **When a shader in
  `source/Shaders.cpp` changes, change it here too**, and when a game's rules
  change, mirror that as well.
- `vendor/` — the shared kit, vendored from `stoatworks-backend/resolume-demo/`.
  **Do not edit these.** Fix the master and re-run `./sync.sh`.

## Deploying

Not yet. When the games are all in, from the repo root:

```bash
cf-run npx wrangler deploy
```

Verify **by content, never by status code**:

```bash
curl -s 'https://coinop-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```
