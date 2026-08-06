#include "games/Rafters.h"

#include <algorithm>
#include <climits>

namespace coinop
{

namespace
{

constexpr float kGravity  = 0.09f;
constexpr float kJump     = 0.80f;
constexpr float kWalk     = 0.34f;
/// How long a crawler stays on its back.
///
/// It has to be longer than it takes to cross a floor, find the gap in the one
/// above and climb through it -- at a third of a cell a tick that is comfortably
/// over a hundred ticks on a 32-wide grid. Ninety was not enough and the flip
/// was a move with no follow-up.
constexpr int kFlipTicks  = 170;
constexpr int kBumpTicks  = 5;
constexpr int kBumpReach  = 1;

/// Rows between platforms.
///
/// Three, and it is load-bearing rather than a look. A jump from a standing
/// start reaches `kJump^2 / (2 * kGravity)` cells, which is a shade over three
/// and a half: enough to strike the underside of the platform three rows up,
/// and enough to land on top of it if there is a gap to pass through. At four
/// rows the bump still connects and the climb does not, and the game becomes
/// one floor deep.
constexpr int kSpacing = 3;

} // namespace

bool Rafters::Solid( int x, int y ) const
{
	if( x < 0 || y < 0 || x >= mW || y >= mH )
		return true;

	return mSolid[ size_t( y ) * size_t( mW ) + size_t( x ) ] != 0;
}

int Rafters::Flipped() const
{
	int n = 0;
	for( const Crawler& c : mCrawler )
		if( c.flip > 0 )
			++n;

	return n;
}

IVec Rafters::Player() const
{
	return { int16_t( Round( mPlayer.x ) ), int16_t( Round( mPlayer.y ) ) };
}

void Rafters::BuildLevel( const GameConfig& cfg, Rng& rng )
{
	const size_t n = size_t( mW ) * size_t( mH );
	mSolid.assign( n, 0 );

	for( int x = 0; x < mW; ++x )
	{
		mSolid[ Index( x, 0 ) ]      = 1;
		mSolid[ Index( x, mH - 1 ) ] = 1;
	}
	for( int y = 0; y < mH; ++y )
	{
		mSolid[ Index( 0, y ) ]      = 1;
		mSolid[ Index( mW - 1, y ) ] = 1;
	}

	mPlatformY.clear();
	for( int y = mH - 2; y >= 3; y -= kSpacing )
		mPlatformY.push_back( int16_t( y ) );

	// Stored top first, which is the order the autopilot reads them in.
	std::reverse( mPlatformY.begin(), mPlatformY.end() );

	for( size_t i = 0; i < mPlatformY.size(); ++i )
	{
		const int y = mPlatformY[ i ];
		for( int x = 1; x <= mW - 2; ++x )
			mSolid[ Index( x, y ) ] = 1;

		// The bottom platform is the floor and has no gaps -- a hole in it
		// would drop the player into the bottom wall with nothing to stand on.
		if( y == mH - 2 )
			continue;

		// Gaps are the only route upward, so every platform above the floor
		// gets at least one, and they are kept clear of the side walls so a gap
		// is never a corner the player cannot line up on.
		const int lo   = 3;
		const int hi   = std::max( lo, mW - 5 );
		const int gaps = ( mW >= 20 ) ? 2 : 1;

		for( int g = 0; g < gaps; ++g )
		{
			const int gx = lo + int( rng.Below( uint32_t( std::max( 1, hi - lo + 1 ) ) ) );
			for( int k = 0; k < 2; ++k )
				if( gx + k <= mW - 2 )
					mSolid[ Index( gx + k, y ) ] = 0;
		}
	}

	const float diff = std::clamp( cfg.difficulty, 0.0f, 1.0f );
	mCrawlSpeed      = 0.12f + 0.10f * diff;
	mSpawnEvery      = std::max( 20, int( std::lround( 110.0f - 60.0f * diff ) ) );
	mWaveTotal       = 3;
	mReleased        = 0;
	mSpawnTimer      = 0;

	mCrawler.clear();
	mBumpX     = -1;
	mBumpY     = -1;
	mBumpTimer = 0;
}

void Rafters::Reset( const GameConfig& cfg, Rng& rng )
{
	mW = cfg.gridW;
	mH = cfg.gridH;

	BuildLevel( cfg, rng );

	mPlayer          = Actor{};
	mPlayer.x        = float( mW / 2 );
	mPlayer.y        = float( mH - 3 );
	mPlayer.dir      = 1;
	mPlayer.onGround = true;

	mScore = 0;
	mLives = 3;
	mWave  = 0;
	mTicks = 0;
	mHurt  = 0;
	mStuck = 0;
}

float Rafters::TickHz( const GameConfig& cfg ) const
{
	// Jump arcs live in fractions of a cell, so this needs to be fast enough
	// that an arc is a curve rather than three positions.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 20.0f + 40.0f * t * t;
}

void Rafters::SpawnCrawler( Rng& rng )
{
	if( mPlatformY.empty() )
		return;

	// Weighted toward the top, but not only the top. Crawlers turn at ledges
	// rather than falling -- which is the right rule, see MoveActor -- and the
	// consequence is that whichever floor one is released onto is the floor it
	// spends its life on. Release them all at the top and every floor below is
	// scenery.
	//
	// They still enter from a side edge, in view, which is the property that
	// mattered: nothing arrives on the player's floor unannounced.
	const size_t floors = mPlatformY.size();
	size_t pick         = rng.Below( uint32_t( floors ) );
	if( rng.Chance( 0.45f ) )
		pick = 0;

	const int topY = mPlatformY[ std::min( pick, floors - 1 ) ];

	Crawler c;
	c.body.dir = rng.Chance( 0.5f ) ? 1 : -1;
	c.body.x   = float( c.body.dir > 0 ? 1 : mW - 2 );
	c.body.y   = float( topY - 1 );
	c.body.onGround = true;
	c.flip     = 0;
	c.speed    = 0;

	mCrawler.push_back( c );
	++mReleased;
}

void Rafters::MoveActor( Actor& a, float speed, bool turnAtEdges, bool isPlayer )
{
	const int cy = Round( a.y );

	if( a.dir != 0 && speed > 0.0f )
	{
		const float nx = a.x + float( a.dir ) * speed;
		const int ncx  = Round( nx );

		if( ncx < 1 || ncx > mW - 2 || Solid( ncx, cy ) )
		{
			if( turnAtEdges )
				a.dir = int8_t( -a.dir );
		}
		else if( turnAtEdges && a.onGround && !Solid( ncx, cy + 1 ) )
		{
			// A crawler turns at a ledge rather than walking off it. Letting it
			// fall is more realistic and much worse: the platform it lands on
			// is the one the player was working, and a hazard that arrives from
			// above with no warning is not a hazard anybody can play against.
			a.dir = int8_t( -a.dir );
		}
		else
		{
			a.x = nx;
		}
	}

	const int cx = Round( a.x );

	if( a.onGround )
	{
		if( !Solid( cx, Round( a.y ) + 1 ) )
		{
			a.onGround = false;
			a.vy       = kGravity;
		}
		return;
	}

	// Clamped both ways. Exceed one cell a tick and an actor steps over a
	// one-cell platform without ever testing it -- see the header.
	a.vy = std::clamp( a.vy + kGravity, -kMaxFall, kMaxFall );

	const float ny = a.y + a.vy;
	const int ncy  = Round( ny );

	if( a.vy > 0.0f )
	{
		if( Solid( cx, ncy ) )
		{
			a.y        = float( ncy - 1 );
			a.vy       = 0.0f;
			a.onGround = true;
		}
		else
		{
			a.y = ny;
		}
		return;
	}

	if( Solid( cx, ncy ) )
	{
		a.y  = float( ncy + 1 );
		a.vy = 0.0f;

		if( isPlayer )
		{
			mBumpX     = cx;
			mBumpY     = ncy;
			mBumpTimer = kBumpTicks;
		}
	}
	else
	{
		a.y = ny;
	}
}

void Rafters::LoseLife()
{
	--mLives;
	mHurt = 20;

	if( mLives <= 0 )
		return;

	mPlayer          = Actor{};
	mPlayer.x        = float( mW / 2 );
	mPlayer.y        = float( mH - 3 );
	mPlayer.dir      = 1;
	mPlayer.onGround = true;

	// The wave survives; only what is in the air is cleared. Wiping the
	// crawlers would make being caught the fastest way to clear a floor.
	mBumpTimer = 0;
	mStuck     = 0;
}

/**
	The autopilot, in priority order.

	Two things in here were measured wrong before they were right, and both are
	worth keeping in the comments because both looked correct.

	**Rule 3's radius.** It backed away from anything upright within five cells
	of it, which is most of a 32-wide floor. The result was a player that
	retreated all game, never climbed to get above a crawler, and scored worse
	at Skill 1.0 than the random walker does at Skill 0.1.

	**The pogo.** A jump through a gap goes up through the hole and, with
	nothing steering, comes straight back down through the same hole -- the
	apex clears the platform but the player is still over thin air. Deciding
	the *next* move while airborne made it worse, because the row under the
	player mid-flight is not the row it is trying to reach. So the climb is a
	commitment: `mAimRow` is set on the ground and the only job in the air is to
	land on it. Before that, the player was off the ground 87% of the time.
*/
Rafters::Intent Rafters::ChooseAutopilot( const GameConfig& cfg, Rng& rng )
{
	Intent out;

	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
	const int px      = Round( mPlayer.x );
	const int py      = Round( mPlayer.y );

	if( mPlayer.onGround )
		mAimRow = -1;

	// Airborne with a floor in mind. Nothing else may be decided until it is
	// stood on -- see the note above about the pogo.
	if( !mPlayer.onGround && mAimRow >= 0 )
	{
		if( py <= mAimRow - 1 && !Solid( px, mAimRow ) )
		{
			int bestX = -1;
			int bestD = INT_MAX;
			for( int x = 1; x <= mW - 2; ++x )
			{
				if( !Solid( x, mAimRow ) )
					continue;

				const int d = std::abs( x - px );
				if( d < bestD )
				{
					bestD = d;
					bestX = x;
				}
			}

			if( bestX >= 0 && bestD > 0 )
				out.move = bestX > px ? 1 : -1;
		}

		return out;
	}

	// Deliberate incompetence, the same lever every autopilot in this plugin
	// has. Without it a good player clears wave after wave and the layer stops
	// changing.
	if( rng.Chance( ( 1.0f - skill ) * 0.35f ) )
	{
		out.move = rng.Chance( 0.5f ) ? -1 : 1;
		out.jump = rng.Chance( 0.2f ) && mPlayer.onGround;
		return out;
	}

	// 1. A crawler on its back on this floor is the whole point. Go and kick
	//    it, before it gets up faster than it was.
	{
		const Crawler* best = nullptr;
		int bestDist        = INT_MAX;
		for( const Crawler& c : mCrawler )
		{
			if( c.flip <= 0 || Round( c.body.y ) != py )
				continue;

			const int d = std::abs( Round( c.body.x ) - px );
			if( d < bestDist )
			{
				bestDist = d;
				best     = &c;
			}
		}

		if( best )
		{
			out.move = Round( best->body.x ) > px ? 1 : -1;
			return out;
		}
	}

	// 2. A crawler on the floor above is a bump waiting to happen. Line up
	//    underneath it and hit the ceiling.
	for( const Crawler& c : mCrawler )
	{
		if( c.flip > 0 )
			continue;

		const int cy = Round( c.body.y );
		if( cy >= py || py - cy > kSpacing + 1 )
			continue;

		const int dx = Round( c.body.x ) - px;
		if( std::abs( dx ) <= kBumpReach )
		{
			out.jump = mPlayer.onGround;
			return out;
		}

		out.move = dx > 0 ? 1 : -1;
		return out;
	}

	// 3. An upright crawler sharing this floor is only a threat and there is no
	//    move against it from here. Back off -- but only when it is genuinely
	//    close, for the reason in the note above.
	for( const Crawler& c : mCrawler )
	{
		if( c.flip > 0 || Round( c.body.y ) != py )
			continue;

		const int dx = Round( c.body.x ) - px;
		if( std::abs( dx ) > 2 )
			continue;

		out.move = dx > 0 ? -1 : 1;
		if( px <= 2 )
			out.move = 1;
		if( px >= mW - 3 )
			out.move = -1;

		return out;
	}

	// 4. Travel. Which floor to travel to depends on what the target is, and
	//    getting it backwards is what made the flip pointless: a crawler just
	//    flipped is on the floor *above*, and heading for the bumping position
	//    below it means never collecting the kick that was just set up.
	int wantY = -1;
	{
		const Crawler* target = nullptr;
		int targetDist        = INT_MAX;
		bool chasingFlipped   = false;

		for( const Crawler& c : mCrawler )
		{
			const bool flipped = c.flip > 0;
			if( chasingFlipped && !flipped )
				continue;

			const int d = std::abs( Round( c.body.x ) - px ) +
			              std::abs( Round( c.body.y ) - py ) * 2;

			if( !target || ( flipped && !chasingFlipped ) || d < targetDist )
			{
				targetDist     = d;
				target         = &c;
				chasingFlipped = flipped;
			}
		}

		if( target )
			wantY = Round( target->body.y ) + ( chasingFlipped ? 0 : kSpacing );
	}

	// 5. Nothing released yet. Work upward anyway, so the next crawler does not
	//    arrive with the player parked on the bottom floor with five to climb.
	if( wantY < 0 && !mPlatformY.empty() )
		wantY = mPlatformY.front() - 1 + kSpacing;

	if( wantY < 0 || wantY == py )
		return out;

	if( py > wantY )
	{
		// Up. Gaps are the only route between floors, which is what makes the
		// level layout matter at all.
		const int up = py + 1 - kSpacing;
		int gapX     = -1;
		int gapDist  = INT_MAX;

		if( up >= 1 )
			for( int x = 1; x <= mW - 2; ++x )
			{
				if( Solid( x, up ) )
					continue;

				const int d = std::abs( x - px );
				if( d < gapDist )
				{
					gapDist = d;
					gapX    = x;
				}
			}

		if( gapX < 0 )
			return out;

		if( gapDist > 0 )
		{
			out.move = gapX > px ? 1 : -1;
			return out;
		}

		// Standing on the gap. Commit to the floor above and jump.
		out.jump = mPlayer.onGround;
		mAimRow  = up;
		return out;
	}

	// Down is free: walk into a hole in this floor and fall through it.
	int holeX    = -1;
	int holeDist = INT_MAX;
	for( int x = 1; x <= mW - 2; ++x )
	{
		if( Solid( x, py + 1 ) )
			continue;

		const int d = std::abs( x - px );
		if( d < holeDist )
		{
			holeDist = d;
			holeX    = x;
		}
	}

	if( holeX >= 0 && holeDist > 0 )
		out.move = holeX > px ? 1 : -1;

	return out;
}


void Rafters::Step( const GameConfig& cfg, Input& in, Rng& rng )
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
			case Button::Up:
			case Button::Fire: intent.jump = true; break;
			default: break;
		}
	}

	++mTicks;
	if( mHurt > 0 )
		--mHurt;

	if( cfg.autopilot )
		intent = ChooseAutopilot( cfg, rng );

	if( intent.move != 0 )
		mPlayer.dir = int8_t( intent.move );

	if( intent.jump && mPlayer.onGround )
	{
		mPlayer.onGround = false;
		mPlayer.vy       = -kJump;
	}

	MoveActor( mPlayer, intent.move != 0 ? kWalk : 0.0f, false, true );

	// The bump stays live for a few ticks and reaches a cell either side. Both
	// are there to make the move land at all -- see the header.
	if( mBumpTimer > 0 )
	{
		for( Crawler& c : mCrawler )
		{
			if( c.flip > 0 )
				continue;

			if( Round( c.body.y ) != mBumpY - 1 )
				continue;

			if( std::abs( Round( c.body.x ) - mBumpX ) > kBumpReach )
				continue;

			c.flip = kFlipTicks;
			mScore += 1;
		}

		--mBumpTimer;
	}

	for( Crawler& c : mCrawler )
	{
		if( c.flip > 0 )
		{
			--c.flip;

			// Back on its feet and quicker for it. The clock on a flipped
			// crawler is the whole tension of the mechanic.
			if( c.flip == 0 )
				++c.speed;

			// Still subject to gravity while on its back, or one flipped over a
			// gap would hang in the air.
			MoveActor( c.body, 0.0f, true, false );
			continue;
		}

		MoveActor( c.body, mCrawlSpeed + 0.03f * float( c.speed ), true, false );
	}

	const int px = Round( mPlayer.x );
	const int py = Round( mPlayer.y );

	for( size_t i = 0; i < mCrawler.size(); ++i )
	{
		const Crawler& c = mCrawler[ i ];
		if( Round( c.body.x ) != px || Round( c.body.y ) != py )
			continue;

		if( c.flip > 0 )
		{
			mScore += 5;
			mCrawler.erase( mCrawler.begin() + long( i ) );
			mStuck = 0;
			break;
		}

		LoseLife();
		return;
	}

	++mSpawnTimer;
	if( mSpawnTimer >= mSpawnEvery && mReleased < mWaveTotal )
	{
		mSpawnTimer = 0;
		SpawnCrawler( rng );
	}

	if( mReleased >= mWaveTotal && mCrawler.empty() )
	{
		++mWave;
		mScore += 15;
		mWaveTotal  = 3 + mWave;
		mReleased   = 0;
		mSpawnTimer = mSpawnEvery;
		mCrawlSpeed = std::min( 0.45f, mCrawlSpeed + 0.03f );
		mSpawnEvery = std::max( 14, mSpawnEvery - 8 );
	}

	// A player that has stopped clearing crawlers is stuck on a floor it cannot
	// leave. Ending the life beats a layer that stopped moving.
	++mStuck;
	if( mStuck > mW * mH * 2 )
		LoseLife();
}

void Rafters::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	for( int y = 0; y < mH; ++y )
		for( int x = 0; x < mW; ++x )
			if( Solid( x, y ) )
				grid.Set( x, y, Cell::Wall, y == 0 || y == mH - 1 || x == 0 || x == mW - 1 ? 255 : 200 );

	// The struck cell, lit for exactly as long as the bump is live. It is the
	// only feedback that the move connected.
	if( mBumpTimer > 0 )
		grid.Set( mBumpX, mBumpY, Cell::Ball, 255 );

	for( const Crawler& c : mCrawler )
		grid.Set( Round( c.body.x ), Round( c.body.y ), Cell::Enemy,
		          c.flip > 0 ? 110 : 255 );

	grid.Set( Round( mPlayer.x ), Round( mPlayer.y ), Cell::Head,
	          mHurt > 0 ? 120 : 255 );
}

float Rafters::Intensity() const
{
	if( mCrawler.empty() )
		return 0.0f;

	// Flipped crawlers are the clock running; upright ones near the player are
	// the threat. Both are worth a reaction.
	const float flipped = float( Flipped() ) / float( mCrawler.size() );

	// Not `near`: that is a macro in the Windows headers and this file is
	// compiled there too.
	float threat = 0.0f;
	for( const Crawler& c : mCrawler )
		if( c.flip == 0 && Round( c.body.y ) == Round( mPlayer.y ) &&
		    std::abs( Round( c.body.x ) - Round( mPlayer.x ) ) <= 4 )
			threat = 0.8f;

	return std::clamp( std::max( flipped, threat ), 0.0f, 1.0f );
}

} // namespace coinop
