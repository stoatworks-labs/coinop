#include "Shaders.h"

namespace coinop
{

const char* const kVertexShader = R"(#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

const char* const kEffectDefine = "#define COINOP_OVER_INPUT 1\n";

/**
	The cell renderer.

	The sim hands over a grid of four-byte cells and this decides what a cell
	looks like. Everything below the playfield mapping is styling; nothing here
	knows which game is running, which is the point -- a sixth game needs no
	shader change as long as it draws with the cell types that already exist.
*/
const char* const kCellShader = R"(#version 410 core

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
		//the space `local` lives in. Without this the cells alias hard at small
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
)";

} // namespace coinop
