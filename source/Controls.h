#pragma once

#include "Game.h"

/**
	Host parameters, and what they mean.

	## Every numeric parameter is a plain 0..1 float

	Even where it stands for a grid width, a seed or a tick rate.
	`CFFGLPluginManager::SetParamInfo` clamps an `FF_TYPE_STANDARD` default into
	0..1 *before* returning, and `SetParamRange` can only be called afterwards
	because it finds the parameter by id -- so a parameter declared in cells
	cannot declare a default in cells. There is no `SetParamDefault`. A default
	of 32 becomes 1, silently, and the plugin starts up wrong in a way no build
	step notices.

	So the range lives here, in the conversion, and the host only ever sees
	0..1. This is the same arrangement as the rest of the fleet and it exists
	for the same reason.

	## The grid never depends on the render resolution

	The obvious way to keep cells square is to derive the grid height from the
	output's aspect ratio. It is a trap, and a nasty one.

	Resolume renders a plugin instance at more than one size: the output, the
	preview monitor, a clip thumbnail. Derive the grid from the render target
	and those are three different grids -- and since a grid change is
	structural, `Sim::Configure` restarts the game each time it changes. The
	symptom is a game that resets several times a second, but only while the
	preview is open, and only on some layouts.

	So grid dimensions come from parameters and nothing else. Aspect is its own
	option, the shader letterboxes the playfield into whatever it is given, and
	an instance plays the same game whatever is looking at it.
*/
namespace coinop
{

/// Parameter ids. Declaration order in Coinop.cpp is the order the host shows
/// them, and `SetParamGroup` collapses *runs* of same-group ids -- so
/// reordering these silently splits a group in two.
enum ParamId : unsigned int
{
	// Game
	PT_GAME = 0,
	PT_SPEED,
	PT_DIFFICULTY,
	PT_SKILL,
	PT_AUTOPILOT,
	PT_SEED,
	PT_RESET,

	// Playfield
	PT_GRID,
	PT_ASPECT,
	PT_FIT,

	// Controls. All MIDI/OSC-mappable in the host, which is the only input
	// this plugin can ever have.
	PT_AXIS,
	PT_LEFT,
	PT_RIGHT,
	PT_UP,
	PT_DOWN,
	PT_FIRE,

	// Look
	PT_PALETTE,
	PT_CELL_ROUND,
	PT_CELL_GAP,
	PT_GLOW,
	PT_SCANLINE,
	PT_REACTIVE,

	// Background
	PT_BACK_R,
	PT_BACK_G,
	PT_BACK_B,
	PT_BACK_OPACITY,

	// Output. Declared by both plugins so a composition can be moved between
	// them without the parameter list shifting underneath it; the source plugin
	// has nothing to mix against and ignores both.
	PT_CLIP_BRICKS,
	PT_MIX,

	// -- The Stoatworks About block ------------------------------------------
	//
	// One display-only text line, then one button per link the block carries:
	// the guide, the project page, the source, the funding page. A button opens
	// a browser and stores nothing.
	//
	// How many buttons there are is decided by which URLs StoatworksAbout.h
	// actually holds, so Coinop.cpp static_asserts this run against
	// `about::kParamCount` -- the user guide added the fourth button, and without
	// the assert that would silently shift PT_COUNT and leave the last button
	// undeclared.
	//
	// Last in the enum so no saved composition's parameter ids shift.
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_ABOUT_BUTTON_4,
	PT_COUNT
};

enum class FitMode : int
{
	Fit = 0,///< Letterbox. Cells stay square, edges show background.
	Fill,   ///< Crop. Cells stay square, playfield overflows.
	Stretch,///< Cells distort to fill exactly.
	Count
};

enum class Aspect : int
{
	Wide = 0,///< 16:9
	Classic, ///< 4:3
	Square,  ///< 1:1
	Ultra,   ///< 32:9
	Tall,    ///< 9:16, for a portrait wall
	Count
};

enum class Palette : int
{
	Phosphor = 0,///< Green CRT.
	Amber,
	Ice,
	Candy,
	Mono,
	Fire,
	Count
};

const char* FitName( FitMode m );
const char* AspectName( Aspect a );
const char* PaletteName( Palette p );

/// Grid width in cells, from the 0..1 parameter. Curved because the useful
/// half of the range is the coarse half -- 16 and 24 cells look completely
/// different, 110 and 120 do not.
int GridWidth( float t );

/// Grid height, from the width and the chosen aspect. Never from the render
/// target -- see the header comment.
int GridHeight( int width, Aspect a );

/// Seed as an integer. The parameter is a float the host stores at whatever
/// precision it likes, so the conversion is deliberately coarse: 4096 distinct
/// seeds, each reachable and each stable across a save and reload. A finer
/// mapping would make two loads of the same composition play different games.
uint64_t SeedValue( float t );

/// Fills everything the sim needs from the raw parameter array.
GameConfig ConfigFromParams( const float* params );

} // namespace coinop
