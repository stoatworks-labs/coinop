#pragma once

#include <cstdint>

namespace coinop
{

/// Integer playfield coordinate. Sixteen bits because a grid is never larger
/// than a couple of hundred cells a side and Snake keeps one of these per body
/// segment -- at 4 bytes a segment the whole snake fits in a cache line or two.
struct IVec
{
	int16_t x = 0;
	int16_t y = 0;

	friend bool operator==( IVec a, IVec b ) { return a.x == b.x && a.y == b.y; }
	friend bool operator!=( IVec a, IVec b ) { return !( a == b ); }
};

/// Continuous playfield coordinate, for the games with a ball or a ship.
struct FVec
{
	float x = 0.0f;
	float y = 0.0f;
};

enum class Dir : uint8_t
{
	Up = 0,
	Right,
	Down,
	Left,
	Count
};

inline IVec Ahead( IVec p, Dir d )
{
	switch( d )
	{
		case Dir::Up: return { p.x, int16_t( p.y - 1 ) };
		case Dir::Right: return { int16_t( p.x + 1 ), p.y };
		case Dir::Down: return { p.x, int16_t( p.y + 1 ) };
		default: return { int16_t( p.x - 1 ), p.y };
	}
}

inline Dir TurnLeft( Dir d ) { return Dir( ( unsigned( d ) + 3 ) % 4 ); }
inline Dir TurnRight( Dir d ) { return Dir( ( unsigned( d ) + 1 ) % 4 ); }
inline Dir Opposite( Dir d ) { return Dir( ( unsigned( d ) + 2 ) % 4 ); }

} // namespace coinop
