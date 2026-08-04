#include "games/Drift.h"

#include "Raster.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

float Drift::TickHz( const GameConfig& cfg ) const
{
	(void)cfg;
	return 60.0f;
}

void Drift::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	mScore = 0;
	mLives = 3;
	mWave  = 0;
	mHitFlash = 0;

	mBullets.clear();
	RespawnShip();
	SpawnWave( cfg, rng );
}

void Drift::RespawnShip()
{
	mShip    = { float( mW ) * 0.5f, float( mH ) * 0.5f };
	mShipVel = {};
	mAngle   = 0.0f;

	// Grace period. Respawning into the rock that just killed you, and losing
	// the next life instantly, burns all three in under a second.
	mInvulnTicks = 90;
}

Drift::Rock Drift::MakeRock( int size, FVec at, Rng& rng, const GameConfig& cfg ) const
{
	Rock r;
	r.pos    = at;
	r.size   = size;
	r.radius = 1.0f + float( size ) * 1.6f;
	r.ang    = rng.Range( 0.0f, kPi * 2.0f );
	r.spin   = rng.Range( -1.4f, 1.4f );

	const float speed = ( 1.6f + float( 2 - size ) * 1.1f ) * ( 0.5f + cfg.difficulty * 1.2f );
	const float dir   = rng.Range( 0.0f, kPi * 2.0f );
	r.vel             = { std::cos( dir ) * speed, std::sin( dir ) * speed };

	// A jagged silhouette. Perfectly circular rocks read as bubbles, and at
	// this resolution the jaggedness is most of what says "rock".
	for( int i = 0; i < kRockVerts; ++i )
		r.shape[ i ] = rng.Range( 0.68f, 1.25f );

	return r;
}

void Drift::SpawnWave( const GameConfig& cfg, Rng& rng )
{
	const int count = std::clamp( 3 + mWave + int( cfg.difficulty * 3.0f ), 3, 9 );

	mRocks.clear();
	for( int i = 0; i < count; ++i )
	{
		// Spawn clear of the centre, or a new wave can materialise on top of
		// the ship before the player has moved.
		FVec at;
		for( int attempt = 0; attempt < 32; ++attempt )
		{
			at = { rng.Range( 0.0f, float( mW ) ), rng.Range( 0.0f, float( mH ) ) };

			const float dx = WrapDelta( at.x, mShip.x, float( mW ) );
			const float dy = WrapDelta( at.y, mShip.y, float( mH ) );
			if( std::sqrt( dx * dx + dy * dy ) > float( std::min( mW, mH ) ) * 0.28f )
				break;
		}

		mRocks.push_back( MakeRock( 2, at, rng, cfg ) );
	}
}

void Drift::SplitRock( size_t index, Rng& rng, const GameConfig& cfg )
{
	const Rock parent = mRocks[ index ];

	mRocks.erase( mRocks.begin() + long( index ) );
	mScore += ( 3 - parent.size ) * 20;
	mHitFlash = 8;

	if( parent.size <= 0 )
		return;

	for( int i = 0; i < 2; ++i )
	{
		Rock child = MakeRock( parent.size - 1, parent.pos, rng, cfg );

		// Inherit some of the parent's momentum so the two halves visibly
		// continue what the parent was doing, rather than scattering at random.
		child.vel.x = child.vel.x * 0.7f + parent.vel.x * 0.6f;
		child.vel.y = child.vel.y * 0.7f + parent.vel.y * 0.6f;
		mRocks.push_back( child );
	}
}

void Drift::Autopilot( const GameConfig& cfg, Rng& rng, bool& fire )
{
	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
	const float dt    = 1.0f / TickHz( cfg );

	// Nearest rock by wrapped distance.
	float bestDist = 1e9f;
	float bestBear = 0.0f;
	const Rock* nearest = nullptr;

	for( const Rock& r : mRocks )
	{
		const float dx = WrapDelta( mShip.x, r.pos.x, float( mW ) );
		const float dy = WrapDelta( mShip.y, r.pos.y, float( mH ) );
		const float d  = std::sqrt( dx * dx + dy * dy );

		if( d < bestDist )
		{
			bestDist = d;
			// Screen y grows downward, so the bearing is atan2(dx, -dy) for an
			// angle measured clockwise from up.
			bestBear = std::atan2( dx, -dy );
			nearest  = &r;
		}
	}

	if( !nearest )
		return;

	// Shortest signed turn onto the bearing.
	float delta = bestBear - mAngle;
	while( delta > kPi )
		delta -= kPi * 2.0f;
	while( delta < -kPi )
		delta += kPi * 2.0f;

	// Aim error, so a low skill sprays. Scaled by distance because misjudging
	// a far rock is forgivable and misjudging a near one is what kills.
	const float aimErr = ( 1.0f - skill ) * 0.5f;
	delta += rng.Range( -aimErr, aimErr );

	const float turnRate = ( 2.2f + skill * 1.6f ) * dt;
	mAngle += std::clamp( delta, -turnRate, turnRate );

	// Fire when roughly on target. The tolerance loosens as skill drops, which
	// makes a poor autopilot shoot more and hit less -- the right shape.
	fire = std::abs( delta ) < ( 0.12f + ( 1.0f - skill ) * 0.5f );

	// Evasion. Thrust away from anything close, but only if the autopilot is
	// good enough to have noticed. This is the main thing Skill buys here:
	// below about 0.5 the ship mostly sits still and gets hit.
	mThrusting = false;
	if( bestDist < nearest->radius + 5.0f && rng.Chance( skill ) )
	{
		const float away = bestBear + kPi;
		float fleeDelta  = away - mAngle;
		while( fleeDelta > kPi )
			fleeDelta -= kPi * 2.0f;
		while( fleeDelta < -kPi )
			fleeDelta += kPi * 2.0f;

		mAngle += std::clamp( fleeDelta, -turnRate, turnRate );
		mThrusting = true;
		fire       = false;
	}
	else if( std::sqrt( mShipVel.x * mShipVel.x + mShipVel.y * mShipVel.y ) < 0.5f &&
	         rng.Chance( 0.01f ) )
	{
		// Occasional drift so a safe ship does not sit motionless in the middle
		// of the screen for a whole wave.
		mThrusting = true;
	}
}

void Drift::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( Finished() )
		return;

	const float dt = 1.0f / TickHz( cfg );

	if( mInvulnTicks > 0 )
		--mInvulnTicks;
	if( mHitFlash > 0 )
		--mHitFlash;
	if( mFireCooldown > 0.0f )
		mFireCooldown -= dt;

	bool fire = false;

	if( cfg.autopilot )
	{
		Autopilot( cfg, rng, fire );
	}
	else
	{
		mThrusting = in.Held( Button::Up );

		const float turnRate = 3.4f * dt;
		if( in.Held( Button::Left ) )
			mAngle -= turnRate;
		if( in.Held( Button::Right ) )
			mAngle += turnRate;

		Button b;
		while( in.Pop( b ) )
		{
			if( b == Button::Fire )
				fire = true;
			else if( b == Button::Left )
				mAngle -= 0.25f;
			else if( b == Button::Right )
				mAngle += 0.25f;
			else if( b == Button::Up )
				mThrusting = true;
		}
	}

	// --- Ship -------------------------------------------------------------
	if( mThrusting )
	{
		const float accel = 14.0f * dt;
		mShipVel.x += std::sin( mAngle ) * accel;
		mShipVel.y += -std::cos( mAngle ) * accel;
	}

	// Drag, and a speed cap. No drag at all is more authentic and much less
	// playable on a field this small -- the ship reaches a speed where it
	// crosses the playfield faster than anyone can react.
	const float drag = std::pow( 0.985f, dt * 60.0f );
	mShipVel.x *= drag;
	mShipVel.y *= drag;

	const float sp = std::sqrt( mShipVel.x * mShipVel.x + mShipVel.y * mShipVel.y );
	const float maxSp = 16.0f;
	if( sp > maxSp )
	{
		mShipVel.x *= maxSp / sp;
		mShipVel.y *= maxSp / sp;
	}

	mShip.x = WrapF( mShip.x + mShipVel.x * dt, float( mW ) );
	mShip.y = WrapF( mShip.y + mShipVel.y * dt, float( mH ) );

	// --- Bullets ----------------------------------------------------------
	if( fire && mFireCooldown <= 0.0f && mBullets.size() < 6 )
	{
		Bullet b;
		const float muzzle = 1.2f;
		b.pos  = { WrapF( mShip.x + std::sin( mAngle ) * muzzle, float( mW ) ),
		           WrapF( mShip.y - std::cos( mAngle ) * muzzle, float( mH ) ) };
		b.vel  = { std::sin( mAngle ) * 26.0f + mShipVel.x,
		           -std::cos( mAngle ) * 26.0f + mShipVel.y };
		b.life = 0.9f;
		mBullets.push_back( b );

		mFireCooldown = 0.18f;
	}

	for( Bullet& b : mBullets )
	{
		b.life -= dt;
		b.pos.x = WrapF( b.pos.x + b.vel.x * dt, float( mW ) );
		b.pos.y = WrapF( b.pos.y + b.vel.y * dt, float( mH ) );
	}

	mBullets.erase( std::remove_if( mBullets.begin(), mBullets.end(),
	                                []( const Bullet& b ) { return b.life <= 0.0f; } ),
	                mBullets.end() );

	// --- Rocks ------------------------------------------------------------
	for( Rock& r : mRocks )
	{
		r.pos.x = WrapF( r.pos.x + r.vel.x * dt, float( mW ) );
		r.pos.y = WrapF( r.pos.y + r.vel.y * dt, float( mH ) );
		r.ang += r.spin * dt;
	}

	// --- Bullet/rock ------------------------------------------------------
	for( size_t bi = 0; bi < mBullets.size(); )
	{
		bool consumed = false;

		for( size_t ri = 0; ri < mRocks.size(); ++ri )
		{
			const float dx = WrapDelta( mBullets[ bi ].pos.x, mRocks[ ri ].pos.x, float( mW ) );
			const float dy = WrapDelta( mBullets[ bi ].pos.y, mRocks[ ri ].pos.y, float( mH ) );

			if( dx * dx + dy * dy <= mRocks[ ri ].radius * mRocks[ ri ].radius )
			{
				mBullets.erase( mBullets.begin() + long( bi ) );
				SplitRock( ri, rng, cfg );
				consumed = true;
				break;
			}
		}

		if( !consumed )
			++bi;
	}

	// --- Ship/rock --------------------------------------------------------
	if( mInvulnTicks == 0 )
	{
		for( const Rock& r : mRocks )
		{
			const float dx = WrapDelta( mShip.x, r.pos.x, float( mW ) );
			const float dy = WrapDelta( mShip.y, r.pos.y, float( mH ) );

			// Ship treated as a point with a small hull radius. Polygon-exact
			// collision would be more correct and, at this resolution, entirely
			// invisible.
			const float hull = r.radius + 0.7f;
			if( dx * dx + dy * dy <= hull * hull )
			{
				--mLives;
				mHitFlash = 20;
				mBullets.clear();
				if( mLives > 0 )
					RespawnShip();
				return;
			}
		}
	}

	// --- Wave clear -------------------------------------------------------
	if( mRocks.empty() )
	{
		++mWave;
		mScore += 150;
		SpawnWave( cfg, rng );
	}
}

void Drift::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	// No border. This playfield wraps, and drawing walls around a wrapping
	// field tells the viewer the opposite of the truth.

	for( const Rock& r : mRocks )
	{
		float xs[ kRockVerts ];
		float ys[ kRockVerts ];

		for( int i = 0; i < kRockVerts; ++i )
		{
			const float a = r.ang + float( i ) * ( kPi * 2.0f / float( kRockVerts ) );
			xs[ i ]       = r.pos.x + std::cos( a ) * r.radius * r.shape[ i ];
			ys[ i ]       = r.pos.y + std::sin( a ) * r.radius * r.shape[ i ];
		}

		DrawPolyWrapped( grid, xs, ys, kRockVerts, Cell::Brick,
		                 uint8_t( 140 + r.size * 40 ), uint8_t( r.size ) );
	}

	for( const Bullet& b : mBullets )
		grid.Set( int( std::floor( b.pos.x ) ), int( std::floor( b.pos.y ) ), Cell::Ball, 255 );

	// Ship: a nose, two flanks and a notched tail. Four points is the smallest
	// shape that still reads as pointing somewhere at this size.
	if( mLives > 0 )
	{
		const float nose  = 1.9f;
		const float flank = 1.3f;
		const float sweep = 2.5f;

		const float pts[ 4 ][ 2 ] = {
			{ 0.0f, -nose },
			{ flank, sweep * 0.45f },
			{ 0.0f, sweep * 0.12f },
			{ -flank, sweep * 0.45f },
		};

		float xs[ 4 ];
		float ys[ 4 ];
		const float c = std::cos( mAngle );
		const float s = std::sin( mAngle );

		for( int i = 0; i < 4; ++i )
		{
			xs[ i ] = mShip.x + pts[ i ][ 0 ] * c - pts[ i ][ 1 ] * s;
			ys[ i ] = mShip.y + pts[ i ][ 0 ] * s + pts[ i ][ 1 ] * c;
		}

		// Blink while invulnerable, which is both the convention and the only
		// way to tell that a respawned ship is not yet solid.
		const bool visible = mInvulnTicks == 0 || ( mInvulnTicks / 6 ) % 2 == 0;
		if( visible )
			DrawPolyWrapped( grid, xs, ys, 4, Cell::Head, 255 );

		if( mThrusting && visible )
		{
			const float fx = mShip.x - s * ( sweep * 0.8f );
			const float fy = mShip.y + c * ( sweep * 0.8f );
			grid.Set( int( std::floor( fx ) ), int( std::floor( fy ) ), Cell::Food, 220 );
		}
	}
}

float Drift::Intensity() const
{
	return std::min( 1.0f, float( mRocks.size() ) * 0.08f +
	                           float( mHitFlash ) / 20.0f * 0.6f );
}

} // namespace coinop
