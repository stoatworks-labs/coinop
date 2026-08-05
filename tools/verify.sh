#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the build
# log calls it a success either way -- so the architecture is checked with lipo,
# never with the log.
#
#     tools/verify.sh [BUILD_DIR]
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
echo "ok   FFGL SDK present at $(git -C external/ffgl rev-parse --short HEAD)"

#---------------------------------------------------------------------------
step "The simulation links without a graphics API"
#---------------------------------------------------------------------------
# The property that makes coinoptest possible. If this configuration ever fails
# to build, something has reached from the sim into the plugin, and the harness
# is one commit away from needing a GL context to test a game of Snake.
SIMONLY="$BUILD-simonly"
cmake -S . -B "$SIMONLY" -DCOINOP_BUILD_PLUGINS=OFF -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$SIMONLY" --target coinop_sim -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > /dev/null
echo "ok   coinop_sim builds with COINOP_BUILD_PLUGINS=OFF"

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" > /dev/null
echo "ok   built"

#---------------------------------------------------------------------------
step "Architecture"
#---------------------------------------------------------------------------
for bundle in "Coinop" "Coinop Over"; do
	bin="$BUILD/$bundle.bundle/Contents/MacOS/$bundle"
	[[ -f "$bin" ]] || fail "$bundle: binary missing"

	archs="$(lipo -archs "$bin")"
	[[ "$archs" == *"arm64"* ]] || fail "$bundle: no arm64 slice ($archs)"
	[[ "$archs" == *"x86_64"* ]] || fail "$bundle: no x86_64 slice ($archs)"
	echo "ok   $bundle: $archs"
done

#---------------------------------------------------------------------------
step "plugMain"
#---------------------------------------------------------------------------
# A bundle can load, export plugMain and still report that it contains no
# plugins -- that is what happens when the linker drops the translation unit
# holding CFFGLPluginInfo. Both symbols are checked for that reason.
for bundle in "Coinop" "Coinop Over"; do
	bin="$BUILD/$bundle.bundle/Contents/MacOS/$bundle"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`.
	# Under `set -o pipefail` a `grep -q` that finds its match exits
	# immediately, the writer upstream takes SIGPIPE, and the PIPELINE
	# reports failure even though the symbol is there. It is output-size
	# dependent, so it fires on the bigger binary first and looks
	# intermittent. A herestring is not a pipeline, so nothing can SIGPIPE.
	symbols=$( nm -gU "$bin" 2>/dev/null || true )
	grep -q plugMain <<<"$symbols" || fail "$bundle: plugMain not exported"
	echo "ok   $bundle: plugMain exported"
done

#---------------------------------------------------------------------------
step "Simulation (coinoptest)"
#---------------------------------------------------------------------------
"$BUILD/coinoptest"

#---------------------------------------------------------------------------
step "Shader (coinopgl)"
#---------------------------------------------------------------------------
"$BUILD/coinopgl"

#---------------------------------------------------------------------------
step "Browser demo"
#---------------------------------------------------------------------------
# demo/plugin.js carries this repo's shader text a second time, because a web
# page cannot include a C++ file. A change to Shaders.cpp that is not mirrored
# there is invisible until the demo behaves unlike the plugin.
python3 demo/tools/check_shaders.py

# And the ported SIMULATION, which is the larger gap here than in the other
# demos: the games are reimplemented in JavaScript, so coinoptest's assertions
# cannot reach them. `coinoptest --grid` runs every game under one fully
# specified configuration and prints a digest of the playfield; check_sim.mjs
# drives the JavaScript through the same sequence and diffs it.
#
# This is not decoration. It found the Snake tail gradient sitting one shade
# level bright on two cells, because the C++ casts to uint8_t (truncating) and
# the port used Math.round — invisible on screen, and exactly the kind of drift
# that makes a demo stop being evidence about the plugin.
if command -v node > /dev/null 2>&1; then
	node demo/tools/check_sim.mjs
else
	printf '   --    node not installed, skipping the ported-simulation check\n'
fi

printf '\n\033[32mAll checks passed.\033[0m\n'
