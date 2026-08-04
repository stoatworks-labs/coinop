#pragma once

#include "Grid.h"
#include "Input.h"
#include "Rng.h"

#include <memory>

/**
	What every game in here has to be, and why they are all one plugin.

	## One plugin, a Game dropdown

	Five games could have been ten bundles -- a source and an effect each. They
	are not, for the reason `idler` next door keeps eleven screensavers in one
	plugin: a Resolume user does not want ten entries in the effect list that
	differ only in which sprite moves, and a sixth game should not cost a sixth
	bundle, a sixth plugin ID, a sixth entry in every release note.

	The cost is that the parameter list is the union of what the games need, so
	it is kept deliberately generic -- Speed, Skill, Seed, and a handful of
	controls that mean whatever the current game says they mean. A game that
	wanted six bespoke sliders would not fit here, which is a fair filter on
	what belongs in this plugin at all.

	## Step and Draw are separate, and that is what makes it testable

	`Step` advances state and touches no cells. `Draw` writes cells and mutates
	nothing. So `coinoptest` can run three hundred ticks and assert on the
	snake's actual length, rather than counting green pixels and hoping. It also
	means the host rendering the same frame twice -- which Resolume does, and
	which [tinsel](../../tinsel/source/Tinsel.cpp) documents -- costs a redraw
	and never a second step.

	## Every game owns its own tick rate

	Snake wants four to twenty steps a second and looks wrong at sixty. A ball
	wants sixty or more or it visibly stutters. So `TickHz` is per game and the
	`Sim` accumulator honours it, rather than forcing one rate on both and
	making Snake responsible for skipping fifty ticks out of sixty.
*/
namespace coinop
{

enum class GameId : unsigned int
{
	Snake = 0,
	Bricks,   ///< Ball, paddle, brick field.
	Marchers, ///< A formation that steps down the screen and shoots back.
	Rally,    ///< Two paddles, one ball.
	Drift,    ///< Rotating ship, drifting rocks. Vector shapes, rasterised.
	Count
};

const char* GameName( GameId id );

/**
	Everything a game is told, once per tick. Assembled by the plugin from its
	parameters so that no game ever reaches for a host parameter itself -- which
	is what lets the harness drive a game with no FFGL in the process at all.
*/
struct GameConfig
{
	int gridW      = 32;
	int gridH      = 24;
	uint64_t seed  = 1;
	float speed    = 0.5f;///< 0..1, mapped to a tick rate by each game.
	float skill    = 0.6f;///< 0..1 autopilot competence. 1.0 never loses.
	bool autopilot = true;
	float difficulty = 0.5f;///< 0..1. Ball speed, invader rate, rock count.

	/// Ticks to hold a finished game on screen before it restarts. A dead
	/// playfield that stays dead is a black layer in the middle of a show.
	int respawnTicks = 45;
};

class Game
{
public:
	virtual ~Game() = default;

	/// Start over. Called on construction, on a Reset press, on a config change
	/// that alters the grid, and after the respawn delay on game over.
	virtual void Reset( const GameConfig& cfg, Rng& rng ) = 0;

	/// Advance exactly one tick. Must not touch the grid.
	virtual void Step( const GameConfig& cfg, Input& in, Rng& rng ) = 0;

	/// Project current state onto cells. Must not mutate game state -- it is
	/// called once per rendered frame, which is not once per tick.
	virtual void Draw( const GameConfig& cfg, Grid& grid ) const = 0;

	/// Ticks per second this game wants at `cfg.speed`.
	virtual float TickHz( const GameConfig& cfg ) const = 0;

	virtual int Score() const  = 0;
	virtual bool Finished() const = 0;

	/// 0..1, rises as the game gets more intense. The plugin exposes it so a
	/// palette or a glow can follow the action without the shader knowing what
	/// game it is looking at.
	virtual float Intensity() const { return 0.0f; }
};

std::unique_ptr< Game > MakeGame( GameId id );

} // namespace coinop
