#include "games/Girders.h"

#include <algorithm>
#include <climits>

namespace coinop
{

int Girders::RowY( int r ) const
{
	if( mRows <= 0 )
		return mH - 2;

	return mRowY[ std::clamp( r, 0, mRows - 1 ) ];
}

bool Girders::LadderAt( int gap, int x ) const
{
	if( gap < 0 || gap >= int( mLadder.size() ) )
		return false;

	for( int16_t c : mLadder[ size_t( gap ) ] )
		if( c == x )
			return true;

	return false;
}

void Girders::BuildLevel( const GameConfig& cfg, Rng& rng )
{
	// Rows from the bottom up, four cells apart where there is room. The count
	// is capped rather than derived alone: a 96-cell-tall pixel map would
	// otherwise get twenty-three floors, and a climber that needs twenty-three
	// ladders to reach the prize never reaches it.
	mGap  = std::clamp( mH / 6, 3, 6 );
	mRows = std::clamp( ( mH - 4 ) / mGap, 2, kMaxRows );

	for( int r = 0; r < mRows; ++r )
	{
		mRowY[ r ]   = mH - 2 - r * mGap;
		mRowDir[ r ] = int8_t( ( r % 2 == 0 ) ? 1 : -1 );
	}

	// Trim any row that ended up in the top wall. Happens on a short grid and
	// silently produced a prize sitting on the border before it was handled.
	while( mRows > 2 && mRowY[ mRows - 1 ] < 2 )
		--mRows;

	mLadder.assign( size_t( std::max( 0, mRows - 1 ) ), {} );

	const int lo = 1;
	const int hi = mW - 2;
	const int span = std::max( 1, hi - lo );

	for( int gap = 0; gap < mRows - 1; ++gap )
	{
		// Two ladders per gap, forced apart. One ladder makes the route a
		// single column and the game becomes a queue; two placed without a
		// minimum separation land next to each other about a third of the time
		// and amount to the same thing.
		const int a = lo + int( rng.Below( uint32_t( std::max( 1, span / 2 ) ) ) );
		const int b = lo + span / 2 + int( rng.Below( uint32_t( std::max( 1, span / 2 ) ) ) );

		mLadder[ size_t( gap ) ].push_back( int16_t( std::clamp( a, lo, hi ) ) );
		mLadder[ size_t( gap ) ].push_back( int16_t( std::clamp( b, lo, hi ) ) );
	}

	// The prize sits on the top row, away from the ladder that reaches it, so
	// arriving is not the same instant as winning.
	const int topGap = mRows - 2;
	int prizeX       = mW / 2;
	if( topGap >= 0 && !mLadder[ size_t( topGap ) ].empty() )
	{
		const int ladderX = mLadder[ size_t( topGap ) ][ 0 ];
		prizeX            = ladderX > ( lo + hi ) / 2 ? lo + 1 : hi - 1;
	}

	mPrize = { int16_t( std::clamp( prizeX, lo, hi ) ), int16_t( StandY( mRows - 1 ) ) };

	const float diff = std::clamp( cfg.difficulty, 0.0f, 1.0f );
	mSpawnInterval   = int( std::lround( 52.0f - 34.0f * diff ) );
	mSpawnInterval   = std::max( 8, mSpawnInterval );
	mBarrelSpeed     = std::clamp( int( std::lround( 2.0f + 3.0f * diff ) ), 1, 6 );
	mDescendOdds     = 0.25f + 0.25f * diff;

	mBarrel.clear();
	mSpawnTimer = 0;
}

void Girders::PlaceClimber()
{
	mX        = 1;
	mY        = int16_t( StandY( 0 ) );
	mRow      = 0;
	mClimbing = false;
	mClimbGap = 0;
	mFacing   = 1;
	mJump     = 0;
	mJumpCool = 0;
	mStuck    = 0;
	mProgressRow = 0;
}

void Girders::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mScore = 0;
	mLives = 3;
	mLevel = 0;
	mTicks = 0;

	BuildLevel( cfg, rng );
	PlaceClimber();
}

float Girders::TickHz( const GameConfig& cfg ) const
{
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 6.0f + 22.0f * t * t;
}

void Girders::LoseLife()
{
	--mLives;
	if( mLives > 0 )
	{
		PlaceClimber();
		mBarrel.clear();
		mSpawnTimer = 0;
	}
}

void Girders::MoveBarrel( Barrel& bar, Rng& rng )
{
	// Descending means going *down*, which is a lower row index and a larger y.
	if( bar.descending )
	{
		++bar.y;
		if( bar.y >= StandY( bar.row - 1 ) )
		{
			bar.row        = int8_t( std::max( bar.row - 1, 0 ) );
			bar.y          = int16_t( StandY( bar.row ) );
			bar.dir        = mRowDir[ bar.row ];
			bar.descending = false;
		}
		return;
	}

	const int nx = bar.x + bar.dir;
	if( nx < 1 || nx > mW - 2 )
	{
		// Out of floor. Down a level, or gone if this was the bottom one.
		if( bar.row - 1 >= 0 )
			bar.descending = true;
		else
			bar.row = int8_t( -1 );// marked for removal

		return;
	}

	bar.x = int16_t( nx );

	// The one roll of the dice in a barrel's life. Checked after the move so a
	// barrel cannot take the ladder it started the row on and immediately fall
	// through every floor in one straight line.
	if( bar.row - 1 >= 0 && LadderAt( bar.row - 1, bar.x ) && rng.Chance( mDescendOdds ) )
		bar.descending = true;
}

Girders::Intent Girders::ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const
{
	Intent out;

	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

	// Deliberate incompetence, same as everywhere else in this plugin: without
	// it the climber tops out forever and the layer stops changing.
	if( rng.Chance( ( 1.0f - skill ) * 0.45f ) )
	{
		out.move = rng.Chance( 0.5f ) ? -1 : 1;
		return out;
	}

	if( mClimbing )
	{
		// Committed. Coming back down a ladder to dodge something is a move a
		// good player makes and a two-line autopilot gets wrong far more often
		// than it gets right.
		out.climb = -1;
		return out;
	}

	// Anything rolling toward the climber on this floor, and how far away.
	int threat = INT_MAX;
	for( const Barrel& bar : mBarrel )
	{
		if( bar.row != mRow || bar.descending )
			continue;

		const int delta = int( bar.x ) - int( mX );
		if( delta * bar.dir > 0 )
			continue;// rolling away

		threat = std::min( threat, std::abs( delta ) );
	}

	if( threat <= 2 && mJump == 0 && mJumpCool == 0 && rng.Chance( skill ) )
	{
		out.jump = true;
		return out;
	}

	if( mRow + 1 >= mRows )
	{
		// Top floor: walk at the prize.
		out.move = mPrize.x > mX ? 1 : ( mPrize.x < mX ? -1 : 0 );
		return out;
	}

	// Otherwise head for the nearest ladder up. Ties go to the lower column, so
	// the choice is stable from tick to tick -- an autopilot that reconsiders
	// between two equidistant ladders walks on the spot.
	int target   = mX;
	int bestDist = INT_MAX;
	for( int16_t c : mLadder[ size_t( mRow ) ] )
	{
		const int d = std::abs( int( c ) - int( mX ) );
		if( d < bestDist )
		{
			bestDist = d;
			target   = c;
		}
	}

	if( target == mX )
		out.climb = -1;
	else
		out.move = target > mX ? 1 : -1;

	return out;
}

void Girders::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mLives <= 0 )
		return;

	Intent intent;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Left: intent.move = -1; break;
			case Button::Right: intent.move = 1; break;
			case Button::Up: intent.climb = -1; break;
			case Button::Down: intent.climb = 1; break;
			case Button::Fire: intent.jump = true; break;
			default: break;
		}
	}

	++mTicks;

	if( cfg.autopilot )
		intent = ChooseAutopilot( cfg, rng );

	if( mJump > 0 )
	{
		--mJump;
		if( mJump == 0 )
			mJumpCool = 4;
	}
	else if( mJumpCool > 0 )
	{
		--mJumpCool;
	}
	else if( intent.jump && !mClimbing )
	{
		mJump = 3;
	}

	if( mClimbing )
	{
		if( intent.climb != 0 )
		{
			const int upperY = StandY( mClimbGap + 1 );
			const int lowerY = StandY( mClimbGap );
			mY = int16_t( std::clamp( int( mY ) + intent.climb, upperY, lowerY ) );

			if( mY == upperY )
			{
				mRow      = mClimbGap + 1;
				mClimbing = false;
			}
			else if( mY == lowerY )
			{
				mRow      = mClimbGap;
				mClimbing = false;
			}
		}
	}
	else
	{
		if( intent.move != 0 )
		{
			mFacing = intent.move;
			mX      = int16_t( std::clamp( int( mX ) + intent.move, 1, mW - 2 ) );
		}

		// Rows are numbered from the bottom, so going up is a *higher* row
		// index and a *smaller* y. Getting that backwards is the one mistake
		// this file invites, so both cases name the gap explicitly.
		if( intent.climb < 0 && LadderUpAt( mRow, mX ) )
		{
			mClimbGap = mRow;// the run between mRow and mRow + 1
			mClimbing = true;
			--mY;
		}
		else if( intent.climb > 0 && LadderDownAt( mRow, mX ) )
		{
			mClimbGap = mRow - 1;// the run between mRow - 1 and mRow
			mClimbing = true;
			++mY;
		}
	}

	++mSpawnTimer;
	if( mSpawnTimer >= mSpawnInterval && mRows >= 2 )
	{
		mSpawnTimer = 0;

		Barrel bar;
		bar.row = int8_t( mRows - 1 );
		bar.dir = mRowDir[ mRows - 1 ];
		bar.x   = int16_t( bar.dir > 0 ? 1 : mW - 2 );
		bar.y   = int16_t( StandY( mRows - 1 ) );
		mBarrel.push_back( bar );
	}

	for( Barrel& bar : mBarrel )
	{
		bar.accum += mBarrelSpeed;
		while( bar.accum >= 6 && bar.row >= 0 )
		{
			bar.accum -= 6;
			MoveBarrel( bar, rng );
		}
	}

	mBarrel.erase( std::remove_if( mBarrel.begin(), mBarrel.end(),
	                               []( const Barrel& bar ) { return bar.row < 0; } ),
	               mBarrel.end() );

	// The hop's whole purpose: while it is up, the floor is not lethal. A
	// barrel that is mid-descent is at a different height and still is.
	const bool airborne = mJump > 0 && !mClimbing;
	for( const Barrel& bar : mBarrel )
	{
		if( bar.x != mX )
			continue;

		if( bar.y == mY && !airborne )
		{
			LoseLife();
			return;
		}

		if( bar.y == mY && airborne )
		{
			// Cleared one. Scored once, on the tick the hop starts, or a
			// three-tick hop over a stationary barrel would pay three times.
			if( mJump == 3 )
				++mScore;
		}
	}

	if( !mClimbing && mRow == mRows - 1 && mX == mPrize.x && mY == mPrize.y )
	{
		mScore += 25;
		++mLevel;

		// Each level rebuilds the ladders and tightens the spawn. The rebuild
		// is what keeps a long survival from being the same picture twice.
		//
		// Recomputed from the difficulty each time rather than decremented, or
		// the tightening compounds -- three levels took the interval from 35
		// ticks to its floor and the climber was walking into a solid wall of
		// barrels by its fourth prize.
		BuildLevel( cfg, rng );
		mSpawnInterval = std::max( 7, mSpawnInterval - 3 * mLevel );
		mBarrelSpeed   = std::min( 6, mBarrelSpeed + mLevel / 3 );
		PlaceClimber();
		return;
	}

	// A climber that has stopped making progress -- pinned at one end of a
	// floor by a stream of barrels, usually -- ends the life rather than
	// standing there for the rest of the show.
	//
	// Changing floor is what counts as progress. Anything finer, like moving at
	// all, never trips; anything coarser, like reaching the prize, kills a
	// climber that is simply having a slow level.
	if( mRow != mProgressRow )
	{
		mProgressRow = mRow;
		mStuck       = 0;
	}

	++mStuck;
	if( mStuck > mW * mH )
		LoseLife();
}

void Girders::Draw( const GameConfig& cfg, Grid& grid ) const
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

	for( int r = 0; r < mRows; ++r )
		for( int x = 1; x <= mW - 2; ++x )
			grid.Set( x, RowY( r ), Cell::Wall, 200 );

	// Ladders on top of the girders they pass through, so a rung is visible at
	// the floor it lands on. Drawn as Paddle: it is structure the player uses
	// rather than structure that stops them, and the palette already separates
	// those two things.
	for( int gap = 0; gap < int( mLadder.size() ); ++gap )
		for( int16_t c : mLadder[ size_t( gap ) ] )
			for( int y = StandY( gap + 1 ); y <= StandY( gap ); ++y )
				grid.Set( c, y, Cell::Paddle, 180 );

	grid.Set( mPrize.x, mPrize.y, Cell::Food, 255 );

	for( const Barrel& bar : mBarrel )
		grid.Set( bar.x, bar.y, Cell::Enemy, bar.descending ? 150 : 255 );

	const int drawY = ( mJump > 0 && !mClimbing ) ? mY - 1 : mY;
	grid.Set( mX, drawY, Cell::Head, 255 );
}

float Girders::Intensity() const
{
	// How far up the stack the climber has got, plus a spike for anything
	// rolling at them on this floor.
	float height = mRows > 1 ? float( mRows - 1 - mRow ) / float( mRows - 1 ) : 0.0f;

	for( const Barrel& bar : mBarrel )
		if( bar.row == mRow && std::abs( int( bar.x ) - int( mX ) ) <= 3 )
			height = std::max( height, 0.85f );

	return std::clamp( height, 0.0f, 1.0f );
}

} // namespace coinop
