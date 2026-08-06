#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Swarm: a formation that individual attackers leave, dive out of, and rejoin.

	## Why this is not Marchers with a different name

	[Marchers](Marchers.h) is the other formation shooter here and the two are
	deliberately different games. Marchers is a rigid block stepping down in
	whole cells, and its whole character is the acceleration as it thins out --
	nothing in it ever leaves the formation.

	Here the formation does not descend at all. It drifts side to side and stays
	where it is; the threat is that attackers peel off it one at a time and fly
	a curved run at the ship, then come round and slot back in. Kill the wave
	and it is because you shot it apart, not because you outlasted it.

	That one difference drags a lot behind it. A diver is not on the grid --
	it is at a float position moving on a curve, which puts this game on
	Drift's side of the line rather than Marchers'. See `Raster.h` for why the
	answer to that is to keep the state continuous and quantise only the
	picture, and not to reach for a second renderer.

	## The dive is steering plus a wobble, not a spline

	The obvious implementation is a Bezier per dive: pick a control point, walk
	the parameter, done. It looks correct and plays badly -- the path is decided
	the instant the dive starts, so the ship simply steps aside and the dive is
	wasted. Every dive after the first looks the same, too, because the control
	points come from the same distribution.

	So a dive is two terms instead: a pull toward wherever the ship is *now*,
	and a sine wobble whose sign is picked per dive. The pull is what makes the
	attacker feel like it is coming for you; the wobble is what stops it being
	a homing missile, which would be unavoidable and therefore unfair.

	## Rejoining is what makes the wave readable

	A diver that flies off the bottom and is gone leaves a hole in the
	formation and no way to know how much wave is left. So it comes back --
	round the bottom, in at the top, down to its own slot, and it is dangerous
	the whole way. The consequence worth knowing about is that the number of
	attackers on screen is not the number left alive, and only the second one
	ends the wave.
*/
namespace coinop
{

class Swarm final : public Game
{
public:
	static constexpr int kMaxCols   = 10;
	static constexpr int kMaxRows   = 4;
	static constexpr int kMaxFlyers = kMaxCols * kMaxRows;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0; }
	float Intensity() const override;

	// For coinoptest.
	int Alive() const;
	int Diving() const;
	int Wave() const { return mWave; }
	int Lives() const { return mLives; }
	int ShipX() const { return mShipX; }

private:
	enum class Fly : uint8_t
	{
		Formation = 0,
		Diving,
		Returning,
		Dead
	};

	struct Flyer
	{
		Fly state  = Fly::Formation;
		float x    = 0.0f;
		float y    = 0.0f;
		float phase = 0.0f;
		int8_t col  = 0;
		int8_t row  = 0;
		int8_t swing = 1;
		int bombCool = 0;
	};

	struct Shot
	{
		float x   = 0.0f;
		float y   = 0.0f;
		float vy  = 0.0f;
		bool live = false;
	};

	float SlotX( int col ) const;
	float SlotY( int row ) const;

	void BuildWave( const GameConfig& cfg );
	void LaunchDive( Rng& rng );
	void UpdateFlyer( Flyer& f, const GameConfig& cfg, Rng& rng );
	int ChooseAim( const GameConfig& cfg, Rng& rng ) const;
	void LoseLife();

	int mW = 0;
	int mH = 0;

	int mCols = 6;
	int mRows = 3;
	Flyer mFlyer[ kMaxFlyers ];

	/// Formation drift, as a whole-cell triangle wave rather than a sine.
	/// Integer so the slots the survivors sit in are exact -- a formation whose
	/// members land between cells shimmers, and at this resolution shimmer
	/// reads as damage.
	int mDrift    = 0;
	int mDriftDir = 1;
	int mDriftAmp = 2;
	int mDriftTimer = 0;

	int mShipX = 0;
	Shot mBullet;
	std::vector< Shot > mBomb;

	int mDiveTimer = 0;
	int mDiveEvery = 40;
	int mMaxDivers = 2;
	float mDiveSpeed = 0.30f;

	int mScore = 0;
	int mLives = 3;
	int mWave  = 0;
	int mTicks = 0;
	int mHitFlash = 0;
	int mAiCooldown = 0;
};

} // namespace coinop
