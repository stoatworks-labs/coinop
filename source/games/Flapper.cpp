#include "games/Flapper.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

namespace
{

/// Gravity and the flap, in cells and ticks.
///
/// The pair is chosen so a flap from rest rises a shade over three cells before
/// it turns over -- `kFlap^2 / (2 * kGravity)` -- which is the height that makes
/// a gap of four feel like a gap of four. Raise the flap without raising gravity
/// and the flier climbs the whole playfield on one press, which reads as a
/// balloon rather than a bird and makes every hole below the top unreachable in
/// one move.
/// Gravity and the flap, in cells and ticks.
///
/// A flap rises `kFlap^2 / (2 * kGravity)` cells before it turns over, which at
/// these numbers is a shade over one cell, in about seven ticks. That is smaller
/// than it first looks right: the hop wants to be small against the playfield,
/// because the player's job is to hold an altitude with a lot of small
/// corrections rather than to leap between holes. Raising the flap to a
/// three-cell rise -- which is where this started -- makes the flier a balloon
/// that crosses the whole gap on one press, and the autopilot then could not
/// score a single column on any seed.
///
/// These four numbers were picked by sweeping them against the two things that
/// matter: whether a skilled autopilot actually gets through columns, and
/// whether it still dies. At this setting eight seeds at Skill 1.0 score 177
/// columns over 7300 ticks and all of them end; the same seeds at Skill 0.1
/// score 6 over 3400.
constexpr float kGravity = 0.040f;
constexpr float kFlap    = 0.30f;

/// Terminal sink rate, and deliberately not `kMaxFall`.
///
/// `kMaxFall` is the safety clamp; this is feel. Nothing in this game is thin
/// enough to fall through -- a column is a solid block, not a one-cell ledge --
/// so the sink rate is free to be chosen for how the flier drops. It is well
/// above the climb rate of `kFlap - kGravity`, which is what makes falling the
/// thing that happens to you and climbing the thing you do.
constexpr float kSink = 0.45f;

/// Ticks of velocity the autopilot counts as position. See ChooseFlap.
constexpr float kDamp = 2.0f;

} // namespace

int Flapper::MaxGap() const
{
	// Two rows go to the ceiling and floor and at least one solid cell has to
	// remain above and below the hole, or the "column" is a full-height gap with
	// nothing in it and the game has no obstacles at all.
	return std::max( kMinGap, mH - 4 );
}

bool Flapper::Solid( int x, int y ) const
{
	if( y <= 0 || y >= mH - 1 )
		return true;

	for( const Column& c : mColumn )
	{
		const int left = Round( c.x );
		if( x < left || x > left + kColWidth - 1 )
			continue;

		if( y < c.gapY || y >= c.gapY + c.gapH )
			return true;
	}

	return false;
}

bool Flapper::Blocks( const Column& c, float sweptFrom, int y ) const
{
	// The span the column *crossed* this tick, not the one it landed on. See the
	// header: this is the property that survives someone raising kMaxScroll.
	const int left  = Round( c.x );
	const int right = Round( sweptFrom ) + kColWidth - 1;

	if( mFlierX < left || mFlierX > right )
		return false;

	return y < c.gapY || y >= c.gapY + c.gapH;
}

int Flapper::TargetRow() const
{
	for( const Column& c : mColumn )
	{
		// Anything whose right edge is already behind the flier is history, and
		// aiming at it drags the flier back down through the hole it just left.
		if( Round( c.x ) + kColWidth - 1 < mFlierX )
			continue;

		return c.gapY + c.gapH / 2;
	}

	return ( mH - 1 ) / 2;
}

/**
	The autopilot, which is one line and took three tries to make it one line.

	The obvious rule -- project the flier forward a few ticks under gravity and
	flap if it would end up below the hole -- does not work, and the way it fails
	is worth keeping. Projecting `n` ticks of free fall means the flier flaps as
	soon as it is within a braking distance of the target, so it settles a full
	braking distance *above* it: aiming at row 12 it hovered at row 7 and clipped
	the top of every column it met. It never scored once, on any seed.

	What it wants instead is a damped position error, which is the same thing a
	proportional-derivative controller is: the flier flaps when where it is, plus
	where its current velocity is taking it, is below the hole. `kDamp` is how
	many ticks of velocity count as position. Too small and the flier chases its
	own altitude and oscillates through the gap; too large and it is back to
	flying high above the target for the same reason as the projection did.
*/
bool Flapper::ChooseFlap() const
{
	return ( mY + kDamp * mVy ) > float( TargetRow() );
}

void Flapper::Spawn( Rng& rng )
{
	Column c;
	c.x = mColumn.empty() ? float( mW ) : mColumn.back().x + float( mSpacing );

	// The hole walks from the last one rather than being drawn fresh each time.
	// Independent holes make an early field that is already unplayable and a
	// late field no worse than the early one -- the ramp would have nothing to
	// ramp. Walking it means `mDrift` alone decides how far the flier has to
	// travel between columns, which is what the termination argument rests on.
	const int lo   = 1;
	const int hi   = std::max( lo, mH - 1 - mGapH );
	const int span = 2 * mDrift + 1;

	int y = mLastGapY + int( rng.Below( uint32_t( span ) ) ) - mDrift;
	y     = std::clamp( y, lo, hi );

	c.gapY = y;
	c.gapH = mGapH;

	mLastGapY = y;
	mColumn.push_back( c );
}

void Flapper::Ramp()
{
	const int maxGap = MaxGap();

	// Three columns a step, not one. Narrowing on every column outruns the
	// scroll and the run ends before the field has visibly sped up, which loses
	// the only part of the ramp an audience can see.
	if( mPassed % 3 == 0 && mGapH > kMinGap )
		--mGapH;

	mScroll = std::min( kMaxScroll, mScroll + 0.012f );

	if( mPassed % 4 == 0 && mSpacing > kMinSpacing )
		--mSpacing;

	// The drift is what finally ends it. Gap and spacing bottom out; this keeps
	// going until the hole can be anywhere on the playfield, by which point the
	// distance between consecutive holes exceeds what kMaxFall can cover in the
	// ticks kMinSpacing / kMaxScroll allows.
	if( mPassed % 5 == 0 )
		mDrift = std::min( std::max( 2, maxGap ), mDrift + 1 );
}

void Flapper::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;

	mW = cfg.gridW;
	mH = cfg.gridH;

	// A quarter across. Far enough in that a column is visible for long enough
	// to read before it matters, and not so far that the flier is the last
	// thing on screen.
	mFlierX = std::max( 1, mW / 4 );

	mY  = float( ( mH - 1 ) / 2 );
	mVy = 0.0f;

	const float diff = std::clamp( cfg.difficulty, 0.0f, 1.0f );

	// Difficulty sets where the ramp starts, not where it stops. Both ends of
	// the slider terminate; the hard end just gets there sooner.
	const int maxGap = MaxGap();
	const int wide   = int( std::lround( float( mH ) * ( 0.30f - 0.10f * diff ) ) );

	// std::clamp is undefined when lo > hi, and on a 6-row playfield it is.
	const int lo = std::min( kMinGap + 1, maxGap );
	mGapH        = std::clamp( wide, lo, maxGap );

	mScroll  = 0.12f + 0.10f * diff;
	mSpacing = std::max( kMinSpacing, mW / 3 );
	mDrift   = 2;

	mLastGapY = std::clamp( ( mH - mGapH ) / 2, 1, std::max( 1, mH - 1 - mGapH ) );

	mColumn.clear();

	mScore  = 0;
	mPassed = 0;
	mLives  = 3;
	mTicks  = 0;
	mHurt   = 0;
}

float Flapper::TickHz( const GameConfig& cfg ) const
{
	// Same reasoning as Rafters: the flap is an arc measured in fractions of a
	// cell, and it wants enough ticks to be a curve rather than three positions.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 20.0f + 40.0f * t * t;
}

void Flapper::LoseLife()
{
	--mLives;
	mHurt = 20;

	if( mLives <= 0 )
		return;

	mY  = float( ( mH - 1 ) / 2 );
	mVy = 0.0f;

	// The field is cleared rather than kept. Respawning into the column that
	// just killed you is a life lost on the tick it is granted, and three of
	// those is a game that ends without the flier ever moving.
	mColumn.clear();
	mLastGapY = std::clamp( ( mH - mGapH ) / 2, 1, std::max( 1, mH - 1 - mGapH ) );

	// mPassed, mGapH, mScroll, mSpacing and mDrift all survive. The ramp is the
	// termination guarantee and a life must not rewind it.
}

void Flapper::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mLives <= 0 )
		return;

	bool flap = false;

	Button b;
	while( in.Pop( b ) )
	{
		switch( b )
		{
			case Button::Up:
			case Button::Fire: flap = true; break;
			default: break;
		}
	}

	++mTicks;
	if( mHurt > 0 )
		--mHurt;

	if( cfg.autopilot )
	{
		const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

		// Deliberate incompetence, the same lever every autopilot in this plugin
		// has. Without it a good flier clears columns until the ramp kills it and
		// the Skill parameter does nothing an audience can see.
		if( rng.Chance( ( 1.0f - skill ) * 0.30f ) )
		{
			flap = rng.Chance( 0.5f );
		}
		else
		{
			flap = ChooseFlap();
		}
	}

	if( flap )
		mVy = -kFlap;

	mVy = std::clamp( mVy + kGravity, -kMaxFall, std::min( kSink, kMaxFall ) );
	mY  = mY + mVy;

	// Ceiling and floor. The flier dies on both -- a ceiling that merely stopped
	// it would make holding the button a safe place to wait out the field.
	if( mY <= 0.0f || mY >= float( mH - 1 ) )
	{
		mY = std::clamp( mY, 0.0f, float( mH - 1 ) );
		LoseLife();
		return;
	}

	const int fy = Round( mY );

	for( Column& c : mColumn )
	{
		const float from = c.x;
		c.x -= mScroll;

		if( Blocks( c, from, fy ) )
		{
			LoseLife();
			return;
		}

		// Scored on the tick the column's right edge passes the flier, which is
		// the tick it can no longer be hit.
		if( !c.taken && Round( c.x ) + kColWidth - 1 < mFlierX )
		{
			c.taken = true;
			++mPassed;
			++mScore;
			Ramp();
		}
	}

	while( !mColumn.empty() && Round( mColumn.front().x ) + kColWidth - 1 < 0 )
		mColumn.erase( mColumn.begin() );

	if( mColumn.empty() || mColumn.back().x <= float( mW - mSpacing ) )
		Spawn( rng );
}

void Flapper::Draw( const GameConfig& cfg, Grid& grid ) const
{
	(void)cfg;
	grid.Clear();

	// Ceiling and floor only. There are deliberately no side walls: the columns
	// arrive from off-screen right and leave off-screen left, and a wall at
	// either end would read as the field being enclosed when the whole point is
	// that it is not.
	for( int x = 0; x < mW; ++x )
	{
		grid.Set( x, 0, Cell::Wall, 255 );
		grid.Set( x, mH - 1, Cell::Wall, 255 );
	}

	for( const Column& c : mColumn )
	{
		const int left = Round( c.x );
		for( int k = 0; k < kColWidth; ++k )
		{
			const int x = left + k;
			if( x < 0 || x >= mW )
				continue;

			for( int y = 1; y <= mH - 2; ++y )
				if( y < c.gapY || y >= c.gapY + c.gapH )
					grid.Set( x, y, Cell::Wall, 200 );
		}
	}

	grid.Set( mFlierX, Round( mY ), Cell::Head, mHurt > 0 ? 120 : 255 );
}

float Flapper::Intensity() const
{
	const int maxGap = MaxGap();

	const float tight =
	    maxGap > kMinGap
	        ? 1.0f - float( mGapH - kMinGap ) / float( maxGap - kMinGap )
	        : 1.0f;

	const float fast = mScroll / kMaxScroll;

	// A column about to arrive is worth a reaction on its own, whatever the
	// ramp has got to.
	float close = 0.0f;
	for( const Column& c : mColumn )
	{
		const int left = Round( c.x );
		if( left + kColWidth - 1 >= mFlierX && left - mFlierX <= 4 )
			close = 0.7f;
	}

	return std::clamp( std::max( close, 0.5f * tight + 0.5f * fast ), 0.0f, 1.0f );
}

} // namespace coinop
