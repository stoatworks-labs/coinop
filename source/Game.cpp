#include "Game.h"

#include "games/Bricks.h"
#include "games/Drift.h"
#include "games/Marchers.h"
#include "games/Rally.h"
#include "games/Snake.h"

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
		case GameId::Snake:
		default: return std::make_unique< Snake >();
	}
}

} // namespace coinop
