#pragma once

#include "Game.h"

#include <cmath>
#include <vector>

/**
	Flapper: one button, gravity, and a wall of gaps coming at you.

	## Why this one is cheap enough to be worth having

	It costs no new cell type and no new parameter. The columns are `Wall`,
	which already means "the thing you die on", and the flier is `Head`. The
	whole game is a position, a velocity and a short list of columns -- less
	state than Snake carries -- which is the argument [Game.h](../Game.h) makes
	for why a fourteenth game should not cost a fourteenth bundle.

	It also survives the Grid slider better than anything else in the plugin. A
	field of vertical bars with holes in it is still legible at twelve cells
	across, where a maze is mush and two fighters are two dots.

	## The button is an impulse, not a thrust

	Holding it does nothing, and a press *sets* the vertical velocity rather
	than adding to it. That is the rule that makes the game readable: every
	press produces the same arc from wherever it was pressed, so the player is
	timing one thing rather than managing a throttle. Accumulating instead lets
	a fast tapper climb at whatever speed they can tap, and the obstacles stop
	meaning anything.

	## Termination is structural, and here it has to be

	Every other autopilot in this plugin can eventually be beaten by the game
	getting harder than it is. This one cannot be, at a fixed difficulty:
	holding a single axis between two horizontal edges is a strictly easier
	control problem than [Stacker](Stacker.h) or [Duel](Duel.h), and a competent
	flier keeps flying until the host is switched off. That is not a high score,
	it is a layer that stopped changing -- the thing `respawnTicks` exists to
	prevent.

	So difficulty here is a ramp rather than a setting. Every column passed
	narrows the gap toward `kMinGap`, raises the scroll toward `kMaxScroll`,
	pulls the columns together toward `kMinSpacing`, and widens how far the next
	hole may sit from the last. At the floor of all three there are
	`kMinSpacing / kMaxScroll` ticks between one column and the next -- under
	five -- and a flier clamped to `kMaxFall` of a cell a tick covers about four
	cells in that time, arriving at whatever speed it arrives at. The holes drift
	further apart than that. The run ends because the arithmetic says it must,
	and `coinoptest` does that arithmetic rather than trusting this paragraph.

	The ramp is keyed to columns passed and survives losing a life, which is the
	only reason a flier that dies cheaply three times still terminates the same
	way as one that plays well.

	## Collision is swept, deliberately

	The flier's x never moves; the columns move past it. So a column travelling
	faster than one cell a tick can step from "not yet at the flier" to "past
	the flier" without the flier's column ever being tested, and the symptom is
	a flier passing through solid wall at high scroll speeds only -- which is to
	say, late in a run, rarely, and never in a short test.

	`kMaxScroll` being below one cell a tick already prevents it. The test is
	swept anyway, against the span the column crossed rather than the span it
	landed on, because the clamp is a number someone will tune and the swept
	test is a property that survives them tuning it.

	Vertical motion needs no such care: a column is a solid block many cells
	tall, not a one-cell ledge, so there is nothing thin to pass through. That is
	the one way this game is easier than [Rafters](Rafters.h), whose platforms
	are exactly the hazard `kMaxFall` exists for.
*/
namespace coinop
{

class Flapper final : public Game
{
public:
	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0; }
	float Intensity() const override;

	/// The floors the ramp walks down to. The termination argument in the
	/// header is arithmetic on these, so the test reads them from here rather
	/// than repeating the numbers and drifting out of step with them.
	static constexpr int kMinGap      = 2;
	static constexpr int kMinSpacing  = 4;
	static constexpr float kMaxScroll = 0.85f;

	/// One cell a tick, less a margin. Shared with the vertical clamp because
	/// both exist for the same reason -- see the header.
	static constexpr float kMaxFall = 0.9f;

	/// Two, not one. A one-cell column at 32 wide is a dotted line rather than
	/// an obstacle, and it gives the swept collision test nothing to work with
	/// on the tick it arrives.
	static constexpr int kColWidth = 2;

	// For coinoptest.
	int Lives() const { return mLives; }
	int Passed() const { return mPassed; }
	int Columns() const { return int( mColumn.size() ); }
	int GapHeight() const { return mGapH; }
	int Spacing() const { return mSpacing; }
	float Scroll() const { return mScroll; }
	int FlierX() const { return mFlierX; }
	int FlierY() const { return Round( mY ); }
	bool Solid( int x, int y ) const;

private:
	struct Column
	{
		float x  = 0.0f;///< Left edge in cells. Decreases every tick.
		int gapY = 0;   ///< Top row of the hole.
		int gapH = 0;

		/// Scored already. Per column and not a single counter, because a life
		/// lost between two columns must not re-award the one still on screen.
		bool taken = false;
	};

	static int Round( float v ) { return int( std::floor( v + 0.5f ) ); }

	int MaxGap() const;
	void SpawnAt( float x, Rng& rng );
	void Spawn( Rng& rng );

	/// Fill the playfield with columns as though the run were already under way,
	/// leaving the flier a runway. See the note in the .cpp -- an empty field is
	/// a black layer, and this game starts one three times a game without it.
	void FillField( Rng& rng );

	void Ramp();
	void LoseLife( Rng& rng );
	bool Blocks( const Column& c, float sweptFrom, int y ) const;

	/// Row the autopilot is trying to be on: the middle of the next hole it has
	/// not passed, or mid-playfield when the field is empty.
	int TargetRow() const;

	/// Whether a competent player would flap this tick. See the note in the .cpp
	/// for why this is a damped position error and not a forward projection.
	bool ChooseFlap() const;

	int mW = 0;
	int mH = 0;

	int mFlierX = 0;
	float mY    = 0.0f;
	float mVy   = 0.0f;

	std::vector< Column > mColumn;

	int mGapH     = 4;
	float mScroll = 0.18f;
	int mSpacing  = 12;
	int mDrift    = 2;///< How far the next hole may sit from the last.
	int mLastGapY = 1;

	int mScore  = 0;
	int mPassed = 0;///< Columns cleared, ever. Never reset -- it drives the ramp.
	int mLives  = 3;
	int mTicks  = 0;
	int mHurt   = 0;
};

} // namespace coinop
