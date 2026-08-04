#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Drift: a ship with momentum, rocks that split, and a wrapping playfield.

	## The odd one out, and why it is still a grid game

	Everything else in this plugin is naturally discrete. This is not: it is the
	vector game, all continuous rotation and sub-cell drift, and the honest
	reading is that it does not belong in a cell grid at all.

	It is here anyway, rasterised, for the reason set out in
	[Raster.h](../Raster.h): the plugin's actual job is driving pixel maps and
	LED walls, and a one-pixel antialiased vector line does not survive being
	sampled onto a 30 mm pitch wall -- it lands between fixtures and vanishes. A
	Bresenham'd outline does. So the state stays continuous and only the picture
	is quantised, which is also what keeps the physics honest: nothing here
	snaps to a cell.

	## Momentum is the game

	The temptation with a ship on a grid is to make the controls positional --
	press left, move left. That is a different and much worse game. Rotation and
	thrust with no braking, drifting through your own previous velocity, is the
	whole character of it, and it is why the autopilot below is mostly a problem
	of *not* accelerating.

	## Wrapped distance, everywhere

	Every comparison between two things on this playfield has to go through
	`WrapDelta`. A rock at x=31 on a 32-wide field is one cell from a ship at
	x=0, not thirty-one, and a plain subtraction gets that wrong in the two
	places it matters most: collision detection, which then misses, and the
	autopilot's target selection, which then aims at the wrong rock and turns
	the long way round to do it.
*/
namespace coinop
{

class Drift final : public Game
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
	int RockCount() const { return int( mRocks.size() ); }
	int Lives() const { return mLives; }
	FVec ShipPos() const { return mShip; }

private:
	static constexpr int kRockVerts = 8;

	struct Rock
	{
		FVec pos   = {};
		FVec vel   = {};
		float ang  = 0.0f;
		float spin = 0.0f;
		float radius = 2.0f;
		int size   = 2;///< 2 large, 1 medium, 0 small.
		float shape[ kRockVerts ] = {};
	};

	struct Bullet
	{
		FVec pos  = {};
		FVec vel  = {};
		float life = 0.0f;
	};

	void SpawnWave( const GameConfig& cfg, Rng& rng );
	Rock MakeRock( int size, FVec at, Rng& rng, const GameConfig& cfg ) const;
	void SplitRock( size_t index, Rng& rng, const GameConfig& cfg );
	void RespawnShip();
	void Autopilot( const GameConfig& cfg, Rng& rng, bool& fire );

	int mW = 0;
	int mH = 0;

	FVec mShip    = {};
	FVec mShipVel = {};
	float mAngle  = 0.0f;///< Radians, 0 = up.
	bool mThrusting = false;

	std::vector< Rock > mRocks;
	std::vector< Bullet > mBullets;

	int mScore = 0;
	int mLives = 3;
	int mWave  = 0;
	float mFireCooldown = 0.0f;
	int mInvulnTicks    = 0;///< Grace period after a respawn.
	int mHitFlash       = 0;
};

} // namespace coinop
