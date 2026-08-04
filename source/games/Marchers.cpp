#include "games/Marchers.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

float Marchers::TickHz( const GameConfig& cfg ) const
{
	// Shots and the cannon want a smooth rate; the formation marches on a
	// counter underneath it rather than on its own clock.
	return 20.0f + std::clamp( cfg.speed, 0.0f, 1.0f ) * 40.0f;
}

void Marchers::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;
	mW = cfg.gridW;
	mH = cfg.gridH;

	mScore   = 0;
	mLives   = 3;
	mWave    = 0;
	mLanded  = false;
	mCannonX = mW / 2;
	mHitFlash = 0;

	BuildWave( cfg );
}

void Marchers::BuildWave( const GameConfig& cfg )
{
	// Two cells per invader horizontally so they read as objects rather than
	// as a solid block at low grid sizes.
	mCols = std::clamp( ( mW - 4 ) / 2, 3, 11 );
	mRows = std::clamp( 3 + int( cfg.difficulty * 3.0f ), 3, std::max( 3, ( mH - 8 ) / 2 ) );

	mAlive.assign( size_t( mCols ) * size_t( mRows ), 1 );

	mOffsetX    = 1;
	mOffsetY    = 1 + std::min( mWave, 4 );// Each wave starts lower.
	mMarchDir   = 1;
	mMarchTimer = 0;

	mBullet = Shot{};
	mBombs.clear();
}

bool Marchers::AliveAt( int col, int row ) const
{
	if( col < 0 || row < 0 || col >= mCols || row >= mRows )
		return false;

	return mAlive[ size_t( Index( col, row ) ) ] != 0;
}

int Marchers::Alive() const
{
	int n = 0;
	for( uint8_t a : mAlive )
		n += a ? 1 : 0;
	return n;
}

int Marchers::LowestInColumn( int col ) const
{
	for( int row = mRows - 1; row >= 0; --row )
		if( AliveAt( col, row ) )
			return row;

	return -1;
}

int Marchers::FormationLeft() const
{
	for( int col = 0; col < mCols; ++col )
		for( int row = 0; row < mRows; ++row )
			if( AliveAt( col, row ) )
				return mOffsetX + col * 2;

	return mOffsetX;
}

int Marchers::FormationRight() const
{
	for( int col = mCols - 1; col >= 0; --col )
		for( int row = 0; row < mRows; ++row )
			if( AliveAt( col, row ) )
				return mOffsetX + col * 2;

	return mOffsetX;
}

void Marchers::MarchStep()
{
	const int left  = FormationLeft();
	const int right = FormationRight();

	if( ( mMarchDir > 0 && right + 1 >= mW - 1 ) || ( mMarchDir < 0 && left - 1 <= 0 ) )
	{
		mMarchDir = -mMarchDir;
		++mOffsetY;
	}
	else
	{
		mOffsetX += mMarchDir;
	}

	// Landed. Not a life lost -- the game is simply over, as it should be.
	for( int col = 0; col < mCols; ++col )
	{
		const int row = LowestInColumn( col );
		if( row >= 0 && mOffsetY + row >= mH - 3 )
		{
			mLanded = true;
			return;
		}
	}
}

void Marchers::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( Finished() )
		return;

	if( mHitFlash > 0 )
		--mHitFlash;

	const float hz = TickHz( cfg );
	const float dt = 1.0f / hz;
	const int cannonY = mH - 2;

	// --- March timer ------------------------------------------------------
	//
	// The acceleration. Interval falls with the survivor count, so the last
	// invader moves several times faster than a full formation.
	const int total   = std::max( 1, int( mAlive.size() ) );
	const int alive   = std::max( 1, Alive() );
	const float frac  = float( alive ) / float( total );
	const int interval = std::max( 1, int( ( 2.0f + 16.0f * frac ) /
	                                       ( 0.6f + cfg.difficulty ) ) );

	if( ++mMarchTimer >= interval )
	{
		mMarchTimer = 0;
		MarchStep();
		if( mLanded )
			return;
	}

	// --- Cannon -----------------------------------------------------------
	bool wantFire = false;

	if( cfg.autopilot )
	{
		const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

		if( --mAiCooldown <= 0 )
		{
			mAiCooldown = int( 1.0f + ( 1.0f - skill ) * 8.0f );

			// Aim at the lowest-hanging column -- the one that will land first
			// and the one most likely to be shooting back.
			int bestCol = -1;
			int bestRow = -1;
			for( int col = 0; col < mCols; ++col )
			{
				const int row = LowestInColumn( col );
				if( row > bestRow )
				{
					bestRow = row;
					bestCol = col;
				}
			}

			int target = bestCol >= 0 ? mOffsetX + bestCol * 2 : mCannonX;

			// Dodging, at high skill only. A bomb in the cannon's column is
			// worth stepping out of; a poor player does not notice.
			for( const Shot& bomb : mBombs )
			{
				if( !bomb.live )
					continue;

				if( std::abs( int( bomb.x ) - mCannonX ) <= 1 &&
				    bomb.y > float( mH ) * 0.4f && rng.Chance( skill ) )
				{
					target = mCannonX + ( bomb.x <= mCannonX ? 3 : -3 );
				}
			}

			// Aim error, so a low skill misses honestly rather than by refusing
			// to shoot.
			if( rng.Chance( 1.0f - skill ) )
				target += int( rng.Range( -3.0f, 3.0f ) );

			mCannonX += ( target > mCannonX ) ? 1 : ( target < mCannonX ? -1 : 0 );
		}

		wantFire = !mBullet.live && rng.Chance( 0.15f + skill * 0.35f );
	}
	else
	{
		if( in.Axis() >= 0.0f )
		{
			// A fader drives the cannon directly when one is mapped; the
			// buttons below still work either way.
		}

		Button b;
		while( in.Pop( b ) )
		{
			if( b == Button::Left )
				--mCannonX;
			else if( b == Button::Right )
				++mCannonX;
			else if( b == Button::Fire )
				wantFire = true;
		}

		if( in.Held( Button::Left ) )
			--mCannonX;
		if( in.Held( Button::Right ) )
			++mCannonX;
	}

	mCannonX = std::clamp( mCannonX, 1, mW - 2 );

	if( wantFire && !mBullet.live )
	{
		mBullet.live = true;
		mBullet.x    = int16_t( mCannonX );
		mBullet.y    = float( cannonY - 1 );
		mBullet.vy   = -28.0f;
	}

	// --- Player shot ------------------------------------------------------
	if( mBullet.live )
	{
		// Substepped for the same tunnelling reason as the ball in Bricks: a
		// fast shot must not step over the invader it should have hit.
		float travel = std::abs( mBullet.vy ) * dt;
		while( travel > 0.0f && mBullet.live )
		{
			const float stepLen = std::min( travel, 0.5f );
			travel -= stepLen;
			mBullet.y -= stepLen;

			if( mBullet.y < 1.0f )
			{
				mBullet.live = false;
				break;
			}

			const int row = int( std::floor( mBullet.y ) ) - mOffsetY;
			if( row >= 0 && row < mRows )
			{
				const int col = ( int( mBullet.x ) - mOffsetX );
				if( col >= 0 && ( col % 2 ) == 0 && ( col / 2 ) < mCols &&
				    AliveAt( col / 2, row ) )
				{
					mAlive[ size_t( Index( col / 2, row ) ) ] = 0;
					mScore += 10;
					mBullet.live = false;
					mHitFlash    = 6;
				}
			}
		}
	}

	// --- Bombs ------------------------------------------------------------
	const float bombChance = ( 0.01f + cfg.difficulty * 0.05f ) * ( 1.0f - frac * 0.5f );
	if( rng.Chance( bombChance ) && mBombs.size() < 12 )
	{
		const int col = int( rng.Below( uint32_t( mCols ) ) );
		const int row = LowestInColumn( col );
		if( row >= 0 )
		{
			Shot bomb;
			bomb.live = true;
			bomb.x    = int16_t( mOffsetX + col * 2 );
			bomb.y    = float( mOffsetY + row + 1 );
			bomb.vy   = 7.0f + cfg.difficulty * 9.0f;
			mBombs.push_back( bomb );
		}
	}

	for( Shot& bomb : mBombs )
	{
		if( !bomb.live )
			continue;

		bomb.y += bomb.vy * dt;

		if( bomb.y >= float( cannonY ) && std::abs( int( bomb.x ) - mCannonX ) <= 1 )
		{
			bomb.live = false;
			--mLives;
			mHitFlash = 12;
		}
		else if( bomb.y > float( mH ) )
		{
			bomb.live = false;
		}
	}

	mBombs.erase( std::remove_if( mBombs.begin(), mBombs.end(),
	                              []( const Shot& s ) { return !s.live; } ),
	              mBombs.end() );

	// --- Wave clear -------------------------------------------------------
	if( Alive() == 0 )
	{
		++mWave;
		mScore += 100;
		BuildWave( cfg );
	}
}

void Marchers::Draw( const GameConfig& cfg, Grid& grid ) const
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

	// Formation. Tint by row so the shader can colour the ranks differently.
	for( int row = 0; row < mRows; ++row )
	{
		for( int col = 0; col < mCols; ++col )
		{
			if( !AliveAt( col, row ) )
				continue;

			grid.Set( mOffsetX + col * 2, mOffsetY + row,
			          Cell::Brick, 255, uint8_t( row % 6 ) );
		}
	}

	const int cannonY = mH - 2;
	grid.Set( mCannonX, cannonY, Cell::Paddle, 255 );
	grid.Set( mCannonX - 1, cannonY, Cell::Paddle, 160 );
	grid.Set( mCannonX + 1, cannonY, Cell::Paddle, 160 );
	grid.Set( mCannonX, cannonY - 1, Cell::Paddle, 200 );

	if( mBullet.live )
		grid.Set( mBullet.x, int( std::floor( mBullet.y ) ), Cell::Ball, 255 );

	for( const Shot& bomb : mBombs )
		if( bomb.live )
			grid.Set( bomb.x, int( std::floor( bomb.y ) ), Cell::Food, 200 );
}

float Marchers::Intensity() const
{
	if( mAlive.empty() )
		return 0.0f;

	const float cleared = 1.0f - float( Alive() ) / float( mAlive.size() );
	return std::min( 1.0f, cleared * 0.8f + float( mHitFlash ) / 12.0f * 0.4f );
}

} // namespace coinop
