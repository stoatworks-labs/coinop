#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Girders: climb the stack of platforms, dodge what is rolling down it.

	## The girders are flat, and that was a decision

	The arcade original this descends from is famous for sloped girders, and the
	slope is not decoration there -- it is what makes the barrels roll, and the
	slope reversing floor by floor is what makes them cascade.

	At 32 cells across and 24 down, a slope is a staircase. A girder that drops
	one cell every eight columns is four steps of aliasing that the player has
	to walk up, and every one of them is a place where "am I standing on the
	girder" needs a different answer than the cell above it. It buys a worse
	game and a much worse set of edge cases, on a playfield whose entire point
	is that it survives being sampled onto a pixel map.

	So the girders are flat and the *direction* is the thing that alternates.
	Row 0 rolls right, row 1 rolls left, and a barrel that runs out of floor
	drops to the row below and reverses -- which is the cascade, which was the
	part worth keeping.

	## Everything descends; nothing is thrown

	A barrel leaves the top row and is then entirely deterministic apart from
	one roll of the dice: at a ladder, it may take the ladder. That single
	choice is what stops the hazards arriving in a metronomic stream, and it is
	the only randomness in the hazard path.

	The alternative -- spawning hazards at random positions on random rows --
	was tried on paper and rejected. A barrel that appears next to the climber
	is not a hazard, it is a coin flip, and the player cannot read the board
	well enough to be blamed for it.

	## The hop is three ticks and one cell

	A real jump arc needs sub-cell vertical position, an airborne state that
	interacts with ladders and floors, and a landing test. On a grid this coarse
	it would be four cells of travel and would read as a glitch.

	What the game actually needs from a jump is a window in which a barrel on
	your own floor cannot touch you. So that is what it is: three ticks, drawn
	one cell higher, immune to whatever is rolling along the floor underneath.
	It costs one counter and it does the whole job.
*/
namespace coinop
{

class Girders final : public Game
{
public:
	static constexpr int kMaxRows = 10;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0; }
	float Intensity() const override;

	// For coinoptest.
	int Rows() const { return mRows; }
	int Lives() const { return mLives; }
	int Level() const { return mLevel; }
	int Barrels() const { return int( mBarrel.size() ); }
	int ClimberRow() const { return mRow; }
	IVec Climber() const { return { mX, mY }; }
	bool Climbing() const { return mClimbing; }
	int RowY( int r ) const;
	int StandY( int r ) const { return RowY( r ) - 1; }

private:
	struct Barrel
	{
		int16_t x     = 0;
		int16_t y     = 0;
		int8_t row    = 0;
		int8_t dir    = 1;
		bool descending = false;
		int accum     = 0;
	};

	/// Rows are numbered from the **bottom**: row 0 is the floor the climber
	/// starts on and row `mRows - 1` carries the prize. `mRowY` therefore
	/// decreases as the index rises, which is the one thing to keep straight in
	/// this file -- every sign error in it comes from forgetting it.
	///
	/// Gap `g` is the ladder run between row `g` and row `g + 1`.
	bool LadderAt( int gap, int x ) const;
	bool LadderUpAt( int row, int x ) const { return row + 1 < mRows && LadderAt( row, x ); }
	bool LadderDownAt( int row, int x ) const { return row >= 1 && LadderAt( row - 1, x ); }

	void BuildLevel( const GameConfig& cfg, Rng& rng );
	void PlaceClimber();
	void MoveBarrel( Barrel& bar, Rng& rng );
	void LoseLife();

	/// What the autopilot decided to do this tick, as the four things a player
	/// can do. Kept as a small struct so `Step` runs identical code for a human
	/// and for the autopilot -- a second movement path is a second set of bugs.
	struct Intent
	{
		int move  = 0;///< -1 left, +1 right
		int climb = 0;///< -1 up, +1 down
		bool jump = false;
	};

	Intent ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const;

	int mW = 0;
	int mH = 0;

	int mRows = 0;
	int mGap  = 4;
	int mRowY[ kMaxRows ]   = {};
	int8_t mRowDir[ kMaxRows ] = {};
	std::vector< std::vector< int16_t > > mLadder;///< Per gap, the ladder columns.

	int16_t mX     = 1;
	int16_t mY     = 1;
	int mRow       = 0;
	bool mClimbing = false;
	int mClimbGap  = 0;
	int mFacing    = 1;
	int mJump      = 0;

	/// Ticks before the climber may hop again. Without it the hop is a toggle
	/// for invulnerability -- land, hop, land, hop -- and a climber that is
	/// airborne more often than not cannot be killed by anything rolling on the
	/// floor, which on a small grid produced a level it won forever.
	int mJumpCool  = 0;

	IVec mPrize = {};

	std::vector< Barrel > mBarrel;
	int mSpawnTimer    = 0;
	int mSpawnInterval = 40;
	int mBarrelSpeed   = 3;///< Moves per six ticks.
	float mDescendOdds = 0.35f;

	int mScore  = 0;
	int mLives  = 3;
	int mLevel  = 0;
	int mTicks  = 0;
	int mStuck  = 0;
	int mProgressRow = 0;///< Floor the stuck counter was last reset on.
};

} // namespace coinop
