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
# Every shader, through a real GLSL compiler, before a host has to find out.
#
# A shader that will not compile presents to an operator as "the effect does
# nothing", with the real message buried in the diagnostics log -- so without
# this it is caught at run time, in a host, or not at all.
#
# --target-env=opengl4.5 with -fauto-map-locations: glslc targets SPIR-V, which
# demands an explicit layout( location ) on every uniform and varying. Those are
# Vulkan rules and not GLSL ones, and without the flag every shader "fails" for
# reasons that have nothing to do with the code.
#
# glslc is optional -- `brew install shaderc` -- so a machine without it skips
# rather than fails.
#---------------------------------------------------------------------------
shaders_compile() {
	local dir bad=0 n=0 shader

	if ! command -v glslc >/dev/null 2>&1; then
		printf '   skipped: glslc not installed (brew install shaderc)\n'
		return 0
	fi

	dir="$( mktemp -d )"

	python3 - "$dir" <<'SHADERS_PY'
import re, sys, pathlib
out = pathlib.Path( sys.argv[ 1 ] )

# Where this repo keeps its GLSL.
FILES = [
	"source/Shaders.cpp",
]

# The effect build, which Coinop.cpp splices in when the plugin runs over an input.
VARIANTS = {
	"kCellShader": [
		( "effect", "#define COINOP_OVER_INPUT 1\n" ),
	],
}

named, unnamed = {}, []
for f in FILES:
	text = pathlib.Path( f ).read_text()
	for m in re.finditer( r'(?:(\w+)\s*(?:\[\s*\])?\s*=\s*)?R"\((.*?)\)"', text, re.S ):
		if m.group( 1 ): named[ m.group( 1 ) ] = m.group( 2 )
		else:            unnamed.append( m.group( 2 ) )
	for m in re.finditer( r'(\w+)\s*=\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+);', text ):
		named.setdefault( m.group( 1 ), "".join(
			s.encode().decode( "unicode_escape" )
			for s in re.findall( r'"((?:[^"\\\n]|\\.)*)"', m.group( 2 ) ) ) )

def emit( name, body ):
	# The vertex shader is the one that writes gl_Position; everything else is a
	# fragment shader. glslc takes the stage from the extension.
	ext = ".vert" if re.search( r"\bgl_Position\s*=", body ) else ".frag"
	( out / ( name + ext ) ).write_text( body )

for name, body in named.items():
	if body.lstrip().startswith( "#version" ) and "void main" in body:
		emit( name, body )
		for label, defines in VARIANTS.get( name, [] ):
			# The plugin splices these in after the #version line, which has to
			# stay first. Each build is a separate compile and can fail alone.
			head, rest = body.split( "\n", 1 )
			emit( name + "_" + label, head + "\n" + defines + rest )
SHADERS_PY

	for shader in "$dir"/*.vert "$dir"/*.frag; do
		[ -e "$shader" ] || continue
		n=$(( n + 1 ))
		if ! glslc --target-env=opengl4.5 -fauto-map-locations \
			   "$shader" -o /dev/null 2>"$dir/err"; then
			printf '   %s does not compile\n' "$( basename "$shader" )"
			sed "s|$dir/||; s|^|      |" "$dir/err"
			bad=$(( bad + 1 ))
		fi
	done

	if [ "$n" -eq 0 ]; then
		# No shaders at all is a FAILURE, not a pass. It means the extraction
		# above has lost track of where this repo keeps its GLSL, and a check
		# that silently looks at nothing is worse than no check.
		printf '   no shaders were extracted -- the extraction has gone stale\n'
		rm -rf "$dir"
		return 1
	fi

	if [ "$bad" -eq 0 ]; then
		printf '   %d shaders, all compile\n' "$n"
	fi
	rm -rf "$dir"
	return "$bad"
}

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
shaders_compile || fail "a shader does not compile"

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
#
# COINOP_HARNESS points it at the binary this script just built. Without it the
# checker falls back to build/coinoptest, which is whatever was left there last
# — and a stale harness compares the port against an older plugin and calls the
# difference a port bug.
if command -v node > /dev/null 2>&1; then
	COINOP_HARNESS="$BUILD/coinoptest" node demo/tools/check_sim.mjs
else
	printf '   --    node not installed, skipping the ported-simulation check\n'
fi

printf '\n\033[32mAll checks passed.\033[0m\n'
