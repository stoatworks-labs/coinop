#include "Coinop.h"

/**
	The effect: the same games, over the incoming clip.

	Exists for two reasons, and the second is the interesting one.

	The dull reason is the same one Downpour Over has: getting a generator over
	footage in Resolume otherwise costs a layer and puts the controls somewhere
	other than the clip they belong to.

	The real reason is **Bricks From Clip**. As an effect, the brick field can
	be drawn from the incoming video rather than from the palette -- so the
	bricks *are* the footage, and knocking one out punches a hole through to the
	layer below. That is a genuinely different thing to do with a clip rather
	than a game with a picture behind it, and it is why this plugin is worth a
	second bundle.

	The compositing order is shared with the source: the clip is at the bottom,
	the plugin's own background veils it, and the playfield draws on top. So
	Background Opacity keeps exactly the meaning it has in the source plugin
	rather than becoming a second, differently-behaved control on this side.

	See SourcePlugin.cpp for why this file is listed in its own target rather
	than in the shared library.
*/
namespace
{
class CoinopEffect : public coinop::CoinopPlugin
{
public:
	CoinopEffect() :
		CoinopPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< CoinopEffect >,                    // Create method
	"CO02",                                           // Plugin unique ID of maximum length 4
	"Coinop Over",                                    // Plugin name
	2,                                                // API major version number
	1,                                                // API minor version number
	0,                                                // Plugin major version number
	1,                                                // Plugin minor version number
	FF_EFFECT,                                        // Plugin type
	"Arcade games over the clip",                     // Plugin description
	"Coinop FFGL effect"                              // About
);

extern "C" const char* CoinopEffectBuildStamp()
{
	return "coinop " COINOP_VERSION " effect, built " __DATE__ " " __TIME__;
}
