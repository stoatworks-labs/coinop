#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

/**
	The playfield: a small grid of cells, and the only thing the shader is ever
	told about the game.

	## Why a grid and not geometry

	Every other generator in the fleet draws with geometry or a per-pixel
	function, because every other generator is resolution-independent by
	construction. A game is not: Snake *is* a grid, and Breakout's bricks are a
	grid, and the moment you draw them as scaled quads you have to decide what a
	half-cell means when the output is 1920 wide and the playfield is 32.

	So the sim writes cells, the plugin uploads the cells as a tiny texture --
	32x24 is 3 KB -- and the fragment shader decides what a cell *looks* like.
	That split buys three things:

	- **The look is free to change without touching the game.** Rounded cells,
	  glow, scanlines, a palette swap: all shader, none of it sim.
	- **It reads on a pixel map.** The composition is the fixture layout and
	  Resolume samples it; a snake drawn at 32x24 and blown up is legible on an
	  LED wall in a way an antialiased vector snake is not.
	- **The sim stays testable.** `coinoptest` asserts on cells. No GL context,
	  no framebuffer readback, no tolerance on a float compare.

	## The cell encoding is a contract

	Four bytes per cell, uploaded verbatim as `GL_RGBA8UI` and read in GLSL with
	a `usampler2D` and `texelFetch`.

	**Unsigned-integer texture and not `GL_RGBA8`, deliberately.** A normalised
	byte texture hands the shader 0..1 floats, so recovering the cell type means
	`int( t.r * 255.0 + 0.5 )` and trusting that every driver's fixed-function
	normalise lands on exactly the same float. It very nearly always does. When
	it does not, one cell type is silently mistaken for its neighbour and the
	bug is a brick that renders as a wall on one machine only. An integer
	texture removes the question: the shader reads the byte that was written.
*/
namespace coinop
{

/// What is in a cell. The numeric values are baked into the fragment shader's
/// switch, so appending is safe and reordering is not.
enum class Cell : uint8_t
{
	Empty = 0,
	Wall,     ///< Playfield border. Snake dies on it; the ball bounces off it.
	Body,     ///< Snake body. `shade` runs 255 at the neck to 0 at the tail tip.
	Head,     ///< Snake head.
	Food,
	Brick,    ///< `shade` carries remaining hit points, `tint` the row colour.
	Paddle,
	Ball,
	Enemy,    ///< Anything that is trying to end the run: a pursuer, a barrel,
	          ///< a diving attacker, the other fighter. `shade` drops when it is
	          ///< temporarily harmless -- fleeing, stunned, flipped on its back.
	Count
};

/**
	One cell. Kept to exactly four bytes -- `static_assert`ed below -- because
	the array is memcpy'd into a texture and any padding the compiler inserted
	would land in the shader as garbage.
*/
struct CellData
{
	uint8_t type  = uint8_t( Cell::Empty );
	uint8_t shade = 0;///< Gradient within a type: tail age, brick damage.
	uint8_t tint  = 0;///< Palette slot. Lets one brick field carry per-row hues.
	uint8_t flash = 0;///< 255 the tick a cell changed, decayed by the plugin.
};

static_assert( sizeof( CellData ) == 4, "CellData is uploaded raw as RGBA8UI" );

class Grid
{
public:
	void Resize( int w, int h )
	{
		if( w == mW && h == mH )
			return;

		mW = w;
		mH = h;
		mCells.assign( size_t( w ) * size_t( h ), CellData{} );
	}

	/// Wipe to Empty. Keeps the allocation -- this runs every tick.
	void Clear()
	{
		std::fill( mCells.begin(), mCells.end(), CellData{} );
	}

	int Width() const { return mW; }
	int Height() const { return mH; }

	bool InBounds( int x, int y ) const
	{
		return x >= 0 && y >= 0 && x < mW && y < mH;
	}

	/// Out-of-bounds writes are dropped rather than asserted. A game that
	/// rasterises a paddle straddling the edge, or a ball mid-escape on the
	/// tick before the miss is detected, is not a bug worth crashing a live
	/// show over.
	void Set( int x, int y, CellData c )
	{
		if( InBounds( x, y ) )
			mCells[ size_t( y ) * size_t( mW ) + size_t( x ) ] = c;
	}

	void Set( int x, int y, Cell type, uint8_t shade = 0, uint8_t tint = 0 )
	{
		Set( x, y, CellData{ uint8_t( type ), shade, tint, 0 } );
	}

	CellData Get( int x, int y ) const
	{
		if( !InBounds( x, y ) )
			return CellData{ uint8_t( Cell::Wall ), 0, 0, 0 };

		return mCells[ size_t( y ) * size_t( mW ) + size_t( x ) ];
	}

	Cell TypeAt( int x, int y ) const { return Cell( Get( x, y ).type ); }

	/// Raw bytes for `glTexImage2D`. Row 0 is the top of the playfield; the
	/// shader flips, because FFGL hands us a bottom-left origin.
	const uint8_t* Data() const
	{
		return reinterpret_cast< const uint8_t* >( mCells.data() );
	}

	size_t ByteCount() const { return mCells.size() * sizeof( CellData ); }

private:
	int mW = 0;
	int mH = 0;
	std::vector< CellData > mCells;
};

} // namespace coinop
