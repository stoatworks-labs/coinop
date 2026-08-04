#include "Sim.h"

namespace coinop
{

void Sim::SetGame( GameId id )
{
	if( mGame && id == mId )
		return;

	mId   = id;
	mGame = MakeGame( id );
	Restart();
}

void Sim::Configure( const GameConfig& cfg )
{
	// Structural changes restart; everything else is picked up live. The
	// distinction matters in performance: Speed and Skill are the two an
	// operator actually rides during a show, and a reset on every fader move
	// would make them unusable.
	const bool structural = cfg.gridW != mCfg.gridW ||
	                        cfg.gridH != mCfg.gridH ||
	                        cfg.seed != mCfg.seed;

	mCfg = cfg;
	mGrid.Resize( mCfg.gridW, mCfg.gridH );

	if( structural || !mGame )
		Restart();
}

void Sim::Restart()
{
	if( !mGame )
		mGame = MakeGame( mId );

	mRng.Reseed( mCfg.seed );
	mGrid.Resize( mCfg.gridW, mCfg.gridH );
	mGame->Reset( mCfg, mRng );

	mDeadTicks = 0;
	mAccum     = 0.0;

	// Deliberately not resetting mClock. A restart mid-run must not make the
	// next frame look like the first frame, or the accumulator would treat the
	// gap since the last frame as elapsed time and immediately owe ticks.
}

void Sim::Advance( double hostSeconds, Input& in )
{
	mLastFrameTicks = 0;

	if( !mGame )
		SetGame( mId );

	// --- Defence 2: no elapsed time, no tick. -----------------------------
	//
	// The first frame establishes the clock and steps nothing. Every frame
	// after that only owes time if the host clock actually moved forward. A
	// repeated frame -- preview, thumbnail, a second output pass -- lands here
	// with hostSeconds unchanged and falls straight through to Draw.
	//
	// A clock that went *backwards* is the host looping or an operator
	// scrubbing; treat it as a fresh start rather than as negative time.
	if( mClock < 0.0 || hostSeconds < mClock )
	{
		mClock = hostSeconds;
		mGame->Draw( mCfg, mGrid );
		return;
	}

	double elapsed = hostSeconds - mClock;
	mClock         = hostSeconds;

	if( elapsed <= 0.0 )
	{
		mGame->Draw( mCfg, mGrid );
		return;
	}

	// --- Defence 3: cap the catch-up. -------------------------------------
	if( elapsed > kMaxElapsed )
		elapsed = kMaxElapsed;

	// --- Defence 1: ticks come from the clock. ----------------------------
	const float hz = mGame->TickHz( mCfg );
	if( hz <= 0.0f )
	{
		mGame->Draw( mCfg, mGrid );
		return;
	}

	const double stepSeconds = 1.0 / double( hz );
	mAccum += elapsed;

	while( mAccum >= stepSeconds && mLastFrameTicks < kMaxTicksPerFrame )
	{
		mAccum -= stepSeconds;

		if( mGame->Finished() )
		{
			// Hold the finished playfield briefly, then start again. A game
			// that stops on game over leaves a static layer in the middle of a
			// show, which is worse than any amount of losing.
			if( ++mDeadTicks >= mCfg.respawnTicks )
			{
				// Advance the seed rather than reusing it, or every life is a
				// pixel-identical replay of the last one. The run stays
				// reproducible from the Seed parameter because this walk is
				// itself deterministic.
				mRng.Reseed( mRng.Next() );
				mGame->Reset( mCfg, mRng );
				mDeadTicks = 0;
				in.Clear();
			}
		}
		else
		{
			mGame->Step( mCfg, in, mRng );
		}

		++mTicks;
		++mLastFrameTicks;
	}

	// Drop any surplus the cap left behind. Keeping it would mean the next
	// frame starts already owing the ticks this one refused, which is the
	// spiral the cap exists to prevent.
	if( mLastFrameTicks >= kMaxTicksPerFrame )
		mAccum = 0.0;

	mGame->Draw( mCfg, mGrid );
}

} // namespace coinop
