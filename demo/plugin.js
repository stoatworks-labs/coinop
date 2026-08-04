/**
 * Coinop — browser demo.
 *
 * `VERTEX_SHADER` and `CELL_SHADER` below are copied unedited from
 * `source/Shaders.cpp`, `#ifdef COINOP_OVER_INPUT` branches included — the two
 * bundles really are one shader compiled twice, and the picker in the transport
 * bar compiles it both ways here for the same reason.
 *
 * Everything else is a port of the simulation: `Rng.h`, `Grid.h`, `Input.h`,
 * `Sim.cpp`'s accumulator, `Controls.cpp`'s mappings, and one game class per
 * game from `source/games/`.
 *
 * **This plugin breaks the fleet's rule, knowingly, and so does this page.**
 * Everywhere else in this set a frame is a pure function of (something, phase).
 * A game cannot be written that way — Snake *is* accumulated state, and there is
 * no closed form for "where is the snake at t=91.3" that does not involve having
 * played the first 91.3 seconds. So `Sim` contains the damage with three
 * defences, all three of which are ported here because all three matter as much
 * in a browser tab as in a host:
 *
 *   1. Steps come from the clock, never from frames. A frame rate drop makes the
 *      game render less smoothly; it does not make it play slower.
 *   2. A frame with no elapsed time redraws and does not step.
 *   3. Catch-up is capped, so a backgrounded tab does not come back owing four
 *      hundred ticks and kill the snake before a pixel reaches the screen.
 *
 * The consequence for this page is worth stating plainly: **Step and Restart do
 * not mean here what they mean on the other demos.** There, any frame renders on
 * its own. Here, Step advances the simulation by one frame's worth of time and
 * cannot go back, and Restart starts a new game rather than rewinding this one.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, bindTexture } from './vendor/gl.js';

//===========================================================================
// The shaders. Copied from source/Shaders.cpp.
//===========================================================================

const VERTEX_SHADER = `#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
`;

const EFFECT_DEFINE = '#define COINOP_OVER_INPUT 1\n';

const CELL_SHADER = `#version 410 core

in vec2 uv;
out vec4 fragColor;

//---------------------------------------------------------------------------
// The playfield.
//
// CellTexture is GL_RGBA8UI and read with texelFetch, never with texture().
// A normalised byte texture would hand this shader 0..1 floats and recovering
// the cell type would mean int( t.r * 255.0 + 0.5 ) and trusting every driver
// to land on the same float. An integer texture reads back the byte that was
// written, which for a value that selects a branch is the only safe thing.
//---------------------------------------------------------------------------
uniform usampler2D CellTexture;
uniform vec2  GridSize;      //cells across, cells down
uniform vec2  Resolution;    //render target, pixels
uniform int   FitMode;       //0 fit, 1 fill, 2 stretch

uniform int   PaletteMode;
uniform float CellRound;     //0 square, 1 circle
uniform float CellGap;       //0 touching, 1 mostly gap
uniform float Glow;
uniform float Scanline;
uniform float Reactive;      //how much Intensity pushes the look
uniform float Intensity;     //0..1 from the game

uniform vec4  BackColor;

#ifdef COINOP_OVER_INPUT
uniform sampler2D InputTexture;
uniform vec2  MaxUV;
uniform float Mix;
uniform int   ClipBricks;    //1: bricks show the clip through themselves
#endif

//Cell types. These match the Cell enum in Grid.h and the numbers are a
//contract -- appending is safe, reordering is not.
const uint CELL_EMPTY  = 0u;
const uint CELL_WALL   = 1u;
const uint CELL_BODY   = 2u;
const uint CELL_HEAD   = 3u;
const uint CELL_FOOD   = 4u;
const uint CELL_BRICK  = 5u;
const uint CELL_PADDLE = 6u;
const uint CELL_BALL   = 7u;

//---------------------------------------------------------------------------
// Palettes. Six sets of five, indexed by role rather than by cell type, so a
// palette does not have to know what a Marcher is.
//
// Roles: 0 structure (walls), 1 primary (snake, paddle), 2 accent (head, ball),
// 3 target (food, bricks), 4 secondary (brick rows).
//---------------------------------------------------------------------------
vec3 paletteColor( int mode, int role, float tint )
{
	vec3 structure, primary, accent, target, secondary;

	if( mode == 0 )       //Phosphor
	{
		structure = vec3( 0.05, 0.30, 0.12 );
		primary   = vec3( 0.20, 0.95, 0.35 );
		accent    = vec3( 0.75, 1.00, 0.80 );
		target    = vec3( 0.95, 0.85, 0.25 );
		secondary = vec3( 0.10, 0.65, 0.30 );
	}
	else if( mode == 1 )  //Amber
	{
		structure = vec3( 0.30, 0.14, 0.02 );
		primary   = vec3( 1.00, 0.62, 0.10 );
		accent    = vec3( 1.00, 0.92, 0.70 );
		target    = vec3( 1.00, 0.35, 0.10 );
		secondary = vec3( 0.72, 0.40, 0.06 );
	}
	else if( mode == 2 )  //Ice
	{
		structure = vec3( 0.06, 0.16, 0.32 );
		primary   = vec3( 0.35, 0.75, 1.00 );
		accent    = vec3( 0.90, 0.98, 1.00 );
		target    = vec3( 0.60, 0.40, 1.00 );
		secondary = vec3( 0.20, 0.50, 0.85 );
	}
	else if( mode == 3 )  //Candy
	{
		structure = vec3( 0.22, 0.10, 0.28 );
		primary   = vec3( 1.00, 0.35, 0.65 );
		accent    = vec3( 1.00, 0.90, 0.45 );
		target    = vec3( 0.45, 1.00, 0.80 );
		secondary = vec3( 0.70, 0.35, 1.00 );
	}
	else if( mode == 4 )  //Mono
	{
		structure = vec3( 0.22 );
		primary   = vec3( 0.90 );
		accent    = vec3( 1.00 );
		target    = vec3( 0.62 );
		secondary = vec3( 0.45 );
	}
	else                  //Fire
	{
		structure = vec3( 0.20, 0.05, 0.02 );
		primary   = vec3( 1.00, 0.30, 0.05 );
		accent    = vec3( 1.00, 0.90, 0.55 );
		target    = vec3( 1.00, 0.72, 0.15 );
		secondary = vec3( 0.75, 0.12, 0.05 );
	}

	vec3 c = primary;
	if( role == 0 )      c = structure;
	else if( role == 2 ) c = accent;
	else if( role == 3 ) c = target;
	else if( role == 4 ) c = secondary;

	//Tint rotates between the primary and secondary ends of the palette, which
	//is how one brick field gets per-row colour without six more uniforms.
	if( tint > 0.0 )
		c = mix( c, secondary, clamp( tint, 0.0, 1.0 ) * 0.75 );

	return c;
}

int roleFor( uint type )
{
	if( type == CELL_WALL )   return 0;
	if( type == CELL_BODY )   return 1;
	if( type == CELL_PADDLE ) return 1;
	if( type == CELL_HEAD )   return 2;
	if( type == CELL_BALL )   return 2;
	if( type == CELL_FOOD )   return 3;
	if( type == CELL_BRICK )  return 4;
	return 1;
}

//---------------------------------------------------------------------------
// Playfield mapping. The grid's aspect comes from a parameter and the render
// target's from the host, and they are almost never the same -- see
// Controls.h for why the grid is deliberately not derived from the resolution.
//---------------------------------------------------------------------------
vec2 playfieldUV( vec2 p, out bool inside )
{
	float gridAR = GridSize.x / max( GridSize.y, 1.0 );
	float outAR  = Resolution.x / max( Resolution.y, 1.0 );

	if( FitMode == 2 )
	{
		inside = true;
		return p;
	}

	//Fit letterboxes, Fill crops. The only difference is which way the
	//comparison goes.
	bool wider = ( FitMode == 0 ) ? ( outAR > gridAR ) : ( outAR < gridAR );

	if( wider )
	{
		float s = gridAR / outAR;
		p.x = ( p.x - 0.5 ) / s + 0.5;
	}
	else
	{
		float s = outAR / gridAR;
		p.y = ( p.y - 0.5 ) / s + 0.5;
	}

	inside = all( greaterThanEqual( p, vec2( 0.0 ) ) ) &&
	         all( lessThan( p, vec2( 1.0 ) ) );
	return p;
}

//Signed distance to a rounded box, used for the cell shape. Negative inside.
float roundedBox( vec2 local, float halfSize, float radius )
{
	vec2 d = abs( local ) - vec2( halfSize - radius );
	return length( max( d, 0.0 ) ) + min( max( d.x, d.y ), 0.0 ) - radius;
}

//Coverage of one cell at a local position, 0 outside and 1 inside, with an
//edge softened by roughly a pixel so the shape does not alias.
float cellCoverage( vec2 local, float px )
{
	float halfSize = mix( 0.5, 0.22, clamp( CellGap, 0.0, 1.0 ) );
	float radius   = clamp( CellRound, 0.0, 1.0 ) * halfSize;
	float d        = roundedBox( local - 0.5, halfSize, radius );
	return 1.0 - smoothstep( -px, px, d );
}

void main()
{
	bool inside;
	vec2 p = playfieldUV( uv, inside );

	vec3 col = BackColor.rgb;
	float alpha = BackColor.a;

#ifdef COINOP_OVER_INPUT
	vec2 clipUV = uv * MaxUV;
	vec4 clip   = texture( InputTexture, clipUV );

	//The clip is underneath, the plugin's own background veils it, and the
	//playfield draws on top. Background Opacity therefore keeps exactly the
	//meaning it has in the source plugin.
	col   = mix( clip.rgb, BackColor.rgb, BackColor.a );
	alpha = max( clip.a, BackColor.a );
#endif

	if( inside )
	{
		//Row 0 of the cell texture is the top of the playfield; FFGL's origin
		//is bottom-left, so y flips here and nowhere else.
		vec2 cellF  = vec2( p.x, 1.0 - p.y ) * GridSize;
		ivec2 cell  = ivec2( floor( cellF ) );
		vec2 local  = fract( cellF );

		ivec2 maxCell = ivec2( GridSize ) - ivec2( 1 );
		cell = clamp( cell, ivec2( 0 ), maxCell );

		uvec4 c = texelFetch( CellTexture, cell, 0 );
		uint type = c.r;

		//Softening width in cell-local units: one output pixel, expressed in
		//the space \`local\` lives in. Without this the cells alias hard at small
		//grid sizes and shimmer when the playfield is scaled.
		float px = max( GridSize.x / max( Resolution.x, 1.0 ),
		                GridSize.y / max( Resolution.y, 1.0 ) ) * 0.5;
		px = clamp( px, 0.002, 0.5 );

		float boost = 1.0 + Intensity * Reactive * 1.4;

		if( type != CELL_EMPTY )
		{
			float shade = float( c.g ) / 255.0;
			float tint  = float( c.b ) / 5.0;

			vec3 cc = paletteColor( PaletteMode, roleFor( type ), tint );

			//Shade is a gradient within a type -- the snake's tail, a brick's
			//remaining hit points. Never taken to zero, or the tail tip
			//disappears entirely and the snake looks severed.
			cc *= mix( 0.35, 1.0, shade );
			cc *= boost;

#ifdef COINOP_OVER_INPUT
			//The reason the effect variant exists: the brick field is the clip.
			//Breaking a brick punches a hole through to the layer below.
			if( ClipBricks == 1 && type == CELL_BRICK )
				cc = clip.rgb * ( 0.65 + 0.35 * shade ) * boost;
#endif

			float cov = cellCoverage( local, px );
			col = mix( col, cc, cov );
			alpha = max( alpha, cov );
		}

		//---------------------------------------------------------------
		// Glow. Nine texelFetches of the neighbourhood, weighted by
		// distance. Cheap, and it is what stops a 32x24 playfield from
		// looking like a spreadsheet.
		//---------------------------------------------------------------
		if( Glow > 0.001 )
		{
			vec3 bloom = vec3( 0.0 );
			for( int dy = -1; dy <= 1; ++dy )
			{
				for( int dx = -1; dx <= 1; ++dx )
				{
					ivec2 n = clamp( cell + ivec2( dx, dy ), ivec2( 0 ), maxCell );
					uvec4 nc = texelFetch( CellTexture, n, 0 );
					if( nc.r == CELL_EMPTY )
						continue;

					vec2 centre = vec2( n - cell ) + vec2( 0.5 );
					float d = length( local - centre );
					float w = exp( -d * d * 1.6 );

					bloom += paletteColor( PaletteMode, roleFor( nc.r ),
					                       float( nc.b ) / 5.0 ) *
					         w * ( float( nc.g ) / 255.0 * 0.7 + 0.3 );
				}
			}

			col += bloom * Glow * 0.28 * boost;
			alpha = max( alpha, min( 1.0, length( bloom ) * Glow * 0.28 ) );
		}
	}

	//Scanlines, in output space rather than cell space -- they are a property
	//of the imagined display, not of the playfield, so they must not scale with
	//the grid.
	if( Scanline > 0.001 )
	{
		float line = 0.5 + 0.5 * cos( uv.y * Resolution.y * 3.14159265 );
		col *= mix( 1.0, 0.55 + 0.45 * line, clamp( Scanline, 0.0, 1.0 ) );
	}

#ifdef COINOP_OVER_INPUT
	col   = mix( clip.rgb, col, clamp( Mix, 0.0, 1.0 ) );
	alpha = mix( clip.a, alpha, clamp( Mix, 0.0, 1.0 ) );
#endif

	fragColor = vec4( col, clamp( alpha, 0.0, 1.0 ) );
}
`;

/// Insert a define after the `#version` line, which must come first.
function withDefines(shader, defines) {
  if (!defines) return shader;
  const afterVersion = shader.indexOf('\n');
  return afterVersion === -1
    ? shader
    : shader.slice(0, afterVersion + 1) + defines + shader.slice(afterVersion + 1);
}

//===========================================================================
// Port of source/Rng.h
//
// xorshift64*, in BigInt because it has to be exact in 64 bits. "Same seed, same
// game" is what makes the Seed parameter mean anything, and it is what lets a
// run somebody liked be got back — so this cannot be Math.random, and it cannot
// be a 53-bit approximation of the C++ either.
//===========================================================================

const U64 = (1n << 64n) - 1n;

class Rng {
  constructor(seed) {
    this.reseed(seed);
  }

  /// A zero state is xorshift's one fixed point — it emits zero forever. Seed 0
  /// is exactly the seed a user leaves the slider on, so it is remapped rather
  /// than left to produce a game where the food never moves.
  reseed(seed) {
    this.state = BigInt.asUintN(64, seed) || 0x9e3779b97f4a7c15n;
  }

  next() {
    let s = this.state;
    s ^= s >> 12n;
    s = (s ^ (s << 25n)) & U64;
    s ^= s >> 27n;
    this.state = s;
    return (s * 0x2545f4914f6cdd1dn) & U64;
  }

  /**
   * Uniform in [0, n). Rejection rather than `% n`, because modulo is biased
   * toward low values whenever n does not divide 2^32 — and n here is a grid
   * width, which never does. The bias is small, but it is a bias in *where the
   * food appears*, which is the one thing a player would notice over a long run.
   */
  below(n) {
    if (n === 0) return 0;
    const bn = BigInt(n >>> 0);
    const limit = ((1n << 32n) % bn); // 2^32 mod n, as C++'s uint32(-int32(n)) % n
    let v;
    do {
      v = this.next() >> 32n;
    } while (v < limit);
    return Number(v % bn);
  }

  /// Uniform in [0, 1). 24 bits, which is every value a float can hold in that
  /// range without repeats.
  unit() {
    return Number(this.next() >> 40n) * (1 / 16777216);
  }

  range(lo, hi) {
    return lo + (hi - lo) * this.unit();
  }

  chance(p) {
    return this.unit() < p;
  }
}

//===========================================================================
// Port of source/Controls.cpp
//===========================================================================

const clamp01 = (v) => Math.min(1, Math.max(0, v));
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

const FIT_NAMES = ['Fit', 'Fill', 'Stretch'];
const ASPECT_NAMES = ['16:9', '4:3', '1:1', '32:9', '9:16'];
const PALETTE_NAMES = ['Phosphor', 'Amber', 'Ice', 'Candy', 'Mono', 'Fire'];
const GAME_NAMES = ['Snake', 'Bricks', 'Marchers', 'Rally', 'Drift'];

/// 12 to 128, squared. A pixel map is usually at the coarse end and that is
/// where the slider needs its resolution.
const gridWidth = (t) => Math.round(12 + 116 * clamp01(t) * clamp01(t));

function gridHeight(width, aspect) {
  let ratio = 9 / 16;
  if (aspect === 1) ratio = 3 / 4;
  else if (aspect === 2) ratio = 1;
  else if (aspect === 3) ratio = 9 / 32;
  else if (aspect === 4) ratio = 16 / 9;
  // Floor of 8 rows. Below that Marchers has nowhere to put a formation and
  // Snake has no room to turn, and both would spend their whole life dying.
  return clamp(Math.round(width * ratio), 8, 256);
}

/**
 * Spread the 4096 steps across the 64-bit space rather than using them raw.
 * Adjacent seeds should give unrelated games; 1 and 2 fed straight into
 * xorshift give streams that are visibly related for the first few draws, which
 * shows up as two layers on neighbouring seeds putting the first apple in nearly
 * the same place.
 */
function seedValue(t) {
  const step = BigInt(Math.round(clamp01(t) * 4095));
  let z = (step * 0x9e3779b97f4a7c15n + 0xda3e39cb94b95bdbn) & U64;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & U64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & U64;
  return (z ^ (z >> 31n)) & U64;
}

//===========================================================================
// Port of source/Grid.h
//
// Four bytes per cell, uploaded verbatim as RGBA8UI and read in GLSL with a
// usampler2D and texelFetch. An integer texture and not a normalised one,
// deliberately: recovering a cell type from a 0..1 float means trusting every
// driver's normalise to land on the same value, and when it does not, one cell
// type is silently mistaken for its neighbour.
//===========================================================================

const Cell = {
  Empty: 0,
  Wall: 1,
  Body: 2,
  Head: 3,
  Food: 4,
  Brick: 5,
  Paddle: 6,
  Ball: 7,
};

class Grid {
  constructor() {
    this.w = 0;
    this.h = 0;
    this.cells = new Uint8Array(0);
  }

  resize(w, h) {
    if (w === this.w && h === this.h) return;
    this.w = w;
    this.h = h;
    this.cells = new Uint8Array(w * h * 4);
  }

  /// Wipe to Empty. Keeps the allocation — this runs every tick.
  clear() {
    this.cells.fill(0);
  }

  inBounds(x, y) {
    return x >= 0 && y >= 0 && x < this.w && y < this.h;
  }

  /// Out-of-bounds writes are dropped rather than asserted. A game that
  /// rasterises a paddle straddling the edge, or a ball mid-escape on the tick
  /// before the miss is detected, is not a bug worth crashing a live show over.
  set(x, y, type, shade = 0, tint = 0, flash = 0) {
    x |= 0;
    y |= 0;
    if (!this.inBounds(x, y)) return;
    const i = (y * this.w + x) * 4;
    this.cells[i] = type;
    this.cells[i + 1] = shade;
    this.cells[i + 2] = tint;
    this.cells[i + 3] = flash;
  }

  typeAt(x, y) {
    if (!this.inBounds(x, y)) return Cell.Wall;
    return this.cells[(y * this.w + x) * 4];
  }
}

//===========================================================================
// Port of source/Input.h
//
// FFGL has no input events — it has parameters, and you poll them. A press and
// release that both happen between two frames is invisible to a poll, and at
// 50fps that is a 20 ms window a drummer hits constantly. So presses are latched
// when they arrive and drained once per tick, and it is a QUEUE rather than a
// counter because order matters: Left then Up must turn left and then up.
//===========================================================================

const Button = { Left: 0, Right: 1, Up: 2, Down: 3, Fire: 4, Reset: 5 };

class Input {
  constructor() {
    this.queue = [];
    this.held = 0;
    this.axis = 0.5;
  }

  press(b) {
    // A full queue drops the oldest, not the newest: the recent intent is what
    // the player still means.
    if (this.queue.length >= 64) this.queue.shift();
    this.queue.push(b);
  }

  pop() {
    return this.queue.length ? this.queue.shift() : null;
  }

  clear() {
    this.queue.length = 0;
  }

  setHeld(b, down) {
    const bit = 1 << b;
    this.held = down ? this.held | bit : this.held & ~bit;
  }

  isHeld(b) {
    return (this.held & (1 << b)) !== 0;
  }
}

//===========================================================================
// Port of source/Vec.h
//===========================================================================

const Dir = { Up: 0, Right: 1, Down: 2, Left: 3 };

function ahead(p, d) {
  if (d === Dir.Up) return { x: p.x, y: p.y - 1 };
  if (d === Dir.Right) return { x: p.x + 1, y: p.y };
  if (d === Dir.Down) return { x: p.x, y: p.y + 1 };
  return { x: p.x - 1, y: p.y };
}

const turnLeft = (d) => (d + 3) % 4;
const turnRight = (d) => (d + 1) % 4;
const opposite = (d) => (d + 2) % 4;

//===========================================================================
// Port of source/games/Snake.cpp
//
// The reference game: the one that is genuinely a grid all the way down, so
// nothing here has to be reconciled with anything continuous.
//
// The autopilot has to be beatable. A perfect Snake AI exists — follow a
// Hamiltonian cycle and it never dies — and it is unwatchable, and for this
// plugin it never resets, so the layer shows the same slowly-lengthening snake
// for the whole show. So it is greedy-toward-food gated by a survivability
// check, with Skill governing how often it bothers to run the check.
//===========================================================================

class Snake {
  reset(cfg, rng) {
    this.w = cfg.gridW;
    this.h = cfg.gridH;

    this.body = [];
    this.turns = [];
    this.visited = new Uint8Array(this.w * this.h);

    // Start mid-left facing right, three long. Three and not one because a
    // one-cell snake has no body to collide with, so the first few seconds would
    // not exercise the rule that kills it.
    const cy = Math.floor(this.h / 2);
    const cx = Math.max(3, Math.floor(this.w / 4));
    this.body.push({ x: cx, y: cy });
    this.body.push({ x: cx - 1, y: cy });
    this.body.push({ x: cx - 2, y: cy });

    this.dir = Dir.Right;
    this.lastDir = Dir.Right;
    this.grow = 0;
    this.score = 0;
    this.dead = false;
    this.ticksSinceFood = 0;
    this.food = { x: 0, y: 0 };

    this.placeFood(rng);
  }

  /// Playable area, inset by the one-cell wall border.
  minX() { return 1; }
  minY() { return 1; }
  maxX() { return this.w - 2; }
  maxY() { return this.h - 2; }

  /**
   * Four to twenty-two cells a second. The curve is squared because the
   * interesting half of that range is the slow half — the difference between 4
   * and 7 is a different-feeling game, the difference between 18 and 22 is
   * nothing anybody can follow.
   */
  tickHz(cfg) {
    const t = clamp01(cfg.speed);
    return 4 + 18 * t * t;
  }

  blocked(p, ignoreTail) {
    if (p.x < this.minX() || p.y < this.minY() || p.x > this.maxX() || p.y > this.maxY()) {
      return true;
    }

    // The tail cell vacates on the same tick the head arrives, so moving into it
    // is legal — unless the snake is about to grow, in which case it does not
    // vacate. Getting this wrong costs one length of slack in every corner and
    // makes the autopilot far more timid than it needs to be.
    const last = this.body.length ? this.body.length - 1 : 0;
    for (let i = 0; i < this.body.length; i += 1) {
      if (ignoreTail && i === last && this.grow === 0) continue;
      if (this.body[i].x === p.x && this.body[i].y === p.y) return true;
    }

    return false;
  }

  placeFood(rng) {
    const playW = this.maxX() - this.minX() + 1;
    const playH = this.maxY() - this.minY() + 1;
    if (playW <= 0 || playH <= 0) return;

    // Rejection sampling, capped. On a nearly-full board the cap can expire, so
    // there is a deterministic sweep behind it — without that, a long snake
    // makes this loop run for an unbounded time on the render thread.
    for (let attempt = 0; attempt < 256; attempt += 1) {
      const p = { x: this.minX() + rng.below(playW), y: this.minY() + rng.below(playH) };
      if (!this.blocked(p, false)) {
        this.food = p;
        return;
      }
    }

    for (let y = this.minY(); y <= this.maxY(); y += 1) {
      for (let x = this.minX(); x <= this.maxX(); x += 1) {
        const p = { x, y };
        if (!this.blocked(p, false)) {
          this.food = p;
          return;
        }
      }
    }

    // Board full: the snake has won. Nothing left to place.
    this.food = { ...this.body[0] };
  }

  /**
   * Free cells reachable from `from`. Used to reject a move that seals the snake
   * into a pocket smaller than itself — the trap that kills every naive greedy
   * Snake AI within thirty seconds.
   */
  reachableSpace(from) {
    this.visited.fill(0);

    if (from.x < this.minX() || from.y < this.minY() || from.x > this.maxX() || from.y > this.maxY()) {
      return 0;
    }

    // Iterative flood fill with an explicit stack, as in the plugin: recursion
    // would be a 768-deep call chain on a snaking corridor.
    const stack = [from];
    let count = 0;

    while (stack.length) {
      const p = stack.pop();

      if (p.x < this.minX() || p.y < this.minY() || p.x > this.maxX() || p.y > this.maxY()) continue;

      const idx = p.y * this.w + p.x;
      if (this.visited[idx]) continue;
      if (this.blocked(p, true)) continue;

      this.visited[idx] = 1;
      count += 1;

      for (let d = 0; d < 4; d += 1) stack.push(ahead(p, d));
    }

    return count;
  }

  chooseAutopilot(cfg, rng) {
    const forward = this.lastDir;
    const options = [forward, turnLeft(forward), turnRight(forward)];
    const skill = clamp01(cfg.skill);

    // A flat chance of ignoring the analysis entirely. This is the whole reason
    // the autopilot is beatable: at skill 1.0 it never fires, and below that it
    // is what eventually walks the snake into a wall so the layer gets a fresh
    // game. Deliberate incompetence, not a fallback.
    if (rng.chance((1 - skill) * 0.18)) return options[rng.below(3)];

    let bestScore = -1;
    let best = forward;
    let found = false;

    for (const d of options) {
      const next = ahead(this.body[0], d);
      if (this.blocked(next, true)) continue;

      // Manhattan distance to the food, inverted so bigger is better.
      const dist = Math.abs(next.x - this.food.x) + Math.abs(next.y - this.food.y);
      let score = this.w + this.h - dist;

      // The survivability gate. A move that leaves less open space than the
      // snake is long is a move into a pocket it cannot get out of, and it is
      // how every greedy Snake AI dies. Skill decides whether it is consulted,
      // so a mediocre autopilot walks into the trap a good one sees.
      if (rng.chance(skill)) {
        const space = this.reachableSpace(next);
        if (space < this.body.length) score -= 1000;
        else score += Math.min(space, 200);
      }

      if (!found || score > bestScore) {
        bestScore = score;
        best = d;
        found = true;
      }
    }

    // Every direction is fatal. Carry straight on and take it — there is nothing
    // better available and the death is what restarts the game.
    return found ? best : forward;
  }

  step(cfg, input, rng) {
    if (this.dead || !this.body.length) return;

    // Drain the queue into turns. Directions are absolute (a pad with four
    // buttons); Fire also works as a relative right turn for two-button play.
    for (let b = input.pop(); b !== null; b = input.pop()) {
      if (b === Button.Up) this.turns.push(Dir.Up);
      else if (b === Button.Down) this.turns.push(Dir.Down);
      else if (b === Button.Left) this.turns.push(Dir.Left);
      else if (b === Button.Right) this.turns.push(Dir.Right);
      else if (b === Button.Fire) {
        this.turns.push(turnRight(this.turns.length ? this.turns[this.turns.length - 1] : this.lastDir));
      }
    }

    if (cfg.autopilot) {
      this.turns.length = 0;
      this.dir = this.chooseAutopilot(cfg, rng);
    } else if (this.turns.length) {
      // One turn per tick, validated against the direction actually last
      // travelled — not against whatever the previous queued turn was. Guarding
      // against the current direction does not work: at the moment Left is
      // applied the direction is already Up, so Left passes the guard and the
      // snake eats its own neck for no visible reason.
      const want = this.turns.shift();
      if (want !== opposite(this.lastDir)) this.dir = want;

      // Cap the backlog. A held-down MIDI note repeating at 100 Hz would queue
      // turns faster than a 6 Hz tick can spend them.
      if (this.turns.length > 4) this.turns = this.turns.slice(-4);
    }

    const next = ahead(this.body[0], this.dir);
    this.lastDir = this.dir;

    if (this.blocked(next, true)) {
      this.dead = true;
      return;
    }

    this.body.unshift(next);

    if (next.x === this.food.x && next.y === this.food.y) {
      this.score += 1;
      this.grow += 2;
      this.ticksSinceFood = 0;

      // Board full — treat it as a finish so the respawn timer restarts the
      // layer, rather than leaving a solid rectangle on screen for the rest of
      // the show.
      const playCells = (this.maxX() - this.minX() + 1) * (this.maxY() - this.minY() + 1);
      if (this.body.length >= playCells) {
        this.dead = true;
        return;
      }

      this.placeFood(rng);
    } else {
      this.ticksSinceFood += 1;
    }

    if (this.grow > 0) this.grow -= 1;
    else this.body.pop();

    // A snake that has not eaten in a very long time is stuck in a loop the
    // autopilot cannot break. Ending it is better than an immortal layer that
    // stopped changing.
    if (this.ticksSinceFood > this.w * this.h * 2) this.dead = true;
  }

  draw(cfg, grid) {
    grid.clear();

    for (let x = 0; x < this.w; x += 1) {
      grid.set(x, 0, Cell.Wall);
      grid.set(x, this.h - 1, Cell.Wall);
    }
    for (let y = 0; y < this.h; y += 1) {
      grid.set(0, y, Cell.Wall);
      grid.set(this.w - 1, y, Cell.Wall);
    }

    grid.set(this.food.x, this.food.y, Cell.Food, 255);

    // Tail gradient. Drawn back-to-front so the head wins where the body
    // overlaps it on the tick it dies.
    const n = this.body.length;
    for (let i = n - 1; i >= 0; i -= 1) {
      const t = n > 1 ? i / (n - 1) : 0;
      const shade = Math.round(255 * (1 - t));
      grid.set(this.body[i].x, this.body[i].y, i === 0 ? Cell.Head : Cell.Body, shade);
    }
  }

  finished() {
    return this.dead;
  }

  /// Length as a fraction of a board-filling snake, softened. Gives the shader
  /// something that climbs through a run and resets when it dies.
  intensity() {
    const playCells = Math.max(1, (this.w - 2) * (this.h - 2));
    return Math.min(1, (this.body.length / playCells) * 4);
  }
}

//===========================================================================
// Port of source/games/Bricks.cpp
//
// The one that is continuous inside a discrete world: a ball that only ever sits
// on cell centres can travel at four angles, and a paddle that snaps to cells
// cannot be steered with a fader — so the ball and paddle live in float
// playfield coordinates and are rasterised at draw time. The bricks stay a grid,
// because they genuinely are one.
//
// Three traps, all of which ship in first drafts of this game, and all three are
// ported rather than smoothed over: tunnelling (movement is substepped to a
// quarter cell), the horizontal lock (vy is held above a floor), and the sticky
// corner (x and y are resolved separately).
//===========================================================================

const MIN_VERTICAL_SPEED = 0.35;
/// Nonzero so that a perfectly centred return cannot be perfectly vertical.
const MIN_PADDLE_OFFSET = 0.18;
const BRICK_WIDTH = 2;

class Bricks {
  /**
   * Fixed and high. Unlike Snake, Speed here scales the ball's velocity rather
   * than the tick rate — a ball advanced 8 times a second is a ball that
   * visibly teleports, however fast it is nominally travelling.
   */
  tickHz() {
    return 90;
  }

  brickCols() { return Math.floor((this.w - 2) / BRICK_WIDTH); }
  brickRow(y) { return y - this.brickTop; }
  brickIndex(col, row) { return row * this.brickCols() + col; }

  reset(cfg, rng) {
    this.w = cfg.gridW;
    this.h = cfg.gridH;

    this.paddleY = this.h - 3;
    this.paddleHalf = Math.max(1.5, this.w * 0.09);
    this.paddleX = this.w * 0.5;

    this.score = 0;
    this.lives = 3;
    this.level = 0;
    this.flash = 0;

    this.ball = { x: 0, y: 0 };
    this.vel = { x: 0, y: 0 };
    this.aiTarget = 0;
    this.aiCooldown = 0;
    this.brickTop = 2;
    this.brickRows = 5;
    this.bricks = [];
    this.tint = [];

    this.buildField(cfg, rng);
    this.launchBall(rng);
  }

  buildField(cfg, rng) {
    const cols = Math.max(1, this.brickCols());

    this.brickTop = 2;
    this.brickRows = clamp(Math.trunc(3 + cfg.difficulty * 5), 3, Math.max(3, Math.floor(this.h / 3)));

    this.bricks = new Uint8Array(cols * this.brickRows);
    this.tint = new Uint8Array(cols * this.brickRows);

    for (let row = 0; row < this.brickRows; row += 1) {
      for (let col = 0; col < cols; col += 1) {
        const i = this.brickIndex(col, row);

        // Higher rows are tougher, which gives the field a shape as it erodes
        // rather than dissolving uniformly. One extra hit point is plenty — two
        // makes the first minute a grind.
        this.bricks[i] = row < Math.floor(this.brickRows / 3) ? 2 : 1;
        this.tint[i] = row % 6;

        // A few gaps, seeded. Purely cosmetic, but a perfectly solid wall looks
        // generated and a slightly holed one looks played-in.
        if (rng.chance(0.06)) this.bricks[i] = 0;
      }
    }
  }

  launchBall(rng) {
    this.ball = { x: this.paddleX, y: this.paddleY - 1 };
    this.waiting = true;
    this.waitTicks = 0;

    // Always upward, at a randomised angle well clear of vertical — a ball
    // launched at 90 degrees just goes up and comes back down the same column,
    // which looks broken even though it is correct.
    const angle = rng.range(0.6, 1.1) * (rng.chance(0.5) ? 1 : -1);
    this.vel = { x: Math.sin(angle), y: -Math.cos(angle) };
  }

  /// Returns the brick index, or -1.
  brickAt(x, y) {
    const row = this.brickRow(y);
    if (row < 0 || row >= this.brickRows) return -1;
    if (x < 1 || x >= this.w - 1) return -1;

    const col = Math.floor((x - 1) / BRICK_WIDTH);
    if (col < 0 || col >= this.brickCols()) return -1;

    const idx = this.brickIndex(col, row);
    if (idx < 0 || idx >= this.bricks.length || this.bricks[idx] === 0) return -1;
    return idx;
  }

  /// Trap 2. Preserve speed, force the vertical component above a floor.
  clampVertical() {
    const speed = Math.sqrt(this.vel.x * this.vel.x + this.vel.y * this.vel.y);
    if (speed <= 0.0001) {
      this.vel = { x: 0, y: -1 };
      return;
    }

    const minVy = speed * MIN_VERTICAL_SPEED;
    if (Math.abs(this.vel.y) < minVy) {
      this.vel.y = this.vel.y < 0 ? -minVy : minVy;

      // Rescale x so the ball does not speed up as a side effect of being
      // straightened out.
      const remaining = speed * speed - this.vel.y * this.vel.y;
      const newVx = Math.sqrt(Math.max(0, remaining));
      this.vel.x = this.vel.x < 0 ? -newVx : newVx;
    }
  }

  reflectOffPaddle() {
    let offset = clamp((this.ball.x - this.paddleX) / this.paddleHalf, -1, 1);
    const speed = Math.sqrt(this.vel.x * this.vel.x + this.vel.y * this.vel.y);

    // The vertical lock, and the one that only shows up once the autopilot is
    // good. A ball returned from the exact centre leaves straight up; if the
    // column above is cleared it hits the ceiling, comes straight back down the
    // same column, and the paddle — which has had a full traversal to centre
    // itself perfectly on it — returns it at offset 0 again. The better the
    // paddle, the more perfectly the loop closes. Measured before this guard:
    // at Skill 1.0 the ball hit 14 bricks in 74 minutes and the game never
    // ended.
    if (Math.abs(offset) < MIN_PADDLE_OFFSET) {
      offset = this.vel.x < 0 ? -MIN_PADDLE_OFFSET : MIN_PADDLE_OFFSET;
    }

    // Angle from where on the paddle it landed — the control that makes the
    // game playable rather than a coin flip. 60 degrees at the tips.
    const angle = offset * 1.05;
    this.vel = { x: Math.sin(angle) * speed, y: -Math.abs(Math.cos(angle) * speed) };
    this.clampVertical();
  }

  /**
   * Straight-line prediction with wall reflections, ignoring bricks. Bricks
   * would change the answer, but the autopilot re-predicts constantly, so being
   * wrong about a brick two seconds out costs nothing.
   */
  predictLandingX() {
    if (this.vel.y <= 0) return this.paddleX;

    const dist = this.paddleY - 1 - this.ball.y;
    if (dist <= 0) return this.ball.x;

    const x = this.ball.x + this.vel.x * (dist / this.vel.y);

    // Fold the unbounded x back into the playfield — a triangle wave over the
    // playable width, which is exactly what repeated wall reflections do.
    const lo = 1;
    const hi = this.w - 1;
    const span = Math.max(1, hi - lo);

    let t = (x - lo) % (span * 2);
    if (t < 0) t += span * 2;
    if (t > span) t = span * 2 - t;

    return lo + t;
  }

  step(cfg, input, rng) {
    if (this.finished()) return;

    if (this.flash > 0) this.flash = this.flash > 24 ? this.flash - 24 : 0;

    const hz = this.tickHz(cfg);
    const dt = 1 / hz;

    // --- Paddle ---------------------------------------------------------
    if (cfg.autopilot) {
      const skill = clamp01(cfg.skill);

      // Reaction lag: re-aim every few ticks rather than every tick. A paddle
      // that tracks perfectly every frame reads as a machine; one that commits
      // to a target and corrects looks like someone playing.
      this.aiCooldown -= 1;
      if (this.aiCooldown <= 0) {
        this.aiCooldown = Math.trunc(2 + (1 - skill) * 14);

        // Error scaled by skill, and by how far the ball still has to travel —
        // misjudging a ball that is about to arrive is what actually loses a
        // life, so the error is largest early.
        const travel = Math.max(0, this.paddleY - this.ball.y) / this.h;
        const err = (1 - skill) * this.paddleHalf * 2.2 * (0.3 + travel);

        this.aiTarget = this.predictLandingX() + rng.range(-err, err);
      }

      // Move at a finite speed. An autopilot that teleports the paddle never
      // misses regardless of how bad its aim is, which makes Skill do nothing.
      const maxStep = (14 + skill * 26) * dt;
      const delta = clamp(this.aiTarget - this.paddleX, -maxStep, maxStep);
      this.paddleX += delta;
    } else {
      // The axis is the good control here: a fader or an OSC float maps straight
      // onto paddle position across the playfield.
      const want = clamp01(input.axis);
      this.paddleX = this.paddleHalf + want * (this.w - 2 * this.paddleHalf);

      // Buttons still nudge it, for surfaces with no fader to spare.
      for (let b = input.pop(); b !== null; b = input.pop()) {
        if (b === Button.Left) this.paddleX -= 1.5;
        else if (b === Button.Right) this.paddleX += 1.5;
        else if (b === Button.Fire) this.waiting = false;
      }
    }

    this.paddleX = clamp(this.paddleX, this.paddleHalf + 1, this.w - 1 - this.paddleHalf);

    // --- Ball -----------------------------------------------------------
    if (this.waiting) {
      this.ball = { x: this.paddleX, y: this.paddleY - 1 };

      // Autoplay launches itself after a beat; a human gets to wait. Either way
      // there is a cap, so a layer left alone never sits on a held ball.
      this.waitTicks += 1;
      if (this.waitTicks > Math.trunc(hz * (cfg.autopilot ? 0.6 : 3))) this.waiting = false;

      return;
    }

    const speed = (9 + cfg.speed * 22) * (0.7 + cfg.difficulty * 0.9) * (1 + this.level * 0.16);
    let remaining = speed * dt;

    // Trap 1. Never advance more than a quarter cell between collision tests.
    while (remaining > 0) {
      const stepLen = Math.min(remaining, 0.25);
      remaining -= stepLen;

      const vlen = Math.sqrt(this.vel.x * this.vel.x + this.vel.y * this.vel.y);
      if (vlen < 0.0001) break;

      const sx = (this.vel.x / vlen) * stepLen;
      const sy = (this.vel.y / vlen) * stepLen;

      // Trap 3. X and Y resolved separately.
      {
        const nx = this.ball.x + sx;
        const cx = Math.floor(nx);
        const cy = Math.floor(this.ball.y);

        const brick = this.brickAt(cx, cy);
        if (cx < 1 || cx >= this.w - 1) {
          this.vel.x = -this.vel.x;
        } else if (brick >= 0) {
          this.bricks[brick] -= 1;
          this.score += 1;
          this.flash = 255;
          this.vel.x = -this.vel.x;
        } else {
          this.ball.x = nx;
        }
      }

      {
        const ny = this.ball.y + sy;
        const cx = Math.floor(this.ball.x);
        const cy = Math.floor(ny);

        const brick = this.brickAt(cx, cy);
        if (cy < 1) {
          this.vel.y = -this.vel.y;
        } else if (brick >= 0) {
          this.bricks[brick] -= 1;
          this.score += 1;
          this.flash = 255;
          this.vel.y = -this.vel.y;
        } else if (
          this.vel.y > 0
          && cy >= this.paddleY
          && this.ball.y < this.paddleY
          && Math.abs(this.ball.x - this.paddleX) <= this.paddleHalf + 0.5
        ) {
          this.ball.y = this.paddleY - 0.01;
          this.reflectOffPaddle();
        } else {
          this.ball.y = ny;
        }
      }

      this.clampVertical();

      if (this.ball.y > this.h) {
        this.lives -= 1;
        if (this.lives > 0) this.launchBall(rng);
        return;
      }
    }

    // --- Level clear ----------------------------------------------------
    if (this.bricksLeft() === 0) {
      this.level += 1;
      this.buildField(cfg, rng);
      this.launchBall(rng);
    }
  }

  bricksLeft() {
    let n = 0;
    for (const hp of this.bricks) if (hp > 0) n += 1;
    return n;
  }

  draw(cfg, grid) {
    grid.clear();

    for (let y = 0; y < this.h; y += 1) {
      grid.set(0, y, Cell.Wall);
      grid.set(this.w - 1, y, Cell.Wall);
    }
    for (let x = 0; x < this.w; x += 1) grid.set(x, 0, Cell.Wall);

    // Bricks. Each brick is BRICK_WIDTH cells wide and one tall, so the shader
    // gets cell-accurate edges and the effect variant samples the clip exactly
    // under each surviving cell.
    const cols = this.brickCols();
    for (let row = 0; row < this.brickRows; row += 1) {
      for (let col = 0; col < cols; col += 1) {
        const hp = this.bricks[this.brickIndex(col, row)];
        if (hp === 0) continue;

        const tint = this.tint[this.brickIndex(col, row)];
        for (let k = 0; k < BRICK_WIDTH; k += 1) {
          grid.set(1 + col * BRICK_WIDTH + k, this.brickTop + row, Cell.Brick, (hp * 100) & 255, tint);
        }
      }
    }

    const px0 = Math.floor(this.paddleX - this.paddleHalf);
    const px1 = Math.ceil(this.paddleX + this.paddleHalf);
    for (let x = px0; x <= px1; x += 1) grid.set(x, this.paddleY, Cell.Paddle, 255);

    grid.set(Math.floor(this.ball.x), Math.floor(this.ball.y), Cell.Ball, 255, 0);
  }

  finished() {
    return this.lives <= 0;
  }

  /// Rises as the field empties, plus a kick on every hit.
  intensity() {
    if (!this.bricks.length) return 0;
    const cleared = 1 - this.bricksLeft() / this.bricks.length;
    return Math.min(1, cleared * 0.7 + (this.flash / 255) * 0.5);
  }
}

/// Which games this page carries. See the note in `differences`: the plugin has
/// five and this port currently has the ones listed here.
const GAMES = {
  0: () => new Snake(),
  1: () => new Bricks(),
};

const PORTED_GAME_NAMES = Object.keys(GAMES).map((i) => GAME_NAMES[i]);

//===========================================================================
// Port of source/Sim.cpp — the fixed-timestep driver.
//===========================================================================

const MAX_ELAPSED = 0.25;
const MAX_TICKS_PER_FRAME = 16;

class Sim {
  constructor() {
    this.id = 0;
    this.game = null;
    // GameConfig's own defaults, because `SetGame` restarts before the plugin
    // has pushed a config and the C++ member is default-constructed rather than
    // absent.
    this.cfg = {
      gridW: 32,
      gridH: 24,
      seed: 1n,
      speed: 0.5,
      skill: 0.6,
      autopilot: true,
      difficulty: 0.5,
      respawnTicks: 45,
    };
    this.grid = new Grid();
    this.rng = new Rng(1n);

    this.clock = -1;
    this.accum = 0;
    this.ticks = 0;
    this.lastFrameTicks = 0;
    this.deadTicks = 0;
  }

  setGame(id) {
    if (this.game && id === this.id) return;
    this.id = id;
    this.game = (GAMES[id] ?? GAMES[0])();
    this.restart();
  }

  /**
   * Structural changes restart; everything else is picked up live. The
   * distinction matters in performance: Speed and Skill are the two an operator
   * actually rides during a show, and a reset on every fader move would make
   * them unusable.
   */
  configure(cfg) {
    const structural = !this.cfg
      || cfg.gridW !== this.cfg.gridW
      || cfg.gridH !== this.cfg.gridH
      || cfg.seed !== this.cfg.seed;

    this.cfg = cfg;
    this.grid.resize(cfg.gridW, cfg.gridH);

    if (structural || !this.game) this.restart();
  }

  restart() {
    if (!this.game) this.game = (GAMES[this.id] ?? GAMES[0])();

    this.rng.reseed(this.cfg.seed);
    this.grid.resize(this.cfg.gridW, this.cfg.gridH);
    this.game.reset(this.cfg, this.rng);

    this.deadTicks = 0;
    this.accum = 0;

    // Deliberately not resetting the clock. A restart mid-run must not make the
    // next frame look like the first frame, or the accumulator would treat the
    // gap since the last frame as elapsed time and immediately owe ticks.
  }

  advance(hostSeconds, input) {
    this.lastFrameTicks = 0;
    if (!this.game) this.setGame(this.id);

    // Defence 2: no elapsed time, no tick. The first frame establishes the clock
    // and steps nothing; a repeated frame lands here with hostSeconds unchanged
    // and falls straight through to draw. A clock that went backwards is the
    // host looping or an operator scrubbing — a fresh start, not negative time.
    if (this.clock < 0 || hostSeconds < this.clock) {
      this.clock = hostSeconds;
      this.game.draw(this.cfg, this.grid);
      return;
    }

    let elapsed = hostSeconds - this.clock;
    this.clock = hostSeconds;

    if (elapsed <= 0) {
      this.game.draw(this.cfg, this.grid);
      return;
    }

    // Defence 3: cap the catch-up.
    if (elapsed > MAX_ELAPSED) elapsed = MAX_ELAPSED;

    // Defence 1: ticks come from the clock.
    const hz = this.game.tickHz(this.cfg);
    if (hz <= 0) {
      this.game.draw(this.cfg, this.grid);
      return;
    }

    const stepSeconds = 1 / hz;
    this.accum += elapsed;

    while (this.accum >= stepSeconds && this.lastFrameTicks < MAX_TICKS_PER_FRAME) {
      this.accum -= stepSeconds;

      if (this.game.finished()) {
        // Hold the finished playfield briefly, then start again. A game that
        // stops on game over leaves a static layer in the middle of a show,
        // which is worse than any amount of losing.
        this.deadTicks += 1;
        if (this.deadTicks >= this.cfg.respawnTicks) {
          // Advance the seed rather than reusing it, or every life is a
          // pixel-identical replay of the last one. The run stays reproducible
          // from the Seed parameter because this walk is itself deterministic.
          this.rng.reseed(this.rng.next());
          this.game.reset(this.cfg, this.rng);
          this.deadTicks = 0;
          input.clear();
        }
      } else {
        this.game.step(this.cfg, input, this.rng);
      }

      this.ticks += 1;
      this.lastFrameTicks += 1;
    }

    // Drop any surplus the cap left behind. Keeping it would mean the next frame
    // starts already owing the ticks this one refused, which is the spiral the
    // cap exists to prevent.
    if (this.lastFrameTicks >= MAX_TICKS_PER_FRAME) this.accum = 0;

    this.game.draw(this.cfg, this.grid);
  }
}

//===========================================================================
// The renderer.
//===========================================================================

class CoinopRenderer {
  constructor(gl, quad) {
    this.gl = gl;
    this.quad = quad;

    this.programs = {
      source: new Program(gl, VERTEX_SHADER, CELL_SHADER, 'cell'),
      effect: new Program(gl, VERTEX_SHADER, withDefines(CELL_SHADER, EFFECT_DEFINE), 'cell (effect)'),
    };

    this.sim = new Sim();
    this.input = new Input();

    // The cell texture. RGBA8UI, NEAREST — nothing about it may be filtered.
    this.cellTexture = gl.createTexture();
    this.texW = 0;
    this.texH = 0;

    gl.bindTexture(gl.TEXTURE_2D, this.cellTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, null);

    // Rising-edge latching for the event parameters, mirroring LatchButton: an
    // FF_TYPE_EVENT goes to 1 and back to 0 as the host sees fit, and it is the
    // transition that is the press.
    this.lastEvent = {};

    this.installKeyboard();
  }

  /**
   * The keyboard is this page's stand-in for the MIDI or OSC mapping you would
   * use in Resolume. It pushes the same Button values onto the same queue the
   * event parameters do — it is not a control the plugin has.
   */
  installKeyboard() {
    const map = {
      ArrowLeft: Button.Left,
      ArrowRight: Button.Right,
      ArrowUp: Button.Up,
      ArrowDown: Button.Down,
      Space: Button.Fire,
      KeyA: Button.Left,
      KeyD: Button.Right,
      KeyW: Button.Up,
      KeyS: Button.Down,
    };

    window.addEventListener('keydown', (e) => {
      const b = map[e.code];
      if (b === undefined) return;
      e.preventDefault();
      if (!e.repeat) this.input.press(b);
      this.input.setHeld(b, true);
    });

    window.addEventListener('keyup', (e) => {
      const b = map[e.code];
      if (b === undefined) return;
      this.input.setHeld(b, false);
    });
  }

  latch(params, id, button) {
    const now = params.get(id);
    const before = this.lastEvent[id] ?? 0;
    this.lastEvent[id] = now;
    if (before < 0.5 && now >= 0.5) this.input.press(button);
  }

  render({ input: clip, params, width, height, time, variant }) {
    const gl = this.gl;
    const isEffect = variant === 'effect';
    const program = isEffect ? this.programs.effect : this.programs.source;

    //---------------------------------------------------------------------
    // Config, then the simulation.
    //---------------------------------------------------------------------
    const aspect = params.option('aspect');
    const gw = gridWidth(params.get('grid'));
    const cfg = {
      gridW: gw,
      gridH: gridHeight(gw, aspect),
      seed: seedValue(params.get('seed')),
      speed: clamp01(params.get('speed')),
      skill: clamp01(params.get('skill')),
      difficulty: clamp01(params.get('difficulty')),
      autopilot: params.get('autoplay') > 0.5,
      respawnTicks: 45,
    };

    this.sim.setGame(params.option('game'));
    this.sim.configure(cfg);

    this.latch(params, 'left', Button.Left);
    this.latch(params, 'right', Button.Right);
    this.latch(params, 'up', Button.Up);
    this.latch(params, 'down', Button.Down);
    this.latch(params, 'fire', Button.Fire);

    const restartNow = params.get('restart');
    if ((this.lastEvent.restart ?? 0) < 0.5 && restartNow >= 0.5) this.sim.restart();
    this.lastEvent.restart = restartNow;

    this.input.axis = clamp01(params.get('paddle'));

    this.sim.advance(time, this.input);

    //---------------------------------------------------------------------
    // The playfield, uploaded as a tiny integer texture. 32x24 is 3 KB.
    //---------------------------------------------------------------------
    const grid = this.sim.grid;
    bindTexture(gl, 1, this.cellTexture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);

    if (grid.w !== this.texW || grid.h !== this.texH) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8UI, grid.w, grid.h, 0, gl.RGBA_INTEGER, gl.UNSIGNED_BYTE, grid.cells);
      this.texW = grid.w;
      this.texH = grid.h;
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, grid.w, grid.h, gl.RGBA_INTEGER, gl.UNSIGNED_BYTE, grid.cells);
    }

    //---------------------------------------------------------------------
    // Draw.
    //---------------------------------------------------------------------
    gl.disable(gl.BLEND);
    program.use();

    program.setSampler('CellTexture', 1);
    program.set('GridSize', grid.w, grid.h);
    program.set('Resolution', width, height);
    program.setInt('FitMode', params.option('scaling'));

    program.setInt('PaletteMode', params.option('palette'));
    program.set('CellRound', clamp01(params.get('cellShape')));
    program.set('CellGap', clamp01(params.get('cellGap')));
    program.set('Glow', clamp01(params.get('glow')));
    program.set('Scanline', clamp01(params.get('scanlines')));
    program.set('Reactive', clamp01(params.get('reactive')));
    program.set('Intensity', this.sim.game ? this.sim.game.intensity() : 0);

    program.set(
      'BackColor',
      clamp01(params.get('backR')),
      clamp01(params.get('backG')),
      clamp01(params.get('backB')),
      clamp01(params.get('backOpacity')),
    );

    if (isEffect) {
      bindTexture(gl, 0, clip.texture);
      program.setSampler('InputTexture', 0);
      program.set('MaxUV', 1, 1);
      program.set('Mix', clamp01(params.get('mix')));
      program.setInt('ClipBricks', params.get('clipBricks') > 0.5 ? 1 : 0);
    }

    this.quad.draw();
  }
}

//===========================================================================
// The page.
//===========================================================================

const pct = (v) => `${Math.round(v * 100)}%`;

mountDemo({
  name: 'Coinop',
  pluginId: 'CO01',
  tagline: 'Arcade games running inside the composition — playable, or left to the autopilot.',
  repo: 'https://github.com/stoatworks-labs/coinop',
  page: 'https://stoatworks-labs.com/software/coinop/',
  video: 'https://www.youtube.com/watch?v=DZyiCXpSt98',
  showBackdrop: true,

  variants: {
    label: 'Plugin',
    default: 'source',
    options: [
      { id: 'source', name: 'Coinop', hint: 'The source: a playfield over its own background.' },
      { id: 'effect', name: 'Coinop Over', hint: 'The effect: a playfield over the clip, with the brick field built out of it.' },
    ],
  },

  sources: ['scene', 'grid', 'bars', 'spot', 'ramp', 'detail', 'alpha'],

  differences: [
    `INCOMPLETE: the plugin has five games and this page currently carries ${PORTED_GAME_NAMES.length} — ${PORTED_GAME_NAMES.join(', ')}. The rest are not in the dropdown rather than being there and doing nothing.`,
    'Step and Restart do not mean what they mean on the other demos. This is the one plugin in the set with real simulation state, so a frame cannot be rendered on its own: Step advances the game by one frame of time and cannot go back, and Restart begins a new game rather than rewinding this one.',
    'The arrow keys and WASD are this page\'s stand-in for the MIDI or OSC mapping you would use in Resolume. They push the same values onto the same input queue the Left/Right/Up/Down/Fire parameters do — the plugin itself has no keyboard, because FFGL has none.',
    'Those five parameters are FF_TYPE_EVENT in the plugin — a momentary press. Here they are toggles, and it is the 0 to 1 transition that is latched as the press, which is exactly what LatchButton does with the host\'s value changes.',
    'A browser tab that loses focus is throttled, and the accumulator caps catch-up at a quarter of a second. Coming back to the tab, the game has skipped ahead rather than replayed — which is the plugin\'s own behaviour when a layer stops being rendered, and the correct answer: nobody was watching.',
  ],

  params: [
    //---- Game -------------------------------------------------------------
    {
      id: 'game', name: 'Game', type: 'option', default: 0, group: 'Game',
      elements: PORTED_GAME_NAMES,
      hint: 'A dropdown rather than five plugins: five games do not need ten entries in the effect list, and a sixth will not need an eleventh.',
    },
    {
      id: 'speed', name: 'Speed', type: 'standard', default: 0.45, group: 'Game',
      display: (v) => `${(4 + 18 * clamp01(v) * clamp01(v)).toFixed(1)} Hz`,
      hint: 'Every game owns its own tick rate and the accumulator honours it. Snake wants four to twenty-two cells a second; a ball wants sixty or it visibly stutters.',
    },
    { id: 'difficulty', name: 'Difficulty', type: 'standard', default: 0.5, group: 'Game', display: pct },
    {
      id: 'skill', name: 'Skill', type: 'standard', default: 0.65, group: 'Game',
      display: pct,
      hint: 'Deliberate incompetence. A perfect Snake AI never dies, never resets, and leaves the layer showing the same slowly-lengthening snake all night — so Skill governs how often the autopilot bothers to consult its own survivability analysis.',
    },
    {
      id: 'autoplay', name: 'Autoplay', type: 'boolean', default: 1, group: 'Game',
      hint: 'The default and the mode that matters, because nobody hand-plays Snake for the length of a show.',
    },
    {
      id: 'seed', name: 'Seed', type: 'standard', default: 0.25, group: 'Game',
      display: (v) => `${Math.round(clamp01(v) * 4095)}`,
      hint: 'Same seed, same game. The 4096 steps are spread across the 64-bit space rather than used raw, because adjacent seeds fed straight into xorshift put the first apple in nearly the same place.',
    },
    { id: 'restart', name: 'Restart', type: 'boolean', default: 0, group: 'Game' },

    //---- Playfield --------------------------------------------------------
    {
      id: 'grid', name: 'Grid', type: 'standard', default: Math.sqrt((32 - 12) / 116), group: 'Playfield',
      display: (v) => `${gridWidth(v)} cells`,
      hint: 'Squared, because a pixel map is usually at the coarse end and that is where the slider needs its resolution. 32 across is where everything here was designed and measured.',
    },
    { id: 'aspect', name: 'Aspect', type: 'option', default: 0, group: 'Playfield', elements: ASPECT_NAMES },
    { id: 'scaling', name: 'Scaling', type: 'option', default: 0, group: 'Playfield', elements: FIT_NAMES },

    //---- Controls ---------------------------------------------------------
    {
      id: 'paddle', name: 'Paddle', type: 'standard', default: 0.5, group: 'Controls',
      display: pct,
      hint: 'The continuous control: paddle position across the playfield. A fader or an OSC float maps to this directly, and it is by some way the nicest way to play Bricks or Rally.',
    },
    { id: 'left', name: 'Left', type: 'boolean', default: 0, group: 'Controls' },
    { id: 'right', name: 'Right', type: 'boolean', default: 0, group: 'Controls' },
    { id: 'up', name: 'Up / Thrust', type: 'boolean', default: 0, group: 'Controls' },
    { id: 'down', name: 'Down', type: 'boolean', default: 0, group: 'Controls' },
    { id: 'fire', name: 'Fire', type: 'boolean', default: 0, group: 'Controls' },

    //---- Look -------------------------------------------------------------
    {
      id: 'palette', name: 'Palette', type: 'option', default: 0, group: 'Look',
      elements: PALETTE_NAMES,
      hint: 'Indexed by role rather than by cell type, so a palette does not have to know what a Marcher is.',
    },
    { id: 'cellShape', name: 'Cell Shape', type: 'standard', default: 0.25, group: 'Look', display: pct },
    { id: 'cellGap', name: 'Cell Gap', type: 'standard', default: 0.18, group: 'Look', display: pct },
    {
      id: 'glow', name: 'Glow', type: 'standard', default: 0.45, group: 'Look',
      display: pct,
      hint: 'Nine texelFetches of the neighbourhood, weighted by distance. It is what stops a 32×24 playfield from looking like a spreadsheet.',
    },
    {
      id: 'scanlines', name: 'Scanlines', type: 'standard', default: 0.0, group: 'Look',
      display: pct,
      hint: 'In output space rather than cell space — they are a property of the imagined display, not of the playfield, so they must not scale with the grid.',
    },
    { id: 'reactive', name: 'Reactive', type: 'standard', default: 0.35, group: 'Look', display: pct },

    //---- Background -------------------------------------------------------
    { id: 'backR', name: 'Background Red', type: 'colour', default: 0.02, group: 'Background' },
    { id: 'backG', name: 'Background Green', type: 'colour', default: 0.02, group: 'Background' },
    { id: 'backB', name: 'Background Blue', type: 'colour', default: 0.03, group: 'Background' },
    { id: 'backOpacity', name: 'Background Opacity', type: 'standard', default: 1.0, group: 'Background', display: pct },

    //---- Output -----------------------------------------------------------
    {
      id: 'clipBricks', name: 'Bricks From Clip', type: 'boolean', default: 0, group: 'Output',
      hint: 'The reason the effect variant exists: the brick field is the incoming video, so breaking a brick punches a hole through to the layer below.',
    },
    { id: 'mix', name: 'Mix', type: 'standard', default: 1.0, group: 'Output', display: pct },
  ],

  createRenderer: (gl, quad) => new CoinopRenderer(gl, quad),
});
