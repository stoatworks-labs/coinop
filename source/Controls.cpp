#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace coinop
{

const char* FitName( FitMode m )
{
	switch( m )
	{
		case FitMode::Fill: return "Fill";
		case FitMode::Stretch: return "Stretch";
		case FitMode::Fit:
		default: return "Fit";
	}
}

const char* AspectName( Aspect a )
{
	switch( a )
	{
		case Aspect::Classic: return "4:3";
		case Aspect::Square: return "1:1";
		case Aspect::Ultra: return "32:9";
		case Aspect::Tall: return "9:16";
		case Aspect::Wide:
		default: return "16:9";
	}
}

const char* PaletteName( Palette p )
{
	switch( p )
	{
		case Palette::Amber: return "Amber";
		case Palette::Ice: return "Ice";
		case Palette::Candy: return "Candy";
		case Palette::Mono: return "Mono";
		case Palette::Fire: return "Fire";
		case Palette::Phosphor:
		default: return "Phosphor";
	}
}

int GridWidth( float t )
{
	const float clamped = std::clamp( t, 0.0f, 1.0f );

	// 12 to 128, squared. A pixel map is usually at the coarse end and that is
	// where the slider needs its resolution.
	return int( std::lround( 12.0f + 116.0f * clamped * clamped ) );
}

int GridHeight( int width, Aspect a )
{
	float ratio = 9.0f / 16.0f;
	switch( a )
	{
		case Aspect::Classic: ratio = 3.0f / 4.0f; break;
		case Aspect::Square: ratio = 1.0f; break;
		case Aspect::Ultra: ratio = 9.0f / 32.0f; break;
		case Aspect::Tall: ratio = 16.0f / 9.0f; break;
		default: break;
	}

	// Floor of 8 rows. Below that Marchers has nowhere to put a formation and
	// Snake has no room to turn, and both would spend their whole life dying.
	return std::clamp( int( std::lround( float( width ) * ratio ) ), 8, 256 );
}

uint64_t SeedValue( float t )
{
	const float clamped = std::clamp( t, 0.0f, 1.0f );
	const uint64_t step = uint64_t( std::lround( clamped * 4095.0f ) );

	// Spread the 4096 steps across the 64-bit space rather than using them raw.
	// Adjacent seeds should give unrelated games; 1 and 2 fed straight into
	// xorshift give streams that are visibly related for the first few draws,
	// which shows up as two layers on neighbouring seeds putting the first
	// apple in nearly the same place.
	uint64_t z = step * 0x9E3779B97F4A7C15ull + 0xDA3E39CB94B95BDBull;
	z          = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
	z          = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
	return z ^ ( z >> 31 );
}

GameConfig ConfigFromParams( const float* params )
{
	GameConfig cfg;

	const Aspect aspect = Aspect( std::clamp( int( std::lround( params[ PT_ASPECT ] ) ),
	                                          0, int( Aspect::Count ) - 1 ) );

	cfg.gridW      = GridWidth( params[ PT_GRID ] );
	cfg.gridH      = GridHeight( cfg.gridW, aspect );
	cfg.seed       = SeedValue( params[ PT_SEED ] );
	cfg.speed      = std::clamp( params[ PT_SPEED ], 0.0f, 1.0f );
	cfg.skill      = std::clamp( params[ PT_SKILL ], 0.0f, 1.0f );
	cfg.difficulty = std::clamp( params[ PT_DIFFICULTY ], 0.0f, 1.0f );
	cfg.autopilot  = params[ PT_AUTOPILOT ] > 0.5f;

	return cfg;
}

} // namespace coinop
