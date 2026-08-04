# coinop — command reference

`AGENTS.md` is the *why*. This file is how to build, test and install it.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Universal (arm64 + x86_64) by default. For a faster dev build:

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release
```

Simulation only, with no FFGL SDK and no graphics API — this configuration
existing and building is the guarantee that the games stay testable:

```bash
cmake -S . -B build-sim -DCOINOP_BUILD_PLUGINS=OFF
cmake --build build-sim -j8
```

## Verify

Everything, in the order that fails fastest:

```bash
tools/verify.sh
```

The two harnesses separately:

```bash
build/coinoptest --verbose
```

```bash
build/coinopgl --dump
```

`coinoptest` asserts on simulation state and never opens a GL context.
`coinopgl` compiles both shader variants in a headless CGL context, renders a
frame of every game and measures it. `--dump` writes a PPM per game.

## Install

Drops both bundles into the user's Resolume folder:

```bash
cmake --install build
```

Or point it somewhere else:

```bash
cmake --install build --prefix "/path/to/Extra Effects"
```

## Check a bundle by hand

```bash
lipo -archs build/Coinop.bundle/Contents/MacOS/Coinop
```

```bash
nm -gU build/Coinop.bundle/Contents/MacOS/Coinop | grep plugMain
```

## Logs

The plugin writes a rotating log via the fleet's `diag`. Override the location:

```bash
COINOP_LOG_DIR=/tmp/coinop-logs open -a "Resolume Arena"
```
