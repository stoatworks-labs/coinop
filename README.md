# Coinop

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The games are verified
> by an offline harness that drives the real game classes with no graphics API
> present and asserts on simulation state — that the snake is a given length,
> that the ball's vertical component never collapses, that a match ends — and a
> second harness compiles the shaders in a headless GL context and measures what
> they draw (see [Status](#status)). It has **never been loaded into Resolume**.
> Check it in your own rig before trusting it in a show.

Five playable arcade games for [Resolume](https://resolume.com) Arena and
Avenue, as a pair of FFGL plugins. Driven by MIDI or OSC, or left to play
themselves.

**Video:** [What it does, in 50 seconds](https://www.youtube.com/watch?v=PENDING)

![Snake, Bricks, Marchers and Drift, on four of the six palettes](docs/hero.png)

<sub>Four of the five games, on four of the six palettes. Rendered by
`coinopgl`, the offline harness — the real autopilot playing through the real
cell shader, not a Resolume screen capture.</sub>

| Game | What it is |
| --- | --- |
| **Snake** | Grows, turns, traps itself. Two buttons or four. |
| **Bricks** | Ball, paddle, brick field. Best played with a fader. |
| **Marchers** | A formation that steps down the screen and shoots back. |
| **Rally** | Two paddles, one ball. Two players, one plugin instance. |
| **Drift** | Rotating ship, splitting rocks, wrapping playfield. |

`Coinop` is a source. `Coinop Over` is an effect — and in the effect, the brick
field can be built out of the incoming clip, so breaking a brick punches a hole
through to the layer below.

## Why it exists

Two jobs, and most of the design follows from the second:

- **A generator that is genuinely never the same twice.** A seeded game that
  plays itself, resets when it loses, and has an intensity that climbs through a
  run and drops when it restarts.
- **Pixel mapping.** The composition *is* the fixture layout, Resolume's pixel
  mapper samples it, and the playfield is what the lights do. That is why
  everything is drawn as cells, why the grid size is a parameter, and why even
  the vector game is rasterised.

![Drift at nineteen cells across, with the cells round and the gaps open](docs/pixelmap.png)

<sub>The same Drift, with the grid coarsened to nineteen cells and the gaps
opened. This is the shape the plugin takes when a pixel mapper is sampling it —
and the reason the vector game is drawn as cells at all. A hairline vector lands
between fixtures and disappears.</sub>

## Controls

FFGL has no keyboard and no gamepad — it has parameters, and Resolume will MIDI-
or OSC-map any of them. So every control is a parameter:

| Parameter | Good for |
| --- | --- |
| **Paddle** | A fader or an OSC float. The nicest way to play Bricks and Rally. |
| **Left / Right / Up / Down** | Four pads. Steering, and Rally's second player. |
| **Fire** | One pad. Shoot, launch, thrust — and in Snake, "turn right". |
| **Autoplay** | On by default. The autopilot plays, and loses on purpose. |
| **Skill** | How competent the autopilot is. At 1.0 it plays a long game. |
| **Seed** | Same seed, same game. |

## Build

```bash
git clone --recursive https://github.com/stoatworks-labs/coinop
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
```

```bash
cmake --install build
```

See [CLAUDE.md](CLAUDE.md) for the full command reference and
[AGENTS.md](AGENTS.md) for why the code is shaped the way it is.

## Status

Verified offline on macOS, in two harnesses:

- **`coinoptest`** — 101 checks, no GL context. Per-game determinism, the three
  host-timing defences, and the invariants that matter per game: Snake's turn
  queue, Bricks' tunnelling and its two degenerate-angle locks, Rally's
  termination, Marchers' bounds, Drift's wrapping.
- **`coinopgl`** — 15 checks, headless CGL. Both shader variants compile and
  link, every game renders lit cells, the letterbox lands exactly where it
  should, no GL error.

Not verified: the plugin has never been loaded into Resolume, the FFGL parameter
and clock plumbing is the part no harness here reaches, and the Windows build
has never been compiled.

## Licence

MIT. See [LICENSE](LICENSE).

The mechanics here are generic; the names are deliberately not the famous ones.
See the naming note in [AGENTS.md](AGENTS.md).
