#include "games/Rally.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

float Rally::TickHz( const GameConfig& cfg ) const
{
	(void)cfg;
	return 90.0f;
}

void Rally::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mHalf    = std::max( 1.5f, float( mH ) * 0.11f );
	mPaddleL = float( mH ) * 0.5f;
	mPaddleR = float( mH ) * 0.5f;
	mTargetL = mPaddleL;
	mTargetR = mPaddleR;

	mScoreL = 0;
	mScoreR = 0;
	mRally  = 0;
	mFlash  = 0;

	Serve( rng, rng.Chance( 0.5f ) ? -1 : 1 );
}

void Rally::Serve( Rng& rng, int toward )
{
	mBall = { float( mW ) * 0.5f, float( mH ) * 0.5f };

	// Angle kept well off horizontal and well off vertical: a near-horizontal
	// serve is a straight line nobody has to move for, and a near-vertical one
	// bounces off the top and bottom without crossing the court.
	const float angle = rng.Range( 0.35f, 0.85f ) * ( rng.Chance( 0.5f ) ? 1.0f : -1.0f );
	mVel              = { std::cos( angle ) * float( toward ), std::sin( angle ) };

	mServeDelay = 40;
	mRally      = 0;
}

float Rally::PredictY( float atX ) const
{
	if( std::abs( mVel.x ) < 0.0001f )
		return mBall.y;

	const float dist = ( atX - mBall.x ) / mVel.x;
	if( dist <= 0.0f )
		return float( mH ) * 0.5f;

	// Triangle-wave fold, same trick as Bricks: repeated reflections off the
	// top and bottom walls are exactly a fold of the unbounded straight line.
	float y          = mBall.y + mVel.y * dist;
	const float lo   = 1.0f;
	const float hi   = float( mH ) - 1.0f;
	const float span = std::max( 1.0f, hi - lo );

	float t = std::fmod( y - lo, span * 2.0f );
	if( t < 0.0f )
		t += span * 2.0f;
	if( t > span )
		t = span * 2.0f - t;

	return lo + t;
}

void Rally::DrivePaddle( float& y, float& target, int& cooldown,
                         const GameConfig& cfg, Rng& rng, float side )
{
	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
	const float dt    = 1.0f / TickHz( cfg );

	// Only re-aim when the ball is coming this way. A paddle that tracks a
	// receding ball looks like it is following a magnet rather than playing.
	const bool incoming = ( side < 0.0f && mVel.x < 0.0f ) || ( side > 0.0f && mVel.x > 0.0f );

	if( --cooldown <= 0 )
	{
		cooldown = int( 2.0f + ( 1.0f - skill ) * 16.0f );

		if( incoming )
		{
			const float wall = side < 0.0f ? 2.0f : float( mW ) - 3.0f;

			// The error floor is what makes the match end. Even at Skill 1.0
			// this is non-zero, so a long enough rally eventually produces a
			// miss and somebody wins.
			const float err = ( 0.12f + ( 1.0f - skill ) * 2.4f ) * mHalf;
			target          = PredictY( wall ) + rng.Range( -err, err );
		}
		else
		{
			// Drift back toward the middle between rallies.
			target = float( mH ) * 0.5f;
		}
	}

	const float maxStep = ( 10.0f + skill * 24.0f ) * dt;
	y += std::clamp( target - y, -maxStep, maxStep );
	y = std::clamp( y, mHalf + 1.0f, float( mH ) - 1.0f - mHalf );
}

void Rally::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( Finished() )
		return;

	if( mFlash > 0 )
		mFlash = uint8_t( mFlash > 20 ? mFlash - 20 : 0 );

	const float dt = 1.0f / TickHz( cfg );

	// --- Paddles ----------------------------------------------------------
	if( cfg.autopilot )
	{
		DrivePaddle( mPaddleL, mTargetL, mCooldownL, cfg, rng, -1.0f );
		DrivePaddle( mPaddleR, mTargetR, mCooldownR, cfg, rng, 1.0f );
	}
	else
	{
		// Left paddle: the Axis parameter, i.e. a fader.
		const float want = std::clamp( in.Axis(), 0.0f, 1.0f );
		mPaddleL         = mHalf + 1.0f + want * ( float( mH ) - 2.0f - 2.0f * mHalf );

		// Right paddle: Up/Down. Two players, one plugin instance.
		Button b;
		while( in.Pop( b ) )
		{
			if( b == Button::Up )
				mPaddleR -= 1.5f;
			else if( b == Button::Down )
				mPaddleR += 1.5f;
		}

		if( in.Held( Button::Up ) )
			mPaddleR -= 40.0f * dt;
		if( in.Held( Button::Down ) )
			mPaddleR += 40.0f * dt;

		mPaddleR = std::clamp( mPaddleR, mHalf + 1.0f, float( mH ) - 1.0f - mHalf );
	}

	if( mServeDelay > 0 )
	{
		--mServeDelay;
		return;
	}

	// --- Ball -------------------------------------------------------------
	const float speed = ( 10.0f + cfg.speed * 24.0f ) * ( 0.7f + cfg.difficulty * 0.8f ) *
	                    ( 1.0f + float( mRally ) * 0.02f );

	float remaining = speed * dt;

	while( remaining > 0.0f )
	{
		const float stepLen = std::min( remaining, 0.25f );
		remaining -= stepLen;

		const float vlen = std::sqrt( mVel.x * mVel.x + mVel.y * mVel.y );
		if( vlen < 0.0001f )
			break;

		mBall.x += mVel.x / vlen * stepLen;
		mBall.y += mVel.y / vlen * stepLen;

		if( mBall.y < 1.0f )
		{
			mBall.y = 1.0f;
			mVel.y  = std::abs( mVel.y );
		}
		else if( mBall.y > float( mH ) - 1.0f )
		{
			mBall.y = float( mH ) - 1.0f;
			mVel.y  = -std::abs( mVel.y );
		}

		// Paddle faces sit one cell in from each wall.
		const float faceL = 2.0f;
		const float faceR = float( mW ) - 3.0f;

		if( mVel.x < 0.0f && mBall.x <= faceL )
		{
			if( std::abs( mBall.y - mPaddleL ) <= mHalf + 0.5f )
			{
				mBall.x            = faceL;
				const float offset = std::clamp( ( mBall.y - mPaddleL ) / mHalf, -1.0f, 1.0f );
				mVel               = { std::abs( mVel.x ), offset * 0.9f };
				++mRally;
				mFlash = 255;
			}
		}
		else if( mVel.x > 0.0f && mBall.x >= faceR )
		{
			if( std::abs( mBall.y - mPaddleR ) <= mHalf + 0.5f )
			{
				mBall.x            = faceR;
				const float offset = std::clamp( ( mBall.y - mPaddleR ) / mHalf, -1.0f, 1.0f );
				mVel               = { -std::abs( mVel.x ), offset * 0.9f };
				++mRally;
				mFlash = 255;
			}
		}

		if( mBall.x < 0.0f )
		{
			++mScoreR;
			Serve( rng, 1 );
			return;
		}
		if( mBall.x > float( mW ) )
		{
			++mScoreL;
			Serve( rng, -1 );
			return;
		}
	}
}

void Rally::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	for( int x = 0; x < mW; ++x )
	{
		grid.Set( x, 0, Cell::Wall );
		grid.Set( x, mH - 1, Cell::Wall );
	}

	// Centre line, dashed. Costs four lines and is most of what makes it read
	// as a court rather than as two bars and a dot.
	for( int y = 1; y < mH - 1; y += 2 )
		grid.Set( mW / 2, y, Cell::Wall, 90 );

	const int lo = int( std::floor( -mHalf ) );
	const int hi = int( std::ceil( mHalf ) );

	for( int k = lo; k <= hi; ++k )
	{
		grid.Set( 1, int( std::floor( mPaddleL ) ) + k, Cell::Paddle, 255, 0 );
		grid.Set( mW - 2, int( std::floor( mPaddleR ) ) + k, Cell::Paddle, 255, 3 );
	}

	grid.Set( int( std::floor( mBall.x ) ), int( std::floor( mBall.y ) ), Cell::Ball, 255 );
}

float Rally::Intensity() const
{
	// Climbs through a rally and spikes on each return.
	return std::min( 1.0f, float( mRally ) * 0.05f + float( mFlash ) / 255.0f * 0.5f );
}

} // namespace coinop
