#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Trails: riders that never stop, leaving walls behind them. Last one alive
	takes the round.

	## The one rule that makes it work

	A rider moves one cell every tick, always, and the cell it leaves becomes
	solid forever. There is no braking and no reversing. Everything else here
	is a consequence of that.

	It is the best fit for a cell grid of anything in this plugin -- better even
	than Snake, because there is no food to place and no length to track. The
	whole state is a board of who-owns-what plus four positions.

	## Deaths are resolved simultaneously, and that is not a detail

	The obvious loop is: for each rider, move it, then check whether it hit
	something. It gives the first rider in the array a real advantage -- two
	riders driving into the same empty cell head-on means the first one to be
	processed arrives, the cell is now solid, and the second one dies against
	it. Rider 0 wins every head-on, forever, and on autopilot that is visible
	within a minute.

	So a tick computes every rider's next cell first, then kills everything that
	landed somewhere blocked *or* somewhere another rider also landed. Head-on
	is a double kill, which is the right answer and the one the arcade original
	gives.

	## The autopilot is a flood fill, and it is capped twice

	The move worth making is the one that leaves the most room, so each
	candidate direction is scored by how much open space is reachable from it.
	That is a flood fill per direction per rider per tick, and on a 128x96 grid
	with four riders it is twelve fills over twelve thousand cells, twenty times
	a second, on the render thread.

	So the fill has a budget and stops counting once it has seen enough -- the
	difference between "plenty of room" and "even more room" does not change the
	decision, and Snake's autopilot clamps the same quantity for the same
	reason. The budget is also what stops the first tick of a round, when the
	board is empty and every direction reaches every cell, from being the most
	expensive tick in the game.

	Skill is a flat chance of ignoring the fill and turning at random. Without
	it four competent riders spiral into their own corners and survive until the
	board is full, which takes a very long time and is dull for all of it.

	## Rounds, not lives

	There is no natural way to lose a light-cycle match -- somebody always
	survives. Rally has the same problem and solves it the same way: the match
	runs to a target number of round wins, and reaching it finishes the game so
	the layer restarts. Without a target this game would never call `Finished`
	and the respawn would never fire.
*/
namespace coinop
{

class Trails final : public Game
{
public:
	static constexpr int kMaxRiders = 4;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mDone; }
	float Intensity() const override;

	// For coinoptest.
	int Riders() const { return mRiders; }
	int AliveCount() const;
	int Round() const { return mRound; }
	int Wins( int i ) const { return mRider[ i ].wins; }
	int TargetWins() const { return mTarget; }
	IVec Position( int i ) const { return mRider[ i ].pos; }
	bool Alive( int i ) const { return mRider[ i ].alive; }
	int TrailCells() const;

private:
	struct Rider
	{
		IVec pos   = {};
		Dir dir    = Dir::Right;
		bool alive = true;
		int wins   = 0;
	};

	size_t Index( int x, int y ) const { return size_t( y ) * size_t( mW ) + size_t( x ); }

	bool Blocked( int x, int y ) const;
	int FreeSpace( IVec from, int budget );
	Dir ChooseDir( int rider, const GameConfig& cfg, Rng& rng );
	void StartRound();

	int mW = 0;
	int mH = 0;

	int mRiders = 2;
	Rider mRider[ kMaxRiders ];

	std::vector< uint8_t > mBoard;  ///< 0 empty, else rider index + 1.
	std::vector< int32_t > mWritten;///< Tick each trail cell was laid.

	int mRound  = 0;
	int mTarget = 5;
	int mScore  = 0;
	int mTicks  = 0;
	int mHold   = 0;///< Ticks the finished round is held before the next one.
	bool mDone  = false;

	/// Flood-fill scratch. Members so the autopilot allocates nothing per tick.
	std::vector< uint8_t > mVisited;
	std::vector< IVec > mStack;
};

} // namespace coinop
