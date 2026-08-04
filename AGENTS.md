# coinop — orientation for another LLM (or a newcomer)

**What it is:** five playable arcade games as **two** FFGL 2.1 plugins for
Resolume Arena/Avenue. `Coinop` is a source that draws a playfield over its own
background; `Coinop Over` is an effect that draws it over the incoming clip —
and can build the brick field *out of* the clip. C++17 + GLSL 4.10, CMake,
universal macOS `.bundle` and a Windows `.dll`.

`CLAUDE.md` is the command reference — build, verify, install. This file is the
*why*: read it before touching the timestep, the input path, or the cell
encoding.

Family notes: `Diag`, the CMake shape, the OBJECT-library trap and the harness
form are lifted from **downpour** and **orrery**. The host-clock normaliser is
orrery's, unchanged. Where those repos document a trap, it applies here too.

---

## The one idea

**The simulation knows nothing about FFGL, OpenGL, or Resolume.**

`coinop_sim` — every game, the grid, the rasteriser, the input queue, the
fixed-timestep driver — links with no graphics API and no host SDK present.
There is a CMake configuration (`-DCOINOP_BUILD_PLUGINS=OFF`) that builds only
that, and `tools/verify.sh` runs it on every pass specifically so the property
cannot rot.

What it buys is the harness. `coinoptest` drives the real game classes in a
plain command-line binary and asserts on *state*: not "there are fourteen green
pixels" but "the snake is fourteen segments long", "the ball's vertical
component never collapsed", "the match ended 7–6". Verifying a game by rendering
it and counting pixels would be slower, flakier, and unable to express most of
what actually needs checking.

The shader is verified separately, by `coinopgl`, in a headless CGL context.

---

## This plugin breaks the fleet's rule, knowingly

[orrery/AGENTS.md](../orrery/AGENTS.md) states the house rule plainly: *"An
instance's placement is a pure function of (index, phase). There is no
simulation state anywhere."* The reasoning there is correct and worth
re-reading — integrate a velocity a frame at a time and the speed of the motion
becomes whatever frame rate the host managed, so a chase slows down when the
projection load rises and the lights come apart from the music.

A game cannot be written that way. Snake **is** accumulated state; there is no
closed form for "where is the snake at t=91.3" that does not involve having
played the first 91.3 seconds. So `Sim` exists to contain the damage, and it is
three specific defences. All three are asserted in `coinoptest`, because all
three fail in ways that are near-impossible to diagnose from a bug report.

**1. Ticks come from the clock, never from frames.** A frame rate drop makes the
game render less smoothly; it does not make it play slower.

**2. A frame with no elapsed time renders and does not step.** Resolume renders
the same frame more than once — to the preview, to the output, to a clip
thumbnail; [tinsel](../tinsel/source/Tinsel.cpp) says so directly. Without this
guard the ball moves at double speed *whenever the preview monitor happens to be
open*, which is a bug whose behaviour depends on how the operator has arranged
their windows.

**3. Catch-up is capped.** A layer that was bypassed for forty seconds must not
pay out forty seconds of ticks in one frame. The game loses the time it was not
on screen for, which is the right answer: nobody was watching.

---

## FFGL has no input events. It has parameters, and you poll them.

This is the constraint that shapes the whole input path, and it is the reason
`Input.h` is longer than it looks like it needs to be.

There is no keyboard, no mouse and no gamepad in the FFGL ABI. Every control
arrives as a plugin parameter — which Resolume will happily MIDI- or OSC-map, so
a launchpad button really can be the fire button and a fader really is the best
way to play Bricks.

What does **not** work is reading the parameter at render time. An
`FF_TYPE_EVENT` goes to 1 and back to 0 whenever the host feels like it, on the
host's own thread; a press and release that both land between two frames is
invisible to a poll. At 50 fps that is a 20 ms window, and a drummer on a pad
hits inside it constantly. The failure is not a crash — it is a fire button that
misses one press in five and cannot be reproduced on demand.

So presses are latched on the rising edge in `SetFloatParameter`, on whichever
thread delivers them, into a **queue** and not a counter. The queue matters:
Snake taking `Left` then `Up` inside one tick must turn left and then up, and a
counter loses the order and drives into a wall the player never steered at.

`Input` uses a mutex, deliberately. The lock-free version is an SPSC ring that is
only correct if exactly one thread ever pushes, which FFGL does not promise.
This is a render thread with a 20 ms budget, not an audio callback.

---

## The cell encoding is a contract

The sim writes a grid of four-byte cells; the plugin uploads them verbatim as
`GL_RGBA8UI`; the fragment shader reads them with `texelFetch` and decides what
a cell *looks* like. Nothing in the shader knows which game is running, which is
why a sixth game needs no shader change.

**Integer texture, not `GL_RGBA8`.** A normalised byte texture hands the shader
0..1 floats, so recovering the cell type means `int( t.r * 255.0 + 0.5 )` and
trusting every driver's normalise to land on the same float. It nearly always
does. When it does not, one cell type is mistaken for its neighbour and a brick
renders as a wall on one machine only.

**`GL_NEAREST` is mandatory, not cosmetic.** An integer texture cannot be
filtered at all; a linear filter makes the sampler incomplete, every fetch
returns zero, every cell reads as Empty, and the playfield renders as a flat
background with no error anywhere. `coinopgl` measures lit pixels per game
specifically to catch this.

The numeric values in `enum class Cell` are baked into the shader's branches.
**Appending is safe; reordering is not.**

---

## The grid never depends on the render resolution

The obvious way to keep cells square is to derive the grid height from the
output's aspect ratio. It is a trap.

Resolume renders one plugin instance at more than one size — output, preview,
thumbnail. Derive the grid from the render target and those are three different
grids; since a grid change is structural, `Sim::Configure` restarts the game
each time. The symptom is a game that resets several times a second, but only
while the preview is open, and only on some window layouts.

So grid dimensions come from parameters and nothing else. Aspect is its own
option, the shader letterboxes into whatever it is given, and an instance plays
the same game whatever is looking at it.

---

## The autopilot has to be beatable

Nobody hand-plays Snake for the length of a show, so Autoplay is the default and
is the mode that matters. That makes "the autopilot must lose" a **feature
requirement**, not a quality bar.

A perfect Snake AI exists — follow a Hamiltonian cycle and it never dies. It is
also unwatchable, and for a layer in a show it is worse than unwatchable: the
game never resets, so the picture stops changing. Same for a Bricks paddle that
tracks perfectly, and same for two Rally paddles that never miss.

So every autopilot has a Skill parameter that governs deliberate incompetence:
tracking error, reaction lag, and in Snake's case a flat chance of ignoring its
own survivability analysis. `coinoptest` asserts that low skill loses, that high
skill survives substantially longer, and — for every game — that a run
terminates at all.

Rally has no natural failure state at all, so it has a target score. Reaching it
finishes the match and triggers the respawn.

### The vertical lock

Worth reading `Bricks::ReflectOffPaddle` before touching it. A ball returned
from the exact centre of the paddle leaves straight up; if the column above is
cleared it returns down the same column, and a paddle good enough to centre
perfectly closes the loop forever. **The better the autopilot, the worse the
bug.** Measured before the guard: at Skill 1.0 the ball hit 14 bricks in 74
minutes of simulated play and the game never ended.

It is the mirror of the horizontal lock in `ClampVertical`, and it is why the
paddle is never allowed to return the ball perfectly vertically.

---

## Why Drift is in a cell grid

Asteroids is the one game here that was never discrete — rotating ship, rocks
tumbling at arbitrary angles, everything drifting at sub-cell speeds. The honest
reading is that it does not belong in a grid, and the obvious fix is a second
renderer with real antialiased GL lines.

That fix is wrong for the actual job. This plugin's stated use is driving pixel
maps and LED walls: a one-pixel antialiased vector line sampled by Resolume's
pixel mapper onto a 30 mm pitch wall lands between fixtures and disappears. A
Bresenham'd outline survives the trip. It also avoids a second set of
aspect-ratio bugs, a second blend path, and a shader that has to know which game
is running.

So state stays continuous and only the picture is quantised. Nothing in `Drift`
snaps to a cell.

Every comparison on that playfield has to go through `WrapDelta`. A rock at
x=31 on a 32-wide field is one cell from a ship at x=0, not thirty-one, and a
plain subtraction gets it wrong in the two places that matter most: collision,
which then misses, and target selection, which then turns the long way round.

---

## Naming

The mechanics are the public domain part; the names are not. *Breakout* and
*Asteroids* are Atari marks, *Space Invaders* is Taito's. The games here are
called Bricks, Drift, Marchers, Rally and Snake for that reason, the same
instinct that has `nesolume` describing "retro console hardware" rather than
naming a console.

**Tetris is deliberately absent.** It is the best grid fit of any of them and it
is the one to leave alone: Tetris Holding actively litigates look-alikes and won
on non-literal copying in *Tetris Holding v. Xio Interactive* (2012). A stacker
in this plugin would be the single most legally exposed thing in the fleet.

---

## Status

Verified offline, in two harnesses, on macOS:

- `coinoptest` — 101 checks. Determinism per game, the three timing defences,
  and per-game invariants: Snake's turn queue and reversal guard, Bricks'
  tunnelling and both locks, Rally's termination, Marchers' formation bounds,
  Drift's wrapping. Plus, for every game: only known cell types are ever
  written, `Draw` is pure, extreme grid sizes still draw, and the whole thing
  survives a jittery frame clock.
- `coinopgl` — 15 checks. Both shader variants compile and link, every game
  renders lit cells rather than a flat background, the letterbox lands exactly
  where it should, and no GL error is raised.

**It has never been loaded into Resolume.** Only compiled, simulated, rendered
and measured offline. The FFGL parameter and clock plumbing in `Coinop.cpp` is
the part no harness here can exercise — check it in your own rig before trusting
it in a show. The Windows build has never been compiled at all.
