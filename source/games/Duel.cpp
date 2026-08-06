#include "games/Duel.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

namespace
{

/// The middle of a strike, in ticks remaining. Only these connect: before it
/// is the wind-up the other fighter can react to, after it is the recovery
/// that makes throwing one a decision.
constexpr int kActiveHi = 5;
constexpr int kActiveLo = 3;

constexpr int kHitDamage   = 9;
constexpr int kChipDamage  = 2;
constexpr float kKnockback = 1.4f;

/// Closest two fighters may stand. They are one cell wide and this keeps a cell
/// of air between them, so a strike always has somewhere to land.
constexpr float kMinGap = 2.0f;

const int8_t kJumpLift[ 8 ] = { 0, 1, 2, 2, 2, 2, 1, 0 };

/// Not named `Round`: `Duel::Round()` is the round number, and a member
/// hides a free function of the same name inside every member of the class.
int CellOf( float v ) { return int( std::floor( v + 0.5f ) ); }

} // namespace

int Duel::FeetY( int i ) const
{
	const int lift = mFighter[ i ].jump > 0
	                     ? kJumpLift[ std::clamp( 8 - mFighter[ i ].jump, 0, 7 ) ]
	                     : 0;
	return FloorY() - 1 - lift;
}

int Duel::StrikeCell( int i ) const
{
	return CellOf( mFighter[ i ].x ) + mFighter[ i ].face * 2;
}

float Duel::Separation() const
{
	return std::abs( mFighter[ 0 ].x - mFighter[ 1 ].x );
}

void Duel::StartRound()
{
	const float quarter = float( mW ) / 4.0f;

	mFighter[ 0 ].x    = std::max( 2.0f, quarter );
	mFighter[ 1 ].x    = std::min( float( mW - 3 ), float( mW ) - quarter );
	mFighter[ 0 ].face = 1;
	mFighter[ 1 ].face = -1;

	for( Fighter& f : mFighter )
	{
		f.health = kMaxHealth;
		f.action = Action::Idle;
		f.timer  = 0;
		f.cool   = 0;
		f.jump   = 0;
	}

	mRoundTicks = 0;
	mHold       = 0;
}

void Duel::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;

	mW = cfg.gridW;
	mH = cfg.gridH;

	for( Fighter& f : mFighter )
		f.wins = 0;

	mRound = 0;
	mScore = 0;
	mDone  = false;

	// Difficulty is the pace: how fast a fighter closes the gap, which is what
	// decides whether a round is a brawl or a stand-off.
	mStep = 0.16f + 0.16f * std::clamp( cfg.difficulty, 0.0f, 1.0f );

	StartRound();
}

float Duel::TickHz( const GameConfig& cfg ) const
{
	// The wind-up and recovery windows are measured in ticks, so this has to be
	// quick enough that seven ticks is a fifth of a second rather than a wait.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 24.0f + 36.0f * t * t;
}

void Duel::ApplyIntent( int i, int move, bool strike, bool block, bool jump )
{
	Fighter& f = mFighter[ i ];
	Fighter& o = mFighter[ 1 - i ];

	// Fighters always face each other. A fighting game where you can turn your
	// back on the other one is a game about turning around.
	f.face = o.x >= f.x ? 1 : -1;

	if( f.jump > 0 )
		--f.jump;

	if( f.timer > 0 )
	{
		--f.timer;
		if( f.timer == 0 )
		{
			// Read the action before clearing it. Recovery is longer after a
			// strike than after a block, and that difference is the whole
			// reason a strike is a commitment rather than a free action.
			const Action was = f.action;
			f.action         = Action::Idle;
			f.cool           = was == Action::Strike ? 4 : 2;
		}

		// Nothing else may start while an action is running. Being unable to
		// cancel a whiffed strike is the point of throwing one.
		return;
	}

	if( f.cool > 0 )
	{
		--f.cool;
		return;
	}

	if( strike )
	{
		f.action = Action::Strike;
		f.timer  = kStrikeTicks;
		return;
	}

	if( block )
	{
		f.action = Action::Block;
		f.timer  = kBlockTicks;
		return;
	}

	if( jump && f.jump == 0 )
		f.jump = 8;

	if( move != 0 )
	{
		const float want = f.x + float( move ) * mStep;
		const float gap  = std::abs( want - o.x );

		if( gap >= kMinGap || std::abs( want - o.x ) > std::abs( f.x - o.x ) )
			f.x = std::clamp( want, 1.0f, float( mW - 2 ) );
	}
}

void Duel::DecideAi( int i, const GameConfig& cfg, Rng& rng )
{
	Fighter& f       = mFighter[ i ];
	const Fighter& o = mFighter[ 1 - i ];

	if( f.timer > 0 || f.cool > 0 )
	{
		ApplyIntent( i, 0, false, false, false );
		return;
	}

	const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
	const float gap   = std::abs( f.x - o.x );

	// React to a strike that is still winding up. This is the one read in the
	// game, and Skill is entirely whether it is made -- an autopilot that
	// always sees it coming never takes a hit, which is why it must not.
	if( o.action == Action::Strike && o.timer > kActiveLo && gap < 3.5f )
	{
		if( rng.Chance( skill * 0.85f ) )
		{
			ApplyIntent( i, 0, false, true, false );
			return;
		}
	}

	if( gap <= 2.6f )
	{
		// In range. Strike, unless it decides to keep the pressure by backing
		// out -- a fighter that only ever attacks at range is trivially
		// counter-hit and the round becomes a metronome.
		if( rng.Chance( 0.35f + 0.45f * skill ) )
		{
			ApplyIntent( i, 0, true, false, false );
			return;
		}

		ApplyIntent( i, f.x < o.x ? -1 : 1, false, false, false );
		return;
	}

	// Out of range. Close, mostly. The occasional jump is what stops two
	// autopilots from walking into each other in a perfectly straight line
	// every round.
	const bool hop = rng.Chance( ( 1.0f - skill ) * 0.05f );
	ApplyIntent( i, f.x < o.x ? 1 : -1, false, false, hop );
}

void Duel::Resolve( int attacker, int defender )
{
	Fighter& a = mFighter[ attacker ];
	Fighter& d = mFighter[ defender ];

	if( a.action != Action::Strike )
		return;

	if( a.timer > kActiveHi || a.timer < kActiveLo )
		return;

	// A strike lands on the cell in front of the attacker, and only if the
	// defender is at the same height. Jumping over one is a real answer.
	if( CellOf( d.x ) != StrikeCell( attacker ) )
		return;

	if( FeetY( defender ) != FeetY( attacker ) )
		return;

	if( d.action == Action::Block )
	{
		// Chip damage. Blocking has to cost something or holding it forever is
		// the whole strategy.
		d.health -= kChipDamage;
		a.timer   = std::min( a.timer, kActiveLo );
		return;
	}

	d.health -= kHitDamage;
	d.action  = Action::Hurt;
	d.timer   = kHurtTicks;
	d.x       = std::clamp( d.x + float( a.face ) * kKnockback, 1.0f, float( mW - 2 ) );

	// The strike is spent on the hit, so one strike cannot damage twice on
	// consecutive active ticks.
	a.timer = std::min( a.timer, kActiveLo );

	if( attacker == 0 )
		mScore += 1;
}

void Duel::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mDone )
		return;

	int move    = 0;
	bool strike = false;
	bool block  = false;
	bool jump   = false;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Left: move = -1; break;
			case Button::Right: move = 1; break;
			case Button::Up: jump = true; break;
			case Button::Down: block = true; break;
			case Button::Fire: strike = true; break;
			default: break;
		}
	}

	// The pause between rounds, so the knockout is visible rather than being
	// the last frame before everything moves back to its corner.
	if( mHold > 0 )
	{
		--mHold;
		if( mHold == 0 )
			StartRound();
		return;
	}

	++mRoundTicks;

	if( cfg.autopilot )
		DecideAi( 0, cfg, rng );
	else
		ApplyIntent( 0, move, strike, block, jump );

	DecideAi( 1, cfg, rng );

	// Both directions, every tick. Resolving only the player's strikes would
	// give whichever fighter is checked first a free trade on every mutual hit.
	Resolve( 0, 1 );
	Resolve( 1, 0 );

	const bool down = mFighter[ 0 ].health <= 0 || mFighter[ 1 ].health <= 0;

	// A round that has gone the distance is awarded on health. Without it two
	// cautious autopilots circle forever, `Finished` never returns true, and
	// the layer shows one match for the rest of the show -- see the header.
	const bool expired = mRoundTicks >= kRoundTicks;

	if( !down && !expired )
		return;

	int winner = -1;
	if( mFighter[ 0 ].health > mFighter[ 1 ].health )
		winner = 0;
	else if( mFighter[ 1 ].health > mFighter[ 0 ].health )
		winner = 1;

	++mRound;

	if( winner >= 0 )
	{
		++mFighter[ winner ].wins;
		if( winner == 0 )
			mScore += 20;

		if( mFighter[ winner ].wins >= kRoundsToWin )
			mDone = true;
	}

	// A drawn round still counts toward the match length, so a run of draws
	// cannot stall the match indefinitely.
	if( !mDone && mRound >= kRoundsToWin * 3 )
		mDone = true;

	mHold = 20;
}

void Duel::Draw( const GameConfig& cfg, Grid& grid ) const
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

	for( int x = 1; x <= mW - 2; ++x )
		grid.Set( x, FloorY(), Cell::Wall, 200 );

	// Corner posts and a top rope.
	//
	// Two one-cell fighters on a floor leave three quarters of the playfield
	// empty, which on a layer whose whole job is what it looks like is a real
	// fault even though every state assertion passes. The posts bound the
	// arena, and the rope is set above head height so it frames the fight
	// rather than drawing over it.
	const int ropeY = std::max( 3, FloorY() - std::max( 4, mH / 4 ) );
	for( int y = ropeY; y < FloorY(); ++y )
	{
		grid.Set( 1, y, Cell::Wall, 170 );
		grid.Set( mW - 2, y, Cell::Wall, 170 );
	}

	for( int x = 1; x <= mW - 2; ++x )
		grid.Set( x, ropeY, Cell::Wall, 120 );

	//-----------------------------------------------------------------------
	// Health, as two bars growing in from the sides. It is the only number in
	// this game the viewer needs and there is no room to write it.
	//-----------------------------------------------------------------------
	const int barMax = std::max( 1, ( mW - 4 ) / 2 );
	for( int i = 0; i < 2; ++i )
	{
		const int len =
			std::clamp( mFighter[ i ].health * barMax / kMaxHealth, 0, barMax );

		for( int k = 0; k < len; ++k )
		{
			const int x = i == 0 ? 1 + k : mW - 2 - k;
			grid.Set( x, 1, i == 0 ? Cell::Paddle : Cell::Brick, 255,
			          uint8_t( i == 0 ? 0 : 3 ) );
		}
	}

	// Round wins, as pips under the bars.
	for( int i = 0; i < 2; ++i )
		for( int k = 0; k < mFighter[ i ].wins; ++k )
		{
			const int x = i == 0 ? 1 + k * 2 : mW - 2 - k * 2;
			grid.Set( x, 2, Cell::Food, 255 );
		}

	for( int i = 0; i < 2; ++i )
	{
		const Fighter& f = mFighter[ i ];
		const int fx     = CellOf( f.x );
		const int feet   = FeetY( i );

		// Blocking reads as a dimmer, hunched fighter: two cells instead of
		// three. It is the only silhouette change in the game and it is the one
		// the other player has to be able to see instantly.
		const bool blocking = f.action == Action::Block;
		const uint8_t shade = f.action == Action::Hurt ? 110 : ( blocking ? 160 : 255 );

		const Cell body = i == 0 ? Cell::Body : Cell::Enemy;
		const Cell head = i == 0 ? Cell::Head : Cell::Enemy;

		grid.Set( fx, feet, body, shade, uint8_t( i == 0 ? 0 : 2 ) );
		if( !blocking )
			grid.Set( fx, feet - 1, body, shade, uint8_t( i == 0 ? 0 : 2 ) );

		grid.Set( fx, feet - ( blocking ? 1 : 2 ), head, shade,
		          uint8_t( i == 0 ? 0 : 2 ) );

		// The active window of a strike, and nothing else. Drawing the wind-up
		// would tell the other fighter exactly when to block and remove the
		// read the whole game is built on.
		if( f.action == Action::Strike && f.timer <= kActiveHi && f.timer >= kActiveLo )
		{
			grid.Set( fx + f.face, feet - 1, Cell::Ball, 255 );
			grid.Set( StrikeCell( i ), feet - 1, Cell::Ball, 255 );
		}
	}
}

float Duel::Intensity() const
{
	// Rises as the fighters close and as the health drains, which between them
	// track everything worth reacting to here.
	const float gap = std::clamp( Separation() / float( std::max( 4, mW / 2 ) ), 0.0f, 1.0f );
	const int lowest = std::min( mFighter[ 0 ].health, mFighter[ 1 ].health );
	const float hurt = 1.0f - float( std::max( 0, lowest ) ) / float( kMaxHealth );

	return std::clamp( std::max( 1.0f - gap, hurt ), 0.0f, 1.0f );
}

} // namespace coinop
