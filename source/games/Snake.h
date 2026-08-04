#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Snake.

	The reference game for this plugin: it is the one that is genuinely a grid
	all the way down, so nothing here has to be reconciled with anything
	continuous.

	## The turn queue, and why it is not "current direction"

	The naive implementation stores a direction and lets input overwrite it. It
	has a bug everybody who has written this game has shipped at least once:
	moving right, the player taps Up then Left inside one tick, and both writes
	land before the snake moves. Direction ends up Left, which is a reversal
	from Right, and the snake eats its own neck and dies for no visible reason.

	Guarding with "reject the opposite of the current direction" does not fix it
	-- at the moment Left is applied the direction is already Up, so Left is a
	legal turn and the guard passes. The reversal only exists relative to where
	the snake *was*.

	So turns are queued and applied one per tick, each validated against the
	direction the snake actually last moved in. It costs a queue and removes the
	whole class of bug.

	## The autopilot has to be beatable

	A perfect Snake autopilot exists -- follow a Hamiltonian cycle and it never
	dies, ever. It is also unwatchable: the snake solemnly zigzags the whole
	board for the sake of one apple. Worse, for this plugin it never resets, so
	the layer shows the same slowly-lengthening snake for the entire show.

	So the autopilot here is greedy-toward-food gated by a survivability check,
	with Skill governing how often it bothers to run the check. At Skill 1.0 it
	plays a long, careful game. Below that it takes the occasional greedy line
	that traps it, dies, and starts again -- which is what the layer actually
	wants.
*/
namespace coinop
{

class Snake final : public Game
{
public:
	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mDead; }
	float Intensity() const override;

	// Exposed for coinoptest, which asserts on state rather than on pixels.
	int Length() const { return int( mBody.size() ); }
	IVec Head() const { return mBody.empty() ? IVec{} : mBody.front(); }
	IVec Food() const { return mFood; }
	Dir Heading() const { return mDir; }

private:
	/// Playable area, inset by the one-cell wall border.
	int MinX() const { return 1; }
	int MinY() const { return 1; }
	int MaxX() const { return mW - 2; }
	int MaxY() const { return mH - 2; }

	bool Blocked( IVec p, bool ignoreTail ) const;
	void PlaceFood( Rng& rng );
	Dir ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const;

	/// Free cells reachable from `from` if the snake were at `body`. Used to
	/// reject a move that seals the snake into a pocket smaller than itself --
	/// the trap that kills every naive greedy Snake AI within thirty seconds.
	int ReachableSpace( IVec from ) const;

	int mW = 0;
	int mH = 0;

	std::vector< IVec > mBody;///< Front is the head.
	Dir mDir     = Dir::Right;
	Dir mLastDir = Dir::Right;
	std::vector< Dir > mTurns;

	IVec mFood  = {};
	int mGrow   = 0;
	int mScore  = 0;
	bool mDead  = false;
	int mTicksSinceFood = 0;

	/// Scratch for the flood fill, kept as a member so the autopilot does not
	/// allocate every tick.
	mutable std::vector< uint8_t > mVisited;
};

} // namespace coinop
