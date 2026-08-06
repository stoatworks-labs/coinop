#include "games/Trails.h"

#include <algorithm>

namespace coinop
{

bool Trails::Blocked( int x, int y ) const
{
	if( x < 1 || y < 1 || x > mW - 2 || y > mH - 2 )
		return true;

	return mBoard[ Index( x, y ) ] != 0;
}

int Trails::AliveCount() const
{
	int n = 0;
	for( int i = 0; i < mRiders; ++i )
		if( mRider[ i ].alive )
			++n;

	return n;
}

int Trails::TrailCells() const
{
	int n = 0;
	for( uint8_t c : mBoard )
		if( c != 0 )
			++n;

	return n;
}

void Trails::StartRound()
{
	std::fill( mBoard.begin(), mBoard.end(), uint8_t( 0 ) );
	std::fill( mWritten.begin(), mWritten.end(), int32_t( 0 ) );

	// Spread around the middle, each facing the way it has the most room to go.
	// Starting them all facing the same way makes the first five seconds a
	// parade rather than a fight.
	const IVec start[ kMaxRiders ] = {
		{ int16_t( std::max( 1, mW / 4 ) ), int16_t( mH / 2 ) },
		{ int16_t( std::min( mW - 2, mW * 3 / 4 ) ), int16_t( mH / 2 ) },
		{ int16_t( mW / 2 ), int16_t( std::max( 1, mH / 4 ) ) },
		{ int16_t( mW / 2 ), int16_t( std::min( mH - 2, mH * 3 / 4 ) ) },
	};
	const Dir facing[ kMaxRiders ] = { Dir::Right, Dir::Left, Dir::Down, Dir::Up };

	for( int i = 0; i < mRiders; ++i )
	{
		mRider[ i ].pos   = start[ i ];
		mRider[ i ].dir   = facing[ i ];
		mRider[ i ].alive = true;

		mBoard[ Index( mRider[ i ].pos.x, mRider[ i ].pos.y ) ] = uint8_t( i + 1 );
		mWritten[ Index( mRider[ i ].pos.x, mRider[ i ].pos.y ) ] = mTicks;
	}

	mHold = 0;
}

void Trails::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;

	mW = cfg.gridW;
	mH = cfg.gridH;

	const size_t n = size_t( mW ) * size_t( mH );
	mBoard.assign( n, 0 );
	mWritten.assign( n, 0 );
	mVisited.assign( n, 0 );
	mStack.clear();
	mStack.reserve( n / 4 + 8 );

	// Difficulty is how many riders are in the match. Two is a duel and reads
	// clearly; four fills the board four times as fast and is the setting that
	// makes the layer busy.
	mRiders = std::clamp( 2 + int( std::lround( std::clamp( cfg.difficulty, 0.0f, 1.0f ) * 2.0f ) ),
	                      2, kMaxRiders );

	for( Rider& r : mRider )
		r.wins = 0;

	mRound = 0;
	mScore = 0;
	mTicks = 0;
	mDone  = false;

	// Enough rounds that a match is a run rather than a moment, few enough that
	// the layer does actually restart. Rally's target score does the same job.
	mTarget = 5;

	StartRound();
}

float Trails::TickHz( const GameConfig& cfg ) const
{
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 8.0f + 24.0f * t * t;
}

int Trails::FreeSpace( IVec from, int budget )
{
	if( Blocked( from.x, from.y ) )
		return 0;

	std::fill( mVisited.begin(), mVisited.end(), uint8_t( 0 ) );
	mStack.clear();
	mStack.push_back( from );

	int count = 0;
	while( !mStack.empty() )
	{
		const IVec p = mStack.back();
		mStack.pop_back();

		if( Blocked( p.x, p.y ) )
			continue;

		const size_t idx = Index( p.x, p.y );
		if( mVisited[ idx ] )
			continue;

		mVisited[ idx ] = 1;
		++count;

		// The budget. Past it the answer is "plenty" and the decision does not
		// change, so counting further is work spent on nothing -- and on an
		// empty board at the start of a round it is the whole grid, four times
		// over, three directions each.
		if( count >= budget )
			break;

		for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
			mStack.push_back( Ahead( p, Dir( d ) ) );
	}

	return count;
}

Dir Trails::ChooseDir( int rider, const GameConfig& cfg, Rng& rng )
{
	Rider& r        = mRider[ rider ];
	const Dir ahead = r.dir;
	const Dir option[ 3 ] = { ahead, TurnLeft( ahead ), TurnRight( ahead ) };

	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

	// Deliberate incompetence. Four riders that all play well spiral into their
	// own quarters and survive until the board fills, which is a long, static
	// picture -- exactly what AGENTS.md says an autopilot must not produce.
	if( rng.Chance( ( 1.0f - skill ) * 0.22f ) )
	{
		Dir pick[ 3 ];
		int n = 0;
		for( Dir d : option )
		{
			const IVec next = Ahead( r.pos, d );
			if( !Blocked( next.x, next.y ) )
				pick[ n++ ] = d;
		}

		if( n > 0 )
			return pick[ rng.Below( uint32_t( n ) ) ];

		return ahead;
	}

	// A budget in the region of a quarter of the board. Big enough that a
	// genuinely cramped pocket still scores low, small enough that the common
	// case stops early.
	const int budget = std::max( 32, mW * mH / 4 );

	Dir best   = ahead;
	int bestSc = -1;
	bool found = false;

	for( Dir d : option )
	{
		const IVec next = Ahead( r.pos, d );
		if( Blocked( next.x, next.y ) )
			continue;

		int score = FreeSpace( next, budget );

		// A small bias to carrying straight on, so a rider in open space does
		// not zigzag between three directions that all score the budget.
		if( d == ahead )
			score += 3;

		// And a nudge away from the other riders' heads. Two riders converging
		// on the same corridor is the commonest double kill, and neither of
		// them can see it in a flood fill -- the cell is still open.
		for( int i = 0; i < mRiders; ++i )
		{
			if( i == rider || !mRider[ i ].alive )
				continue;

			const int gap = std::abs( int( next.x ) - int( mRider[ i ].pos.x ) ) +
			                std::abs( int( next.y ) - int( mRider[ i ].pos.y ) );
			if( gap <= 2 )
				score -= 40;
		}

		if( !found || score > bestSc )
		{
			bestSc = score;
			best   = d;
			found  = true;
		}
	}

	return best;
}

void Trails::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mDone )
		return;

	++mTicks;

	// The gap between rounds. Long enough to see who won, and the reason the
	// board is not wiped on the same tick as the last collision.
	if( mHold > 0 )
	{
		--mHold;
		if( mHold == 0 )
			StartRound();

		// Input is still drained, or a player turning during the pause finds
		// four queued turns waiting when the next round starts.
		Button drop;
		while( in.Pop( drop ) )
		{
		}
		return;
	}

	Dir playerWant = mRider[ 0 ].dir;
	bool playerSet = false;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Up: playerWant = Dir::Up; playerSet = true; break;
			case Button::Down: playerWant = Dir::Down; playerSet = true; break;
			case Button::Left: playerWant = Dir::Left; playerSet = true; break;
			case Button::Right: playerWant = Dir::Right; playerSet = true; break;
			case Button::Fire:
				playerWant = TurnRight( playerWant );
				playerSet  = true;
				break;
			default: break;
		}
	}

	// Decide every rider's direction before anything moves.
	for( int i = 0; i < mRiders; ++i )
	{
		if( !mRider[ i ].alive )
			continue;

		if( i == 0 && !cfg.autopilot )
		{
			// Reversing into your own trail is instant death and never what the
			// player meant, so it is refused rather than honoured.
			if( playerSet && playerWant != Opposite( mRider[ 0 ].dir ) )
				mRider[ 0 ].dir = playerWant;
		}
		else
		{
			mRider[ i ].dir = ChooseDir( i, cfg, rng );
		}
	}

	IVec next[ kMaxRiders ];
	bool dies[ kMaxRiders ] = {};

	for( int i = 0; i < mRiders; ++i )
	{
		if( !mRider[ i ].alive )
			continue;

		next[ i ] = Ahead( mRider[ i ].pos, mRider[ i ].dir );
		dies[ i ] = Blocked( next[ i ].x, next[ i ].y );
	}

	// The simultaneous part. Two riders arriving in the same cell both die --
	// see the header on why doing this inside the movement loop hands rider 0
	// every head-on in the game.
	for( int i = 0; i < mRiders; ++i )
	{
		if( !mRider[ i ].alive )
			continue;

		for( int j = i + 1; j < mRiders; ++j )
		{
			if( !mRider[ j ].alive )
				continue;

			if( next[ i ] == next[ j ] )
			{
				dies[ i ] = true;
				dies[ j ] = true;
			}
		}
	}

	for( int i = 0; i < mRiders; ++i )
	{
		if( !mRider[ i ].alive )
			continue;

		if( dies[ i ] )
		{
			mRider[ i ].alive = false;
			continue;
		}

		mRider[ i ].pos = next[ i ];

		const size_t idx = Index( next[ i ].x, next[ i ].y );
		mBoard[ idx ]    = uint8_t( i + 1 );
		mWritten[ idx ]  = mTicks;
	}

	if( AliveCount() > 1 )
		return;

	++mRound;

	for( int i = 0; i < mRiders; ++i )
		if( mRider[ i ].alive )
		{
			++mRider[ i ].wins;
			mScore += ( i == 0 ) ? 3 : 1;

			if( mRider[ i ].wins >= mTarget )
				mDone = true;
		}

	// A round everybody lost still counts. Without this a run of mutual head-on
	// kills would loop forever with nobody's total moving.
	if( !mDone && mRound >= mTarget * 4 )
		mDone = true;

	mHold = 12;
}

void Trails::Draw( const GameConfig& cfg, Grid& grid ) const
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

	for( int y = 1; y <= mH - 2; ++y )
		for( int x = 1; x <= mW - 2; ++x )
		{
			const size_t idx = Index( x, y );
			const uint8_t owner = mBoard[ idx ];
			if( owner == 0 )
				continue;

			// Age gradient: the newest stretch of a trail is brightest, so the
			// direction a rider came from is readable at a glance. Floored well
			// above zero -- an old trail is still a wall and has to look like
			// one.
			const int age       = std::max( 0, mTicks - mWritten[ idx ] );
			const uint8_t shade = uint8_t( std::clamp( 255 - age * 3, 110, 255 ) );

			// Rider 0 is the player's, in the palette's primary; the rest are
			// hazards. On autopilot rider 0 is played by the machine too, and
			// it still gets the player's colour -- the layer needs one thing to
			// follow.
			grid.Set( x, y, owner == 1 ? Cell::Body : Cell::Enemy, shade,
			          uint8_t( ( owner - 1 ) % 6 ) );
		}

	for( int i = 0; i < mRiders; ++i )
	{
		if( !mRider[ i ].alive )
			continue;

		grid.Set( mRider[ i ].pos.x, mRider[ i ].pos.y,
		          i == 0 ? Cell::Head : Cell::Ball, 255, uint8_t( i % 6 ) );
	}
}

float Trails::Intensity() const
{
	const int cells = std::max( 1, ( mW - 2 ) * ( mH - 2 ) );
	return std::clamp( float( TrailCells() ) / float( cells ) * 2.0f, 0.0f, 1.0f );
}

} // namespace coinop
