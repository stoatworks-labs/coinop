#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Reflex: the laserdisc mechanic, without the laserdisc.

	## What is actually being ported here

	The arcade cabinet this comes from had no game in it in the usual sense. It
	was a video disc of hand-drawn animation and a controller, and the entire
	mechanic was: a cue appears, you have a fraction of a second to make the
	right input, and you either continue or you die. Everything else about it --
	the artwork, the character, the scenes, the deaths -- is the part that
	belongs to whoever drew it, and none of that is portable to a 32x24 grid of
	cells even if it were free to take.

	The *mechanic* is portable and is the whole of this game. A hazard comes at
	you, a prompt opens, the window closes. Nothing here depicts anything.

	## The window is distance, not a timer

	The obvious build is a countdown: show an arrow, start a clock, wait. It is
	also the version where the player has no idea how long they have got, which
	turns the mechanic from a reaction test into a guess.

	So there is no timer. A hazard travels toward the hero at a known rate, the
	prompt opens at a fixed distance, and the window *is* the travel time. The
	bar under the arrow is the remaining distance drawn out, not a separate
	clock -- which means it cannot disagree with the thing it is measuring, and
	the player can read how long they have from the board itself.

	## Why the autopilot cannot be perfect at this

	Every other game here has an autopilot that fails through misjudgement. This
	one has nothing to misjudge: there is a correct button and it is on the
	screen. Skill can make it press the wrong one, but at Skill 1.0 a
	reaction-time game with a fixed window is a game the machine simply wins,
	forever, and a layer whose game never ends is the thing AGENTS.md says must
	not ship.

	So the window closes as the run gets longer. The lead distance shrinks and
	the hazards speed up, until the prompt is open for a single tick and the
	minimum reaction -- two ticks, floor, at any Skill -- cannot beat it. The
	run ends because the game got faster than anything can play it, which is
	exactly how the arcade original ended too.
*/
namespace coinop
{

class Reflex final : public Game
{
public:
	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0; }
	float Intensity() const override;

	// For coinoptest.
	int Lives() const { return mLives; }
	int Streak() const { return mStreak; }
	int Lead() const { return mLead; }
	int Hazards() const { return int( mHazard.size() ); }
	bool PromptOpen() const;
	Button PromptButton() const { return mPromptButton; }

private:
	struct Hazard
	{
		int32_t id    = 0;
		int16_t x     = 0;
		uint8_t kind  = 0;///< Selects the prompt and how it is drawn.
		bool opened   = false;
		bool answered = false;
	};

	int HeroX() const { return std::max( 2, mW / 4 ); }
	int FloorY() const { return mH - 3; }

	/// Top of the corridor. The prompt goes above it and the hazards below, and
	/// having a line at all is what stops the playfield being two thirds empty
	/// in every frame with no prompt open.
	int CeilY() const { return std::max( 3, FloorY() - std::max( 4, mH / 4 ) ); }

	const Hazard* Prompted() const;
	void Spawn( Rng& rng );
	void Advance( const GameConfig& cfg, Rng& rng );
	void Answer( Button b );
	void Fail();

	int mW = 0;
	int mH = 0;

	std::vector< Hazard > mHazard;

	/// The hazard the prompt currently belongs to, by id, or -1. An id and not
	/// an index or a pointer: the vector is compacted whenever a hazard leaves
	/// the board, and both of the other two go stale when it is.
	int32_t mPromptId    = -1;
	int32_t mNextId      = 1;
	Button mPromptButton = Button::Up;

	int mLead        = 9;
	int mBaseLead    = 9;
	int mScroll      = 3;///< Sixths of a cell per tick.
	int mBaseScroll  = 3;
	int mScrollAccum = 0;
	int mSpawnGap    = 12;
	int mSinceSpawn  = 0;

	int mAiDelay = -1;

	int mPose      = 0;///< 0 stand, 1 up, 2 down, 3 strike, 4 hurt.
	int mPoseTimer = 0;

	int mScore  = 0;
	int mStreak = 0;
	int mLives  = 3;
	int mTicks  = 0;
};

} // namespace coinop
