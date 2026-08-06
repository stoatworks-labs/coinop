#include "games/Reflex.h"

#include "Raster.h"

// <cmath> explicitly: libc++ leaks it through <algorithm> and MSVC does not.
// See AGENTS.md -- this is a macOS-green, Windows-red failure by construction.
#include <algorithm>
#include <cmath>

namespace coinop
{

namespace
{

/// Five hazards, one per button on the controller. The mapping is fixed rather
/// than random per hazard, because the whole game is "read the board, press the
/// thing" -- a hazard that meant a different button each time would make the
/// arrow the only information on screen and the corridor pure decoration.
Button ButtonFor( uint8_t kind )
{
	switch( kind % 5 )
	{
		case 0: return Button::Up;   ///< a gap in the floor: jump it
		case 1: return Button::Fire; ///< something in the way: strike it
		case 2: return Button::Down; ///< something overhead: duck under it
		case 3: return Button::Left; ///< it lunges from the right: go left
		default: return Button::Right;
	}
}

} // namespace

bool Reflex::PromptOpen() const
{
	return mPromptId >= 0;
}

const Reflex::Hazard* Reflex::Prompted() const
{
	if( mPromptId < 0 )
		return nullptr;

	for( const Hazard& h : mHazard )
		if( h.id == mPromptId )
			return &h;

	return nullptr;
}

void Reflex::Reset( const GameConfig& cfg, Rng& rng )
{
	(void)rng;

	mW = cfg.gridW;
	mH = cfg.gridH;

	mHazard.clear();
	mPromptId = -1;
	mNextId   = 1;

	const float diff = std::clamp( cfg.difficulty, 0.0f, 1.0f );

	// The lead distance is the window, in cells. Difficulty sets how much of it
	// there is to start with; the run itself takes it away -- see the header.
	mBaseLead   = std::clamp( int( std::lround( 12.0f - 5.0f * diff ) ), 4, mW / 2 );
	mLead       = mBaseLead;
	mBaseScroll = std::clamp( int( std::lround( 2.0f + 3.0f * diff ) ), 1, 6 );
	mScroll     = mBaseScroll;
	mScrollAccum = 0;

	// Far enough apart that one prompt is resolved before the next opens. Two
	// prompts at once has no sensible answer with one set of buttons.
	mSpawnGap   = std::max( mBaseLead + 4, mW / 2 );
	mSinceSpawn = mSpawnGap;

	mAiDelay   = -1;
	mPose      = 0;
	mPoseTimer = 0;

	mScore  = 0;
	mStreak = 0;
	mTicks  = 0;

	// Five and not three. This game punishes a mistake immediately and
	// completely, and at low Skill three lives were gone in a hundred ticks --
	// about four seconds of layer, which is not a game anybody saw.
	mLives = 5;
}

float Reflex::TickHz( const GameConfig& cfg ) const
{
	// This is a reaction game, so the tick rate is the resolution of the
	// player's reaction. Too low and the window cannot be expressed; too high
	// and the corridor scrolls faster than it reads.
	const float t = std::clamp( cfg.speed, 0.0f, 1.0f );
	return 12.0f + 28.0f * t * t;
}

void Reflex::Spawn( Rng& rng )
{
	Hazard h;
	h.id   = mNextId++;
	h.x    = int16_t( mW - 2 );
	h.kind = uint8_t( rng.Below( 5 ) );
	mHazard.push_back( h );
}

void Reflex::Fail()
{
	--mLives;
	mStreak    = 0;
	mPose      = 4;
	mPoseTimer = 8;
	mPromptId  = -1;
	mAiDelay   = -1;
}

void Reflex::Answer( Button b )
{
	Hazard* target = nullptr;
	for( Hazard& h : mHazard )
		if( h.id == mPromptId )
			target = &h;

	if( !target )
		return;

	target->answered = true;
	mPromptId        = -1;
	mAiDelay         = -1;

	if( b != mPromptButton )
	{
		Fail();
		return;
	}

	++mScore;
	++mStreak;

	// The ramp, and the thing that guarantees the run ends. Keyed off the total
	// rather than off the streak: resetting it on every miss would let a
	// perfect autopilot climb to the same speed three times and never get past
	// it, and the game would only ever end by running out of lives at a
	// difficulty it had already proved it could handle.
	mLead   = std::clamp( mBaseLead - mScore / 5, 2, mBaseLead );
	mScroll = std::clamp( mBaseScroll + mScore / 8, 1, 9 );

	switch( b )
	{
		case Button::Up: mPose = 1; break;
		case Button::Down: mPose = 2; break;
		case Button::Fire: mPose = 3; break;
		default: mPose = 0; break;
	}
	mPoseTimer = 6;
}

void Reflex::Advance( const GameConfig& cfg, Rng& rng )
{
	++mSinceSpawn;
	if( mSinceSpawn >= mSpawnGap )
	{
		mSinceSpawn = 0;
		Spawn( rng );
	}

	for( Hazard& h : mHazard )
		--h.x;

	// Resolve before opening, so a hazard that arrives on the same step it
	// would have opened on is a miss rather than a prompt nobody saw.
	for( Hazard& h : mHazard )
	{
		if( h.answered || h.x > HeroX() )
			continue;

		h.answered = true;
		if( h.id == mPromptId )
			Fail();
	}

	for( Hazard& h : mHazard )
	{
		if( h.opened || h.answered || mPromptId >= 0 )
			continue;

		if( h.x > HeroX() + mLead )
			continue;

		h.opened      = true;
		mPromptId     = h.id;
		mPromptButton = ButtonFor( h.kind );

		if( cfg.autopilot )
		{
			// Two ticks is the floor at any Skill. It is what stops a perfect
			// autopilot from being unbeatable once the window is down to one
			// tick, and it is the reason this game terminates at all.
			const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );
			mAiDelay = 2 + int( rng.Below( uint32_t( 1 + int( ( 1.0f - skill ) * 10.0f ) ) ) );
		}
	}

	mHazard.erase( std::remove_if( mHazard.begin(), mHazard.end(),
	                               []( const Hazard& h ) { return h.x < 0; } ),
	               mHazard.end() );
}

void Reflex::Step( const GameConfig& cfg, Input& in, Rng& rng )
{
	if( mLives <= 0 )
		return;

	++mTicks;
	if( mPoseTimer > 0 && --mPoseTimer == 0 )
		mPose = 0;

	Button b;
	while( in.Pop( b ) )
	{
		if( cfg.autopilot || b == Button::Reset )
			continue;

		// A press with no prompt open is not punished. Mashing has to be free,
		// or the only viable way to play is to not touch the controller.
		if( mPromptId >= 0 )
			Answer( b );
	}

	if( cfg.autopilot && mPromptId >= 0 && mAiDelay >= 0 )
	{
		if( --mAiDelay <= 0 )
		{
			const float skill = std::clamp( cfg.skill, 0.0f, 1.0f );

			// Deliberate incompetence: below full Skill it sometimes presses
			// the wrong one, which is the failure mode a person actually has.
			Button press = mPromptButton;
			if( !rng.Chance( 0.5f + 0.5f * skill ) )
				press = Button( rng.Below( uint32_t( Button::Reset ) ) );

			Answer( press );
		}
	}

	mScrollAccum += mScroll;
	while( mScrollAccum >= 6 )
	{
		mScrollAccum -= 6;
		Advance( cfg, rng );

		if( mLives <= 0 )
			return;
	}
}

void Reflex::Draw( const GameConfig& cfg, Grid& grid ) const
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

	const int floorY = FloorY();
	const int ceilY  = CeilY();
	const int heroX  = HeroX();

	// The corridor floor, with a gap wherever a pit is. Drawn first so hazards
	// and the hero land on top of it.
	for( int x = 1; x <= mW - 2; ++x )
	{
		bool pit = false;
		for( const Hazard& h : mHazard )
			pit = pit || ( h.x == x && h.kind % 5 == 0 );

		if( !pit )
			grid.Set( x, floorY, Cell::Wall, 200 );
	}

	// A ceiling, and markers along it.
	//
	// Neither is decoration. Without them this game draws a floor, a two-cell
	// hero and one hazard, and leaves two thirds of the playfield empty in
	// every frame where no prompt happens to be open -- which on a VJ layer is
	// most of them. The ceiling closes the corridor and the markers give the
	// scroll something to be measured against, so the picture moves even when
	// nothing is being asked of the player.
	if( ceilY > 1 )
		for( int x = 1; x <= mW - 2; ++x )
			grid.Set( x, ceilY, Cell::Wall,
			          ( ( x + mTicks / 8 ) % 4 == 0 ) ? 220 : 130 );

	// Lives, as pips in the top corner. There is nowhere to write a number and
	// this is the one piece of state a viewer cannot infer from the board.
	for( int i = 0; i < mLives && 1 + i * 2 <= mW - 2; ++i )
		grid.Set( 1 + i * 2, 1, Cell::Food, 200 );

	for( const Hazard& h : mHazard )
	{
		const uint8_t shade = h.answered ? 110 : 255;
		switch( h.kind % 5 )
		{
			case 0:
				grid.Set( h.x, floorY + 1, Cell::Enemy, shade );
				break;
			case 1:
				grid.Set( h.x, floorY - 1, Cell::Enemy, shade );
				break;
			case 2:
				// The overhang hangs *from* the ceiling rather than floating
				// under it, so it is obvious what the hero has to duck below.
				for( int y = ceilY + 1; y <= floorY - 3; ++y )
					grid.Set( h.x, y, Cell::Enemy, shade );
				break;
			default:
				grid.Set( h.x, floorY - 1, Cell::Enemy, shade );
				grid.Set( h.x, floorY - 2, Cell::Enemy, shade );
				break;
		}
	}

	// The hero, two cells of it, posed by whatever the last answer was.
	const int poseLift = mPose == 1 ? 2 : 0;
	const bool crouch  = mPose == 2;
	const uint8_t bright = mPose == 4 ? 110 : 255;

	if( crouch )
	{
		grid.Set( heroX, floorY - 1, Cell::Head, bright );
	}
	else
	{
		grid.Set( heroX, floorY - 2 - poseLift, Cell::Head, bright );
		grid.Set( heroX, floorY - 1 - poseLift, Cell::Body, bright );
	}

	if( mPose == 3 )
		grid.Set( heroX + 1, floorY - 1, Cell::Ball, 255 );

	//-----------------------------------------------------------------------
	// The prompt: a big arrow and the distance left, drawn out.
	//-----------------------------------------------------------------------
	const Hazard* prompt = Prompted();
	if( !prompt )
		return;

	// The arrow lives above the corridor, in the space the ceiling now bounds.
	const int arm = std::clamp( std::min( mW, mH ) / 7, 1, 4 );
	const int cx  = mW / 2;
	const int cy  = std::clamp( ceilY / 2 + 1, arm + 2, std::max( arm + 2, ceilY - arm - 2 ) );

	const Cell ink = Cell::Food;

	switch( mPromptButton )
	{
		case Button::Up:
			DrawLine( grid, cx, cy + arm, cx, cy - arm, ink );
			DrawLine( grid, cx, cy - arm, cx - arm, cy, ink );
			DrawLine( grid, cx, cy - arm, cx + arm, cy, ink );
			break;

		case Button::Down:
			DrawLine( grid, cx, cy - arm, cx, cy + arm, ink );
			DrawLine( grid, cx, cy + arm, cx - arm, cy, ink );
			DrawLine( grid, cx, cy + arm, cx + arm, cy, ink );
			break;

		case Button::Left:
			DrawLine( grid, cx + arm, cy, cx - arm, cy, ink );
			DrawLine( grid, cx - arm, cy, cx, cy - arm, ink );
			DrawLine( grid, cx - arm, cy, cx, cy + arm, ink );
			break;

		case Button::Right:
			DrawLine( grid, cx - arm, cy, cx + arm, cy, ink );
			DrawLine( grid, cx + arm, cy, cx, cy - arm, ink );
			DrawLine( grid, cx + arm, cy, cx, cy + arm, ink );
			break;

		default:
			// Fire has no direction, so it is the one glyph that is a shape
			// rather than an arrow.
			DrawLine( grid, cx, cy - arm, cx + arm, cy, ink );
			DrawLine( grid, cx + arm, cy, cx, cy + arm, ink );
			DrawLine( grid, cx, cy + arm, cx - arm, cy, ink );
			DrawLine( grid, cx - arm, cy, cx, cy - arm, ink );
			break;
	}

	// The bar is the remaining distance, not a second clock counting the same
	// thing -- see the header. It cannot disagree with the hazard because it is
	// measured off it.
	const int left = std::clamp( int( prompt->x ) - heroX, 0, std::max( 1, mLead ) );
	const int half = std::max( 1, mW / 4 );
	const int span = left * half / std::max( 1, mLead );

	for( int i = -span; i <= span; ++i )
		grid.Set( cx + i, cy + arm + 2, Cell::Paddle, 220 );
}

float Reflex::Intensity() const
{
	const Hazard* prompt = Prompted();
	if( !prompt )
		return std::clamp( float( mScore ) / 60.0f, 0.0f, 0.35f );

	// Climbs as the hazard closes, which is the one moment in this game that
	// anything is at stake.
	const int left = std::clamp( int( prompt->x ) - HeroX(), 0, std::max( 1, mLead ) );
	return std::clamp( 1.0f - float( left ) / float( std::max( 1, mLead ) ), 0.0f, 1.0f );
}

} // namespace coinop
