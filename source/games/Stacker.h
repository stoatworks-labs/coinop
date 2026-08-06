#pragma once

#include "Game.h"
#include "Vec.h"

#include <vector>

/**
	Stacker: pieces fall, you land them, full runs clear.

	## Read this before changing anything about how it looks

	AGENTS.md used to say a stacker was the one game to leave alone, and it was
	right about the risk. *Tetris Holding v. Xio Interactive* (2012) is the case
	that matters: the court accepted that the **rules** of a falling-block puzzle
	are an unprotectable idea, and then found infringement anyway on the
	**expression** -- and it listed what it meant. The playfield's dimensions.
	The seven specific pieces. Their colours. The ghost piece showing where the
	current one will land. The next-piece preview. Garbage lines. The board
	filling up at the end.

	So the mechanic is here and none of that expression is:

	- **The piece set is not the seven tetrominoes.** Ten polyominoes across
	  three sizes -- two trominoes, four tetrominoes, four pentominoes. It
	  deliberately omits pieces from the classic set and includes sizes it never
	  had.
	- **The playfield is whatever the Grid parameter says**, from 12 cells wide
	  to 128, which is not a 10x20 well and could not be made into one.
	- **Colour comes from the palette**, by role, exactly as it does for every
	  other game in this plugin. There is no per-piece-shape colour scheme.
	- **No ghost piece and no next-piece preview.** Both were named in the
	  judgment and neither is here.
	- **The clear rule is different**, and not cosmetically -- see below.

	That last one is also the reason this plays at all on the hardware this
	plugin exists for. A row of a 32-wide playfield is thirty cells; filling one
	completely takes seven or eight perfectly placed pieces, and a VJ layer that
	clears something once a minute is a layer that looks broken. So:

	**Any horizontal run of `RunLength` or more contiguous filled cells clears.**

	The run length comes from the well width, so the game works the same at 12
	cells across and at 128. Clearing a run removes exactly those cells and lets
	what was above them fall by however many were taken out of that column --
	holes elsewhere in the column survive, which is what keeps "do not bury a
	gap" the thing the game is actually about.

	## The autopilot must lose, and here it is the fall rate that kills it

	Same requirement as every other game in this plugin: a layer showing a
	stacker that never tops out stops changing, which is worse than a layer
	showing one that dies.

	Skill does the usual work -- below 1.0 it takes a randomly chosen legal
	placement instead of the best one it found. But the guarantee of termination
	is structural and does not depend on Skill at all: the fall interval shortens
	as runs are cleared, and the autopilot can only rotate or move one step per
	tick. Once the piece falls a row per tick, it can no longer steer to the
	column it chose, and a perfect evaluator placing pieces at random is dead
	within a minute.

	## Everything is integer

	No part of this game is continuous, so no part of it is float -- including
	the placement search, which sums heights and counts holes. It matters because
	the demo re-implements this in JavaScript and `check_sim.mjs` compares the
	playfield byte for byte; integer arithmetic has nothing to disagree about.
*/
namespace coinop
{

class Stacker final : public Game
{
public:
	/// Cells in the largest piece. The offset arrays are fixed-size at this.
	static constexpr int kMaxPieceCells = 5;
	static constexpr int kPieceKinds    = 10;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mDead; }
	float Intensity() const override;

	// For coinoptest, which asserts on state and not on cells.
	int RunsCleared() const { return mRuns; }
	int RunLength() const { return mRunNeeded; }
	int StackHeight() const;
	int FilledCells() const;
	int FallInterval() const { return mFallInterval; }
	int FallRows() const { return mFallRows; }
	int Level() const { return mLevel; }
	bool PieceActive() const { return mActive; }

private:
	/// One rotation of one piece, normalised so the lowest x and y are 0.
	struct Shape
	{
		int count = 0;
		IVec cell[ kMaxPieceCells ];
		int w = 0;
		int h = 0;
	};

	/// A candidate placement, as the autopilot sees it.
	struct Placement
	{
		int rot   = 0;
		int x     = 0;
		int score = 0;
		bool ok   = false;
	};

	static Shape ShapeOf( int kind, int rot );

	int WellL() const { return 1; }
	int WellR() const { return mW - 2; }
	int WellT() const { return 1; }
	int WellB() const { return mH - 2; }
	int WellW() const { return WellR() - WellL() + 1; }
	int WellH() const { return WellB() - WellT() + 1; }

	size_t Index( int x, int y ) const { return size_t( y ) * size_t( mW ) + size_t( x ); }

	bool Occupied( const std::vector< uint8_t >& board, int x, int y ) const;
	bool Fits( const std::vector< uint8_t >& board, const Shape& s, int x, int y ) const;
	void Lock();
	void SpawnPiece( const GameConfig& cfg, Rng& rng );
	bool TryRotate( int delta );

	/// Every maximal run of `mRunNeeded` or more, written into `mask`. Returns
	/// the number of cells marked and, through `runs`, how many separate runs
	/// they came from -- the score wants the cells and the level wants the runs.
	int FindRuns( const std::vector< uint8_t >& board, std::vector< uint8_t >& mask,
	              int& runs ) const;

	/// Remove everything `mask` marks and let each affected column fall by the
	/// number of its own cells that went. Holes elsewhere in the column survive,
	/// which is the whole tension of the game.
	void Collapse( std::vector< uint8_t >& board, std::vector< uint8_t >& mask ) const;

	/// Drop `s` down column `x` in `board` and return the resting y, or -1 if it
	/// does not fit at all. Used by the autopilot and by the hard drop.
	int RestingY( const std::vector< uint8_t >& board, const Shape& s, int x ) const;

	int EvaluateBoard( const std::vector< uint8_t >& board, int cleared, int landingY ) const;
	Placement ChoosePlacement( const GameConfig& cfg, Rng& rng ) const;

	int mW = 0;
	int mH = 0;

	std::vector< uint8_t > mBoard;  ///< 0 empty, else tint+1.
	std::vector< uint8_t > mClear;  ///< 1 where a run is mid-flash.
	mutable std::vector< uint8_t > mScratch;    ///< Autopilot's trial board.
	mutable std::vector< uint8_t > mScratchMask;///< and its trial run mask.

	bool mActive = false;
	int mKind    = 0;
	int mRot     = 0;
	int mX       = 0;
	int mY       = 0;

	int mFallTimer    = 0;
	int mFallInterval = 12;
	int mBaseInterval = 12;

	/// Rows the piece drops per fall step. One, until the interval has already
	/// been squeezed down to a single tick and the game still needs somewhere
	/// to go -- see `Lock`. This is what actually ends a good run: an autopilot
	/// that can move one column per tick cannot steer a piece falling four.
	int mFallRows = 1;
	int mRunNeeded    = 6;
	int mClearTimer   = 0;

	int mScore = 0;
	int mRuns  = 0;
	int mLevel = 0;
	bool mDead = false;

	/// Where the autopilot decided this piece is going, planned once on spawn
	/// and then walked toward one step a tick. Teleporting the piece to its
	/// column would be easier and would look like a bug.
	int mAiRot     = 0;
	int mAiX       = 0;
	bool mAiPlanned = false;
};

} // namespace coinop
