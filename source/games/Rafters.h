#pragma once

#include "Game.h"
#include "Vec.h"

#include <cmath>
#include <vector>

/**
	Rafters: platforms, crawlers, and a floor you hit from underneath.

	## The mechanic worth having

	Jumping on an enemy is the platform game everyone has written. The arcade
	game this descends from did something better and stranger: you hit the
	*floor* the enemy is standing on, from below, and the enemy flips onto its
	back. It is then harmless and worth points until it rights itself, at which
	point it is faster than it was.

	That single rule is why the game is here. It makes the interesting position
	the one directly underneath the thing that can kill you, and it puts a clock
	on every kill -- you have to reach a flipped crawler before it gets up, and
	the route there is usually back up a platform you just jumped down from.

	## One cell tall, and the physics is four lines

	The player and the crawlers are a single cell each. A two-cell character
	needs a head test and a feet test, an answer for straddling a ledge, and an
	answer for a one-cell gap that the feet fit through and the head does not --
	on a playfield that may be twelve cells across.

	One cell removes all of it and costs nothing that reads at this resolution.
	Vertical motion is a velocity and a gravity, clamped so that no actor can
	move more than one cell in a tick; horizontal motion is a fraction of a cell
	against a solid test. Collision is then just "is the cell I am moving into
	solid", which is a lookup.

	The clamp is not optional. Let an actor fall 1.4 cells in a tick and it
	passes through a one-cell-thick platform without ever testing the cell it
	went through -- and every platform here is one cell thick.

	## Why the bump has a radius and a duration

	A bump that flipped only the crawler in the player's exact column would be
	unhittable: the player is moving, the crawler is moving, and the tick they
	share a column is not a tick anybody can aim for. So the bump reaches a cell
	either side and stays live for a few ticks, and both numbers are there to
	make the move land rather than to make it generous.

	It is also the only feedback the player gets that the bump happened at all,
	which is why the struck cell lights up for exactly as long as it is live.
*/
namespace coinop
{

class Rafters final : public Game
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
	int Wave() const { return mWave; }
	int Crawlers() const { return int( mCrawler.size() ); }
	int Flipped() const;
	IVec Player() const;
	bool OnGround() const { return mPlayer.onGround; }
	bool Solid( int x, int y ) const;

private:
	/// Anything that stands on a platform and falls off the end of one.
	struct Actor
	{
		float x       = 0.0f;
		float y       = 0.0f;
		float vy      = 0.0f;
		int8_t dir    = 1;
		bool onGround = false;
	};

	struct Crawler
	{
		Actor body;
		int flip  = 0;///< Ticks left on its back. 0 means upright and lethal.
		int speed = 0;///< Rises each time it rights itself.
	};

	/// The clamp that keeps a one-cell platform solid. See the header.
	static constexpr float kMaxFall = 0.9f;

	size_t Index( int x, int y ) const { return size_t( y ) * size_t( mW ) + size_t( x ); }

	static int Round( float v ) { return int( std::floor( v + 0.5f ) ); }

	void BuildLevel( const GameConfig& cfg, Rng& rng );
	void SpawnCrawler( Rng& rng );
	void MoveActor( Actor& a, float speed, bool turnAtEdges, bool isPlayer );
	void LoseLife();

	struct Intent
	{
		int move  = 0;
		bool jump = false;
	};

	/// Not const: the climb is a commitment, and `mAimRow` is where it is
	/// remembered between the jump and the landing.
	Intent ChooseAutopilot( const GameConfig& cfg, Rng& rng );

	int mW = 0;
	int mH = 0;

	std::vector< uint8_t > mSolid;
	std::vector< int16_t > mPlatformY;///< Platform rows, top first.

	Actor mPlayer;
	std::vector< Crawler > mCrawler;

	/// The platform row the autopilot jumped toward, or -1 when it is standing
	/// on something. See ChooseAutopilot for what goes wrong without it.
	int mAimRow = -1;

	int mBumpX     = -1;
	int mBumpY     = -1;
	int mBumpTimer = 0;

	int mSpawnTimer = 0;
	int mSpawnEvery = 90;
	int mWaveTotal  = 3;///< Crawlers this wave still has to release.
	int mReleased   = 0;
	float mCrawlSpeed = 0.16f;

	int mScore = 0;
	int mLives = 3;
	int mWave  = 0;
	int mTicks = 0;
	int mHurt  = 0;
	int mStuck = 0;
};

} // namespace coinop
