#include "games/Swarm.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

namespace
{

/// Row the ship sits on, one clear of the bottom wall.
int ShipRow( int h ) { return h - 2; }

} // namespace

float Swarm::SlotX( int col ) const
{
	// Two cells apart, so a formation reads as a formation rather than as a
	// solid bar, and centred on whatever width the grid happens to be.
	const int span = ( mCols - 1 ) * 2;
	const int left = ( mW - span ) / 2;
	return float( std::clamp( left + col * 2 + mDrift, 1, mW - 2 ) );
}

float Swarm::SlotY( int row ) const
{
	return float( 2 + row * 2 );
}

int Swarm::Alive() const
{
	int n = 0;
	for( const Flyer& f : mFlyer )
		if( f.state != Fly::Dead )
			++n;

	return n;
}

int Swarm::Diving() const
{
	int n = 0;
	for( const Flyer& f : mFlyer )
		if( f.state == Fly::Diving )
			++n;

	return n;
}

void Swarm::BuildWave( const GameConfig& cfg )
{
	mCols = std::clamp( ( mW - 4 ) / 2, 3, kMaxCols );
	mRows = std::clamp( mH / 8, 2, kMaxRows );

	// A formation taller than the space above the ship would start the wave
	// already on top of the player.
	while( mRows > 1 && SlotY( mRows - 1 ) > float( ShipRow( mH ) - 3 ) )
		--mRows;

	for( int i = 0; i < kMaxFlyers; ++i )
	{
		Flyer& f = mFlyer[ i ];
		f.col    = int8_t( i % kMaxCols );
		f.row    = int8_t( i / kMaxCols );
		f.state  = ( f.col < mCols && f.row < mRows ) ? Fly::Formation : Fly::Dead;
		f.x      = SlotX( f.col );
		f.y      = SlotY( f.row );
		f.phase  = 0.0f;
		f.swing  = 1;
		f.bombCool = 0;
	}

	const float diff = std::clamp( cfg.difficulty, 0.0f, 1.0f );

	mDiveEvery = std::max( 8, int( std::lround( 52.0f - 34.0f * diff ) ) - mWave * 3 );
	mMaxDivers = std::clamp( 1 + mWave / 2 + int( diff * 2.0f ), 1, 5 );
	mDiveSpeed = 0.22f + 0.20f * diff + 0.02f * float( mWave );
	mDiveSpeed = std::min( mDiveSpeed, 0.85f );

	mDriftAmp   = std::clamp( ( mW - ( mCols - 1 ) * 2 ) / 2 - 2, 1, 6 );
	mDrift      = 0;
	mDriftDir   = 1;
	mDriftTimer = 0;
	mDiveTimer  = 0;

	mBomb.clear();
	mBullet.live = false;
}

void Swarm::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;

	mW = cfg.gridW;
	mH = cfg.gridH;

	mScore = 0;
	mLives = 3;
	mWave  = 0;
	mTicks = 0;
	mHitFlash   = 0;
	mAiCooldown = 0;
	mShipX = mW / 2;

	BuildWave( cfg );
}

float Swarm::TickHz( const GameConfig& cfg ) const
{
	// Faster than Marchers, because the divers move in fractions of a cell and
	// a low tick rate turns a curve into a staircase.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 20.0f + 40.0f * t * t;
}

void Swarm::LaunchDive( Rng& rng )
{
	// Count first, then take the n-th, so the choice is uniform over the
	// survivors. Rejection sampling on a nearly-cleared wave spins.
	int candidates = 0;
	for( const Flyer& f : mFlyer )
		if( f.state == Fly::Formation )
			++candidates;

	if( candidates == 0 )
		return;

	int pick = int( rng.Below( uint32_t( candidates ) ) );
	for( Flyer& f : mFlyer )
	{
		if( f.state != Fly::Formation )
			continue;

		if( pick-- > 0 )
			continue;

		f.state = Fly::Diving;
		f.x     = SlotX( f.col );
		f.y     = SlotY( f.row );
		f.phase = 0.0f;
		f.swing = int8_t( rng.Chance( 0.5f ) ? 1 : -1 );
		return;
	}
}

void Swarm::UpdateFlyer( Flyer& f, const GameConfig& cfg, Rng& rng )
{
	switch( f.state )
	{
		case Fly::Formation:
			f.x = SlotX( f.col );
			f.y = SlotY( f.row );
			break;

		case Fly::Diving:
		{
			f.phase += 1.0f;
			f.y += mDiveSpeed;

			// Two terms, and the header explains why neither works alone: the
			// pull is re-read every tick so the dive tracks the ship, and the
			// wobble is what stops it being a homing missile.
			const float toward = std::clamp( float( mShipX ) - f.x, -1.0f, 1.0f );
			f.x += toward * 0.30f + float( f.swing ) * std::sin( f.phase * 0.22f ) * 0.35f;
			f.x = std::clamp( f.x, 1.0f, float( mW - 2 ) );

			if( f.bombCool > 0 )
				--f.bombCool;
			else if( rng.Chance( 0.05f + 0.05f * std::clamp( cfg.difficulty, 0.0f, 1.0f ) ) )
			{
				Shot s;
				s.x   = f.x;
				s.y   = f.y + 1.0f;
				s.vy  = 0.30f + 0.20f * std::clamp( cfg.difficulty, 0.0f, 1.0f );
				s.live = true;
				mBomb.push_back( s );
				f.bombCool = 20;
			}

			// Off the bottom. Round it comes, in at the top, at its own column
			// -- see the header on why it is not simply gone.
			if( f.y > float( mH ) )
			{
				f.state = Fly::Returning;
				f.y     = -2.0f;
				f.x     = SlotX( f.col );
			}
			break;
		}

		case Fly::Returning:
		{
			const float sx = SlotX( f.col );
			const float sy = SlotY( f.row );

			f.y += mDiveSpeed;
			f.x += std::clamp( sx - f.x, -0.5f, 0.5f );

			if( f.y >= sy )
			{
				f.state = Fly::Formation;
				f.x     = sx;
				f.y     = sy;
			}
			break;
		}

		default:
			break;
	}
}

int Swarm::ChooseAim( const GameConfig& cfg, Rng& rng ) const
{
	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

	// Dodge first. A bomb about to land is worth more than any shot, and an
	// autopilot that shoots through its own death is the single thing that
	// makes a shooter look unplayed rather than played badly.
	for( const Shot& s : mBomb )
	{
		if( !s.live )
			continue;

		if( s.y < float( ShipRow( mH ) - 4 ) )
			continue;

		if( std::abs( s.x - float( mShipX ) ) > 1.5f )
			continue;

		if( rng.Chance( skill ) )
			return s.x > float( mShipX ) ? -1 : 1;
	}

	// A diver that has got low is no longer a target, it is an impact. Aiming
	// at it lines the ship up underneath the thing about to land on it, which
	// is exactly the wrong move and was measured as such: the ship lost three
	// lives in under three hundred ticks, every wave, at every Skill.
	{
		const Flyer* incoming = nullptr;
		for( const Flyer& f : mFlyer )
		{
			if( f.state != Fly::Diving )
				continue;

			if( f.y < float( ShipRow( mH ) - 6 ) )
				continue;

			if( std::abs( f.x - float( mShipX ) ) > 2.5f )
				continue;

			if( !incoming || f.y > incoming->y )
				incoming = &f;
		}

		if( incoming && rng.Chance( skill ) )
			return incoming->x > float( mShipX ) ? -1 : 1;
	}

	// Then the nearest diver, because it is the one that will reach the ship.
	// Formation members are shot at only when nothing is diving.
	const Flyer* target = nullptr;
	float best          = 0.0f;
	for( const Flyer& f : mFlyer )
	{
		if( f.state == Fly::Dead )
			continue;

		const bool diving = f.state == Fly::Diving;
		const float rank  = ( diving ? 1000.0f : 0.0f ) + f.y;
		if( !target || rank > best )
		{
			best   = rank;
			target = &f;
		}
	}

	if( !target )
		return 0;

	// Tracking error, the same lever every autopilot in this plugin has. At
	// skill 1.0 it lines up exactly; below that it aims at a cell it has not
	// checked, misses, and eventually gets hit -- which is the requirement.
	const float slop = ( 1.0f - skill ) * 3.0f;
	const float want = target->x + rng.Range( -slop, slop );

	if( want > float( mShipX ) + 0.5f )
		return 1;
	if( want < float( mShipX ) - 0.5f )
		return -1;

	return 0;
}

void Swarm::LoseLife()
{
	--mLives;
	mHitFlash = 12;

	// The wave stays; only the ship and everything in flight resets. Wiping the
	// wave would make being hit a reward.
	mBomb.clear();
	mBullet.live = false;
	mShipX       = mW / 2;

	for( Flyer& f : mFlyer )
		if( f.state == Fly::Diving || f.state == Fly::Returning )
		{
			f.state = Fly::Formation;
			f.x     = SlotX( f.col );
			f.y     = SlotY( f.row );
		}
}

void Swarm::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mLives <= 0 )
		return;

	int move  = 0;
	bool fire = false;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Left: move = -1; break;
			case Button::Right: move = 1; break;
			case Button::Fire: fire = true; break;
			default: break;
		}
	}

	++mTicks;
	if( mHitFlash > 0 )
		--mHitFlash;

	if( cfg.autopilot )
	{
		move = ChooseAim( cfg, rng );

		// Reaction lag. A shot every tick is not a difficulty setting, it is a
		// different game -- the wave evaporates before a diver ever reaches the
		// ship and nobody watching sees the thing this game is about.
		if( mAiCooldown > 0 )
			--mAiCooldown;
		else
		{
			fire        = true;
			mAiCooldown = int( std::lround( 1.0f + 10.0f * ( 1.0f - std::clamp( cfg.skill, 0.0f, 1.0f ) ) ) );
		}
	}

	mShipX = std::clamp( mShipX + move, 1, mW - 2 );

	if( fire && !mBullet.live )
	{
		mBullet.x    = float( mShipX );
		mBullet.y    = float( ShipRow( mH ) - 1 );
		mBullet.vy   = -0.9f;
		mBullet.live = true;
	}

	// Formation drift. A triangle wave on a timer, so the slots stay on exact
	// cells -- see the header field comment.
	++mDriftTimer;
	if( mDriftTimer >= 6 )
	{
		mDriftTimer = 0;
		mDrift += mDriftDir;
		if( mDrift >= mDriftAmp || mDrift <= -mDriftAmp )
			mDriftDir = -mDriftDir;
	}

	++mDiveTimer;
	if( mDiveTimer >= mDiveEvery && Diving() < mMaxDivers )
	{
		mDiveTimer = 0;
		LaunchDive( rng );
	}

	for( Flyer& f : mFlyer )
		UpdateFlyer( f, cfg, rng );

	if( mBullet.live )
	{
		mBullet.y += mBullet.vy;
		if( mBullet.y < 1.0f )
			mBullet.live = false;
	}

	if( mBullet.live )
	{
		for( Flyer& f : mFlyer )
		{
			if( f.state == Fly::Dead )
				continue;

			// A cell either side, because the bullet moves nearly a cell a tick
			// and an exact-cell test lets it pass straight through a flyer on
			// the tick they cross. Marchers has the same guard for the same
			// reason.
			if( std::abs( f.x - mBullet.x ) > 0.75f || std::abs( f.y - mBullet.y ) > 0.9f )
				continue;

			// A diving attacker is worth more than one sitting in the formation.
			mScore += f.state == Fly::Diving ? 3 : 1;
			f.state      = Fly::Dead;
			mBullet.live = false;
			break;
		}
	}

	for( Shot& s : mBomb )
	{
		if( !s.live )
			continue;

		s.y += s.vy;
		if( s.y > float( mH - 1 ) )
		{
			s.live = false;
			continue;
		}

		if( int( std::lround( s.y ) ) == ShipRow( mH ) &&
		    std::abs( s.x - float( mShipX ) ) < 0.9f )
		{
			s.live = false;
			LoseLife();
			return;
		}
	}

	mBomb.erase( std::remove_if( mBomb.begin(), mBomb.end(),
	                             []( const Shot& s ) { return !s.live; } ),
	             mBomb.end() );

	for( const Flyer& f : mFlyer )
	{
		if( f.state != Fly::Diving )
			continue;

		if( std::abs( f.x - float( mShipX ) ) < 0.9f &&
		    std::abs( f.y - float( ShipRow( mH ) ) ) < 0.9f )
		{
			LoseLife();
			return;
		}
	}

	if( Alive() == 0 )
	{
		++mWave;
		mScore += 20;
		BuildWave( cfg );
	}
}

void Swarm::Draw( const GameConfig& cfg, Grid& grid ) const
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

	for( const Flyer& f : mFlyer )
	{
		if( f.state == Fly::Dead )
			continue;

		// Divers are drawn at full brightness and formation members a shade
		// down, which is the only cue the player has for which of them is
		// currently trying to kill them.
		const uint8_t shade = f.state == Fly::Formation ? 190 : 255;
		grid.Set( int( std::floor( f.x + 0.5f ) ), int( std::floor( f.y + 0.5f ) ),
		          Cell::Enemy, shade, uint8_t( f.row % 6 ) );
	}

	for( const Shot& s : mBomb )
		if( s.live )
			grid.Set( int( std::floor( s.x + 0.5f ) ), int( std::floor( s.y + 0.5f ) ),
			          Cell::Enemy, 120 );

	if( mBullet.live )
		grid.Set( int( std::floor( mBullet.x + 0.5f ) ), int( std::floor( mBullet.y + 0.5f ) ),
		          Cell::Ball, 255 );

	// Four cells of ship, because one cell is a dot and the player needs to see
	// which way it is pointing at a glance.
	//
	// The wings go on the ship's own row and the nose *above* it. Hanging them
	// a row below put them on `mH - 1`, which is the bottom border: the ship
	// overdrew the wall it stands on, and the only place that showed was a
	// rendered frame.
	const int shipY      = ShipRow( mH );
	const uint8_t bright = mHitFlash > 0 ? 120 : 255;
	grid.Set( mShipX, shipY - 1, Cell::Paddle, bright );
	grid.Set( mShipX, shipY, Cell::Paddle, bright );
	grid.Set( mShipX - 1, shipY, Cell::Paddle, uint8_t( bright * 3 / 4 ) );
	grid.Set( mShipX + 1, shipY, Cell::Paddle, uint8_t( bright * 3 / 4 ) );
}

float Swarm::Intensity() const
{
	// Divers in the air, not attackers alive. A full formation sitting still is
	// the calm part of the wave and should look like it.
	const float divers = float( Diving() ) / float( std::max( 1, mMaxDivers ) );
	return std::clamp( divers, 0.0f, 1.0f );
}

} // namespace coinop
