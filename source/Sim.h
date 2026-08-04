#pragma once

#include "Game.h"

/**
	The fixed-timestep driver: the piece that makes a stateful simulation
	survive a host that was never designed to run one.

	## This plugin breaks the fleet's rule, knowingly

	[orrery/AGENTS.md](../../orrery/AGENTS.md) states the house rule plainly:
	*"An instance's placement is a pure function of (index, phase). There is no
	simulation state anywhere."* The reason given there is exactly right --
	integrate a velocity a frame at a time and the speed of the motion becomes
	whatever frame rate the host happened to manage, so a chase slows down when
	the projection load goes up and the lights come apart from the music.

	A game cannot be written that way. Snake *is* accumulated state; there is no
	closed form for "where is the snake at t=91.3" that does not involve having
	played the first 91.3 seconds. So this file exists to contain the damage,
	and it is three specific defences.

	## Defence 1: steps come from the clock, never from frames

	The accumulator takes host seconds and pays out fixed ticks. A frame rate
	drop makes the game render less smoothly and does not make it play slower.
	Two instances on two layers at different frame rates play at the same speed.

	## Defence 2: a frame with no elapsed time renders and does not step

	Resolume renders the same frame more than once -- to the preview, to the
	output, to a clip thumbnail. [tinsel](../../tinsel/source/Tinsel.cpp) puts
	it directly: *"rendering the same frame twice gives the same picture
	twice."* For a stateless generator that is free. Here, a second render of
	frame N with no guard is a second tick: the ball moves at double speed
	whenever the preview monitor is open, and at normal speed when it is closed.
	That is a bug that changes behaviour depending on which windows the operator
	has arranged, and it would be miserable to diagnose from a bug report.

	`hostSeconds` not advancing means no time has passed, so no tick is owed.

	## Defence 3: catch-up is capped

	When a layer is bypassed, or its deck is not showing, or the clip is
	stopped, `ProcessOpenGL` simply stops being called. Come back forty seconds
	later and an honest accumulator owes four hundred ticks, pays them all in
	one frame, and the snake -- which never gets to render between them -- has
	run the length of the playfield eleven times and is dead before a single
	pixel reaches the screen. Worse, on a slow machine the catch-up itself takes
	longer than the time it is catching up on, and the plugin never recovers.

	So elapsed time is clamped and the tick count per frame is capped. The game
	loses time it was not on screen for, which is the correct answer: nobody was
	watching.
*/
namespace coinop
{

class Sim
{
public:
	/// Longest gap the accumulator will honour. Beyond this the game simply
	/// skips ahead. A quarter second is about twelve frames -- long enough to
	/// ride out a heavy composition change, short enough that a resumed layer
	/// does not lurch.
	static constexpr double kMaxElapsed = 0.25;

	/// Hard cap on ticks paid out in one frame, so a game whose tick rate is
	/// high and whose frame rate has collapsed cannot spiral.
	static constexpr int kMaxTicksPerFrame = 16;

	void SetGame( GameId id );
	GameId CurrentGame() const { return mId; }

	/// Applies a config, restarting the game if the change is structural --
	/// grid size or seed. Speed and skill changes take effect without a reset,
	/// because an operator riding a Speed fader does not expect the snake to
	/// die.
	void Configure( const GameConfig& cfg );

	/// One rendered frame. `hostSeconds` is the normalised host clock. Steps
	/// zero or more ticks, then redraws the grid.
	void Advance( double hostSeconds, Input& in );

	/// Force a restart -- the Reset parameter, or a game-over respawn.
	void Restart();

	const Grid& Playfield() const { return mGrid; }
	const GameConfig& Config() const { return mCfg; }

	int Score() const { return mGame ? mGame->Score() : 0; }
	float Intensity() const { return mGame ? mGame->Intensity() : 0.0f; }

	/// Diagnostics, and the harness's window onto the accumulator.
	uint64_t TicksRun() const { return mTicks; }
	int LastFrameTicks() const { return mLastFrameTicks; }

private:
	GameId mId = GameId::Snake;
	std::unique_ptr< Game > mGame;
	GameConfig mCfg;
	Grid mGrid;
	Rng mRng{ 1 };

	double mClock  = -1.0;///< Last host time seen. Negative until the first frame.
	double mAccum  = 0.0;
	uint64_t mTicks = 0;
	int mLastFrameTicks = 0;
	int mDeadTicks      = 0;///< Counts up once Finished(), triggers the respawn.
};

} // namespace coinop
