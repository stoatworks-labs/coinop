#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Bricks: a ball, a paddle, and a field to knock holes in.

	## The one that is continuous inside a discrete world

	Snake is a grid game. This is not: a ball that only ever sits on cell
	centres can travel at four angles, and a paddle that snaps to cells cannot
	be steered with a fader. So the ball and paddle live in float playfield
	coordinates and are rasterised at draw time. The bricks stay a grid, because
	they genuinely are one.

	## Three traps, all of which ship in first drafts of this game

	**1. Tunnelling.** Move the ball by `vel * dt` in one go and at any decent
	speed it steps straight over a brick -- or through the paddle, which reads
	as the ball passing through a solid object on its way to costing a life.
	Movement here is substepped so no step is longer than a quarter cell.

	**2. The horizontal lock.** Reflect off enough surfaces and the vertical
	component can approach zero, at which point the ball rattles side to side
	forever, hits nothing, and the game never ends. Real cabinets solve this
	with paddle geometry; here `vy` is simply held above a floor after every
	reflection.

	**3. The sticky corner.** Reflecting both axes on a single collision test
	lets a ball that clips a brick corner reverse into the cell it came from and
	immediately collide again, jittering in place. The axes are resolved
	separately -- move in x, test, then move in y, test -- so a corner hit
	reflects each component at most once per substep.

	## The effect variant is why this game is in here at all

	As an FFGL effect the brick field is the incoming clip: each brick cell
	samples the video underneath it, so the bricks *are* the footage and
	breaking them punches holes through to the layer below. That is a real VJ
	move rather than a novelty, and it needs nothing from this file -- the
	shader samples at the cell's own UV. The sim just has to be honest about
	which cells are still standing.
*/
namespace coinop
{

class Bricks final : public Game
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
	FVec Ball() const { return mBall; }
	FVec Velocity() const { return mVel; }
	int BricksLeft() const;
	int Lives() const { return mLives; }
	int Level() const { return mLevel; }
	float PaddleX() const { return mPaddleX; }

private:
	static constexpr float kMinVerticalSpeed = 0.35f;///< Fraction of total speed.

	/// Smallest paddle-relative hit offset the ball may leave with. Nonzero so
	/// that a perfectly centred return cannot be perfectly vertical -- see the
	/// vertical lock in Bricks.cpp.
	static constexpr float kMinPaddleOffset = 0.18f;
	static constexpr int kBrickWidth        = 2;

	int BrickCols() const { return ( mW - 2 ) / kBrickWidth; }
	int BrickRow( int y ) const { return y - mBrickTop; }
	int BrickIndex( int col, int row ) const { return row * BrickCols() + col; }

	bool BrickAt( int x, int y, int& outIndex ) const;
	void BuildField( const GameConfig& cfg, Rng& rng );
	void LaunchBall( Rng& rng );
	void ReflectOffPaddle();
	float PredictLandingX() const;
	void ClampVertical();

	int mW = 0;
	int mH = 0;

	FVec mBall = {};
	FVec mVel  = {};
	float mPaddleX     = 0.0f;///< Centre, in cells.
	float mPaddleHalf  = 3.0f;
	int mPaddleY       = 0;

	std::vector< uint8_t > mBricks;///< Hit points, 0 = gone. Row-major.
	std::vector< uint8_t > mTint;  ///< Palette slot per brick.
	int mBrickTop  = 2;
	int mBrickRows = 5;

	int mScore = 0;
	int mLives = 3;
	int mLevel = 0;
	bool mWaiting = true;///< Ball held on the paddle until Fire, or autoplay.
	int mWaitTicks = 0;

	float mAiTarget = 0.0f;
	int mAiCooldown = 0;
	uint8_t mFlash  = 0;
};

} // namespace coinop
