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
#include "games/Drift.h"
#include "games/Marchers.h"
#include "games/Rally.h"
#include "games/Snake.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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
	TestAllGames();

	std::printf( "\n%s%d checks, %d failed\033[0m\n",
	             gFailed ? "\033[31m" : "\033[32m", gChecks, gFailed );

	return gFailed ? 1 : 0;
}
