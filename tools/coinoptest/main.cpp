/**
	coinoptest -- where the games are actually verified.

	No GL context, no window server, no FFGL. It links `coinop_sim` and drives
	the real game classes directly, which is only possible because the
	simulation has no graphics dependency (see the CMakeLists). That buys
	assertions on state rather than on pixels: not "there are 14 green cells"
	but "the snake is 14 segments long".

	The tests are organised around the traps documented in the headers, because
	those are the things that will actually break. Each one fails loudly with
	the values that disagreed.

	    build/coinoptest            run everything
	    build/coinoptest --verbose  print per-case detail
*/

#include "Raster.h"
#include "Sim.h"
#include "games/Bricks.h"
#include "games/Chase.h"
#include "games/Drift.h"
#include "games/Duel.h"
#include "games/Flapper.h"
#include "games/Girders.h"
#include "games/Marchers.h"
#include "games/Rafters.h"
#include "games/Rally.h"
#include "games/Reflex.h"
#include "games/Snake.h"
#include "games/Stacker.h"
#include "games/Swarm.h"
#include "games/Trails.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace coinop;

namespace
{

int gChecks = 0;
int gFailed = 0;
bool gVerbose = false;
const char* gSection = "";

void Section( const char* name )
{
	gSection = name;
	std::printf( "\n\033[1m== %s\033[0m\n", name );
}

void Check( bool ok, const std::string& what )
{
	++gChecks;
	if( ok )
	{
		if( gVerbose )
			std::printf( "  ok   %s\n", what.c_str() );
	}
	else
	{
		++gFailed;
		std::printf( "  \033[31mFAIL\033[0m %s  [%s]\n", what.c_str(), gSection );
	}
}

GameConfig BaseConfig()
{
	GameConfig cfg;
	cfg.gridW      = 32;
	cfg.gridH      = 24;
	cfg.seed       = 12345;
	cfg.speed      = 0.5f;
	cfg.skill      = 0.7f;
	cfg.autopilot  = true;
	cfg.difficulty = 0.5f;
	return cfg;
}

/// Run a game for n ticks under autopilot, returning the grid it ends on.
std::vector< uint8_t > RunGame( GameId id, const GameConfig& cfg, int ticks )
{
	auto game = MakeGame( id );
	Rng rng( cfg.seed );
	Input in;
	Grid grid;
	grid.Resize( cfg.gridW, cfg.gridH );

	game->Reset( cfg, rng );
	for( int i = 0; i < ticks; ++i )
	{
		if( game->Finished() )
		{
			// Mirror what Sim does on a finish, so a replay stays comparable.
			rng.Reseed( rng.Next() );
			game->Reset( cfg, rng );
		}
		game->Step( cfg, in, rng );
	}
	game->Draw( cfg, grid );

	return std::vector< uint8_t >( grid.Data(), grid.Data() + grid.ByteCount() );
}

//---------------------------------------------------------------------------
// Determinism. The Seed parameter has to mean something, and every other test
// in this file assumes a replay reproduces.
//---------------------------------------------------------------------------
void TestDeterminism()
{
	Section( "Determinism" );

	for( unsigned i = 0; i < unsigned( GameId::Count ); ++i )
	{
		const GameId id = GameId( i );
		GameConfig cfg  = BaseConfig();

		const auto a = RunGame( id, cfg, 900 );
		const auto b = RunGame( id, cfg, 900 );
		Check( a == b, std::string( GameName( id ) ) + ": same seed replays identically" );

		cfg.seed     = 999;
		const auto c = RunGame( id, cfg, 900 );
		Check( a != c, std::string( GameName( id ) ) + ": a different seed diverges" );
	}

	// The RNG's own contract, since every game leans on it.
	Rng r( 0 );
	bool nonZero = false;
	for( int i = 0; i < 8; ++i )
		nonZero = nonZero || r.Next() != 0;
	Check( nonZero, "Rng: seed 0 does not collapse to a zero stream" );

	Rng u( 7 );
	bool inRange = true;
	for( int i = 0; i < 10000; ++i )
	{
		const float v = u.Unit();
		inRange       = inRange && v >= 0.0f && v < 1.0f;
	}
	Check( inRange, "Rng: Unit stays in [0,1)" );

	Rng b2( 3 );
	bool bounded = true;
	for( int i = 0; i < 10000; ++i )
		bounded = bounded && b2.Below( 17 ) < 17;
	Check( bounded, "Rng: Below respects its bound" );
}

//---------------------------------------------------------------------------
// The FFGL timing defences. These are the reason Sim exists at all, and they
// are the ones that would be near-impossible to diagnose from a bug report.
//---------------------------------------------------------------------------
void TestSimGuards()
{
	Section( "Sim timing defences" );

	{
		// Defence 2: Resolume renders the same frame more than once. A repeated
		// frame must redraw and must not step, or the game runs at double speed
		// whenever the preview monitor happens to be open.
		Sim sim;
		Input in;
		GameConfig cfg = BaseConfig();
		sim.SetGame( GameId::Bricks );
		sim.Configure( cfg );

		sim.Advance( 10.0, in );// establishes the clock
		sim.Advance( 10.5, in );
		const uint64_t after = sim.TicksRun();

		sim.Advance( 10.5, in );
		Check( sim.TicksRun() == after, "repeated frame with no elapsed time runs no ticks" );
		Check( sim.LastFrameTicks() == 0, "repeated frame reports zero ticks" );

		sim.Advance( 10.5, in );
		sim.Advance( 10.5, in );
		Check( sim.TicksRun() == after, "three repeated frames still run no ticks" );
	}

	{
		// Defence 3: a layer that was bypassed for forty seconds must not pay
		// out forty seconds of ticks in one frame.
		Sim sim;
		Input in;
		GameConfig cfg = BaseConfig();
		sim.SetGame( GameId::Snake );
		sim.Configure( cfg );

		sim.Advance( 0.0, in );
		sim.Advance( 0.02, in );
		sim.Advance( 40.0, in );

		Check( sim.LastFrameTicks() <= Sim::kMaxTicksPerFrame,
		       "a 40 s gap is capped at kMaxTicksPerFrame" );

		// And the surplus must not be carried: the next normal frame should run
		// a normal number of ticks, not the backlog.
		sim.Advance( 40.02, in );
		Check( sim.LastFrameTicks() <= 4, "the capped surplus is dropped, not carried" );
	}

	{
		// A clock that goes backwards is a host looping or an operator
		// scrubbing. It must not produce negative elapsed time.
		Sim sim;
		Input in;
		sim.SetGame( GameId::Rally );
		sim.Configure( BaseConfig() );

		sim.Advance( 100.0, in );
		sim.Advance( 100.1, in );
		sim.Advance( 5.0, in );
		Check( sim.LastFrameTicks() == 0, "a backward clock steps nothing" );

		sim.Advance( 5.1, in );
		Check( sim.LastFrameTicks() > 0, "and the clock re-establishes afterwards" );
	}

	{
		// The first frame establishes the clock and must not step, or every
		// instance starts by paying out whatever the host's first timestamp was.
		Sim sim;
		Input in;
		sim.SetGame( GameId::Snake );
		sim.Configure( BaseConfig() );

		sim.Advance( 999.0, in );
		Check( sim.TicksRun() == 0, "the first frame establishes the clock and steps nothing" );
	}

	{
		// Speed changes must not restart the game; grid changes must.
		Sim sim;
		Input in;
		GameConfig cfg = BaseConfig();
		sim.SetGame( GameId::Snake );
		sim.Configure( cfg );

		for( int i = 0; i < 60; ++i )
			sim.Advance( double( i ) * 0.02, in );

		const uint64_t before = sim.TicksRun();
		cfg.speed             = 0.9f;
		sim.Configure( cfg );
		sim.Advance( 1.3, in );
		Check( sim.TicksRun() > before, "a speed change does not reset the accumulator" );
	}
}

//---------------------------------------------------------------------------
// Snake.
//---------------------------------------------------------------------------
void TestSnake()
{
	Section( "Snake" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Snake snake;
	snake.Reset( cfg, rng );

	Check( snake.Length() == 3, "starts three segments long" );

	// Growth. Run until the score changes and confirm the body actually grew.
	int startLen   = snake.Length();
	int startScore = snake.Score();
	int ticks      = 0;
	while( snake.Score() == startScore && ticks < 20000 && !snake.Finished() )
	{
		snake.Step( cfg, in, rng );
		++ticks;
	}

	Check( snake.Score() > startScore, "the autopilot reaches food" );
	Check( snake.Length() > startLen, "eating lengthens the snake" );

	// The head must never be on a wall cell, and the food must never be inside
	// the snake -- both are silent corruption rather than crashes.
	{
		Rng r2( 77 );
		Snake s2;
		GameConfig c2 = cfg;
		s2.Reset( c2, r2 );

		bool headInside = true;
		bool foodClear  = true;
		Grid grid;
		grid.Resize( c2.gridW, c2.gridH );

		for( int i = 0; i < 5000 && !s2.Finished(); ++i )
		{
			s2.Step( c2, in, r2 );

			const IVec h = s2.Head();
			headInside   = headInside && h.x >= 1 && h.y >= 1 &&
			             h.x <= c2.gridW - 2 && h.y <= c2.gridH - 2;

			s2.Draw( c2, grid );
			foodClear = foodClear && grid.TypeAt( s2.Food().x, s2.Food().y ) == Cell::Food;
		}

		Check( headInside, "the head never leaves the playable area" );
		Check( foodClear, "food never spawns underneath the snake" );
	}

	// The reversal bug: moving right, queue Left then Up inside one tick. The
	// naive implementation applies both, ends up going Left, and dies on its
	// own neck. Turns are applied one per tick against the last direction
	// actually travelled, so this must survive.
	{
		GameConfig manual = cfg;
		manual.autopilot  = false;
		Rng r3( 5 );
		Snake s3;
		s3.Reset( manual, r3 );

		Check( s3.Heading() == Dir::Right, "starts heading right" );

		Input queue;
		queue.Press( Button::Left );
		queue.Press( Button::Up );

		s3.Step( manual, queue, r3 );
		Check( !s3.Finished(), "a Left+Up burst while moving right does not kill the snake" );
		Check( s3.Heading() != Dir::Left, "the illegal reversal is rejected" );

		s3.Step( manual, queue, r3 );
		Check( !s3.Finished(), "and the queued Up is applied on the next tick" );
		Check( s3.Heading() == Dir::Up, "the queued turn survives in order" );
	}

	// A poor autopilot must actually lose, or the layer never resets.
	{
		GameConfig weak = cfg;
		weak.skill      = 0.0f;
		Rng r4( 11 );
		Snake s4;
		s4.Reset( weak, r4 );

		int t = 0;
		while( !s4.Finished() && t < 20000 )
		{
			s4.Step( weak, in, r4 );
			++t;
		}
		Check( s4.Finished(), "skill 0 dies rather than playing forever" );
		if( gVerbose )
			std::printf( "       (died after %d ticks)\n", t );
	}

	// And a good one must last meaningfully longer, or Skill does nothing.
	{
		auto survival = []( float skill, uint64_t seed ) {
			GameConfig c = BaseConfig();
			c.skill      = skill;
			Rng r( seed );
			Input i2;
			Snake s;
			s.Reset( c, r );
			int t = 0;
			while( !s.Finished() && t < 60000 )
			{
				s.Step( c, i2, r );
				++t;
			}
			return t;
		};

		long weakTotal = 0;
		long goodTotal = 0;
		for( uint64_t seed = 1; seed <= 6; ++seed )
		{
			weakTotal += survival( 0.05f, seed );
			goodTotal += survival( 1.0f, seed );
		}

		Check( goodTotal > weakTotal * 2,
		       "high skill survives substantially longer than low skill" );
		if( gVerbose )
			std::printf( "       (weak %ld ticks, good %ld ticks over 6 seeds)\n",
			             weakTotal, goodTotal );
	}
}

//---------------------------------------------------------------------------
// Bricks.
//---------------------------------------------------------------------------
void TestBricks()
{
	Section( "Bricks" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Bricks game;
	game.Reset( cfg, rng );

	const int startBricks = game.BricksLeft();
	Check( startBricks > 0, "the field starts populated" );

	bool inBounds     = true;
	bool verticalOk   = true;
	bool livesSane    = true;
	int prevBricks    = startBricks;
	int prevLevel     = game.Level();
	bool monotonic    = true;

	for( int i = 0; i < 200000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		const FVec b = game.Ball();

		// Trap 1: tunnelling. A ball that has passed through a wall is out of
		// bounds horizontally -- the miss below the paddle is the only legal
		// escape.
		inBounds = inBounds && b.x >= 0.0f && b.x <= float( cfg.gridW );

		// Trap 2: the horizontal lock. If the vertical component can reach zero
		// the ball rattles side to side forever and the game never ends.
		const FVec v      = game.Velocity();
		const float speed = std::sqrt( v.x * v.x + v.y * v.y );
		if( speed > 0.01f )
			verticalOk = verticalOk && std::abs( v.y ) / speed > 0.2f;

		livesSane = livesSane && game.Lives() >= 0 && game.Lives() <= 3;

		// Bricks only ever decrease. The field refills on a level clear, which
		// happens inside the same Step that destroys the last brick -- so the
		// count is never observed at zero, and the level counter is the only
		// honest way to tell a refill from a brick reappearing.
		const int now = game.BricksLeft();
		if( now > prevBricks )
			monotonic = monotonic && game.Level() > prevLevel;

		prevBricks = now;
		prevLevel  = game.Level();
	}

	Check( inBounds, "the ball never tunnels through a side wall" );
	Check( verticalOk, "the vertical component never collapses (no horizontal lock)" );
	Check( livesSane, "the life count stays in range" );
	Check( monotonic, "bricks only reappear on a level clear" );

	// Trap 4: the vertical lock. A perfectly centred return goes straight up,
	// and a perfect paddle centres perfectly -- so the better the autopilot,
	// the more exactly the loop closes. Skill 1.0 is the case that hung, so it
	// is the case that has to be asserted.
	for( float skill : { 0.0f, 0.5f, 1.0f } )
	{
		GameConfig c = BaseConfig();
		c.skill      = skill;
		Rng r( c.seed );
		Input i2;
		Bricks g;
		g.Reset( c, r );

		int t = 0;
		while( !g.Finished() && t < 200000 )
		{
			g.Step( c, i2, r );
			++t;
		}

		Check( g.Finished(),
		       "a game at skill " + std::to_string( skill ) + " ends (no vertical lock)" );
		if( gVerbose )
			std::printf( "       (skill %.2f: %d ticks, %.0f s, level %d, score %d)\n",
			             double( skill ), t, t / 90.0, g.Level(), g.Score() );
	}

	// The paddle must stay on the playfield even when driven hard from outside.
	{
		GameConfig manual = cfg;
		manual.autopilot  = false;
		Rng r( 3 );
		Bricks g2;
		g2.Reset( manual, r );

		Input axis;
		bool paddleOk = true;
		for( int i = 0; i < 2000; ++i )
		{
			axis.SetAxis( ( i % 2 ) ? -5.0f : 5.0f );// deliberately out of range
			g2.Step( manual, axis, r );
			paddleOk = paddleOk && g2.PaddleX() >= 0.0f && g2.PaddleX() <= float( manual.gridW );
		}
		Check( paddleOk, "an out-of-range axis cannot push the paddle off the field" );
	}
}

//---------------------------------------------------------------------------
// Rally. The one with no natural failure state.
//---------------------------------------------------------------------------
void TestRally()
{
	Section( "Rally" );

	for( float skill : { 0.0f, 0.5f, 1.0f } )
	{
		GameConfig cfg = BaseConfig();
		cfg.skill      = skill;
		Rng rng( cfg.seed );
		Input in;
		Rally game;
		game.Reset( cfg, rng );

		int t = 0;
		bool inBounds = true;
		while( !game.Finished() && t < 400000 )
		{
			game.Step( cfg, in, rng );
			const FVec b = game.Ball();
			inBounds     = inBounds && b.y >= -1.0f && b.y <= float( cfg.gridH ) + 1.0f;
			++t;
		}

		Check( game.Finished(),
		       "a match at skill " + std::to_string( skill ) + " reaches a result" );
		Check( inBounds, "the ball stays on the court at skill " + std::to_string( skill ) );
		if( gVerbose )
			std::printf( "       (skill %.2f: %d-%d in %d ticks)\n", double( skill ),
			             game.ScoreLeft(), game.ScoreRight(), t );
	}
}

//---------------------------------------------------------------------------
// Marchers.
//---------------------------------------------------------------------------
void TestMarchers()
{
	Section( "Marchers" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Marchers game;
	game.Reset( cfg, rng );

	const int startAlive = game.Alive();
	Check( startAlive > 0, "a wave starts populated" );

	Grid grid;
	grid.Resize( cfg.gridW, cfg.gridH );

	bool cannonOk = true;
	bool wallsIntact = true;
	int prevAlive = startAlive;
	bool monotonic = true;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		cannonOk = cannonOk && game.CannonX() >= 1 && game.CannonX() <= cfg.gridW - 2;

		const int now = game.Alive();
		if( now > prevAlive )
			monotonic = monotonic && prevAlive == 0;
		prevAlive = now;

		// The formation must never be drawn over the border. Checking the
		// rendered grid rather than the offsets catches an off-by-one in the
		// draw that the offsets alone would miss.
		if( i % 97 == 0 )
		{
			game.Draw( cfg, grid );
			for( int y = 0; y < cfg.gridH; ++y )
			{
				wallsIntact = wallsIntact &&
				              grid.TypeAt( 0, y ) == Cell::Wall &&
				              grid.TypeAt( cfg.gridW - 1, y ) == Cell::Wall;
			}
		}
	}

	Check( cannonOk, "the cannon stays on the playfield" );
	Check( monotonic, "invaders only reappear on a new wave" );
	Check( wallsIntact, "the formation never overdraws the border" );
	Check( game.Finished(), "a game of Marchers eventually ends" );
	if( gVerbose )
		std::printf( "       (reached wave %d, score %d)\n", game.Wave(), game.Score() );
}

//---------------------------------------------------------------------------
// Drift. The vector game in a grid.
//---------------------------------------------------------------------------
void TestDrift()
{
	Section( "Drift" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Drift game;
	game.Reset( cfg, rng );

	const int startRocks = game.RockCount();
	Check( startRocks >= 3, "a wave starts with rocks" );

	bool wrapped   = true;
	bool livesSane = true;
	bool sawSplit  = false;
	int prevRocks  = startRocks;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		// Wrapping is the whole coordinate system here. A ship that leaves the
		// playfield means WrapF let a negative through -- the classic fmod
		// sign bug, where the ship drifts off the left edge and never returns.
		const FVec s = game.ShipPos();
		wrapped      = wrapped && s.x >= 0.0f && s.x < float( cfg.gridW ) &&
		          s.y >= 0.0f && s.y < float( cfg.gridH );

		livesSane = livesSane && game.Lives() >= 0 && game.Lives() <= 3;

		// A large rock splitting into two mediums nets +1.
		if( game.RockCount() > prevRocks )
			sawSplit = true;
		prevRocks = game.RockCount();
	}

	Check( wrapped, "the ship always wraps back onto the playfield" );
	Check( livesSane, "the life count stays in range" );
	Check( sawSplit, "rocks split when shot" );
	Check( game.Finished(), "a game of Drift eventually ends" );

	// WrapDelta is what makes collision and targeting work across the seam.
	Check( std::abs( WrapDelta( 1.0f, 31.0f, 32.0f ) - ( -2.0f ) ) < 0.001f,
	       "WrapDelta takes the short way round the seam" );
	Check( std::abs( WrapF( -0.5f, 32.0f ) - 31.5f ) < 0.001f,
	       "WrapF brings a negative coordinate back inside" );
}

//---------------------------------------------------------------------------
// Stacker. The falling-block game, and the one whose *differences* from the
// obvious implementation are the thing worth asserting -- the clear rule, the
// board that is whatever size the Grid parameter says, and the fall ramp that
// is the only reason a good autopilot ever tops out.
//---------------------------------------------------------------------------
void TestStacker()
{
	Section( "Stacker" );

	// The run length is derived from the well, which is what lets one game work
	// at 12 cells across and at 128. A fixed number would be unclearable at one
	// end and trivial at the other.
	for( int w : { 12, 32, 96 } )
	{
		GameConfig cfg = BaseConfig();
		cfg.gridW      = w;
		cfg.gridH      = std::max( 10, w * 3 / 4 );

		Rng rng( cfg.seed );
		Stacker game;
		game.Reset( cfg, rng );

		const int run = game.RunLength();
		Check( run >= 4 && run <= 12 && run <= w - 2,
		       "Stacker: run length suits a " + std::to_string( w ) + "-wide well (" +
		           std::to_string( run ) + ")" );
	}

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Stacker game;
	game.Reset( cfg, rng );

	const int wellCells = ( cfg.gridW - 2 ) * ( cfg.gridH - 2 );

	bool heightSane = true;
	bool fillSane   = true;
	int prevFilled  = 0;
	int clears      = 0;

	for( int i = 0; i < 40000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		heightSane = heightSane && game.StackHeight() <= cfg.gridH - 2;
		fillSane   = fillSane && game.FilledCells() <= wellCells;

		// A clear removes at least a whole run. Anything less means the run
		// finder and the collapse disagree about which cells went, which is the
		// bug that silently eats a column.
		const int filled = game.FilledCells();
		if( filled < prevFilled )
		{
			++clears;
			Check( prevFilled - filled >= game.RunLength(),
			       "Stacker: a clear removes at least one whole run" );
		}
		prevFilled = filled;

		if( clears > 3 )
			break;
	}

	Check( heightSane, "Stacker: the stack never leaves the well" );
	Check( fillSane, "Stacker: the board never holds more cells than it has" );
	Check( clears > 0, "Stacker: runs actually clear under autopilot" );

	// The fall ramp. This is the load-bearing one: without it a Skill 1.0
	// autopilot reached one tick per row and cleared runs indefinitely, and the
	// game never ended at all -- measured, before `mFallRows` existed.
	{
		GameConfig hard = BaseConfig();
		hard.skill      = 1.0f;

		Rng r2( 99 );
		Input in2;
		Stacker g2;
		g2.Reset( hard, r2 );

		bool ramped = false;
		int ticks   = 0;
		for( ; ticks < 200000 && !g2.Finished(); ++ticks )
		{
			g2.Step( hard, in2, r2 );
			ramped = ramped || g2.FallRows() > 1 || g2.FallInterval() <= 1;
		}

		Check( ramped, "Stacker: the fall rate ramps past what one step a tick can steer" );
		Check( g2.Finished(), "Stacker: even a perfect autopilot eventually tops out" );
		if( gVerbose )
			std::printf( "       (topped out after %d ticks, level %d, %d rows a step)\n",
			             ticks, g2.Level(), g2.FallRows() );
	}
}

//---------------------------------------------------------------------------
// Chase. The maze is generated, so the assertions are about the maze as much
// as about the game.
//---------------------------------------------------------------------------
void TestChase()
{
	Section( "Chase" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Chase game;
	game.Reset( cfg, rng );

	Check( game.PelletsLeft() > 0, "a maze starts with pellets in it" );

	// The loop pass. A perfect maze carved on the odd lattice has exactly
	// `rooms + (rooms - 1)` open cells -- every room plus one corridor per edge
	// of a spanning tree. More than that means walls came down afterwards, and
	// walls coming down is the difference between a maze you can be chased
	// through and a maze that is all dead ends.
	{
		const int roomsX = ( cfg.gridW - 2 + 1 ) / 2;
		const int roomsY = ( cfg.gridH - 2 + 1 ) / 2;
		const int perfect = roomsX * roomsY * 2 - 1;
		Check( game.OpenCells() > perfect,
		       "the maze has loops in it, not just a spanning tree (" +
		           std::to_string( game.OpenCells() ) + " > " + std::to_string( perfect ) + ")" );
	}

	bool inBounds   = true;
	bool noReversal = true;
	bool sawFright  = false;
	int lives       = game.Lives();
	bool livesSane  = true;

	IVec prev[ Chase::kPursuers ];
	IVec prev2[ Chase::kPursuers ];
	bool wasReviving[ Chase::kPursuers ] = {};
	for( int p = 0; p < Chase::kPursuers; ++p )
		prev[ p ] = prev2[ p ] = game.Pursuer( p );

	int level = game.Level();

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		// A lost life or a finished level moves everything at once.
		const bool teleported = game.Lives() != lives || game.Level() != level;
		level                 = game.Level();

		sawFright = sawFright || game.Fright() > 0;

		const IVec pl = game.Player();
		inBounds = inBounds && game.Open( pl.x, pl.y );

		livesSane = livesSane && game.Lives() <= lives;
		lives     = game.Lives();

		for( int p = 0; p < Chase::kPursuers; ++p )
		{
			const IVec now  = game.Pursuer( p );
			const int moved = std::abs( int( now.x ) - int( prev[ p ].x ) ) +
			                  std::abs( int( now.y ) - int( prev[ p ].y ) );

			// A teleport rather than a step: eaten and sent home, a life lost,
			// or a new maze. The history restarts, or the jump reads as a
			// reversal -- and an eaten pursuer whose home is one cell away
			// jumps a distance indistinguishable from a step, which is why
			// `Reviving` is checked as well as the distance.
			if( moved > 1 || game.Reviving( p ) || wasReviving[ p ] || teleported )
			{
				prev2[ p ] = prev[ p ] = now;
				wasReviving[ p ]       = game.Reviving( p );
				continue;
			}

			wasReviving[ p ] = false;

			// Reversing is banned *unless there is nothing else*, and the rule
			// is what makes a corridor safe to commit to -- without it a
			// pursuer at a junction oscillates on the spot. So a reversal is
			// only a fault when the cell it turned around in had another way
			// out, which is exactly what a dead end does not.
			if( moved == 1 && prev[ p ] != prev2[ p ] && now == prev2[ p ] )
			{
				int exits = 0;
				for( unsigned d = 0; d < unsigned( Dir::Count ); ++d )
				{
					const IVec n = Ahead( prev[ p ], Dir( d ) );
					if( n != prev2[ p ] && game.Open( n.x, n.y ) )
						++exits;
				}

				if( exits > 0 )
					noReversal = false;
			}

			if( moved == 1 )
			{
				prev2[ p ] = prev[ p ];
				prev[ p ]  = now;
			}
		}
	}

	Check( inBounds, "the player never leaves the corridors" );
	Check( noReversal, "pursuers never reverse on the spot" );
	Check( sawFright, "a power pellet gets eaten and turns the board around" );
	Check( livesSane, "lives only ever go down" );
	Check( game.Finished(), "a game of Chase eventually ends" );

	// Skill has to matter, or Autoplay has one setting.
	auto scoreAt = []( float skill ) {
		GameConfig c = BaseConfig();
		c.skill      = skill;
		Rng r( 4242 );
		Input i2;
		Chase g;
		g.Reset( c, r );
		for( int t = 0; t < 12000 && !g.Finished(); ++t )
			g.Step( c, i2, r );
		return g.Score();
	};

	const int dim = scoreAt( 0.05f );
	const int good = scoreAt( 1.0f );
	Check( good > dim, "a skilled player eats more than a hopeless one (" +
	                       std::to_string( good ) + " > " + std::to_string( dim ) + ")" );
}

//---------------------------------------------------------------------------
// Girders. Rows are numbered from the bottom and that is where every sign
// error in the file comes from, so the geometry is asserted first.
//---------------------------------------------------------------------------
void TestGirders()
{
	Section( "Girders" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Girders game;
	game.Reset( cfg, rng );

	Check( game.Rows() >= 2, "a level has floors to climb" );

	// Row 0 is the bottom and the last row is the top. Getting this backwards
	// puts the prize on the floor the climber starts on and the game is won on
	// the first tick.
	bool ordered = true;
	for( int r = 1; r < game.Rows(); ++r )
		ordered = ordered && game.RowY( r ) < game.RowY( r - 1 );

	Check( ordered, "floors are ordered bottom-up, row 0 lowest" );
	Check( game.RowY( game.Rows() - 1 ) >= 1 &&
	           game.RowY( 0 ) <= cfg.gridH - 2,
	       "every floor is inside the playfield" );

	bool rowSane   = true;
	bool sawBarrel = false;
	bool climbed   = false;
	int lives      = game.Lives();
	bool livesSane = true;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		sawBarrel = sawBarrel || game.Barrels() > 0;
		climbed   = climbed || game.ClimberRow() > 0;
		rowSane   = rowSane && game.ClimberRow() >= 0 && game.ClimberRow() < game.Rows();

		livesSane = livesSane && game.Lives() <= lives;
		lives     = game.Lives();
	}

	Check( sawBarrel, "hazards are released and roll" );
	Check( climbed, "the climber gets off the bottom floor" );
	Check( rowSane, "the climber is always on a floor that exists" );
	Check( livesSane, "lives only ever go down" );
	Check( game.Finished(), "a game of Girders eventually ends" );
}

//---------------------------------------------------------------------------
// Swarm. The thing that makes it not Marchers is that attackers leave the
// formation and come back, so that is what is checked.
//---------------------------------------------------------------------------
void TestSwarm()
{
	Section( "Swarm" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Swarm game;
	game.Reset( cfg, rng );

	const int startAlive = game.Alive();
	Check( startAlive > 0, "a wave starts populated" );

	bool sawDive   = false;
	bool sawRejoin = false;
	bool shipOk    = true;
	bool countsOk  = true;
	int peakDiving = 0;
	int lives      = game.Lives();
	bool livesSane = true;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		const int diving = game.Diving();
		peakDiving       = std::max( peakDiving, diving );
		sawDive          = sawDive || diving > 0;

		// A diver that comes back is the whole point -- see the header. If
		// divers only ever left, this would never see the count fall while the
		// number alive stayed put.
		if( sawDive && diving == 0 && game.Alive() > 0 )
			sawRejoin = true;

		countsOk = countsOk && diving <= game.Alive();
		shipOk   = shipOk && game.ShipX() >= 1 && game.ShipX() <= cfg.gridW - 2;

		livesSane = livesSane && game.Lives() <= lives;
		lives     = game.Lives();
	}

	Check( sawDive, "attackers peel out of the formation" );
	Check( sawRejoin, "and the formation goes quiet again, so they rejoined" );
	Check( countsOk, "never more divers than attackers alive" );
	Check( shipOk, "the ship stays on the playfield" );
	Check( livesSane, "lives only ever go down" );
	Check( game.Finished(), "a game of Swarm eventually ends" );
	if( gVerbose )
		std::printf( "       (reached wave %d, peak %d diving)\n", game.Wave(), peakDiving );

	// Skill has to reach the dodge, not just the aim. Aiming at a diver that
	// has already got low lines the ship up under the thing about to land on
	// it, and before the dodge existed every Skill lost three lives inside
	// three hundred ticks.
	auto swarmScore = []( float skill ) {
		int total = 0;
		for( uint64_t seed = 1; seed <= 6; ++seed )
		{
			GameConfig c = BaseConfig();
			c.skill      = skill;
			c.seed       = seed;

			Rng r( seed );
			Input i2;
			Swarm g;
			g.Reset( c, r );
			for( int t = 0; t < 20000 && !g.Finished(); ++t )
				g.Step( c, i2, r );

			total += g.Score();
		}
		return total;
	};

	const int poor = swarmScore( 0.1f );
	const int able = swarmScore( 1.0f );
	Check( able > poor, "a skilled gunner outscores a hopeless one (" +
	                        std::to_string( able ) + " > " + std::to_string( poor ) + ")" );
}

//---------------------------------------------------------------------------
// Trails. The assertion that matters is the one about simultaneity, because
// the bug it guards against is invisible in a single round.
//---------------------------------------------------------------------------
void TestTrails()
{
	Section( "Trails" );

	GameConfig cfg = BaseConfig();
	cfg.difficulty = 1.0f;// four riders

	Rng rng( cfg.seed );
	Input in;
	Trails game;
	game.Reset( cfg, rng );

	Check( game.Riders() == Trails::kMaxRiders, "Difficulty at full puts four riders out" );
	Check( game.AliveCount() == game.Riders(), "everybody starts the round alive" );

	bool trailGrows = true;
	bool boundsOk   = true;
	int prevTrail   = game.TrailCells();
	int prevRound   = game.Round();
	int prevAlive   = game.AliveCount();

	for( int i = 0; i < 40000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		for( int r = 0; r < game.Riders(); ++r )
		{
			if( !game.Alive( r ) )
				continue;

			const IVec p = game.Position( r );
			boundsOk = boundsOk && p.x >= 1 && p.y >= 1 && p.x <= cfg.gridW - 2 &&
			           p.y <= cfg.gridH - 2;
		}

		// Trails are permanent within a round. The only time the count may fall
		// is the wipe, which happens when the between-rounds hold expires --
		// several ticks *after* the round number changed, so the round number
		// alone does not identify it. One or fewer riders standing does.
		const int trail = game.TrailCells();
		if( trail < prevTrail && prevAlive > 1 )
			trailGrows = false;

		prevTrail = trail;
		prevAlive = game.AliveCount();
		prevRound = game.Round();
	}

	Check( boundsOk, "riders never leave the arena" );
	Check( trailGrows, "a trail is never unwritten inside a round" );
	Check( game.Finished(), "a match of Trails eventually ends" );

	// The head-on. Resolving deaths inside the movement loop hands rider 0
	// every head-on collision in the game, because it arrives first and the
	// cell is solid by the time rider 1 is asked. It is invisible in one round
	// and obvious over forty matches, which is why this is a sweep.
	int winsByZero  = 0;
	int winsByOther = 0;
	for( uint64_t seed = 1; seed <= 40; ++seed )
	{
		GameConfig c = BaseConfig();
		c.difficulty = 0.0f;// two riders, so a head-on is between 0 and 1
		c.seed       = seed;
		c.skill      = 0.5f;

		Rng r( seed );
		Input i2;
		Trails g;
		g.Reset( c, r );

		for( int t = 0; t < 20000 && !g.Finished(); ++t )
			g.Step( c, i2, r );

		winsByZero += g.Wins( 0 );
		winsByOther += g.Wins( 1 );
	}

	Check( winsByZero > 0 && winsByOther > 0,
	       "neither rider has a structural advantage (" + std::to_string( winsByZero ) +
	           " vs " + std::to_string( winsByOther ) + ")" );
}

//---------------------------------------------------------------------------
// Reflex. One claim in the header carries the whole game: it terminates even
// at Skill 1.0, because the window closes faster than anything can react.
//---------------------------------------------------------------------------
void TestReflex()
{
	Section( "Reflex" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Reflex game;
	game.Reset( cfg, rng );

	const int startLead = game.Lead();
	Check( startLead > 2, "the window starts open" );

	bool sawPrompt = false;
	bool leadSane  = true;
	int minLead    = startLead;
	int lives      = game.Lives();
	bool livesSane = true;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		sawPrompt = sawPrompt || game.PromptOpen();
		minLead   = std::min( minLead, game.Lead() );
		leadSane  = leadSane && game.Lead() >= 2 && game.Lead() <= startLead;

		livesSane = livesSane && game.Lives() <= lives;
		lives     = game.Lives();
	}

	Check( sawPrompt, "prompts open" );
	Check( leadSane, "the window never inverts or opens wider than it started" );
	Check( livesSane, "lives only ever go down" );
	Check( game.Finished(), "a game of Reflex eventually ends" );

	// The one that matters. A reaction test with a fixed window is a game the
	// machine simply wins, so the window has to close faster than the two-tick
	// floor on its reaction -- otherwise the layer shows one immortal run for
	// the whole show.
	{
		GameConfig perfect = BaseConfig();
		perfect.skill      = 1.0f;

		Rng r2( 31337 );
		Input in2;
		Reflex g2;
		g2.Reset( perfect, r2 );

		int ticks = 0;
		for( ; ticks < 200000 && !g2.Finished(); ++ticks )
			g2.Step( perfect, in2, r2 );

		Check( g2.Finished(), "even a perfect reaction eventually runs out of window" );
		if( gVerbose )
			std::printf( "       (perfect play ended after %d ticks, score %d, lead %d)\n",
			             ticks, g2.Score(), g2.Lead() );
	}
}

//---------------------------------------------------------------------------
// Rafters. The mechanic is the bump, so the test is that the bump happens and
// that what it hits ends up on its back.
//---------------------------------------------------------------------------
void TestRafters()
{
	Section( "Rafters" );

	GameConfig cfg = BaseConfig();
	cfg.skill      = 0.9f;

	Rng rng( cfg.seed );
	Input in;
	Rafters game;
	game.Reset( cfg, rng );

	bool sawCrawler = false;
	bool sawFlip    = false;
	bool standing   = true;
	bool boundsOk   = true;
	int lives       = game.Lives();
	bool livesSane  = true;

	for( int i = 0; i < 80000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		sawCrawler = sawCrawler || game.Crawlers() > 0;
		sawFlip    = sawFlip || game.Flipped() > 0;

		const IVec p = game.Player();
		boundsOk = boundsOk && p.x >= 1 && p.x <= cfg.gridW - 2 && p.y >= 1 &&
		           p.y <= cfg.gridH - 2;

		// One cell tall, one cell thick, and the clamp on vertical speed is
		// what keeps that true. An actor that falls more than a cell a tick
		// steps straight through a platform without testing it, and the
		// symptom is a player standing inside solid rock.
		standing = standing && !game.Solid( p.x, p.y );
	}

	Check( sawCrawler, "crawlers are released" );
	Check( sawFlip, "hitting the floor from below flips what is standing on it" );
	Check( boundsOk, "the player stays on the playfield" );
	Check( standing, "the player never ends a tick inside a platform" );
	Check( livesSane, "lives only ever go down" );
	Check( game.Finished(), "a game of Rafters eventually ends" );
	if( gVerbose )
		std::printf( "       (reached wave %d, score %d)\n", game.Wave(), game.Score() );

	// AGENTS.md's requirement, and the one this autopilot failed twice on the
	// way to getting right: high Skill has to survive substantially longer. It
	// did not while the pogo was in -- the player spent 87% of the run in the
	// air and died faster than the random walker.
	auto survived = []( float skill ) {
		int total = 0;
		for( uint64_t seed = 1; seed <= 6; ++seed )
		{
			GameConfig c = BaseConfig();
			c.skill      = skill;
			c.seed       = seed;

			Rng r( seed );
			Input i2;
			Rafters g;
			g.Reset( c, r );

			int t = 0;
			for( ; t < 40000 && !g.Finished(); ++t )
				g.Step( c, i2, r );

			total += t;
		}
		return total;
	};

	const int clumsy = survived( 0.1f );
	const int adept  = survived( 1.0f );
	Check( adept > clumsy, "a skilled player lasts longer than a clumsy one (" +
	                           std::to_string( adept ) + " > " + std::to_string( clumsy ) + ")" );
}

//---------------------------------------------------------------------------
// Duel. Two autopilots at the same skill can stand off forever, so the round
// timer is the assertion.
//---------------------------------------------------------------------------
void TestDuel()
{
	Section( "Duel" );

	GameConfig cfg = BaseConfig();
	Rng rng( cfg.seed );
	Input in;
	Duel game;
	game.Reset( cfg, rng );

	Check( game.Health( 0 ) == Duel::kMaxHealth && game.Health( 1 ) == Duel::kMaxHealth,
	       "both fighters start on full health" );

	bool gapOk    = true;
	bool healthOk = true;
	bool bothHurt = false;
	int seenHit0  = 0;
	int seenHit1  = 0;

	for( int i = 0; i < 60000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		// Fighters are one cell wide and must never occupy the same cell, or a
		// strike has nowhere to land and they pass through each other.
		gapOk = gapOk && game.Separation() >= 1.0f;

		healthOk = healthOk && game.Health( 0 ) <= Duel::kMaxHealth &&
		           game.Health( 1 ) <= Duel::kMaxHealth;

		if( game.Health( 0 ) < Duel::kMaxHealth )
			++seenHit0;
		if( game.Health( 1 ) < Duel::kMaxHealth )
			++seenHit1;
	}

	bothHurt = seenHit0 > 0 && seenHit1 > 0;

	Check( gapOk, "the fighters never share a cell" );
	Check( healthOk, "health never rises above the maximum" );
	Check( bothHurt, "both fighters land hits over a match" );
	Check( game.Finished(), "a match of Duel eventually ends" );
	Check( game.Wins( 0 ) >= Duel::kRoundsToWin || game.Wins( 1 ) >= Duel::kRoundsToWin ||
	           game.Round() >= Duel::kRoundsToWin * 3,
	       "the match ended on rounds won or on the round limit" );

	// Two evenly matched autopilots that both decide to keep their distance are
	// the case with no natural end. Without the round timer `Finished` never
	// returns true and the layer shows one match for the rest of the show.
	{
		GameConfig even = BaseConfig();
		even.skill      = 1.0f;
		even.difficulty = 0.0f;// slowest approach, longest stand-off

		Rng r2( 77 );
		Input in2;
		Duel g2;
		g2.Reset( even, r2 );

		int ticks = 0;
		for( ; ticks < 200000 && !g2.Finished(); ++ticks )
			g2.Step( even, in2, r2 );

		Check( g2.Finished(), "a stand-off between two perfect fighters still ends" );
		if( gVerbose )
			std::printf( "       (stand-off resolved after %d ticks, %d rounds)\n",
			             ticks, g2.Round() );
	}
}

//---------------------------------------------------------------------------
// Flapper. Two things need asserting that no generic check catches: that the
// flier gets through columns at all, and that it still eventually dies.
//---------------------------------------------------------------------------
void TestFlapper()
{
	Section( "Flapper" );

	GameConfig cfg = BaseConfig();
	cfg.skill      = 1.0f;

	Rng rng( cfg.seed );
	Input in;
	Flapper game;
	game.Reset( cfg, rng );

	bool sawColumn = false;
	bool boundsOk  = true;
	bool clearOk   = true;
	bool rampOk    = true;
	bool livesOk   = true;
	int lives      = game.Lives();
	int startGap   = game.GapHeight();

	for( int i = 0; i < 80000 && !game.Finished(); ++i )
	{
		game.Step( cfg, in, rng );

		sawColumn = sawColumn || game.Columns() > 0;

		// The tick a life is lost is the crash frame: the flier is inside
		// whatever killed it, on purpose, because that is where the player is
		// shown to have hit. Asserting anything about its position on that tick
		// asserts that crashing does not happen.
		const bool crashed = game.Lives() < lives;
		livesOk            = livesOk && game.Lives() <= lives;
		lives              = game.Lives();

		if( !crashed )
		{
			const int fy = game.FlierY();
			boundsOk     = boundsOk && fy >= 1 && fy <= cfg.gridH - 2;

			// The swept collision test's whole job. A flier standing inside a
			// column on a tick it survived means a column crossed it untested --
			// which is what happens if kMaxScroll is ever raised past a cell a
			// tick and the sweep is dropped for a plain overlap.
			clearOk = clearOk && !game.Solid( game.FlierX(), fy );
		}

		// The ramp only ever tightens, and never past its floors.
		rampOk = rampOk && game.GapHeight() >= Flapper::kMinGap &&
		         game.Spacing() >= Flapper::kMinSpacing &&
		         game.Scroll() <= Flapper::kMaxScroll + 1e-4f;
	}

	Check( sawColumn, "columns arrive" );
	Check( boundsOk, "the flier stays between the ceiling and the floor" );
	Check( clearOk, "the flier never ends a tick inside a column" );
	Check( rampOk, "the ramp never passes its own floors" );
	Check( livesOk, "lives only ever go down" );
	Check( game.Finished(), "a game of Flapper eventually ends" );

	// The check this game exists to fail. A flier that never passes a column
	// still satisfies every assertion above -- it dies three times, in bounds,
	// having touched nothing -- and that is precisely what the first autopilot
	// did on every seed, because it projected free fall forward and settled a
	// braking distance above the hole it was aiming at. Scoring is the only
	// thing that distinguishes playing the game from surviving next to it.
	Check( game.Score() > 0, "a skilled flier gets through columns (" +
	                             std::to_string( game.Score() ) + ")" );

	// The scroll must stay under a cell a tick, or a column can step from one
	// side of the flier to the other without the flier's own column ever being
	// the one tested. The sweep in Blocks covers it; this keeps the two
	// defences from silently becoming one.
	Check( Flapper::kMaxScroll < 1.0f, "a column never moves a whole cell in a tick" );

	// The termination argument from the header, done as arithmetic rather than
	// trusted as prose. At the floor of the ramp there are this many ticks
	// between columns, and the flier cannot cross the playfield in them, so a
	// hole that has drifted the width of the playfield away is unreachable.
	{
		const float ticksBetween = float( Flapper::kMinSpacing ) / Flapper::kMaxScroll;
		const float reach        = ticksBetween * Flapper::kMaxFall;// generous
		Check( reach < float( cfg.gridH - 4 ),
		       "at the ramp floor a hole can drift further than the flier can travel" );
	}

	// The ramp has to actually engage, or termination rests on nothing.
	Check( game.GapHeight() < startGap, "the gap narrows as columns are passed" );

	// Skill has to be visible, in both directions. Survival alone is not enough
	// here: a flier that refuses to move survives a while and plays nothing.
	auto play = []( float skill ) {
		int ticks = 0;
		int score = 0;
		for( uint64_t seed = 1; seed <= 8; ++seed )
		{
			GameConfig c = BaseConfig();
			c.skill      = skill;
			c.seed       = seed;

			Rng r( seed );
			Input i2;
			Flapper g;
			g.Reset( c, r );

			int t = 0;
			for( ; t < 40000 && !g.Finished(); ++t )
				g.Step( c, i2, r );

			ticks += t;
			score += g.Score();
		}
		return std::pair< int, int >{ ticks, score };
	};

	const auto clumsy = play( 0.1f );
	const auto adept  = play( 1.0f );

	Check( adept.first > clumsy.first, "a skilled flier lasts longer (" +
	                                       std::to_string( adept.first ) + " > " +
	                                       std::to_string( clumsy.first ) + ")" );
	Check( adept.second > clumsy.second, "a skilled flier scores more (" +
	                                         std::to_string( adept.second ) + " > " +
	                                         std::to_string( clumsy.second ) + ")" );

	// Both ends of Difficulty terminate. Difficulty sets where the ramp starts,
	// and an easy start must not mean an endless run.
	{
		bool allEnded = true;
		int longest   = 0;
		for( float diff : { 0.0f, 1.0f } )
			for( uint64_t seed = 1; seed <= 4; ++seed )
			{
				GameConfig c = BaseConfig();
				c.skill      = 1.0f;
				c.difficulty = diff;
				c.seed       = seed;

				Rng r( seed );
				Input i2;
				Flapper g;
				g.Reset( c, r );

				int t = 0;
				for( ; t < 200000 && !g.Finished(); ++t )
					g.Step( c, i2, r );

				allEnded = allEnded && g.Finished();
				longest  = std::max( longest, t );
			}

		Check( allEnded, "a perfect flier ends at both ends of Difficulty" );
		if( gVerbose )
			std::printf( "       (longest perfect run %d ticks)\n", longest );
	}
}

//---------------------------------------------------------------------------
// Things every game has to be true of, whatever it is.
//---------------------------------------------------------------------------
void TestAllGames()
{
	Section( "Every game" );

	for( unsigned i = 0; i < unsigned( GameId::Count ); ++i )
	{
		const GameId id = GameId( i );
		const std::string name = GameName( id );

		// Cell types must all be inside the enum. The shader switches on this
		// byte; a value it does not know renders as nothing, and the object
		// simply disappears from the playfield.
		{
			GameConfig cfg = BaseConfig();
			auto game      = MakeGame( id );
			Rng rng( cfg.seed );
			Input in;
			Grid grid;
			grid.Resize( cfg.gridW, cfg.gridH );

			game->Reset( cfg, rng );
			bool typesOk = true;

			for( int t = 0; t < 6000; ++t )
			{
				if( game->Finished() )
					game->Reset( cfg, rng );
				game->Step( cfg, in, rng );

				if( t % 53 == 0 )
				{
					game->Draw( cfg, grid );
					for( int y = 0; y < cfg.gridH; ++y )
						for( int x = 0; x < cfg.gridW; ++x )
							typesOk = typesOk && grid.Get( x, y ).type < uint8_t( Cell::Count );
				}
			}
			Check( typesOk, name + ": never writes a cell type the shader does not know" );
		}

		// Draw must be a pure projection. Resolume renders the same frame more
		// than once; if Draw mutated state, a repeated render would advance the
		// game and the double-render guard in Sim would be pointless.
		{
			GameConfig cfg = BaseConfig();
			auto game      = MakeGame( id );
			Rng rng( cfg.seed );
			Input in;
			Grid a, b;
			a.Resize( cfg.gridW, cfg.gridH );
			b.Resize( cfg.gridW, cfg.gridH );

			game->Reset( cfg, rng );
			for( int t = 0; t < 500; ++t )
				game->Step( cfg, in, rng );

			game->Draw( cfg, a );
			game->Draw( cfg, a );// twice
			game->Draw( cfg, b );

			const bool same = std::memcmp( a.Data(), b.Data(), a.ByteCount() ) == 0;
			Check( same, name + ": Draw is pure -- redrawing does not advance the game" );
		}

		// Extreme grid sizes must not crash or produce an empty playfield. A
		// user will drag the Grid slider to both ends within ten seconds of
		// loading the plugin.
		{
			for( int dim : { 8, 12, 96 } )
			{
				GameConfig cfg = BaseConfig();
				cfg.gridW      = dim;
				cfg.gridH      = std::max( 8, dim * 3 / 4 );

				auto game = MakeGame( id );
				Rng rng( cfg.seed );
				Input in;
				Grid grid;
				grid.Resize( cfg.gridW, cfg.gridH );

				game->Reset( cfg, rng );
				for( int t = 0; t < 1500; ++t )
				{
					if( game->Finished() )
						game->Reset( cfg, rng );
					game->Step( cfg, in, rng );
				}
				game->Draw( cfg, grid );

				int nonEmpty = 0;
				for( int y = 0; y < cfg.gridH; ++y )
					for( int x = 0; x < cfg.gridW; ++x )
						if( grid.TypeAt( x, y ) != Cell::Empty )
							++nonEmpty;

				Check( nonEmpty > 0,
				       name + ": draws something at " + std::to_string( cfg.gridW ) + "x" +
				           std::to_string( cfg.gridH ) );
			}
		}

		// Tick rate must be positive and sane at both ends of the Speed slider,
		// or Sim's accumulator either stalls or spirals.
		{
			auto game      = MakeGame( id );
			GameConfig slow = BaseConfig();
			slow.speed      = 0.0f;
			GameConfig fast = BaseConfig();
			fast.speed      = 1.0f;

			const float lo = game->TickHz( slow );
			const float hi = game->TickHz( fast );
			Check( lo > 0.5f && hi > 0.5f && lo <= 240.0f && hi <= 240.0f,
			       name + ": tick rate is sane across the Speed range" );
		}

		// And the whole thing has to survive being driven through Sim with a
		// realistic, jittery frame clock rather than a perfect one.
		{
			Sim sim;
			Input in;
			GameConfig cfg = BaseConfig();
			cfg.skill      = 0.4f;
			sim.SetGame( id );
			sim.Configure( cfg );

			Rng jitter( 42 );
			double clock = 0.0;
			for( int f = 0; f < 4000; ++f )
			{
				clock += 0.016 + double( jitter.Range( -0.004f, 0.02f ) );
				sim.Advance( clock, in );
			}

			Check( sim.TicksRun() > 0, name + ": runs under a jittery frame clock" );
			Check( sim.Playfield().Width() == cfg.gridW, name + ": playfield keeps its size" );
		}
	}
}

//---------------------------------------------------------------------------
// --grid: the playfield, as numbers a second implementation can be held to.
//
// Everything above asserts on behaviour, which is the right shape for catching
// a broken game but the wrong shape for catching a broken *port*. The browser
// demo re-implements every one of these games in JavaScript, and nothing was
// able to say whether that re-implementation agreed with this one -- a port
// that draws a plausible game is the exact failure the rest of this harness
// exists to prevent.
//
// So: run each game under a fixed, fully specified configuration and print the
// resulting grid as a digest plus a per-type census. `demo/tools/check_sim.mjs`
// reproduces this from the ported JavaScript and diffs it.
//
// The configuration is coinopgl's, deliberately -- same grid, same seed, same
// skill, same 400 frames of 20 ms -- so the two harnesses describe the same
// instant of the same game and can be read against each other.
//
// FNV-1a over the raw cell bytes. The census is printed as well because a bare
// hash that differs tells you nothing about HOW, and the first question on a
// mismatch is always "is it one cell or is it a different game".
//---------------------------------------------------------------------------
/// The whole playfield as type digits, one row per line, for eyeballing a
/// mismatch that the digest has already found. Row 0 is the top, as stored.
bool gGridMap = false;

void DumpGrids()
{
	std::printf( "grid  32x24  seed 7  skill 0.70  autopilot  400 frames x 20 ms\n" );

	for( unsigned i = 0; i < unsigned( GameId::Count ); ++i )
	{
		const GameId id = GameId( i );

		Sim sim;
		Input in;
		GameConfig cfg;
		cfg.gridW     = 32;
		cfg.gridH     = 24;
		cfg.seed      = 7;
		cfg.skill     = 0.7f;
		cfg.autopilot = true;

		sim.SetGame( id );
		sim.Configure( cfg );

		double clock = 0.0;
		for( int f = 0; f < 400; ++f )
		{
			clock += 0.02;
			sim.Advance( clock, in );
		}

		const Grid& grid = sim.Playfield();

		// FNV-1a, 64-bit, over exactly the bytes that reach the texture.
		uint64_t hash        = 14695981039346656037ULL;
		const uint8_t* bytes = grid.Data();
		for( size_t b = 0; b < grid.ByteCount(); ++b )
		{
			hash ^= bytes[ b ];
			hash *= 1099511628211ULL;
		}

		int census[ int( Cell::Count ) ] = {};
		for( int y = 0; y < grid.Height(); ++y )
			for( int x = 0; x < grid.Width(); ++x )
			{
				const int t = int( grid.TypeAt( x, y ) );
				if( t >= 0 && t < int( Cell::Count ) )
					++census[ t ];
			}

		// Per-plane sums as well as the digest. A digest that differs says
		// nothing about WHERE, and the four bytes fail for very different
		// reasons: `type` is the game's logic, `shade` and `tint` are its
		// presentation, and `flash` is written by the plugin rather than the sim
		// and should be zero throughout a harness run. Localising the plane is
		// the difference between "the port plays a different game" and "the port
		// shades the snake's tail differently".
		unsigned long long planeShade = 0, planeTint = 0, planeFlash = 0;
		for( size_t b = 0; b < grid.ByteCount(); b += 4 )
		{
			planeShade += bytes[ b + 1 ];
			planeTint += bytes[ b + 2 ];
			planeFlash += bytes[ b + 3 ];
		}

		std::printf( "%-10s hash %016llx  ticks %llu  planes %llu %llu %llu  cells",
		             GameName( id ), ( unsigned long long )hash,
		             ( unsigned long long )sim.TicksRun(),
		             planeShade, planeTint, planeFlash );
		for( int t = 0; t < int( Cell::Count ); ++t )
			std::printf( " %d", census[ t ] );
		std::printf( "\n" );

		if( gGridMap )
			for( int y = 0; y < grid.Height(); ++y )
			{
				std::printf( "  %-8s %02d ", GameName( id ), y );
				for( int x = 0; x < grid.Width(); ++x )
					std::printf( "%d", int( grid.TypeAt( x, y ) ) );
				std::printf( "  shade " );
				for( int x = 0; x < grid.Width(); ++x )
					std::printf( "%02x", grid.Get( x, y ).shade );
				std::printf( "  tint " );
				for( int x = 0; x < grid.Width(); ++x )
					std::printf( "%02x", grid.Get( x, y ).tint );
				std::printf( "\n" );
			}
	}
}

} // namespace

int main( int argc, char** argv )
{
	bool dumpGrid = false;
	for( int i = 1; i < argc; ++i )
	{
		if( std::strcmp( argv[ i ], "--verbose" ) == 0 )
			gVerbose = true;
		if( std::strcmp( argv[ i ], "--grid" ) == 0 )
			dumpGrid = true;
		if( std::strcmp( argv[ i ], "--grid-map" ) == 0 )
		{
			dumpGrid = true;
			gGridMap = true;
		}
	}

	if( dumpGrid )
	{
		DumpGrids();
		return 0;
	}

	std::printf( "coinoptest -- simulation harness, no GL context\n" );

	TestDeterminism();
	TestSimGuards();
	TestSnake();
	TestBricks();
	TestRally();
	TestMarchers();
	TestDrift();
	TestStacker();
	TestChase();
	TestGirders();
	TestSwarm();
	TestTrails();
	TestReflex();
	TestRafters();
	TestDuel();
	TestFlapper();
	TestAllGames();

	std::printf( "\n%s%d checks, %d failed\033[0m\n",
	             gFailed ? "\033[31m" : "\033[32m", gChecks, gFailed );

	return gFailed ? 1 : 0;
}
