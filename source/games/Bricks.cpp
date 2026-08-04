#include "games/Bricks.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

float Bricks::TickHz( const GameConfig& cfg ) const
{
	// Fixed and high. Unlike Snake, Speed here scales the ball's velocity
	// rather than the tick rate -- a ball advanced 8 times a second is a ball
	// that visibly teleports, however fast it is nominally travelling.
	(void)cfg;
	return 90.0f;
}

void Bricks::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mPaddleY    = mH - 3;
	mPaddleHalf = std::max( 1.5f, float( mW ) * 0.09f );
	mPaddleX    = float( mW ) * 0.5f;

	mScore  = 0;
	mLives  = 3;
	mLevel  = 0;
	mFlash  = 0;

	BuildField( cfg, rng );
	LaunchBall( rng );
}

void Bricks::BuildField( const GameConfig& cfg, Rng& rng )
{
	const int cols = std::max( 1, BrickCols() );

	mBrickTop  = 2;
	mBrickRows = std::clamp( int( 3.0f + cfg.difficulty * 5.0f ), 3, std::max( 3, mH / 3 ) );

	mBricks.assign( size_t( cols ) * size_t( mBrickRows ), 0 );
	mTint.assign( size_t( cols ) * size_t( mBrickRows ), 0 );

	for( int row = 0; row < mBrickRows; ++row )
	{
		for( int col = 0; col < cols; ++col )
		{
			const size_t i = size_t( BrickIndex( col, row ) );

			// Higher rows are tougher, which gives the field a shape as it
			// erodes rather than dissolving uniformly. One extra hit point is
			// plenty -- two makes the first minute a grind.
			mBricks[ i ] = uint8_t( row < mBrickRows / 3 ? 2 : 1 );
			mTint[ i ]   = uint8_t( row % 6 );

			// A few gaps, seeded. Purely cosmetic, but a perfectly solid wall
			// looks generated and a slightly holed one looks played-in.
			if( rng.Chance( 0.06f ) )
				mBricks[ i ] = 0;
		}
	}
}

void Bricks::LaunchBall( Rng& rng )
{
	mBall      = { mPaddleX, float( mPaddleY ) - 1.0f };
	mWaiting   = true;
	mWaitTicks = 0;

	// Always upward, at a randomised angle well clear of vertical -- a ball
	// launched at 90 degrees just goes up and comes back down the same column,
	// which looks broken even though it is correct.
	const float angle = rng.Range( 0.6f, 1.1f ) * ( rng.Chance( 0.5f ) ? 1.0f : -1.0f );
	mVel = { std::sin( angle ), -std::cos( angle ) };
}

bool Bricks::BrickAt( int x, int y, int& outIndex ) const
{
	const int row = BrickRow( y );
	if( row < 0 || row >= mBrickRows )
		return false;

	if( x < 1 || x >= mW - 1 )
		return false;

	const int col = ( x - 1 ) / kBrickWidth;
	if( col < 0 || col >= BrickCols() )
		return false;

	const int idx = BrickIndex( col, row );
	if( idx < 0 || size_t( idx ) >= mBricks.size() || mBricks[ size_t( idx ) ] == 0 )
		return false;

	outIndex = idx;
	return true;
}

void Bricks::ClampVertical()
{
	// Trap 2. Preserve speed, force the vertical component above a floor.
	const float speed = std::sqrt( mVel.x * mVel.x + mVel.y * mVel.y );
	if( speed <= 0.0001f )
	{
		mVel = { 0.0f, -1.0f };
		return;
	}

	const float minVy = speed * kMinVerticalSpeed;
	if( std::abs( mVel.y ) < minVy )
	{
		mVel.y = mVel.y < 0.0f ? -minVy : minVy;

		// Rescale x so the ball does not speed up as a side effect of being
		// straightened out.
		const float remaining = speed * speed - mVel.y * mVel.y;
		const float newVx     = std::sqrt( std::max( 0.0f, remaining ) );
		mVel.x                = mVel.x < 0.0f ? -newVx : newVx;
	}
}

void Bricks::ReflectOffPaddle()
{
	float offset      = std::clamp( ( mBall.x - mPaddleX ) / mPaddleHalf, -1.0f, 1.0f );
	const float speed = std::sqrt( mVel.x * mVel.x + mVel.y * mVel.y );

	// Trap 4, and the one that only shows up once the autopilot is good: the
	// vertical lock.
	//
	// A ball returned from the exact centre of the paddle leaves at offset 0,
	// which is straight up. If the column above happens to be cleared it hits
	// the ceiling, comes straight back down the same column, and the paddle --
	// which has had a full traversal to centre itself perfectly on it -- returns
	// it at offset 0 again. The better the paddle, the more exactly it centres,
	// and the more perfectly the loop closes.
	//
	// Measured before this guard: at Skill 1.0 the ball hit 14 bricks in 74
	// minutes of simulated play and the game never ended. It is the mirror of
	// the horizontal lock in ClampVertical, and it needs the mirror fix -- the
	// paddle is never allowed to return the ball perfectly vertically.
	if( std::abs( offset ) < kMinPaddleOffset )
		offset = mVel.x < 0.0f ? -kMinPaddleOffset : kMinPaddleOffset;

	// Angle from where on the paddle it landed -- the control that makes the
	// game playable rather than a coin flip. 60 degrees at the tips.
	const float angle = offset * 1.05f;
	mVel              = { std::sin( angle ) * speed, -std::abs( std::cos( angle ) * speed ) };
	ClampVertical();
}

float Bricks::PredictLandingX() const
{
	// Straight-line prediction with wall reflections, ignoring bricks. Bricks
	// would change the answer, but the autopilot re-predicts constantly, so
	// being wrong about a brick two seconds out costs nothing.
	if( mVel.y <= 0.0f )
		return mPaddleX;

	const float dist = ( float( mPaddleY ) - 1.0f - mBall.y );
	if( dist <= 0.0f )
		return mBall.x;

	float x = mBall.x + mVel.x * ( dist / mVel.y );

	// Fold the unbounded x back into the playfield -- a triangle wave over the
	// playable width, which is exactly what repeated wall reflections do.
	const float lo   = 1.0f;
	const float hi   = float( mW ) - 1.0f;
	const float span = std::max( 1.0f, hi - lo );

	float t = std::fmod( x - lo, span * 2.0f );
	if( t < 0.0f )
		t += span * 2.0f;
	if( t > span )
		t = span * 2.0f - t;

	return lo + t;
}

void Bricks::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( Finished() )
		return;

	if( mFlash > 0 )
		mFlash = uint8_t( mFlash > 24 ? mFlash - 24 : 0 );

	const float hz = TickHz( cfg );
	const float dt = 1.0f / hz;

	// --- Paddle -----------------------------------------------------------
	if( cfg.autopilot )
	{
		const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

		// Reaction lag: re-aim every few ticks rather than every tick. A paddle
		// that tracks perfectly every frame reads as a machine; one that
		// commits to a target and corrects looks like someone playing.
		if( --mAiCooldown <= 0 )
		{
			mAiCooldown = int( 2.0f + ( 1.0f - skill ) * 14.0f );

			// Error scaled by skill, and by how far the ball still has to
			// travel -- misjudging a ball that is about to arrive is what
			// actually loses a life, so the error is largest early.
			const float travel = std::max( 0.0f, float( mPaddleY ) - mBall.y ) / float( mH );
			const float err    = ( 1.0f - skill ) * mPaddleHalf * 2.2f * ( 0.3f + travel );

			mAiTarget = PredictLandingX() + rng.Range( -err, err );
		}

		// Move at a finite speed. An autopilot that teleports the paddle never
		// misses regardless of how bad its aim is, which makes Skill do nothing.
		const float maxStep = ( 14.0f + skill * 26.0f ) * dt;
		const float delta   = std::clamp( mAiTarget - mPaddleX, -maxStep, maxStep );
		mPaddleX += delta;
	}
	else
	{
		// The axis is the good control here: a fader or an OSC float maps
		// straight onto paddle position across the playfield.
		const float want = std::clamp( in.Axis(), 0.0f, 1.0f );
		mPaddleX         = mPaddleHalf + want * ( float( mW ) - 2.0f * mPaddleHalf );

		// Buttons still nudge it, for surfaces with no fader to spare.
		Button b;
		while( in.Pop( b ) )
		{
			if( b == Button::Left )
				mPaddleX -= 1.5f;
			else if( b == Button::Right )
				mPaddleX += 1.5f;
			else if( b == Button::Fire )
				mWaiting = false;
		}
	}

	mPaddleX = std::clamp( mPaddleX, mPaddleHalf + 1.0f, float( mW ) - 1.0f - mPaddleHalf );

	// --- Ball -------------------------------------------------------------
	if( mWaiting )
	{
		mBall = { mPaddleX, float( mPaddleY ) - 1.0f };

		// Autoplay launches itself after a beat; a human gets to wait. Either
		// way there is a cap, so a layer left alone never sits on a held ball.
		if( ++mWaitTicks > int( hz * ( cfg.autopilot ? 0.6f : 3.0f ) ) )
			mWaiting = false;

		return;
	}

	const float speed = ( 9.0f + cfg.speed * 22.0f ) * ( 0.7f + cfg.difficulty * 0.9f ) *
	                    ( 1.0f + float( mLevel ) * 0.16f );

	float remaining = speed * dt;

	// Trap 1. Never advance more than a quarter cell between collision tests.
	while( remaining > 0.0f )
	{
		const float stepLen = std::min( remaining, 0.25f );
		remaining -= stepLen;

		const float vlen = std::sqrt( mVel.x * mVel.x + mVel.y * mVel.y );
		if( vlen < 0.0001f )
			break;

		const float sx = mVel.x / vlen * stepLen;
		const float sy = mVel.y / vlen * stepLen;

		// Trap 3. X and Y resolved separately.
		{
			const float nx = mBall.x + sx;
			const int cx   = int( std::floor( nx ) );
			const int cy   = int( std::floor( mBall.y ) );

			int brick = -1;
			if( cx < 1 || cx >= mW - 1 )
			{
				mVel.x = -mVel.x;
			}
			else if( BrickAt( cx, cy, brick ) )
			{
				--mBricks[ size_t( brick ) ];
				mScore += 1;
				mFlash = 255;
				mVel.x = -mVel.x;
			}
			else
			{
				mBall.x = nx;
			}
		}

		{
			const float ny = mBall.y + sy;
			const int cx   = int( std::floor( mBall.x ) );
			const int cy   = int( std::floor( ny ) );

			int brick = -1;
			if( cy < 1 )
			{
				mVel.y = -mVel.y;
			}
			else if( BrickAt( cx, cy, brick ) )
			{
				--mBricks[ size_t( brick ) ];
				mScore += 1;
				mFlash = 255;
				mVel.y = -mVel.y;
			}
			else if( mVel.y > 0.0f && cy >= mPaddleY &&
			         mBall.y < float( mPaddleY ) &&
			         std::abs( mBall.x - mPaddleX ) <= mPaddleHalf + 0.5f )
			{
				mBall.y = float( mPaddleY ) - 0.01f;
				ReflectOffPaddle();
			}
			else
			{
				mBall.y = ny;
			}
		}

		ClampVertical();

		if( mBall.y > float( mH ) )
		{
			--mLives;
			if( mLives > 0 )
				LaunchBall( rng );
			return;
		}
	}

	// --- Level clear ------------------------------------------------------
	if( BricksLeft() == 0 )
	{
		++mLevel;
		BuildField( cfg, rng );
		LaunchBall( rng );
	}
}

int Bricks::BricksLeft() const
{
	int n = 0;
	for( uint8_t hp : mBricks )
		if( hp > 0 )
			++n;
	return n;
}

void Bricks::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	for( int y = 0; y < mH; ++y )
	{
		grid.Set( 0, y, Cell::Wall );
		grid.Set( mW - 1, y, Cell::Wall );
	}
	for( int x = 0; x < mW; ++x )
		grid.Set( x, 0, Cell::Wall );

	// Bricks. Each brick is kBrickWidth cells wide and one tall, so the shader
	// gets cell-accurate edges and the effect variant samples the clip exactly
	// under each surviving cell.
	const int cols = BrickCols();
	for( int row = 0; row < mBrickRows; ++row )
	{
		for( int col = 0; col < cols; ++col )
		{
			const uint8_t hp = mBricks[ size_t( BrickIndex( col, row ) ) ];
			if( hp == 0 )
				continue;

			const uint8_t tint = mTint[ size_t( BrickIndex( col, row ) ) ];
			for( int k = 0; k < kBrickWidth; ++k )
				grid.Set( 1 + col * kBrickWidth + k, mBrickTop + row,
				          Cell::Brick, uint8_t( hp * 100 ), tint );
		}
	}

	const int px0 = int( std::floor( mPaddleX - mPaddleHalf ) );
	const int px1 = int( std::ceil( mPaddleX + mPaddleHalf ) );
	for( int x = px0; x <= px1; ++x )
		grid.Set( x, mPaddleY, Cell::Paddle, 255 );

	grid.Set( int( std::floor( mBall.x ) ), int( std::floor( mBall.y ) ),
	          Cell::Ball, 255, 0 );
}

float Bricks::Intensity() const
{
	if( mBricks.empty() )
		return 0.0f;

	// Rises as the field empties, plus a kick on every hit.
	const float cleared = 1.0f - float( BricksLeft() ) / float( mBricks.size() );
	return std::min( 1.0f, cleared * 0.7f + float( mFlash ) / 255.0f * 0.5f );
}

} // namespace coinop
