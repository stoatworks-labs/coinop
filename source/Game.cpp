#include "Game.h"

#include "games/Bricks.h"
#include "games/Chase.h"
#include "games/Drift.h"
#include "games/Duel.h"
#include "games/Girders.h"
#include "games/Marchers.h"
#include "games/Rafters.h"
#include "games/Rally.h"
#include "games/Reflex.h"
#include "games/Snake.h"
#include "games/Stacker.h"
#include "games/Swarm.h"
#include "games/Trails.h"

namespace coinop
{

const char* GameName( GameId id )
{
	switch( id )
	{
		case GameId::Snake: return "Snake";
		case GameId::Bricks: return "Bricks";
		case GameId::Marchers: return "Marchers";
		case GameId::Rally: return "Rally";
		case GameId::Drift: return "Drift";
		case GameId::Stacker: return "Stacker";
		case GameId::Chase: return "Chase";
		case GameId::Girders: return "Girders";
		case GameId::Swarm: return "Swarm";
		case GameId::Trails: return "Trails";
		case GameId::Reflex: return "Reflex";
		case GameId::Rafters: return "Rafters";
		case GameId::Duel: return "Duel";
		default: return "Snake";
	}
}

std::unique_ptr< Game > MakeGame( GameId id )
{
	switch( id )
	{
		case GameId::Bricks: return std::make_unique< Bricks >();
		case GameId::Marchers: return std::make_unique< Marchers >();
		case GameId::Rally: return std::make_unique< Rally >();
		case GameId::Drift: return std::make_unique< Drift >();
		case GameId::Stacker: return std::make_unique< Stacker >();
		case GameId::Chase: return std::make_unique< Chase >();
		case GameId::Girders: return std::make_unique< Girders >();
		case GameId::Swarm: return std::make_unique< Swarm >();
		case GameId::Trails: return std::make_unique< Trails >();
		case GameId::Reflex: return std::make_unique< Reflex >();
		case GameId::Rafters: return std::make_unique< Rafters >();
		case GameId::Duel: return std::make_unique< Duel >();
		case GameId::Snake:
		default: return std::make_unique< Snake >();
	}
}

} // namespace coinop
