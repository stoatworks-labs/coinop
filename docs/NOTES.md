# Notes

Working notes for this repo: status, decisions, and the traps that have actually bitten.
Migrated out of Claude Code's memory on 2026-08-24, so they are written in the first
person and dated by when each thing was learned — that date is usually the useful part.

Cross-cutting notes that are not specific to this repo live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

*coinop — thirteen arcade games as two FFGL plugins for Resolume; the fleet's only stateful generator. RELEASED v0.2.0*

`~/Projects/coinop` — **fourteen** games as **two** FFGL bundles (source +
`Coinop Over` effect) with a Game dropdown, not twenty-six bundles. Snake,
Bricks, Marchers (invaders), Rally (pong), Drift (asteroids) shipped v0.1.0;
**Stacker, Chase, Girders, Swarm, Trails, Reflex, Rafters, Duel added
2026-08-06** (stacker, dot-eater, ladder-climb, diving formation, light cycles,
laserdisc prompt, platform-bump, one-on-one fighter). **RELEASED v0.2.0 on 2026-08-06** (v0.1.0 2026-08-04), MIT, public: `github.com/stoatworks-labs/coinop`.
macOS builds notarised (15th repo in the fleet). Two videos, both up: `SNqhfKOn-xs` (v0.2.0, the eight new games) and
`DZyiCXpSt98` (v0.1.0, the original five). Reel `DbtmPDRjNAo`. Site page live. Video project at `stoatworks-backend/video/projects/coinop`.

**Browser demo LIVE 2026-08-05** at `coinop-demo.stoatworks-labs.com` — **all 13
games ported** (the eight new ones on 2026-08-06) and all 13 match
`coinoptest --grid` byte for byte. Every one of the eight matched on its FIRST
run, including Swarm (per-diver sine) and Rafters/Duel (float physics with
sub-cell collision thresholds). Not deployed yet — the page is built but
`cf-run npx wrangler deploy` has not been run.

`check_sim.mjs` now takes `COINOP_HARNESS` — verify.sh builds into
`build-verify` but the checker used to hardcode `build/`, so a stale binary
silently compared the port against an older plugin. That bit once, for real. The shaders are copied character for character; the five simulations are a
hand port, checked by `demo/tools/check_sim.mjs` against a new `coinoptest --grid`
mode (one fixed config reduced to an FNV-1a digest of the playfield, plus ticks,
per-plane sums and a cell census). All five match byte for byte, **including
Drift**, which is all float trig and sub-cell wrapping and was the one expected to
diverge under JS doubles. It agrees at 478 ticks; nothing guarantees it stays
agreed over a longer run, and the page says so.

The checker found a real bug on its first honest run: the snake's tail gradient
was one shade level bright on two cells, because the C++ casts to `uint8_t`
(truncating) and the port used `Math.round`. See
[hand ported demo verification](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_hand_ported_demo_verification.md).

**It is the one repo in the fleet that breaks the stateless-generator rule**
that [orrery](https://github.com/stoatworks-labs/orrery/blob/main/docs/NOTES.md) (`orrery`) states ("no simulation state anywhere"). That rule is
right and a game cannot obey it, so `Sim` carries three named defences instead —
see AGENTS.md. The double-render guard is the load-bearing one: Resolume renders
the same frame to preview, output and thumbnail, so without it the ball moves at
double speed *only while the preview monitor is open*.

The eight new games cost **one** new cell type between them: `Cell::Enemy` with
its own `hazard` role added to all six palettes. Everything else reused
Wall/Food/Brick/Paddle/Head/Body. `Food` could not be reused for pursuers —
it is the palette's *target* role and a hazard means the opposite.

**Skill alone does not guarantee termination** and three games prove it, all
found by measuring: Stacker at Skill 1.0 cleared runs for 6000 ticks and never
ended (fixed by the piece falling >1 row per step past the interval floor);
Reflex is a reaction test the machine simply wins (fixed by the window closing
below the 2-tick reaction floor); Duel's two perfect fighters stand off forever
(fixed by a round tick limit). Girders had the mirror bug — a hop with no
cooldown was an invulnerability toggle and the level was won forever.

**The architecture rule worth keeping:** `coinop_sim` links with no graphics API
and no FFGL. `-DCOINOP_BUILD_PLUGINS=OFF` builds it alone and `tools/verify.sh`
runs that configuration every pass, specifically so the property cannot rot. It
is what lets `coinoptest` assert "the snake is 14 long" instead of counting green
pixels. Two harnesses: `coinoptest` (**268** checks, no GL) and `coinopgl` (**33**
checks, headless CGL — shader compile + rendered measurement). Both sweep every
game automatically from `GameId::Count`, so adding a game gets the generic
checks for free.

The state harness cannot see how a game *looks*: three real faults survived all
243 checks and were only found by rendering PPMs and looking — Swarm's ship
overdrew the bottom border, and Reflex and Duel left two-thirds of the playfield
empty. Always dump and look.

Distinct from **cartridge**, which was being built in a parallel session the same
evening: that one hosts real libretro cores and needs the operator to supply a
core and a ROM. coinop ships self-contained with no licensing surface.

Naming is deliberate: Breakout/Asteroids are Atari marks, Space Invaders is
Taito's. **The old "Tetris is deliberately absent" rule was superseded on
2026-08-06** — a stacker is in, as `Stacker`, because *Tetris Holding v. Xio*
(2012) held that the falling-block **rules** are an unprotectable idea and Xio
lost on **expression**: playfield dimensions, the seven pieces, their colours,
ghost piece, next-piece preview, garbage lines. So the working rule is now
**take the mechanic, build the expression from the plugin's own parts** —
Stacker uses ten mixed polyominoes (not the seven tetrominoes), clears a
contiguous **run** rather than a full row (which is also the only rule that
works on a 32-wide playfield), sizes the well from the Grid parameter, has no
ghost piece and no preview, and takes colour from the palette by role. See
AGENTS.md "Naming" and `source/games/Stacker.h`.


## v0.3.0, 2026-08-06 — Flapper, the fourteenth

One button against gravity through gaps in a scrolling wall. **Cost no new cell
type and no new parameter** — columns are `Wall`, the flier is `Head` — which is
the one-plugin bet paying off a second time. Released the same evening as v0.2.0,
signed, notarised and **built entirely on this Mac** because GitHub's hosted
runners would not pick up jobs (see **ci actions quota restored** (working-practice note, kept in Claude memory));
Windows x64 came from the Parallels guest ([windows 11 parallels vm](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_windows_11_parallels_vm.md)).

**Two faults passed all 266 checks and were found only by looking at frames.**
The first autopilot never passed a single column on any seed — it projected free
fall forward and so parked a braking distance *above* the target hole; dying in
bounds having touched nothing satisfies every state assertion there is. Then the
playfield scrolled in from the right over ~2s and did it again on every death,
because `LoseLife` cleared the columns. An empty playfield is a legal playfield.
`Score() > 0` and a populated-field check now exist; the rule "render it and look
at it" is in AGENTS.md. That is now **three** times in this repo (Swarm, Reflex/
Duel, Flapper) — see [hand ported demo verification](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_hand_ported_demo_verification.md).

**Termination could not come from Skill.** Holding one axis between two edges is
too easy to misjudge, so a competent flier never dies at a fixed difficulty.
Difficulty is a *ramp* — gap, scroll, spacing and drift all walk to floors at
which the next hole is unreachable — and coinoptest does that arithmetic on the
ramp's own constants.

**The demo port needed `Math.fround`, and it is the only one that does.** The
other thirteen match in plain JS doubles because they re-quantise to whole cells
constantly, so drift never reaches a cell index. Flapper's altitude and column
positions are continuous for the whole run, so a double and a float diverge until
one crosses a `round()` boundary — `0.12f + 0.10f * 0.5f` is 0.169999998 as a
float and 0.17 as a double, wrong before the first column existed. Round after
each individual operation, where the C++ rounds, and pre-round the constants.
Do NOT retrofit it to the other thirteen: they match as they stand.

Related: [ffgl sdk bugs](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_sdk_bugs.md), [ffgl audio bpm patterns](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_audio_bpm_patterns.md),
[idler](https://github.com/stoatworks-labs/idler/blob/main/docs/NOTES.md) (`idler`) (screensavers, same one-plugin-many-modes shape).


## v0.2.0 release, 2026-08-06 — two traps worth keeping

**A `v*` tag push did NOT trigger the release workflow.** Twice, on a repo whose
workflow has `on: push: tags: ['v*']`, with Actions enabled, the workflow active
and the tag confirmed on the remote — no run was ever created. Cause unidentified.
`gh workflow run release.yml --ref v0.2.0` works and satisfies the publish job's
`startsWith(github.ref, 'refs/tags/v')` gate, so a manual dispatch is the
workaround. **Check that a run actually appeared after pushing a tag.**

**`std::lround` needs `<cmath>` and libc++ hides that.** Five new games included
only `<algorithm>`, built fine on macOS, passed 243 checks, and failed to compile
on MSVC — after the mac job had gone green. Second failure of this exact shape in
the repo (NOMINMAX was the first). There is no Windows compiler on this Mac so
neither is catchable locally.

**The fleet's branding lockup is GONE from `~/Downloads`** and every
`video/projects/*/build.py` still points at it. Recovered an 881px transparent
copy by keying the navy out of coinop's v0.1.0 intro card; it now lives at
`stoatworks-backend/branding/lockup.png`. **Only coinop's build.py was
repointed** — the next project video cut will hit the same FileNotFoundError.
