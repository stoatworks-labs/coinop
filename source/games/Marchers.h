#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Marchers: a formation that steps down the screen while you shoot up at it.

	The best grid fit of the five. The formation genuinely is a rectangular
	array that moves in whole-cell steps, so there is no reconciliation to do
	between the sim and the cells -- unlike Bricks, nothing here is continuous.

	## The tick rate is the difficulty

	The original's famous acceleration was an accident of hardware: the machine
	redrew every alien every frame, so as they died there were fewer to draw and
	the survivors sped up. It turned out to be the best thing about the game.

	It is reproduced deliberately here -- the march interval is a function of
	how many are left -- because "the last one moves fast" is the whole
	character of the game, and a formation that marches at a constant rate feels
	inert by comparison.

	## Bombs come from the bottom of a column, not from anywhere

	Picking a random live invader to drop a bomb lets one shoot through the two
	rows beneath it, which looks like a bug even to someone who has never seen
	the original. Bombs are dropped by the lowest survivor in a randomly chosen
	column.
*/
namespace coinop
{

class Marchers final : public Game
{
public:
	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0 || mLanded; }
	float Intensity() const override;

	// For coinoptest.
	int Alive() const;
	int Wave() const { return mWave; }
	int CannonX() const { return mCannonX; }

private:
	struct Shot
	{
		int16_t x = 0;
		float y   = 0.0f;
		float vy  = 0.0f;
		bool live = false;
	};

	int Index( int col, int row ) const { return row * mCols + col; }
	bool AliveAt( int col, int row ) const;
	int LowestInColumn( int col ) const;
	int FormationLeft() const;
	int FormationRight() const;
	void BuildWave( const GameConfig& cfg );
	void MarchStep();

	int mW = 0;
	int mH = 0;

	int mCols = 8;
	int mRows = 4;
	std::vector< uint8_t > mAlive;

	int mOffsetX  = 1;///< Formation origin, in cells.
	int mOffsetY  = 2;
	int mMarchDir = 1;
	int mMarchTimer = 0;

	int mCannonX = 0;
	Shot mBullet;                 ///< The player's single shot. One at a time, as it should be.
	std::vector< Shot > mBombs;

	int mScore  = 0;
	int mLives  = 3;
	int mWave   = 0;
	bool mLanded = false;///< Formation reached the cannon row: instant loss.
	int mHitFlash = 0;

	int mAiCooldown = 0;
};

} // namespace coinop
