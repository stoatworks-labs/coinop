#pragma once

#include "Game.h"
#include "Vec.h"

/**
	Rally: two paddles, one ball.

	In here partly because it is four hundred lines cheaper than everything else
	and partly because it is the best of the five on a pixel map -- three moving
	objects on a black field, no detail to lose, legible at any grid size down
	to about 16x12.

	## It has to be able to end

	Rally is the only game of the five with no built-in failure state: two
	competent paddles rally forever, and a layer that never resets is a layer
	that stops being interesting after ninety seconds. So there is a target
	score, and reaching it finishes the game and triggers the respawn -- the
	match restarts rather than the rally continuing into the next set.

	The other half of that is that neither paddle may be perfect. Both sides run
	the same autopilot with the same Skill, and both are given a reaction lag
	and a tracking error that scale with it. At Skill 1.0 the rallies are long
	and the match still ends, because the error floor is never quite zero.

	## Two-player is the reason the input model is what it is

	Left paddle takes the Axis parameter, right paddle takes Up/Down. Map a
	fader to one and two pads to the other and two people can play it from one
	Resolume instance -- which, for a plugin whose entire input surface is
	MIDI-mapped parameters, is a genuinely good use of the constraint.
*/
namespace coinop
{

class Rally final : public Game
{
public:
	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScoreL + mScoreR; }
	bool Finished() const override { return mScoreL >= kTarget || mScoreR >= kTarget; }
	float Intensity() const override;

	// For coinoptest.
	FVec Ball() const { return mBall; }
	FVec Velocity() const { return mVel; }
	int ScoreLeft() const { return mScoreL; }
	int ScoreRight() const { return mScoreR; }

private:
	static constexpr int kTarget = 7;

	void Serve( Rng& rng, int toward );
	float PredictY( float atX ) const;
	void DrivePaddle( float& y, float& target, int& cooldown,
	                  const GameConfig& cfg, Rng& rng, float side );

	int mW = 0;
	int mH = 0;

	FVec mBall = {};
	FVec mVel  = {};

	float mPaddleL = 0.0f;///< Centre, in cells.
	float mPaddleR = 0.0f;
	float mHalf    = 2.5f;

	int mScoreL = 0;
	int mScoreR = 0;
	int mRally  = 0;

	float mTargetL  = 0.0f;
	float mTargetR  = 0.0f;
	int mCooldownL  = 0;
	int mCooldownR  = 0;
	int mServeDelay = 0;
	uint8_t mFlash  = 0;
};

} // namespace coinop
