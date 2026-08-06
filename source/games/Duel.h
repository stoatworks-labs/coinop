#pragma once

#include "Game.h"
#include "Vec.h"

/**
	Duel: two fighters, one strip of floor, best of three.

	## A fighting game is a state machine with a clock on it

	Everything that makes a one-on-one fighter what it is happens inside about a
	fifth of a second: a strike has a wind-up during which it can be beaten to
	the punch, an active window in which it connects, and a recovery during
	which its owner cannot do anything at all. Take those three phases away and
	you have two sprites touching each other, which is not a game.

	So a strike here is not an instant. It is `kStrikeTicks` long and only the
	middle of it can hit; the rest is the window in which the other fighter can
	block, back off, or land one first. Recovery is what makes throwing a strike
	a decision rather than a free action -- and it is the reason the autopilot
	can be beaten at all, because a strike thrown at nothing is a gap.

	## Blocking costs, or nobody would ever stop

	A block that is free is a block that is always on, and two fighters who are
	always blocking is a very long round. So blocking pins the fighter in place
	and still lets a little damage through. That is enough to make holding it a
	losing strategy without making it useless, which is the only property it
	needs.

	## Rounds are the reason this game can end

	Two fighters at the same skill can circle each other for a very long time,
	and the layer needs the picture to reset. Rally solves this with a target
	score and this solves it the same way: rounds are won on health, the match
	is `kRoundsToWin` rounds, and a round that goes past `kRoundTicks` is
	awarded to whoever is ahead. That last one is not a nicety -- without it, two
	autopilots at Skill 1.0 that have both decided to keep their distance never
	finish, and `Finished` never returns true.
*/
namespace coinop
{

class Duel final : public Game
{
public:
	static constexpr int kMaxHealth   = 96;
	static constexpr int kRoundsToWin = 2;
	static constexpr int kStrikeTicks = 7;
	static constexpr int kBlockTicks  = 10;
	static constexpr int kHurtTicks   = 9;
	static constexpr int kRoundTicks  = 1600;

	void Reset( const GameConfig& cfg, Rng& rng ) override;
	void Step( const GameConfig& cfg, Input& in, Rng& rng ) override;
	void Draw( const GameConfig& cfg, Grid& grid ) const override;
	float TickHz( const GameConfig& cfg ) const override;

	int Score() const override { return mScore; }
	bool Finished() const override { return mDone; }
	float Intensity() const override;

	// For coinoptest.
	int Health( int i ) const { return mFighter[ i ].health; }
	int Wins( int i ) const { return mFighter[ i ].wins; }
	int Round() const { return mRound; }
	float Separation() const;
	bool Striking( int i ) const { return mFighter[ i ].action == Action::Strike; }

private:
	enum class Action : uint8_t
	{
		Idle = 0,
		Strike,
		Block,
		Hurt
	};

	struct Fighter
	{
		float x     = 0.0f;
		int8_t face = 1;
		int health  = kMaxHealth;
		int wins    = 0;
		Action action = Action::Idle;
		int timer   = 0;
		int cool    = 0;
		int jump    = 0;
	};

	int FloorY() const { return mH - 2; }
	int FeetY( int i ) const;

	/// The cell a strike occupies while it is active. Two cells in front, so
	/// there is a range at which one fighter can hit and the other cannot.
	int StrikeCell( int i ) const;

	void StartRound();
	void ApplyIntent( int i, int move, bool strike, bool block, bool jump );
	void DecideAi( int i, const GameConfig& cfg, Rng& rng );
	void Resolve( int attacker, int defender );

	int mW = 0;
	int mH = 0;

	Fighter mFighter[ 2 ];

	int mRound      = 0;
	int mRoundTicks = 0;
	int mHold       = 0;
	int mScore      = 0;
	bool mDone      = false;

	float mStep = 0.22f;
};

} // namespace coinop
