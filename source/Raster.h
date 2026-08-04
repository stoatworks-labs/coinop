#pragma once

#include "Grid.h"

#include <cmath>

/**
	Drawing vector shapes into a cell grid.

	## Why Drift is a grid game at all

	Asteroids is the one game in here that was never a grid: a rotating ship, a
	rock tumbling at 23 degrees, everything drifting at sub-cell speeds. The
	obvious move is to give it a second renderer -- real GL lines, antialiased,
	resolution-independent -- and the obvious move is wrong twice.

	It is wrong architecturally, because a second renderer means a second set of
	aspect-ratio bugs, a second blend path, a second thing for the effect
	variant to composite, and a shader that has to know which game is running.

	It is wrong for the actual job, because this plugin's stated use is driving
	pixel maps and LED walls. A one-pixel antialiased vector line sampled by
	Resolume's pixel mapper onto a 30 mm pitch wall is *nothing* -- it lands
	between fixtures and disappears. A chunky rasterised line is the only
	version of Asteroids that survives the trip to the lights, and it happens to
	look right doing it.

	So the ship stays a rotating polygon in float space, and gets Bresenham'd
	into cells at draw time. State is continuous; only the picture is discrete.
*/
namespace coinop
{

/// Integer Bresenham. Used rather than a float DDA because the grid is small
/// and a DDA's rounding lets a near-horizontal line drop a cell here and there
/// -- on a 32-wide playfield a one-cell gap in a rock's outline is a hole you
/// can see the background through.
inline void DrawLine( Grid& grid, int x0, int y0, int x1, int y1,
                      Cell type, uint8_t shade = 255, uint8_t tint = 0 )
{
	const int dx  = std::abs( x1 - x0 );
	const int dy  = -std::abs( y1 - y0 );
	const int sx  = x0 < x1 ? 1 : -1;
	const int sy  = y0 < y1 ? 1 : -1;
	int err       = dx + dy;

	// A degenerate line is one cell, not an infinite loop. Two rocks that have
	// drifted onto the same point produce exactly this.
	for( int guard = 0; guard < 4096; ++guard )
	{
		grid.Set( x0, y0, type, shade, tint );

		if( x0 == x1 && y0 == y1 )
			return;

		const int e2 = 2 * err;
		if( e2 >= dy )
		{
			err += dy;
			x0 += sx;
		}
		if( e2 <= dx )
		{
			err += dx;
			y0 += sy;
		}
	}
}

/**
	A closed polygon in playfield coordinates, wrapped at the edges.

	Wrapping is done by drawing the shape up to four times at offset origins
	rather than by clipping each segment: a rock straddling the left edge has to
	appear on the right, and a rock in a corner has to appear in all four. Four
	cheap redraws into a 3 KB grid is less code than a correct wrapping clipper,
	and `Grid::Set` already drops what lands out of bounds.
*/
inline void DrawPolyWrapped( Grid& grid, const float* xs, const float* ys, int n,
                             Cell type, uint8_t shade = 255, uint8_t tint = 0 )
{
	if( n < 2 )
		return;

	const int w = grid.Width();
	const int h = grid.Height();

	for( int oy = -1; oy <= 1; ++oy )
	{
		for( int ox = -1; ox <= 1; ++ox )
		{
			for( int i = 0; i < n; ++i )
			{
				const int j = ( i + 1 ) % n;

				const int x0 = int( std::floor( xs[ i ] ) ) + ox * w;
				const int y0 = int( std::floor( ys[ i ] ) ) + oy * h;
				const int x1 = int( std::floor( xs[ j ] ) ) + ox * w;
				const int y1 = int( std::floor( ys[ j ] ) ) + oy * h;

				// Skip the seven offsets that cannot touch the grid, so a
				// screen full of rocks is not nine times the work.
				const int loX = x0 < x1 ? x0 : x1;
				const int hiX = x0 < x1 ? x1 : x0;
				const int loY = y0 < y1 ? y0 : y1;
				const int hiY = y0 < y1 ? y1 : y0;
				if( hiX < 0 || loX >= w || hiY < 0 || loY >= h )
					continue;

				DrawLine( grid, x0, y0, x1, y1, type, shade, tint );
			}
		}
	}
}

/// Wrap a scalar into [0, extent). `fmod` alone keeps the sign, so a ship that
/// drifts off the left edge at -0.5 comes back as -0.5 and never reappears.
inline float WrapF( float v, float extent )
{
	if( extent <= 0.0f )
		return 0.0f;

	v = std::fmod( v, extent );
	return v < 0.0f ? v + extent : v;
}

inline int WrapI( int v, int extent )
{
	if( extent <= 0 )
		return 0;

	v %= extent;
	return v < 0 ? v + extent : v;
}

/// Shortest signed distance from a to b on a wrapped axis. Needed by the
/// autopilots: the nearest rock to a ship at x=1 may be at x=31, and a plain
/// subtraction says it is thirty cells away and points the guns the wrong way.
inline float WrapDelta( float a, float b, float extent )
{
	float d = b - a;
	while( d > extent * 0.5f )
		d -= extent;
	while( d < -extent * 0.5f )
		d += extent;
	return d;
}

} // namespace coinop
