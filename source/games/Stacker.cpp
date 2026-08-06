#include "games/Stacker.h"

#include <algorithm>

namespace coinop
{

namespace
{

/**
	The piece set.

	Ten polyominoes across three sizes. Two trominoes, four tetrominoes, four
	pentominoes -- chosen to be a set, not *the* set: it leaves out pieces the
	classic seven has and includes two sizes it does not. See the header for why
	that is deliberate rather than arbitrary.

	The weights matter more than they look. An even draw over ten kinds puts a
	pentomino in front of the player two times in five, and a five-cell piece
	needs five cells of clean floor -- the board fills faster than any placement
	can clear it and the game is over before it has started. Small pieces are
	the ones that make a run finishable.
*/
struct BasePiece
{
	int count;
	int8_t x[ 5 ];
	int8_t y[ 5 ];
	int weight;
};

const BasePiece kBase[ Stacker::kPieceKinds ] = {
	// Trominoes -- the ones that finish a run off.
	{ 3, { 0, 1, 2, 0, 0 }, { 0, 0, 0, 0, 0 }, 4 },///< bar
	{ 3, { 0, 0, 1, 0, 0 }, { 0, 1, 1, 0, 0 }, 4 },///< corner

	// Tetrominoes. Not the classic seven: no straight four and no J.
	{ 4, { 0, 1, 0, 1, 0 }, { 0, 0, 1, 1, 0 }, 3 },///< square
	{ 4, { 0, 1, 2, 1, 0 }, { 0, 0, 0, 1, 0 }, 3 },///< tee
	{ 4, { 1, 2, 0, 1, 0 }, { 0, 0, 1, 1, 0 }, 2 },///< skew
	{ 4, { 0, 0, 1, 2, 0 }, { 0, 1, 1, 1, 0 }, 3 },///< foot

	// Pentominoes. Rare on purpose.
	{ 5, { 0, 1, 2, 3, 4 }, { 0, 0, 0, 0, 0 }, 1 },///< long bar
	{ 5, { 0, 2, 0, 1, 2 }, { 0, 0, 1, 1, 1 }, 1 },///< cup
	{ 5, { 0, 1, 0, 1, 0 }, { 0, 0, 1, 1, 2 }, 1 },///< block-and-tail
	{ 5, { 0, 0, 1, 1, 2 }, { 0, 1, 1, 2, 2 }, 1 },///< stair
};

int TotalWeight()
{
	int total = 0;
	for( const BasePiece& p : kBase )
		total += p.weight;
	return total;
}

int PickKind( Rng& rng )
{
	int roll = int( rng.Below( uint32_t( TotalWeight() ) ) );
	for( int i = 0; i < Stacker::kPieceKinds; ++i )
	{
		roll -= kBase[ i ].weight;
		if( roll < 0 )
			return i;
	}
	return 0;
}

} // namespace

/**
	One rotation, computed rather than tabulated.

	A table of forty shapes would be forty chances to typo a cell into the wrong
	column, and the compiler would not catch one. Rotating is nine lines and the
	rotation of a polyomino has exactly one correct answer.

	`(x,y) -> (-y,x)` and then normalise the bounding box back to the origin.
	Normalising is the part that is easy to leave out and it is what keeps the
	piece under the player's hand: without it a rotation about the origin walks
	the piece diagonally off the well.
*/
Stacker::Shape Stacker::ShapeOf( int kind, int rot )
{
	kind = std::clamp( kind, 0, kPieceKinds - 1 );
	rot  = ( ( rot % 4 ) + 4 ) % 4;

	Shape s;
	s.count = kBase[ kind ].count;
	for( int i = 0; i < s.count; ++i )
		s.cell[ i ] = { int16_t( kBase[ kind ].x[ i ] ), int16_t( kBase[ kind ].y[ i ] ) };

	for( int r = 0; r < rot; ++r )
		for( int i = 0; i < s.count; ++i )
		{
			const int16_t x = s.cell[ i ].x;
			s.cell[ i ]     = { int16_t( -s.cell[ i ].y ), x };
		}

	int16_t minX = s.cell[ 0 ].x, minY = s.cell[ 0 ].y;
	int16_t maxX = minX, maxY = minY;
	for( int i = 1; i < s.count; ++i )
	{
		minX = std::min( minX, s.cell[ i ].x );
		minY = std::min( minY, s.cell[ i ].y );
		maxX = std::max( maxX, s.cell[ i ].x );
		maxY = std::max( maxY, s.cell[ i ].y );
	}

	for( int i = 0; i < s.count; ++i )
	{
		s.cell[ i ].x = int16_t( s.cell[ i ].x - minX );
		s.cell[ i ].y = int16_t( s.cell[ i ].y - minY );
	}

	s.w = maxX - minX + 1;
	s.h = maxY - minY + 1;
	return s;
}

void Stacker::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mBoard.assign( size_t( mW ) * size_t( mH ), 0 );
	mClear.assign( size_t( mW ) * size_t( mH ), 0 );
	mScratch.assign( size_t( mW ) * size_t( mH ), 0 );
	mScratchMask.assign( size_t( mW ) * size_t( mH ), 0 );

	// The run that clears is a third of the well, never fewer than four cells
	// and never more than twelve. A fixed number would make the game trivial at
	// 128 cells across and impossible at 12 -- and the Grid slider reaches both
	// ends within ten seconds of the plugin being loaded.
	mRunNeeded = std::clamp( WellW() / 3, 4, 12 );

	// Difficulty is the starting fall interval, in ticks. Level takes it down
	// from there and the floor is one tick a row, at which point the autopilot
	// can no longer steer and the run ends -- see the header.
	mBaseInterval = int( std::lround( 15.0f - 9.0f * std::clamp( cfg.difficulty, 0.0f, 1.0f ) ) );
	mBaseInterval = std::max( 3, mBaseInterval );
	mFallInterval = mBaseInterval;
	mFallRows     = 1;

	mActive     = false;
	mKind       = 0;
	mRot        = 0;
	mX          = 0;
	mY          = 0;
	mFallTimer  = 0;
	mClearTimer = 0;
	mScore      = 0;
	mRuns       = 0;
	mLevel      = 0;
	mDead       = false;
	mAiPlanned  = false;

	SpawnPiece( cfg, rng );
}

float Stacker::TickHz( const GameConfig& cfg ) const
{
	// The tick is the input rate, not the fall rate -- gravity is a countdown in
	// ticks. It has to be fast enough that a rotation feels immediate and slow
	// enough that the autopilot's one-step-per-tick movement is legible.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 8.0f + 28.0f * t * t;
}

bool Stacker::Occupied( const std::vector< uint8_t >& board, int x, int y ) const
{
	if( x < WellL() || x > WellR() || y < WellT() || y > WellB() )
		return true;

	return board[ Index( x, y ) ] != 0;
}

bool Stacker::Fits( const std::vector< uint8_t >& board, const Shape& s, int x, int y ) const
{
	for( int i = 0; i < s.count; ++i )
		if( Occupied( board, x + s.cell[ i ].x, y + s.cell[ i ].y ) )
			return false;

	return true;
}

int Stacker::RestingY( const std::vector< uint8_t >& board, const Shape& s, int x ) const
{
	int y = WellT();
	if( !Fits( board, s, x, y ) )
		return -1;

	while( Fits( board, s, x, y + 1 ) )
		++y;

	return y;
}

int Stacker::FindRuns( const std::vector< uint8_t >& board, std::vector< uint8_t >& mask,
                       int& runs ) const
{
	std::fill( mask.begin(), mask.end(), uint8_t( 0 ) );
	runs = 0;

	int cells = 0;
	for( int y = WellT(); y <= WellB(); ++y )
	{
		int run = 0;
		// One past the right edge, so a run that ends against the wall is closed
		// by the same code as one that ends against a gap. Special-casing the
		// last column is how a full row comes to be the one thing that does not
		// clear.
		for( int x = WellL(); x <= WellR() + 1; ++x )
		{
			const bool filled = x <= WellR() && board[ Index( x, y ) ] != 0;
			if( filled )
			{
				++run;
				continue;
			}

			if( run >= mRunNeeded )
			{
				++runs;
				cells += run;
				for( int k = x - run; k < x; ++k )
					mask[ Index( k, y ) ] = 1;
			}
			run = 0;
		}
	}

	return cells;
}

void Stacker::Collapse( std::vector< uint8_t >& board, std::vector< uint8_t >& mask ) const
{
	for( int x = WellL(); x <= WellR(); ++x )
	{
		int drop = 0;
		for( int y = WellB(); y >= WellT(); --y )
		{
			const size_t idx = Index( x, y );
			if( mask[ idx ] )
			{
				board[ idx ] = 0;
				mask[ idx ]  = 0;
				++drop;
			}
			else if( drop > 0 && board[ idx ] != 0 )
			{
				board[ Index( x, y + drop ) ] = board[ idx ];
				board[ idx ]                  = 0;
			}
		}
	}
}

void Stacker::SpawnPiece( const GameConfig& cfg, Rng& rng )
{
	(void)cfg;

	mKind = PickKind( rng );
	mRot  = 0;

	const Shape s = ShapeOf( mKind, mRot );

	mX = WellL() + ( WellW() - s.w ) / 2;
	mY = WellT();

	mFallTimer = 0;
	mAiPlanned = false;

	// Topped out. Note this is the only way the game ends -- there is no timer
	// and no life count, so a stack that reaches the spawn row is what restarts
	// the layer.
	if( !Fits( mBoard, s, mX, mY ) )
	{
		mDead   = true;
		mActive = false;
		return;
	}

	mActive = true;
}

bool Stacker::TryRotate( int delta )
{
	const int want  = ( ( mRot + delta ) % 4 + 4 ) % 4;
	const Shape s   = ShapeOf( mKind, want );

	// Wall kicks, nearest first. Without them a piece against the right wall
	// simply refuses to turn, which reads as an unresponsive control rather
	// than as a rule.
	static const int kKicks[ 5 ] = { 0, -1, 1, -2, 2 };
	for( int k : kKicks )
	{
		if( Fits( mBoard, s, mX + k, mY ) )
		{
			mRot = want;
			mX += k;
			return true;
		}
	}

	return false;
}

void Stacker::Lock()
{
	const Shape s = ShapeOf( mKind, mRot );

	// Tint runs 0..5 because the shader divides the tint byte by five. Keying it
	// off the piece kind rather than off a colour table is what keeps the look
	// entirely in the palette's hands.
	const uint8_t tint = uint8_t( mKind % 6 );
	for( int i = 0; i < s.count; ++i )
	{
		const int px = mX + s.cell[ i ].x;
		const int py = mY + s.cell[ i ].y;
		if( px >= WellL() && px <= WellR() && py >= WellT() && py <= WellB() )
			mBoard[ Index( px, py ) ] = uint8_t( tint + 1 );
	}

	mActive    = false;
	mAiPlanned = false;

	int runs         = 0;
	const int cells  = FindRuns( mBoard, mClear, runs );
	if( cells > 0 )
	{
		mScore += cells;
		mRuns += runs;

		// The level, and with it the fall rate, is driven by runs and not by
		// cells -- otherwise one lucky clear on a 128-wide well would jump five
		// levels at once and end the game on the spot.
		mLevel        = mRuns / 4;
		mFallInterval = std::max( 1, mBaseInterval - mLevel );

		// And past the point where the interval cannot shorten any further, the
		// piece starts falling more than a row at a time.
		//
		// This is the part that makes termination structural rather than a hope.
		// Measured without it: at Skill 1.0 the autopilot reached one tick per
		// row and stayed there, clearing runs indefinitely -- six thousand ticks
		// with the game never once ending, which is precisely the immortal layer
		// AGENTS.md says an autopilot must not produce. A piece that falls four
		// rows a tick cannot be steered by something that moves one column a
		// tick, however well it evaluates the board.
		const int over = mLevel - ( mBaseInterval - 1 );
		mFallRows      = over > 0 ? std::min( 1 + over / 3, 6 ) : 1;

		// Held for a few ticks before the collapse. Purely presentational, and
		// the reason it is worth the state: at eight cells a second a run that
		// vanishes on the tick it completes is a frame nobody sees.
		mClearTimer = 3;
	}
}

int Stacker::EvaluateBoard( const std::vector< uint8_t >& board, int cleared, int landingY ) const
{
	int aggregate = 0;
	int holes     = 0;
	int bumpiness = 0;
	int prevH     = -1;

	for( int x = WellL(); x <= WellR(); ++x )
	{
		int h        = 0;
		bool covered = false;

		for( int y = WellT(); y <= WellB(); ++y )
		{
			if( board[ Index( x, y ) ] != 0 )
			{
				if( !covered )
				{
					covered = true;
					h       = WellB() - y + 1;
				}
			}
			else if( covered )
			{
				++holes;
			}
		}

		aggregate += h;
		if( prevH >= 0 )
			bumpiness += std::abs( h - prevH );
		prevH = h;
	}

	// How close the board is to a run, summed over rows. Without this term the
	// evaluator is only ever asked to keep the stack low and flat, and on a
	// wide well "low and flat" means spreading every piece as far from the last
	// one as possible -- which is the exact opposite of building a run.
	//
	// Measured on a 96-cell-wide well before it existed: the autopilot cleared
	// sixteen runs in six thousand ticks and the game never ended. It is a
	// small weight because a long run is only worth anything if it is also
	// low, and a large one bought towers.
	int contiguity = 0;
	for( int y = WellT(); y <= WellB(); ++y )
	{
		int run  = 0;
		int best = 0;
		for( int x = WellL(); x <= WellR(); ++x )
		{
			run  = board[ Index( x, y ) ] != 0 ? run + 1 : 0;
			best = std::max( best, run );
		}
		contiguity += best;
	}

	// Dellacherie's weights in spirit, integers in fact. A hole is worth far
	// more than the height it saves, because a hole is permanent until whatever
	// is on top of it clears and the height is not.
	return cleared * 24 - aggregate * 3 - holes * 16 - bumpiness * 2 + landingY * 2 +
	       contiguity;
}

Stacker::Placement Stacker::ChoosePlacement( const GameConfig& cfg, Rng& rng ) const
{
	Placement best;
	Placement any;
	int alternatives = 0;

	for( int rot = 0; rot < 4; ++rot )
	{
		const Shape s = ShapeOf( mKind, rot );
		if( s.w > WellW() || s.h > WellH() )
			continue;

		for( int x = WellL(); x + s.w - 1 <= WellR(); ++x )
		{
			const int y = RestingY( mBoard, s, x );
			if( y < 0 )
				continue;

			mScratch = mBoard;
			for( int i = 0; i < s.count; ++i )
				mScratch[ Index( x + s.cell[ i ].x, y + s.cell[ i ].y ) ] = 1;

			int runs          = 0;
			const int cleared = FindRuns( mScratch, mScratchMask, runs );
			if( cleared > 0 )
				Collapse( mScratch, mScratchMask );

			Placement p;
			p.rot   = rot;
			p.x     = x;
			p.score = EvaluateBoard( mScratch, cleared, y );
			p.ok    = true;

			if( !best.ok || p.score > best.score )
				best = p;

			// Reservoir sample of one over every legal placement, so the
			// deliberate mistake below is uniform rather than "always the
			// leftmost thing that fitted".
			++alternatives;
			if( rng.Below( uint32_t( alternatives ) ) == 0 )
				any = p;
		}
	}

	// Deliberate incompetence, same contract as every other autopilot here: the
	// layer needs the game to end sometimes. This is the only thing Skill does
	// in this game -- termination itself is guaranteed by the fall rate, not by
	// this.
	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
	if( any.ok && rng.Chance( ( 1.0f - skill ) * 0.55f ) )
		return any;

	return best;
}

void Stacker::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mDead )
		return;

	int move       = 0;
	int rotate     = 0;
	bool softDrop  = false;
	bool hardDrop  = false;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Left: --move; break;
			case Button::Right: ++move; break;
			case Button::Up: ++rotate; break;
			case Button::Down: softDrop = true; break;
			case Button::Fire: hardDrop = true; break;
			default: break;
		}
	}

	// The clear flash owns the whole tick. Input is drained above rather than
	// left in the queue, or a player mashing through the flash gets three
	// rotations at once on the tick the next piece appears.
	if( mClearTimer > 0 )
	{
		--mClearTimer;
		if( mClearTimer == 0 )
			Collapse( mBoard, mClear );
		return;
	}

	if( !mActive )
	{
		SpawnPiece( cfg, rng );
		if( mDead )
			return;
	}

	if( cfg.autopilot )
	{
		move     = 0;
		rotate   = 0;
		hardDrop = false;

		if( !mAiPlanned )
		{
			const Placement p = ChoosePlacement( cfg, rng );
			mAiRot            = p.ok ? p.rot : mRot;
			mAiX              = p.ok ? p.x : mX;
			mAiPlanned        = true;
		}

		// One step a tick, deliberately. Snapping the piece to its column would
		// be a line of code and would look like a rendering fault -- the piece
		// has to be seen to travel.
		if( mRot != mAiRot )
			rotate = 1;
		else if( mX < mAiX )
			move = 1;
		else if( mX > mAiX )
			move = -1;
		else
			softDrop = true;
	}

	if( rotate != 0 )
		TryRotate( rotate > 0 ? 1 : -1 );

	if( move != 0 )
	{
		const int step  = move > 0 ? 1 : -1;
		const Shape s   = ShapeOf( mKind, mRot );
		if( Fits( mBoard, s, mX + step, mY ) )
			mX += step;
	}

	if( hardDrop )
	{
		const Shape s = ShapeOf( mKind, mRot );
		int y         = mY;
		while( Fits( mBoard, s, mX, y + 1 ) )
			++y;

		mY = y;
		Lock();
		return;
	}

	++mFallTimer;
	if( mFallTimer < ( softDrop ? 1 : mFallInterval ) )
		return;

	mFallTimer    = 0;
	const Shape s = ShapeOf( mKind, mRot );

	// Locking is decided on the first row that will not take the piece, not
	// after the whole drop -- otherwise a multi-row fall passes straight
	// through a one-cell ledge.
	for( int row = 0; row < mFallRows; ++row )
	{
		if( !Fits( mBoard, s, mX, mY + 1 ) )
		{
			Lock();
			return;
		}

		++mY;
	}
}

void Stacker::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	for( int x = 0; x < mW; ++x )
	{
		grid.Set( x, 0, Cell::Wall );
		grid.Set( x, mH - 1, Cell::Wall );
	}
	for( int y = 0; y < mH; ++y )
	{
		grid.Set( 0, y, Cell::Wall );
		grid.Set( mW - 1, y, Cell::Wall );
	}

	for( int y = WellT(); y <= WellB(); ++y )
		for( int x = WellL(); x <= WellR(); ++x )
		{
			const size_t idx = Index( x, y );
			if( mBoard[ idx ] == 0 )
				continue;

			// A cell mid-flash draws as Food rather than Brick: it is about to
			// be worth something, and the palette's target colour is the one
			// thing on screen that already means exactly that.
			if( mClear[ idx ] )
				grid.Set( x, y, Cell::Food, 255 );
			else
				grid.Set( x, y, Cell::Brick, 255, uint8_t( mBoard[ idx ] - 1 ) );
		}

	if( mActive )
	{
		const Shape s = ShapeOf( mKind, mRot );
		for( int i = 0; i < s.count; ++i )
			grid.Set( mX + s.cell[ i ].x, mY + s.cell[ i ].y, Cell::Head, 255,
			          uint8_t( mKind % 6 ) );
	}
}

int Stacker::StackHeight() const
{
	for( int y = WellT(); y <= WellB(); ++y )
		for( int x = WellL(); x <= WellR(); ++x )
			if( mBoard[ Index( x, y ) ] != 0 )
				return WellB() - y + 1;

	return 0;
}

int Stacker::FilledCells() const
{
	int n = 0;
	for( int y = WellT(); y <= WellB(); ++y )
		for( int x = WellL(); x <= WellR(); ++x )
			if( mBoard[ Index( x, y ) ] != 0 )
				++n;

	return n;
}

float Stacker::Intensity() const
{
	const int h = WellH();
	if( h <= 0 )
		return 0.0f;

	// How close the stack is to the top. Climbs through a run and drops on a
	// clear, which is exactly the shape a reactive glow wants.
	return std::clamp( float( StackHeight() ) / float( h ), 0.0f, 1.0f );
}

} // namespace coinop
