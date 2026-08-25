#include "Coinop.h"

#include "Diag.h"
#include "Shaders.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <string>

namespace coinop
{

/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Wall clock, to calibrate the host's against. Steady rather than system, so
/// nothing here moves if the machine's clock is corrected.
double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

// The buttons are declared one per link, so the run in the enum and the run the
// block actually has must agree. They diverge the day somebody writes a user
// guide, and this is what says so.
static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h -- "
               "add or remove a PT_ABOUT_BUTTON_n to match" );

CoinopPlugin::CoinopPlugin( bool over ) :
	overInput( over )
{
	SetMinInputs( over ? 1 : 0 );
	SetMaxInputs( over ? 1 : 0 );

	// The host drives the clock where it can, so that a repeated frame renders
	// the same picture twice rather than advancing the game. Sim leans on this
	// heavily -- see the double-render defence in Sim.cpp.
	SetTimeSupported( true );

	//-----------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter, so
	// these assignments are what the host is told the defaults are -- and they
	// have to be in 0..1, because SetParamInfo clamps an FF_TYPE_STANDARD
	// default before returning and there is no SetParamDefault to fix it
	// afterwards.
	//-----------------------------------------------------------------------
	params[ PT_GAME ]       = 0.0f;
	params[ PT_SPEED ]      = 0.45f;
	params[ PT_DIFFICULTY ] = 0.5f;
	params[ PT_SKILL ]      = 0.65f;
	params[ PT_AUTOPILOT ]  = 1.0f;
	params[ PT_SEED ]       = 0.25f;
	params[ PT_RESET ]      = 0.0f;

	// 32 cells across at the default, which is the size everything in here was
	// designed and measured at.
	params[ PT_GRID ]   = std::sqrt( ( 32.0f - 12.0f ) / 116.0f );
	params[ PT_ASPECT ] = 0.0f;
	params[ PT_FIT ]    = 0.0f;

	params[ PT_AXIS ]  = 0.5f;
	params[ PT_LEFT ]  = 0.0f;
	params[ PT_RIGHT ] = 0.0f;
	params[ PT_UP ]    = 0.0f;
	params[ PT_DOWN ]  = 0.0f;
	params[ PT_FIRE ]  = 0.0f;

	params[ PT_PALETTE ]    = 0.0f;
	params[ PT_CELL_ROUND ] = 0.25f;
	params[ PT_CELL_GAP ]   = 0.18f;
	params[ PT_GLOW ]       = 0.45f;
	params[ PT_SCANLINE ]   = 0.0f;
	params[ PT_REACTIVE ]   = 0.35f;

	params[ PT_BACK_R ]       = 0.02f;
	params[ PT_BACK_G ]       = 0.02f;
	params[ PT_BACK_B ]       = 0.03f;
	params[ PT_BACK_OPACITY ] = overInput ? 0.55f : 1.0f;

	params[ PT_CLIP_BRICKS ] = overInput ? 1.0f : 0.0f;
	params[ PT_MIX ]         = 1.0f;

	//-----------------------------------------------------------------------
	// Parameters, in the order the host will show them. Groups collapse runs of
	// consecutive same-group ids, so the declaration order and the enum order
	// have to agree.
	//-----------------------------------------------------------------------
	SetOptionParamInfo( PT_GAME, "Game", int( GameId::Count ), params[ PT_GAME ] );
	for( unsigned i = 0; i < unsigned( GameId::Count ); ++i )
		SetParamElementInfo( PT_GAME, i, GameName( GameId( i ) ), float( i ) );

	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );
	SetParamInfof( PT_DIFFICULTY, "Difficulty", FF_TYPE_STANDARD );
	SetParamInfof( PT_SKILL, "Skill", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUTOPILOT, "Autoplay", FF_TYPE_BOOLEAN );
	SetParamInfof( PT_SEED, "Seed", FF_TYPE_STANDARD );
	SetParamInfof( PT_RESET, "Restart", FF_TYPE_EVENT );
	SetParamGroup( PT_GAME, "Game" );
	SetParamGroup( PT_SPEED, "Game" );
	SetParamGroup( PT_DIFFICULTY, "Game" );
	SetParamGroup( PT_SKILL, "Game" );
	SetParamGroup( PT_AUTOPILOT, "Game" );
	SetParamGroup( PT_SEED, "Game" );
	SetParamGroup( PT_RESET, "Game" );

	SetParamInfof( PT_GRID, "Grid", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_ASPECT, "Aspect", int( Aspect::Count ), params[ PT_ASPECT ] );
	for( unsigned i = 0; i < unsigned( Aspect::Count ); ++i )
		SetParamElementInfo( PT_ASPECT, i, AspectName( Aspect( i ) ), float( i ) );

	SetOptionParamInfo( PT_FIT, "Scaling", int( FitMode::Count ), params[ PT_FIT ] );
	for( unsigned i = 0; i < unsigned( FitMode::Count ); ++i )
		SetParamElementInfo( PT_FIT, i, FitName( FitMode( i ) ), float( i ) );

	SetParamGroup( PT_GRID, "Playfield" );
	SetParamGroup( PT_ASPECT, "Playfield" );
	SetParamGroup( PT_FIT, "Playfield" );

	// The controls. Every one of these is MIDI- and OSC-mappable in the host,
	// which is the only input an FFGL plugin can ever have.
	SetParamInfof( PT_AXIS, "Paddle", FF_TYPE_STANDARD );
	SetParamInfof( PT_LEFT, "Left", FF_TYPE_EVENT );
	SetParamInfof( PT_RIGHT, "Right", FF_TYPE_EVENT );
	SetParamInfof( PT_UP, "Up / Thrust", FF_TYPE_EVENT );
	SetParamInfof( PT_DOWN, "Down", FF_TYPE_EVENT );
	SetParamInfof( PT_FIRE, "Fire", FF_TYPE_EVENT );
	SetParamGroup( PT_AXIS, "Controls" );
	SetParamGroup( PT_LEFT, "Controls" );
	SetParamGroup( PT_RIGHT, "Controls" );
	SetParamGroup( PT_UP, "Controls" );
	SetParamGroup( PT_DOWN, "Controls" );
	SetParamGroup( PT_FIRE, "Controls" );

	SetOptionParamInfo( PT_PALETTE, "Palette", int( Palette::Count ), params[ PT_PALETTE ] );
	for( unsigned i = 0; i < unsigned( Palette::Count ); ++i )
		SetParamElementInfo( PT_PALETTE, i, PaletteName( Palette( i ) ), float( i ) );

	SetParamInfof( PT_CELL_ROUND, "Cell Shape", FF_TYPE_STANDARD );
	SetParamInfof( PT_CELL_GAP, "Cell Gap", FF_TYPE_STANDARD );
	SetParamInfof( PT_GLOW, "Glow", FF_TYPE_STANDARD );
	SetParamInfof( PT_SCANLINE, "Scanlines", FF_TYPE_STANDARD );
	SetParamInfof( PT_REACTIVE, "Reactive", FF_TYPE_STANDARD );
	SetParamGroup( PT_PALETTE, "Look" );
	SetParamGroup( PT_CELL_ROUND, "Look" );
	SetParamGroup( PT_CELL_GAP, "Look" );
	SetParamGroup( PT_GLOW, "Look" );
	SetParamGroup( PT_SCANLINE, "Look" );
	SetParamGroup( PT_REACTIVE, "Look" );

	SetParamInfof( PT_BACK_R, "Background Red", FF_TYPE_RED );
	SetParamInfof( PT_BACK_G, "Background Green", FF_TYPE_GREEN );
	SetParamInfof( PT_BACK_B, "Background Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_BACK_OPACITY, "Background Alpha", FF_TYPE_STANDARD );
	SetParamGroup( PT_BACK_R, "Background" );
	SetParamGroup( PT_BACK_G, "Background" );
	SetParamGroup( PT_BACK_B, "Background" );
	SetParamGroup( PT_BACK_OPACITY, "Background" );

	SetParamInfof( PT_CLIP_BRICKS, "Bricks From Clip", FF_TYPE_BOOLEAN );
	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );
	SetParamGroup( PT_CLIP_BRICKS, "Output" );
	SetParamGroup( PT_MIX, "Output" );
	// The About block. Declared inline rather than through a helper, because
	// SetParamInfo is protected on CFFGLPlugin and nothing outside the class
	// can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
		SetParamGroup( id, "About" );


	FFGLLog::LogToHost( "Coinop created" );
}

//---------------------------------------------------------------------------
// Parameters.
//---------------------------------------------------------------------------

void CoinopPlugin::LatchButton( unsigned int index, float oldValue, float newValue )
{
	// Rising edge only. An FF_TYPE_EVENT goes to 1 and back to 0 as the host
	// sees fit, and it is the transition that is the press -- reading the level
	// would either miss the press entirely or repeat it every frame the host
	// happened to leave it high.
	if( !( oldValue < 0.5f && newValue >= 0.5f ) )
		return;

	switch( index )
	{
		case PT_LEFT: input.Press( Button::Left ); break;
		case PT_RIGHT: input.Press( Button::Right ); break;
		case PT_UP: input.Press( Button::Up ); break;
		case PT_DOWN: input.Press( Button::Down ); break;
		case PT_FIRE: input.Press( Button::Fire ); break;
		default: break;
	}
}

//---------------------------------------------------------------------------
char* CoinopPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		// Function-local rather than a member: the line is built from
		// compile-time facts, so it is the same for every instance, and the
		// host only needs the pointer to outlive the call.
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

//---------------------------------------------------------------------------
FFResult CoinopPlugin::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult CoinopPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing, so they are handled
	// before any of the bookkeeping below: pressing one is not the operator
	// editing a control.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	const float previous = params[ index ];
	params[ index ]      = value;

	// Held state as well as the edge, for the games that want a level rather
	// than an event -- thrust in Drift, and steering in Marchers.
	switch( index )
	{
		case PT_LEFT: input.SetHeld( Button::Left, value >= 0.5f ); break;
		case PT_RIGHT: input.SetHeld( Button::Right, value >= 0.5f ); break;
		case PT_UP: input.SetHeld( Button::Up, value >= 0.5f ); break;
		case PT_DOWN: input.SetHeld( Button::Down, value >= 0.5f ); break;
		case PT_FIRE: input.SetHeld( Button::Fire, value >= 0.5f ); break;
		case PT_AXIS: input.SetAxis( value ); break;
		default: break;
	}

	LatchButton( index, previous, value );

	// Restart is an event too, but it is handled on the render thread rather
	// than here: restarting the sim from the parameter thread would race the
	// draw that is reading the grid.
	return FF_SUCCESS;
}

float CoinopPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

//---------------------------------------------------------------------------
// GL lifetime.
//---------------------------------------------------------------------------

FFResult CoinopPlugin::InitGL( const FFGLViewportStruct* vp )
{
	(void)vp;

	diag::init();
	const GLubyte* version = glGetString( GL_VERSION );
	diag::info( std::string( "InitGL, GL " ) +
	            ( version ? reinterpret_cast< const char* >( version ) : "unknown" ) );

	std::string fragment = kCellShader;
	if( overInput )
	{
		// After the #version line, which must be first in a GLSL source.
		const size_t afterVersion = fragment.find( '\n' );
		if( afterVersion != std::string::npos )
			fragment.insert( afterVersion + 1, kEffectDefine );
	}

	if( !shader.Compile( kVertexShader, fragment.c_str() ) )
	{
		// The failure that actually happens, and from the operator's side it
		// looks like "the plugin does nothing" with no message anywhere.
		diag::error( "cell shader would not compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "screen quad would not initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	glGenTextures( 1, &cellTexture );
	if( cellTexture == 0 )
	{
		diag::error( "could not create the cell texture" );
		DeInitGL();
		return FF_FAIL;
	}

	glBindTexture( GL_TEXTURE_2D, cellTexture );

	// GL_NEAREST is not a style choice here: an integer texture cannot be
	// filtered at all, and a linear filter on GL_RGBA8UI makes the sampler
	// incomplete -- every fetch returns zero, so the playfield renders empty
	// with no error anywhere.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );

	cellTexW = 0;
	cellTexH = 0;

	sim.SetGame( GameId( std::clamp( int( std::lround( params[ PT_GAME ] ) ),
	                                 0, int( GameId::Count ) - 1 ) ) );
	sim.Configure( ConfigFromParams( params ) );

	return FF_SUCCESS;
}

FFResult CoinopPlugin::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();

	if( cellTexture != 0 )
	{
		glDeleteTextures( 1, &cellTexture );
		cellTexture = 0;
	}
	cellTexW = 0;
	cellTexH = 0;

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
// Render.
//---------------------------------------------------------------------------

void CoinopPlugin::UpdateClock()
{
	// FFGL never says what unit SetTime arrives in, and hosts disagree:
	// Resolume sends MILLISECONDS (measured live at 20.0 per frame at its
	// 50 fps, and the SDK's own Particles sample divides by 1000), while the
	// offline harness sends seconds. Reading it raw is a thousand times fast
	// on the one host that matters and exactly right on the one that gets
	// tested, which is how it stays hidden.
	//
	// This used to guess the unit from the magnitude of a single frame delta
	// and then lock. That had three holes: a delta between 0.5 and 2.0 decided
	// nothing, a burst of sub-0.5 ms frames at load -- a thumbnail render on a
	// quick GPU -- locked it to "seconds" for the rest of the session, and
	// while undecided it assumed seconds, which is precisely the millisecond
	// host's wrong answer.
	//
	// So measure instead of guessing. steady_clock says how much real time
	// passed, the host says how much host time passed, and the ratio names the
	// unit outright. Nothing plausible sits between 1 and 1000, so both bands
	// are wide and a frame fitting neither simply does not vote.
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	// Never read `hostTime` before the host has set it: CFFGLPlugin's
	// constructor initialises bpm and barPhase and leaves hostTime
	// uninitialised, so until SetTime lands it is whatever was in that memory.
	const double raw = hostTimeSeen ? hostTime : -1.0;

	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;

		// A paused host, a looping clip or a stalled frame tells us nothing.
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			// Several frames rather than one, so a single odd frame -- the
			// first after a seek, say -- cannot decide it on its own.
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				diag::info( std::string( "host clock is " )
				            + ( clockScale == 0.001 ? "milliseconds" : "seconds" )
				            + ", scale=" + std::to_string( clockScale ) );
			}
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	// Until the unit is settled -- and for a host that never calls SetTime at
	// all -- run on the real clock. Wrong in origin but right in rate, where
	// assuming seconds would be a thousand times fast on Resolume.
	hostSeconds = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

FFResult CoinopPlugin::SetTime( double time )
{
	hostTimeSeen = true;
	return CFFGLPlugin::SetTime( time );
}

void CoinopPlugin::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void CoinopPlugin::TickClockForTest()
{
	UpdateClock();
}

double CoinopPlugin::ClockScaleForTest() const
{
	return clockScale;
}

double CoinopPlugin::HostSecondsForTest() const
{
	return hostSeconds;
}

void CoinopPlugin::UploadCells()
{
	const Grid& grid = sim.Playfield();
	if( grid.Width() <= 0 || grid.Height() <= 0 )
		return;

	glBindTexture( GL_TEXTURE_2D, cellTexture );

	// RGBA8UI is four bytes per texel, so the default 4-byte unpack alignment
	// happens to be right -- set it anyway, because it is a global bit of
	// state that belongs to whoever rendered before us.
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	if( grid.Width() != cellTexW || grid.Height() != cellTexH )
	{
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8UI, grid.Width(), grid.Height(), 0,
		              GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, grid.Data() );
		cellTexW = grid.Width();
		cellTexH = grid.Height();
	}
	else
	{
		// The common path. A 32x24 playfield is 3 KB, so this is not worth a
		// PBO or a dirty-rectangle scheme.
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, grid.Width(), grid.Height(),
		                 GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, grid.Data() );
	}

	glBindTexture( GL_TEXTURE_2D, 0 );
}

void CoinopPlugin::Render( int width, int height, GLuint inputTexture,
                           float maxU, float maxV )
{
	UpdateClock();

	// Restart is consumed here rather than in SetFloatParameter, so that the
	// sim is only ever mutated from the render thread.
	const bool wantReset = params[ PT_RESET ] >= 0.5f;
	if( wantReset && !resetLatched )
	{
		sim.Restart();
		input.Clear();
	}
	resetLatched = wantReset;

	const GameId want = GameId( std::clamp( int( std::lround( params[ PT_GAME ] ) ),
	                                        0, int( GameId::Count ) - 1 ) );
	if( want != sim.CurrentGame() )
	{
		sim.SetGame( want );
		input.Clear();
	}

	sim.Configure( ConfigFromParams( params ) );
	sim.Advance( hostSeconds, input );

	UploadCells();

	ffglex::ScopedShaderBinding shaderBinding( shader.GetGLID() );

	const Grid& grid = sim.Playfield();
	shader.Set( "GridSize", float( grid.Width() ), float( grid.Height() ) );
	shader.Set( "Resolution", float( width ), float( height ) );
	shader.Set( "FitMode", std::clamp( int( std::lround( params[ PT_FIT ] ) ),
	                                   0, int( FitMode::Count ) - 1 ) );
	shader.Set( "PaletteMode", std::clamp( int( std::lround( params[ PT_PALETTE ] ) ),
	                                       0, int( Palette::Count ) - 1 ) );
	shader.Set( "CellRound", params[ PT_CELL_ROUND ] );
	shader.Set( "CellGap", params[ PT_CELL_GAP ] );
	shader.Set( "Glow", params[ PT_GLOW ] );
	shader.Set( "Scanline", params[ PT_SCANLINE ] );
	shader.Set( "Reactive", params[ PT_REACTIVE ] );
	shader.Set( "Intensity", sim.Intensity() );
	shader.Set( "BackColor", params[ PT_BACK_R ], params[ PT_BACK_G ],
	            params[ PT_BACK_B ], params[ PT_BACK_OPACITY ] );

	// Sampler units. The cell texture is unit 0 in the source build and unit 1
	// in the effect build, so that the clip keeps unit 0 -- which is what the
	// host hands over and what every other plugin in the fleet uses for it.
	const int cellUnit = overInput ? 1 : 0;
	shader.Set( "CellTexture", cellUnit );

	if( overInput )
	{
		shader.Set( "InputTexture", 0 );
		shader.Set( "MaxUV", maxU, maxV );
		shader.Set( "Mix", params[ PT_MIX ] );
		shader.Set( "ClipBricks", params[ PT_CLIP_BRICKS ] >= 0.5f ? 1 : 0 );
	}

	if( overInput )
	{
		ffglex::ScopedSamplerActivation clipSampler( 0 );
		ffglex::Scoped2DTextureBinding clipBinding( inputTexture );

		ffglex::ScopedSamplerActivation cellSampler( GLuint( cellUnit ) );
		ffglex::Scoped2DTextureBinding cellBinding( cellTexture );

		quad.Draw();
	}
	else
	{
		ffglex::ScopedSamplerActivation cellSampler( GLuint( cellUnit ) );
		ffglex::Scoped2DTextureBinding cellBinding( cellTexture );

		quad.Draw();
	}
}

FFResult CoinopPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	int width           = 0;
	int height          = 0;
	GLuint inputTexture = 0;
	float maxU          = 1.0f;
	float maxV          = 1.0f;

	if( overInput )
	{
		if( pGL == nullptr || pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;

		const FFGLTextureStruct& texture = *pGL->inputTextures[ 0 ];
		inputTexture                     = texture.Handle;
		width                            = texture.Width;
		height                           = texture.Height;

		// The input texture can be larger than the picture; MaxUV is the
		// fraction actually drawn. Only the clip fetch uses it -- the playfield
		// is drawn in frame space and never touches it.
		const FFGLTexCoords coords = GetMaxGLTexCoords( texture );
		maxU                       = coords.s;
		maxV                       = coords.t;
	}
	else
	{
		width  = int( currentViewport.width );
		height = int( currentViewport.height );
	}

	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	Render( width, height, inputTexture, maxU, maxV );
	return FF_SUCCESS;
}

} // namespace coinop
