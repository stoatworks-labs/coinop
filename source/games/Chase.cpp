#include "games/Chase.h"

// <cmath> explicitly: libc++ leaks it through <algorithm> and MSVC does not.
// See AGENTS.md -- this is a macOS-green, Windows-red failure by construction.
#include <algorithm>
#include <cmath>
#include <climits>

namespace coinop
{

namespace
{

IVec Delta( Dir d )
{
	switch( d )
	{
		case Dir::Up: return { 0, -1 };
		case Dir::Right: return { 1, 0 };
		case Dir::Down: return { 0, 1 };
		default: return { -1, 0 };
	}
}

int Manhattan( IVec a, IVec b )
{
	return std::abs( int( a.x ) - int( b.x ) ) + std::abs( int( a.y ) - int( b.y ) );
}

/// Fraction of interior walls knocked through after the carve. Enough to give
/// every junction somewhere else to go; low enough that the maze is still a
/// maze rather than a room with pillars in it.
constexpr float kLoopRate = 0.16f;

/// How long a scatter lasts, and how often one comes round.
constexpr int kScatterTicks = 70;
constexpr int kChasePeriod  = 340;

} // namespace

bool Chase::Open( int x, int y ) const
{
	if( x < 0 || y < 0 || x >= mW || y >= mH )
		return false;

	return mMaze[ size_t( y ) * size_t( mW ) + size_t( x ) ] == 0;
}

int Chase::OpenCells() const
{
	int n = 0;
	for( int y = 0; y < mH; ++y )
		for( int x = 0; x < mW; ++x )
			if( Open( x, y ) )
				++n;

	return n;
}

/**
	Carve the maze, then ruin it slightly.

	The backtracker is the standard iterative one on the odd lattice: every cell
	at odd (x,y) is a room, every cell between two rooms is a wall that may or
	may not come down. Iterative and not recursive for the same reason Snake's
	flood fill is -- a 128x96 grid is six thousand rooms and that is not a call
	depth to hand to a render thread.

	The second pass is the one that matters for the game. A backtracker leaves
	a perfect maze: exactly one route between any two cells, so every junction
	the player takes wrongly ends in a dead end with four pursuers behind them.
	Knocking a proportion of the remaining walls through turns dead ends into
	loops, and a loop is the only thing that makes being chased survivable.
*/
void Chase::BuildMaze( Rng& rng )
{
	const size_t n = size_t( mW ) * size_t( mH );
	mMaze.assign( n, 1 );
	mPellet.assign( n, 0 );
	mPelletsLeft = 0;

	int maxX = mW - 2;
	int maxY = mH - 2;
	if( ( maxX & 1 ) == 0 )
		--maxX;
	if( ( maxY & 1 ) == 0 )
		--maxY;

	if( maxX < 1 || maxY < 1 )
	{
		// Too small for a lattice. Open the interior rather than carve nothing
		// and hand the rest of the game an entirely solid board.
		for( int y = 1; y <= mH - 2; ++y )
			for( int x = 1; x <= mW - 2; ++x )
				mMaze[ Index( x, y ) ] = 0;
	}
	else
	{
		std::vector< int32_t > stack;
		stack.reserve( size_t( maxX ) * size_t( maxY ) / 2 + 4 );

		mMaze[ Index( 1, 1 ) ] = 0;
		stack.push_back( int32_t( Index( 1, 1 ) ) );

		while( !stack.empty() )
		{
			const int32_t idx = stack.back();
			const int cx      = idx % mW;
			const int cy      = idx / mW;

			int candidate[ 4 ];
			int count = 0;
			for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
			{
				const IVec s = Delta( Dir( d ) );
				const int nx = cx + s.x * 2;
				const int ny = cy + s.y * 2;
				if( nx >= 1 && nx <= maxX && ny >= 1 && ny <= maxY &&
				    mMaze[ Index( nx, ny ) ] == 1 )
					candidate[ count++ ] = int( d );
			}

			if( count == 0 )
			{
				stack.pop_back();
				continue;
			}

			const Dir d  = Dir( candidate[ rng.Below( uint32_t( count ) ) ] );
			const IVec s = Delta( d );
			mMaze[ Index( cx + s.x, cy + s.y ) ]         = 0;
			mMaze[ Index( cx + s.x * 2, cy + s.y * 2 ) ] = 0;
			stack.push_back( int32_t( Index( cx + s.x * 2, cy + s.y * 2 ) ) );
		}

		for( int y = 1; y <= maxY; ++y )
			for( int x = 1; x <= maxX; ++x )
			{
				if( mMaze[ Index( x, y ) ] == 0 )
					continue;

				const bool horiz = Open( x - 1, y ) && Open( x + 1, y );
				const bool vert  = Open( x, y - 1 ) && Open( x, y + 1 );
				if( ( horiz || vert ) && rng.Chance( kLoopRate ) )
					mMaze[ Index( x, y ) ] = 0;
			}
	}

	for( int y = 1; y <= mH - 2; ++y )
		for( int x = 1; x <= mW - 2; ++x )
			if( Open( x, y ) )
			{
				mPellet[ Index( x, y ) ] = 1;
				++mPelletsLeft;
			}

	// Four power pellets, one toward each corner. Nearest-open rather than a
	// fixed coordinate because the carve decides which cells exist, and a power
	// pellet inside a wall is a power pellet nobody can reach.
	const IVec corner[ 4 ] = { { 1, 1 },
	                          { int16_t( mW - 2 ), 1 },
	                          { 1, int16_t( mH - 2 ) },
	                          { int16_t( mW - 2 ), int16_t( mH - 2 ) } };

	for( IVec c : corner )
	{
		int bestIdx  = -1;
		int bestDist = INT_MAX;
		for( int y = 1; y <= mH - 2; ++y )
			for( int x = 1; x <= mW - 2; ++x )
			{
				if( mPellet[ Index( x, y ) ] != 1 )
					continue;

				const int d = Manhattan( { int16_t( x ), int16_t( y ) }, c );
				if( d < bestDist )
				{
					bestDist = d;
					bestIdx  = int( Index( x, y ) );
				}
			}

		if( bestIdx >= 0 )
			mPellet[ size_t( bestIdx ) ] = 2;
	}
}

void Chase::PlaceActors()
{
	// The player starts as far from the middle as the maze allows, and the
	// pursuers start in it. Anything else and the first three seconds of every
	// run are a pursuer standing on the player.
	auto nearestOpen = [ & ]( IVec want, const IVec* taken, int takenCount ) -> IVec {
		int bestIdx  = -1;
		int bestDist = INT_MAX;
		for( int y = 1; y <= mH - 2; ++y )
			for( int x = 1; x <= mW - 2; ++x )
			{
				if( !Open( x, y ) )
					continue;

				const IVec p{ int16_t( x ), int16_t( y ) };
				bool skip = false;
				for( int i = 0; i < takenCount; ++i )
					skip = skip || taken[ i ] == p;
				if( skip )
					continue;

				const int d = Manhattan( p, want );
				if( d < bestDist )
				{
					bestDist = d;
					bestIdx  = int( Index( x, y ) );
				}
			}

		if( bestIdx < 0 )
			return { 1, 1 };

		return { int16_t( bestIdx % mW ), int16_t( bestIdx / mW ) };
	};

	mHome = nearestOpen( { int16_t( mW / 2 ), int16_t( mH - 2 ) }, nullptr, 0 );
	mPos  = mHome;
	mDir  = Dir::Left;
	mTurns.clear();

	IVec taken[ kPursuers ];
	const IVec centre{ int16_t( mW / 2 ), int16_t( mH / 2 ) };
	for( int i = 0; i < kPursuers; ++i )
	{
		taken[ i ]      = nearestOpen( centre, taken, i );
		mGhost[ i ].pos  = taken[ i ];
		mGhost[ i ].home = taken[ i ];
		mGhost[ i ].dir  = Dir( i % int( Dir::Count ) );
		mGhost[ i ].accum  = 0;
		mGhost[ i ].revive = 0;
		mGhost[ i ].kind   = uint8_t( i );
	}

	mWander = centre;
	mFright = 0;
	mChain  = 0;
	mSinceP = 0;

	// Every life starts with the pursuers heading away. Without it the player
	// respawns into the same converging blob that just caught them.
	mScatter = kScatterTicks;
}

void Chase::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	const size_t n = size_t( mW ) * size_t( mH );
	mDist.assign( n, -1 );
	mFirst.assign( n, -1 );
	mDanger.assign( n, -1 );
	mQueue.clear();
	mQueue.reserve( n );

	mScore = 0;
	mLives = 3;
	mLevel = 0;
	mTicks = 0;

	// Difficulty is how fast the pursuers move, in sixths of the player's rate.
	// Four sixths is a hunt the player can win; six sixths is a pursuer that
	// simply catches them, and the top of the slider should mean that.
	mGhostSpeed = int( std::lround( 2.0f + 3.0f * std::clamp( cfg.difficulty, 0.0f, 1.0f ) ) );
	mGhostSpeed = std::clamp( mGhostSpeed, 2, 6 );

	BuildMaze( rng );
	PlaceActors();
}

float Chase::TickHz( const GameConfig& cfg ) const
{
	// One cell a tick for the player, so this is the movement rate directly.
	// Squared for the same reason Snake's is: the slow half is the half worth
	// having resolution in.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 6.0f + 20.0f * t * t;
}

void Chase::FloodFromPlayer()
{
	std::fill( mDist.begin(), mDist.end(), int32_t( -1 ) );
	std::fill( mFirst.begin(), mFirst.end(), int8_t( -1 ) );
	mQueue.clear();

	// Seeded with the player's neighbours rather than the player, each carrying
	// the direction it was entered from. That is what makes one pass enough:
	// the answer to "which way to the nearest pellet" is read straight off the
	// pellet's cell, with no path to walk back.
	for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
	{
		const IVec s = Delta( Dir( d ) );
		const int nx = mPos.x + s.x;
		const int ny = mPos.y + s.y;
		if( !Open( nx, ny ) )
			continue;

		const size_t idx = Index( nx, ny );
		if( mDist[ idx ] >= 0 )
			continue;

		mDist[ idx ]  = 1;
		mFirst[ idx ] = int8_t( d );
		mQueue.push_back( int32_t( idx ) );
	}

	for( size_t head = 0; head < mQueue.size(); ++head )
	{
		const int32_t c = mQueue[ head ];
		const int cx    = c % mW;
		const int cy    = c / mW;

		for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
		{
			const IVec s = Delta( Dir( d ) );
			const int nx = cx + s.x;
			const int ny = cy + s.y;
			if( !Open( nx, ny ) )
				continue;

			const size_t idx = Index( nx, ny );
			if( mDist[ idx ] >= 0 )
				continue;

			mDist[ idx ]  = mDist[ c ] + 1;
			mFirst[ idx ] = mFirst[ c ];
			mQueue.push_back( int32_t( idx ) );
		}
	}
}

void Chase::FloodFromGhosts()
{
	std::fill( mDanger.begin(), mDanger.end(), int32_t( -1 ) );
	mQueue.clear();

	for( const Ghost& g : mGhost )
	{
		if( g.revive > 0 || !Open( g.pos.x, g.pos.y ) )
			continue;

		const size_t idx = Index( g.pos.x, g.pos.y );
		if( mDanger[ idx ] >= 0 )
			continue;

		mDanger[ idx ] = 0;
		mQueue.push_back( int32_t( idx ) );
	}

	for( size_t head = 0; head < mQueue.size(); ++head )
	{
		const int32_t c = mQueue[ head ];
		const int cx    = c % mW;
		const int cy    = c / mW;

		for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
		{
			const IVec s = Delta( Dir( d ) );
			const int nx = cx + s.x;
			const int ny = cy + s.y;
			if( !Open( nx, ny ) )
				continue;

			const size_t idx = Index( nx, ny );
			if( mDanger[ idx ] >= 0 )
				continue;

			mDanger[ idx ] = mDanger[ c ] + 1;
			mQueue.push_back( int32_t( idx ) );
		}
	}
}

IVec Chase::GhostTarget( const Ghost& g, Rng& rng ) const
{
	(void)rng;

	// Scattering, every pursuer heads for its own corner. Four different
	// corners, so they come apart rather than queueing into one.
	if( mScatter > 0 )
	{
		switch( g.kind % 4 )
		{
			case 0: return { 1, 1 };
			case 1: return { int16_t( mW - 2 ), 1 };
			case 2: return { 1, int16_t( mH - 2 ) };
			default: return { int16_t( mW - 2 ), int16_t( mH - 2 ) };
		}
	}

	switch( g.kind )
	{
		case 0:
			return mPos;

		case 1:
		{
			// Four cells along the player's heading. Clamped rather than wrapped
			// -- a target outside the board is fine as a direction to head in,
			// but one that has wrapped around points the ambusher backwards.
			const IVec s = Delta( mDir );
			return { int16_t( std::clamp( int( mPos.x ) + s.x * 4, 0, mW - 1 ) ),
			         int16_t( std::clamp( int( mPos.y ) + s.y * 4, 0, mH - 1 ) ) };
		}

		case 2:
			// Far away it patrols a corner, close up it commits. The effect is a
			// pursuer that keeps leaving and coming back, which is what stops
			// all four arriving as one blob.
			return Manhattan( g.pos, mPos ) > 8 ? IVec{ 1, int16_t( mH - 2 ) } : mPos;

		default:
			return mWander;
	}
}

Dir Chase::StepGhost( const Ghost& g, IVec target, bool flee ) const
{
	Dir best      = g.dir;
	int bestDist  = 0;
	bool found    = false;
	const Dir back = Opposite( g.dir );

	// Reversing is banned. It is the rule that makes a corridor safe to commit
	// to, and without it a pursuer at a junction oscillates in place.
	for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
	{
		if( Dir( d ) == back )
			continue;

		const IVec s = Delta( Dir( d ) );
		const int nx = g.pos.x + s.x;
		const int ny = g.pos.y + s.y;
		if( !Open( nx, ny ) )
			continue;

		const int dist =
			Manhattan( { int16_t( nx ), int16_t( ny ) }, target );

		if( !found || ( flee ? dist > bestDist : dist < bestDist ) )
		{
			bestDist = dist;
			best     = Dir( d );
			found    = true;
		}
	}

	// A dead end. Reversing is the only legal move and refusing it would park
	// the pursuer there for the rest of the level.
	return found ? best : back;
}

Dir Chase::ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const
{
	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

	int bestIdx  = -1;
	int bestDist = INT_MAX;

	for( int y = 1; y <= mH - 2; ++y )
		for( int x = 1; x <= mW - 2; ++x )
		{
			const size_t idx = Index( x, y );
			if( mPellet[ idx ] == 0 || mDist[ idx ] < 0 )
				continue;

			// A power pellet is worth walking a little further for, and the
			// discount is what makes the autopilot occasionally turn the game
			// around instead of grinding the nearest dot forever.
			const int weighted = mDist[ idx ] - ( mPellet[ idx ] == 2 ? 6 : 0 );
			if( weighted < bestDist )
			{
				bestDist = weighted;
				bestIdx  = int( idx );
			}
		}

	// While the power pellet is up, the pursuers are the prize.
	if( mFright > 4 )
		for( const Ghost& g : mGhost )
		{
			if( g.revive > 0 )
				continue;

			const size_t idx = Index( g.pos.x, g.pos.y );
			if( mDist[ idx ] < 0 || mDist[ idx ] > mFright )
				continue;

			if( mDist[ idx ] - 4 < bestDist )
			{
				bestDist = mDist[ idx ] - 4;
				bestIdx  = int( idx );
			}
		}

	Dir chosen  = mDir;
	bool picked = false;
	if( bestIdx >= 0 && mFirst[ size_t( bestIdx ) ] >= 0 )
	{
		chosen = Dir( mFirst[ size_t( bestIdx ) ] );
		picked = true;
	}

	// The safety check, and the only thing Skill governs. Consulted, it refuses
	// a step that walks inside two cells of a live pursuer whenever anything
	// else is legal. Not consulted, the player beelines for the dot and gets
	// eaten -- which is the behaviour the layer needs some of the time.
	const bool dangerous = [ & ] {
		if( mFright > 0 || !picked )
			return false;

		const IVec s = Delta( chosen );
		const int nx = mPos.x + s.x;
		const int ny = mPos.y + s.y;
		if( !Open( nx, ny ) )
			return true;

		const int32_t d = mDanger[ Index( nx, ny ) ];
		return d >= 0 && d <= 2;
	}();

	if( ( !picked || dangerous ) && rng.Chance( picked ? skill : 1.0f ) )
	{
		int bestSafety = INT_MIN;
		bool found     = false;
		for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
		{
			const IVec s = Delta( Dir( d ) );
			const int nx = mPos.x + s.x;
			const int ny = mPos.y + s.y;
			if( !Open( nx, ny ) )
				continue;

			const size_t idx    = Index( nx, ny );
			const int32_t danger = mDanger[ idx ] < 0 ? 999 : mDanger[ idx ];

			// Distance from the nearest pursuer first, distance to the nearest
			// pellet as the tie-break, so a cornered player still walks toward
			// something rather than into the wall it happened to check first.
			const int safety = danger * 64 - ( mDist[ idx ] < 0 ? 0 : mDist[ idx ] );
			if( !found || safety > bestSafety )
			{
				bestSafety = safety;
				chosen     = Dir( d );
				found      = true;
			}
		}
	}

	return chosen;
}

void Chase::MovePlayer( Dir d )
{
	const IVec s = Delta( d );
	const int nx = mPos.x + s.x;
	const int ny = mPos.y + s.y;

	if( !Open( nx, ny ) )
		return;

	mDir = d;
	mPos = { int16_t( nx ), int16_t( ny ) };
}

void Chase::LoseLife()
{
	--mLives;
	if( mLives > 0 )
		PlaceActors();
}

void Chase::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mLives <= 0 )
		return;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Up: mTurns.push_back( Dir::Up ); break;
			case Button::Down: mTurns.push_back( Dir::Down ); break;
			case Button::Left: mTurns.push_back( Dir::Left ); break;
			case Button::Right: mTurns.push_back( Dir::Right ); break;
			default: break;
		}
	}

	if( mTurns.size() > 4 )
		mTurns.erase( mTurns.begin(), mTurns.end() - 4 );

	++mTicks;

	if( mScatter > 0 )
		--mScatter;
	else if( mTicks % kChasePeriod == 0 )
		mScatter = kScatterTicks;

	if( mFright > 0 )
	{
		--mFright;
		if( mFright == 0 )
			mChain = 0;
	}

	// The wanderer's target, moved on a slow beat. Re-rolled every tick it is a
	// random walk, which reverses on the spot and reads as a bug.
	if( mTicks % 24 == 0 )
	{
		const int x = 1 + int( rng.Below( uint32_t( std::max( 1, mW - 2 ) ) ) );
		const int y = 1 + int( rng.Below( uint32_t( std::max( 1, mH - 2 ) ) ) );
		mWander     = { int16_t( std::min( x, mW - 2 ) ), int16_t( std::min( y, mH - 2 ) ) };
	}

	FloodFromPlayer();
	FloodFromGhosts();

	Dir want = mDir;
	if( cfg.autopilot )
	{
		mTurns.clear();
		want = ChooseAutopilot( cfg, rng );
	}
	else if( !mTurns.empty() )
	{
		const Dir queued = mTurns.front();
		const IVec s     = Delta( queued );
		if( Open( mPos.x + s.x, mPos.y + s.y ) )
		{
			want = queued;
			mTurns.erase( mTurns.begin() );
		}
		else if( mTurns.size() > 1 )
		{
			// A turn that is still illegal by the time the next one arrives is a
			// turn the player has given up on. Holding it forever means the
			// second press never lands.
			mTurns.erase( mTurns.begin() );
		}
	}

	const IVec playerWas = mPos;
	MovePlayer( want );

	const size_t here = Index( mPos.x, mPos.y );
	if( mPellet[ here ] != 0 )
	{
		if( mPellet[ here ] == 2 )
		{
			// Long enough to cross a good part of the board, and it scales with
			// the board so it means the same thing at 12 cells and at 128.
			mFright = std::clamp( ( mW + mH ) * 2, 30, 220 );
			mChain  = 0;
			mScore += 5;
		}
		else
		{
			++mScore;
		}

		mPellet[ here ] = 0;
		--mPelletsLeft;
		mSinceP = 0;
	}
	else
	{
		++mSinceP;
	}

	for( Ghost& g : mGhost )
	{
		if( g.revive > 0 )
		{
			--g.revive;
			continue;
		}

		const bool flee = mFright > 0;
		g.accum += flee ? 3 : mGhostSpeed;

		while( g.accum >= 6 )
		{
			g.accum -= 6;

			const IVec was = g.pos;
			const Dir d    = StepGhost( g, GhostTarget( g, rng ), flee );
			const IVec s   = Delta( d );
			if( Open( g.pos.x + s.x, g.pos.y + s.y ) )
			{
				g.dir = d;
				g.pos = { int16_t( g.pos.x + s.x ), int16_t( g.pos.y + s.y ) };
			}

			// Both the landed-on case and the walked-through case. Two actors
			// stepping past each other in a corridor swap cells and never share
			// one, so testing positions alone misses the head-on collision
			// entirely -- and a head-on in a corridor is the commonest way this
			// game ends.
			const bool touched = g.pos == mPos || ( g.pos == playerWas && mPos == was );
			if( !touched )
				continue;

			if( flee )
			{
				mChain = std::min( mChain + 1, 4 );
				mScore += 10 * mChain;
				g.pos    = g.home;
				g.dir    = Dir::Up;
				g.revive = 24;
			}
			else
			{
				LoseLife();
				return;
			}
		}
	}

	if( mPelletsLeft <= 0 )
	{
		++mLevel;
		mGhostSpeed = std::min( 6, mGhostSpeed + 1 );
		BuildMaze( rng );
		PlaceActors();
		return;
	}

	// A run that has stopped eating is a run the autopilot has got stuck in --
	// usually circling a loop just out of reach of a pellet it will not risk.
	// Ending it beats a layer that stopped changing.
	if( mSinceP > mW * mH * 2 )
		LoseLife();
}

void Chase::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	for( int y = 0; y < mH; ++y )
		for( int x = 0; x < mW; ++x )
		{
			if( !Open( x, y ) )
			{
				grid.Set( x, y, Cell::Wall );
				continue;
			}

			const uint8_t p = mPellet[ Index( x, y ) ];
			if( p == 1 )
				grid.Set( x, y, Cell::Food, 110 );
			else if( p == 2 )
				grid.Set( x, y, Cell::Food, 255 );
		}

	for( const Ghost& g : mGhost )
	{
		// Reviving pursuers are drawn dim rather than hidden. A pursuer that
		// vanishes and reappears somewhere else looks like a dropped frame; one
		// that sits faintly at its home reads as what it is.
		const uint8_t shade = g.revive > 0 ? 60 : ( mFright > 0 ? 110 : 255 );
		grid.Set( g.pos.x, g.pos.y, Cell::Enemy, shade, g.kind );
	}

	grid.Set( mPos.x, mPos.y, Cell::Head, 255 );
}

float Chase::Intensity() const
{
	// Two things at once: how close a pursuer is, and whether the board has
	// been turned around. Both are moments a reactive glow should follow.
	if( mFright > 0 )
		return 1.0f;

	int nearest = INT_MAX;
	for( const Ghost& g : mGhost )
		if( g.revive == 0 )
			nearest = std::min( nearest, Manhattan( g.pos, mPos ) );

	if( nearest == INT_MAX )
		return 0.0f;

	const float reach = float( std::max( 4, ( mW + mH ) / 4 ) );
	return std::clamp( 1.0f - float( nearest ) / reach, 0.0f, 1.0f );
}

} // namespace coinop
