#include "Coinop.h"

/**
	The generator: the playfield over its own background, no input.

	**This file is listed directly in the CoinopSource target, not in
	coinop_core.** Both plugins share the class; what they do not share is the
	`CFFGLPluginInfo` below, and putting either registration in the shared
	library would register both plugins into both bundles.

	It is also why the shared library is an OBJECT library rather than a STATIC
	one. `CFFGLPluginInfo` registers itself from a file-scope constructor and
	nothing ever references it by name, so in an archive the linker is entitled
	to drop the whole translation unit -- giving a bundle that loads, exports
	`plugMain`, and reports that it contains no plugins.

		nm -gU Coinop.bundle/Contents/MacOS/Coinop | grep plugMain
*/
namespace
{
class CoinopSource : public coinop::CoinopPlugin
{
public:
	CoinopSource() :
		CoinopPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< CoinopSource >,                    // Create method
	"CO01",                                           // Plugin unique ID of maximum length 4
	"Coinop",                                         // Plugin name
	2,                                                // API major version number
	1,                                                // API minor version number
	0,                                                // Plugin major version number
	1,                                                // Plugin minor version number
	FF_SOURCE,                                        // Plugin type
	"Fourteen playable arcade games as a generator.\n\nSnake, Bricks, Marchers, Rally, Drift, Stacker, Chase, Girders, Swarm, Trails and the rest. Driven by MIDI or OSC, or left to play themselves while you do something else.\n\nThese are real simulations rather than animations, so unlike the rest of the range they carry state. The ticks come from the clock rather than from frames: a frame rate drop makes the game render less smoothly and does not make it play slower, so it stays with the music when the show gets heavy.",// Plugin description
	"Coinop FFGL source"                              // About
);

extern "C" const char* CoinopSourceBuildStamp()
{
	return "coinop " COINOP_VERSION " source, built " __DATE__ " " __TIME__;
}
