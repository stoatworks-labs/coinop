#include "games/Snake.h"

#include <algorithm>

namespace coinop
{

void Snake::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mBody.clear();
	mTurns.clear();
	mVisited.assign( size_t( mW ) * size_t( mH ), 0 );

	// Start mid-left facing right, three long. Three and not one because a
	// one-cell snake has no body to collide with, so the first few seconds
	// would not exercise the rule that kills it.
	const int16_t cy = int16_t( mH / 2 );
	const int16_t cx = int16_t( std::max( 3, mW / 4 ) );
	mBody.push_back( { cx, cy } );
	mBody.push_back( { int16_t( cx - 1 ), cy } );
	mBody.push_back( { int16_t( cx - 2 ), cy } );

	mDir     = Dir::Right;
	mLastDir = Dir::Right;
	mGrow    = 0;
	mScore   = 0;
	mDead    = false;
	mTicksSinceFood = 0;

	PlaceFood( rng );
}

float Snake::TickHz( const GameConfig& cfg ) const
{
	// Four to twenty-two cells a second. The curve is squared because the
	// interesting half of that range is the slow half -- the difference between
	// 4 and 7 is a different-feeling game, the difference between 18 and 22 is
	// nothing anybody can follow.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 4.0f + 18.0f * t * t;
}

bool Snake::Blocked( IVec p, bool ignoreTail ) const
{
	if( p.x < MinX() || p.y < MinY() || p.x > MaxX() || p.y > MaxY() )
		return true;

	// The tail cell vacates on the same tick the head arrives, so moving into
	// it is legal -- unless the snake is about to grow, in which case it does
	// not vacate. Getting this wrong costs one length of slack in every corner
	// and makes the autopilot far more timid than it needs to be.
	const size_t last = mBody.empty() ? 0 : mBody.size() - 1;
	for( size_t i = 0; i < mBody.size(); ++i )
	{
		if( ignoreTail && i == last && mGrow == 0 )
			continue;

		if( mBody[ i ] == p )
			return true;
	}

	return false;
}

void Snake::PlaceFood( Rng& rng )
{
	const int playW = MaxX() - MinX() + 1;
	const int playH = MaxY() - MinY() + 1;

	if( playW <= 0 || playH <= 0 )
		return;

	// Rejection sampling, capped. On a nearly-full board the cap can expire, so
	// there is a deterministic sweep behind it -- without that, a long snake
	// makes this loop run for an unbounded time on the render thread.
	for( int attempt = 0; attempt < 256; ++attempt )
	{
		const IVec p{ int16_t( MinX() + int( rng.Below( uint32_t( playW ) ) ) ),
		              int16_t( MinY() + int( rng.Below( uint32_t( playH ) ) ) ) };

		if( !Blocked( p, false ) )
		{
			mFood = p;
			return;
		}
	}

	for( int y = MinY(); y <= MaxY(); ++y )
	{
		for( int x = MinX(); x <= MaxX(); ++x )
		{
			const IVec p{ int16_t( x ), int16_t( y ) };
			if( !Blocked( p, false ) )
			{
				mFood = p;
				return;
			}
		}
	}

	// Board full: the snake has won. Nothing left to place, and the win is
	// handled as a finish below.
	mFood = mBody.front();
}

int Snake::ReachableSpace( IVec from ) const
{
	std::fill( mVisited.begin(), mVisited.end(), uint8_t( 0 ) );

	if( from.x < MinX() || from.y < MinY() || from.x > MaxX() || from.y > MaxY() )
		return 0;

	// Iterative flood fill with an explicit stack. Recursion here would be a
	// 768-deep call chain on a snaking corridor, on whatever stack Resolume
	// gave the render thread.
	std::vector< IVec > stack;
	stack.reserve( size_t( mW ) * size_t( mH ) / 4 );
	stack.push_back( from );

	int count = 0;
	while( !stack.empty() )
	{
		const IVec p = stack.back();
		stack.pop_back();

		if( p.x < MinX() || p.y < MinY() || p.x > MaxX() || p.y > MaxY() )
			continue;

		const size_t idx = size_t( p.y ) * size_t( mW ) + size_t( p.x );
		if( mVisited[ idx ] )
			continue;

		if( Blocked( p, true ) )
			continue;

		mVisited[ idx ] = 1;
		++count;

		for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
			stack.push_back( Ahead( p, Dir( d ) ) );
	}

	return count;
}

Dir Snake::ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const
{
	const Dir forward = mLastDir;
	const Dir options[ 3 ] = { forward, TurnLeft( forward ), TurnRight( forward ) };

	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

	// A flat chance of ignoring the analysis entirely. This is the whole reason
	// the autopilot is beatable: at skill 1.0 it never fires, and below that it
	// is what eventually walks the snake into a wall so the layer gets a fresh
	// game. Deliberate incompetence, not a fallback.
	if( rng.Chance( ( 1.0f - skill ) * 0.18f ) )
		return options[ rng.Below( 3 ) ];

	int bestScore  = -1;
	Dir best       = forward;
	bool found     = false;

	for( Dir d : options )
	{
		const IVec next = Ahead( mBody.front(), d );
		if( Blocked( next, true ) )
			continue;

		// Manhattan distance to the food, inverted so bigger is better.
		const int dist = std::abs( int( next.x ) - int( mFood.x ) ) +
		                 std::abs( int( next.y ) - int( mFood.y ) );
		int score = ( mW + mH ) - dist;

		// The survivability gate. A move that leaves less open space than the
		// snake is long is a move into a pocket it cannot get out of, and it is
		// how every greedy Snake AI dies. Checking it costs a flood fill per
		// candidate -- three fills over 768 cells, twenty times a second, which
		// is nothing.
		//
		// Skill decides whether it is consulted, so a mediocre autopilot walks
		// into the trap a good one sees.
		if( rng.Chance( skill ) )
		{
			const int space = ReachableSpace( next );
			if( space < int( mBody.size() ) )
				score -= 1000;
			else
				score += std::min( space, 200 );
		}

		if( !found || score > bestScore )
		{
			bestScore = score;
			best      = d;
			found     = true;
		}
	}

	// Every direction is fatal. Carry straight on and take it -- there is
	// nothing better available and the death is what restarts the game.
	return found ? best : forward;
}

void Snake::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mDead || mBody.empty() )
		return;

	// Drain the queue into turns. Directions are absolute (a pad with four
	// buttons) and Left/Right also work as relative turns when the game is
	// being driven from two buttons -- see below.
	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Up: mTurns.push_back( Dir::Up ); break;
			case Button::Down: mTurns.push_back( Dir::Down ); break;
			case Button::Left: mTurns.push_back( Dir::Left ); break;
			case Button::Right: mTurns.push_back( Dir::Right ); break;
			case Button::Fire:
				// Two-button play: Fire is "turn right from wherever you are".
				// A four-button pad is nicer, but two buttons is what a lot of
				// MIDI surfaces have spare, and Snake is playable on two.
				mTurns.push_back( TurnRight( mTurns.empty() ? mLastDir : mTurns.back() ) );
				break;
			default: break;
		}
	}

	if( cfg.autopilot )
	{
		mTurns.clear();
		mDir = ChooseAutopilot( cfg, rng );
	}
	else if( !mTurns.empty() )
	{
		// One turn per tick, validated against the direction actually last
		// travelled -- not against whatever the previous queued turn was. See
		// the header for why that distinction is the whole point.
		const Dir want = mTurns.front();
		mTurns.erase( mTurns.begin() );

		if( want != Opposite( mLastDir ) )
			mDir = want;

		// Cap the backlog. A held-down MIDI note repeating at 100 Hz would
		// otherwise queue turns faster than a 6 Hz tick can spend them, and the
		// snake would still be working through the queue seconds after the
		// player let go.
		if( mTurns.size() > 4 )
			mTurns.erase( mTurns.begin(), mTurns.end() - 4 );
	}

	const IVec next = Ahead( mBody.front(), mDir );
	mLastDir        = mDir;

	if( Blocked( next, true ) )
	{
		mDead = true;
		return;
	}

	mBody.insert( mBody.begin(), next );

	if( next == mFood )
	{
		++mScore;
		mGrow += 2;
		mTicksSinceFood = 0;

		// Board full -- the snake has filled the playfield. Treat it as a
		// finish so the respawn timer restarts the layer, rather than leaving a
		// solid rectangle on screen for the rest of the show.
		const int playCells = ( MaxX() - MinX() + 1 ) * ( MaxY() - MinY() + 1 );
		if( int( mBody.size() ) >= playCells )
		{
			mDead = true;
			return;
		}

		PlaceFood( rng );
	}
	else
	{
		++mTicksSinceFood;
	}

	if( mGrow > 0 )
		--mGrow;
	else
		mBody.pop_back();

	// A snake that has not eaten in a very long time is stuck in a loop the
	// autopilot cannot break -- usually circling its own body at low skill.
	// Ending it is better than an immortal layer that stopped changing.
	if( mTicksSinceFood > mW * mH * 2 )
		mDead = true;
}

void Snake::Draw( const GameConfig& cfg, Grid& grid ) const
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

	grid.Set( mFood.x, mFood.y, Cell::Food, 255 );

	// Tail gradient. Drawn back-to-front so the head wins where the body
	// overlaps it on the tick it dies.
	const size_t n = mBody.size();
	for( size_t i = n; i-- > 0; )
	{
		const float t       = n > 1 ? float( i ) / float( n - 1 ) : 0.0f;
		const uint8_t shade = uint8_t( 255.0f * ( 1.0f - t ) );
		grid.Set( mBody[ i ].x, mBody[ i ].y, i == 0 ? Cell::Head : Cell::Body, shade );
	}
}

float Snake::Intensity() const
{
	// Length as a fraction of a board-filling snake, softened. Gives the shader
	// something that climbs through a run and resets when it dies.
	const int playCells = std::max( 1, ( mW - 2 ) * ( mH - 2 ) );
	return std::min( 1.0f, float( mBody.size() ) / float( playCells ) * 4.0f );
}

} // namespace coinop
