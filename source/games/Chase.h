#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Chase: eat the maze, do not get caught, turn the tables on a power pellet.

	## The maze is generated, and that is the interesting constraint

	The obvious way to do a dot-eater is to type the maze in as a string
	literal. It is also the way that stops working the moment the Grid parameter
	moves, and the Grid parameter is the first thing anyone touches -- this
	plugin's whole reason to exist is that the playfield matches a pixel map or
	an LED wall, which is whatever size the wall is. A fixed maze would mean one
	supported grid size and a letterbox everywhere else.

	So the maze is carved at Reset, on the odd lattice, with an iterative
	backtracker. Deterministic from the seed, correct at 12 cells across and at
	128.

	**A perfect maze is the wrong maze.** A backtracker produces exactly one
	path between any two cells, which means every wrong turn is a dead end and
	the game becomes "walk into a cul-de-sac, die". So once it is carved, walls
	with open cells on both sides are knocked through at a fixed rate. Loops are
	what make a chase a chase: somewhere to run *to*.

	## Pursuers differ, and none of them is random

	Four pursuers with one behaviour between them converge into a single blob
	one cell apart, which is both trivially avoidable and dull to watch. Each
	has its own target instead:

	- **Hunter** goes for the player's cell.
	- **Ambusher** goes for four cells ahead of the player's heading, so it
	  arrives where the player is going rather than where they are. It is the
	  one that closes off the escape.
	- **Patroller** goes for a fixed corner while it is far away and for the
	  player once it is close, so it drifts in and out of the fight.
	- **Wanderer** picks its target from a slowly changing point on the board.
	  Not random per step -- a genuinely random walk looks broken, because it
	  reverses on the spot.

	Reversing is banned for all of them, which is what makes a corridor safe to
	commit to and is the single rule doing most of the work here.

	## The player's autopilot, and why it is two flood fills

	One BFS from the player, carrying the *first step* taken, gives the move
	toward the nearest pellet in one pass -- no path to reconstruct. A second
	BFS, seeded from every pursuer at once, gives distance-to-danger for the
	whole board.

	Skill is whether the second one is consulted. At 1.0 the player refuses a
	step that walks inside two cells of a pursuer whenever an alternative
	exists; at 0.0 it beelines for the nearest pellet and gets eaten, which is
	the point -- see AGENTS.md on why the autopilot has to be beatable.

	During a power pellet the danger map inverts and pursuers become targets.
	That is not decoration either: it is the only part of the run where the
	picture moves *toward* the player instead of away, and a layer that never
	does it looks like it only has one idea.
*/
namespace coinop
{

class Chase final : public Game
{
public:
	static constexpr int kPursuers = 4;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mLives <= 0; }
	float Intensity() const override;

	// For coinoptest.
	int PelletsLeft() const { return mPelletsLeft; }
	int Lives() const { return mLives; }
	int Level() const { return mLevel; }
	IVec Player() const { return mPos; }
	IVec Pursuer( int i ) const { return mGhost[ i ].pos; }

	/// An eaten pursuer, sitting out its spell at home. Exposed because a
	/// pursuer that is sent home has not *moved* there, and a test that reads
	/// positions cannot otherwise tell the difference between a teleport and a
	/// step -- which made the reversal check report a fault every time one got
	/// eaten a cell away from its own home.
	bool Reviving( int i ) const { return mGhost[ i ].revive > 0; }
	int Fright() const { return mFright; }
	bool Open( int x, int y ) const;
	int OpenCells() const;

private:
	struct Ghost
	{
		IVec pos   = {};
		IVec home  = {};
		Dir dir    = Dir::Left;
		int accum  = 0;
		int revive = 0;///< Ticks until an eaten pursuer is back in play.
		uint8_t kind = 0;
	};

	size_t Index( int x, int y ) const { return size_t( y ) * size_t( mW ) + size_t( x ); }

	void BuildMaze( Rng& rng );
	void PlaceActors();
	void FloodFromPlayer();
	void FloodFromGhosts();
	IVec GhostTarget( const Ghost& g, Rng& rng ) const;
	Dir StepGhost( const Ghost& g, IVec target, bool flee ) const;
	Dir ChooseAutopilot( const GameConfig& cfg, Rng& rng ) const;
	void MovePlayer( Dir d );
	void LoseLife();

	int mW = 0;
	int mH = 0;

	std::vector< uint8_t > mMaze;  ///< 1 wall, 0 open.
	std::vector< uint8_t > mPellet;///< 0 none, 1 pellet, 2 power pellet.
	int mPelletsLeft = 0;

	IVec mPos      = {};
	IVec mHome     = {};
	Dir mDir       = Dir::Left;
	std::vector< Dir > mTurns;

	Ghost mGhost[ kPursuers ];
	int mGhostSpeed = 4;///< Moves per six ticks.

	/// Ticks of scatter left. During it the pursuers head for their corners
	/// instead of for the player.
	///
	/// It is not a difficulty setting, it is what makes the game have a shape.
	/// Four pursuers that hunt continuously from the first tick converge on the
	/// player within a few seconds and every life ends the same way -- measured
	/// at Skill 0.7, three lives gone in 185 ticks, which is about nine seconds
	/// of layer. Scatter gives the player the room to get anywhere at all, and
	/// it comes back periodically so a long run breathes rather than being one
	/// unbroken chase.
	int mScatter    = 0;

	int mFright     = 0;///< Ticks of power-pellet time left.
	int mChain      = 0;///< Pursuers eaten on this power pellet, for scoring.
	IVec mWander    = {};

	int mScore  = 0;
	int mLives  = 3;
	int mLevel  = 0;
	int mTicks  = 0;
	int mSinceP = 0;///< Ticks since the last pellet, so a stuck run still ends.

	/// Flood-fill scratch, kept as members so the autopilot allocates nothing
	/// per tick. `mFirst` is the direction the player would leave in to reach
	/// each cell, which is what removes the path reconstruction.
	std::vector< int32_t > mDist;
	std::vector< int8_t > mFirst;
	std::vector< int32_t > mDanger;
	std::vector< int32_t > mQueue;
};

} // namespace coinop
