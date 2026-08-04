/**
	coinopgl -- the shader half of the verification, in a headless GL context.

	`coinoptest` proves the games are correct and never touches a GPU.
	Everything it cannot see is in here: whether the fragment shader actually
	compiles, and whether the cell texture survives the trip to the sampler.

	Both are silent failures. A shader that will not compile gives a plugin that
	loads, exports `plugMain`, appears in Resolume's effect list and draws
	nothing; a mis-specified integer texture gives one that draws a background
	and no playfield. Neither produces a message anywhere the operator can see,
	and neither is caught by a build that goes green.

	This is deliberately *not* the FFGL plugin class -- it is the same shader
	source and the same texture format, driven by the same `Sim`, through a
	CGL context with no window server. What it does not cover is the FFGL
	parameter and clock plumbing in Coinop.cpp, which is thin and which nothing
	here can exercise without a host.

	    build/coinopgl            compile both variants and render a frame each
	    build/coinopgl --dump     also write a PPM of what it rendered
*/

#include "Controls.h"
#include "Shaders.h"
#include "Sim.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace coinop;

namespace
{

int gChecks = 0;
int gFailed = 0;

void Check( bool ok, const std::string& what )
{
	++gChecks;
	if( ok )
		std::printf( "  ok   %s\n", what.c_str() );
	else
	{
		++gFailed;
		std::printf( "  \033[31mFAIL\033[0m %s\n", what.c_str() );
	}
}

CGLContextObj MakeContext()
{
	// Core profile 3.2 is the newest CGL will name; macOS then hands back a
	// 4.1 context, which is what the shaders declare.
	const CGLPixelFormatAttribute attribs[] = {
		kCGLPFAOpenGLProfile, CGLPixelFormatAttribute( kCGLOGLPVersion_3_2_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, CGLPixelFormatAttribute( 32 ),
		CGLPixelFormatAttribute( 0 )
	};

	CGLPixelFormatObj pix = nullptr;
	GLint npix            = 0;
	if( CGLChoosePixelFormat( attribs, &pix, &npix ) != kCGLNoError || pix == nullptr )
		return nullptr;

	CGLContextObj ctx = nullptr;
	const CGLError err = CGLCreateContext( pix, nullptr, &ctx );
	CGLDestroyPixelFormat( pix );

	if( err != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( ctx );
	return ctx;
}

GLuint CompileStage( GLenum type, const char* src, std::string& log )
{
	const GLuint id = glCreateShader( type );
	glShaderSource( id, 1, &src, nullptr );
	glCompileShader( id );

	GLint ok = GL_FALSE;
	glGetShaderiv( id, GL_COMPILE_STATUS, &ok );
	if( ok == GL_TRUE )
		return id;

	GLint len = 0;
	glGetShaderiv( id, GL_INFO_LOG_LENGTH, &len );
	std::vector< char > buf( size_t( len > 1 ? len : 1 ) );
	glGetShaderInfoLog( id, len, nullptr, buf.data() );
	log.assign( buf.data() );

	glDeleteShader( id );
	return 0;
}

GLuint BuildProgram( bool overInput, std::string& log )
{
	std::string fragment = kCellShader;
	if( overInput )
	{
		const size_t afterVersion = fragment.find( '\n' );
		if( afterVersion != std::string::npos )
			fragment.insert( afterVersion + 1, kEffectDefine );
	}

	const GLuint vs = CompileStage( GL_VERTEX_SHADER, kVertexShader, log );
	if( vs == 0 )
		return 0;

	const GLuint fs = CompileStage( GL_FRAGMENT_SHADER, fragment.c_str(), log );
	if( fs == 0 )
	{
		glDeleteShader( vs );
		return 0;
	}

	const GLuint prog = glCreateProgram();
	glAttachShader( prog, vs );
	glAttachShader( prog, fs );
	glBindAttribLocation( prog, 0, "vPosition" );
	glBindAttribLocation( prog, 1, "vUV" );
	glLinkProgram( prog );

	glDeleteShader( vs );
	glDeleteShader( fs );

	GLint ok = GL_FALSE;
	glGetProgramiv( prog, GL_LINK_STATUS, &ok );
	if( ok == GL_TRUE )
		return prog;

	GLint len = 0;
	glGetProgramiv( prog, GL_INFO_LOG_LENGTH, &len );
	std::vector< char > buf( size_t( len > 1 ? len : 1 ) );
	glGetProgramInfoLog( prog, len, nullptr, buf.data() );
	log.assign( buf.data() );

	glDeleteProgram( prog );
	return 0;
}

void SetF( GLuint p, const char* n, float a ) { glUniform1f( glGetUniformLocation( p, n ), a ); }
void SetF2( GLuint p, const char* n, float a, float b ) { glUniform2f( glGetUniformLocation( p, n ), a, b ); }
void SetF4( GLuint p, const char* n, float a, float b, float c, float d ) { glUniform4f( glGetUniformLocation( p, n ), a, b, c, d ); }
void SetI( GLuint p, const char* n, int a ) { glUniform1i( glGetUniformLocation( p, n ), a ); }

/// Render one frame of a game and return the pixels.
std::vector< uint8_t > RenderGame( GLuint prog, GameId id, int outW, int outH, bool dump )
{
	// --- Playfield ---------------------------------------------------------
	Sim sim;
	Input in;
	GameConfig cfg;
	cfg.gridW = 32;
	cfg.gridH = 24;
	cfg.seed  = 7;
	cfg.skill = 0.7f;
	cfg.autopilot = true;

	sim.SetGame( id );
	sim.Configure( cfg );

	double clock = 0.0;
	for( int f = 0; f < 400; ++f )
	{
		clock += 0.02;
		sim.Advance( clock, in );
	}

	const Grid& grid = sim.Playfield();

	GLuint cellTex = 0;
	glGenTextures( 1, &cellTex );
	glBindTexture( GL_TEXTURE_2D, cellTex );

	// GL_NEAREST is mandatory, not cosmetic: an integer texture cannot be
	// filtered, and a linear filter makes the sampler incomplete so every fetch
	// returns zero and the playfield renders empty.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8UI, grid.Width(), grid.Height(), 0,
	              GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, grid.Data() );

	// --- Target ------------------------------------------------------------
	GLuint fbo = 0, colour = 0;
	glGenTextures( 1, &colour );
	glBindTexture( GL_TEXTURE_2D, colour );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0 );

	// --- Quad --------------------------------------------------------------
	const float verts[] = {
		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,
	};

	GLuint vao = 0, vbo = 0;
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
	glGenBuffers( 1, &vbo );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ), nullptr );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ),
	                       reinterpret_cast< void* >( 2 * sizeof( float ) ) );

	// --- Draw --------------------------------------------------------------
	glViewport( 0, 0, outW, outH );
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	glUseProgram( prog );
	SetF2( prog, "GridSize", float( grid.Width() ), float( grid.Height() ) );
	SetF2( prog, "Resolution", float( outW ), float( outH ) );
	SetI( prog, "FitMode", 0 );
	SetI( prog, "PaletteMode", 0 );
	SetF( prog, "CellRound", 0.25f );
	SetF( prog, "CellGap", 0.18f );
	SetF( prog, "Glow", 0.45f );
	SetF( prog, "Scanline", 0.0f );
	SetF( prog, "Reactive", 0.35f );
	SetF( prog, "Intensity", sim.Intensity() );
	SetF4( prog, "BackColor", 0.02f, 0.02f, 0.03f, 1.0f );
	SetI( prog, "CellTexture", 0 );

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, cellTex );

	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glFinish();

	std::vector< uint8_t > pixels( size_t( outW ) * size_t( outH ) * 4 );
	glReadPixels( 0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );

	if( dump )
	{
		const std::string path = std::string( "coinopgl-" ) + GameName( id ) + ".ppm";
		if( FILE* f = std::fopen( path.c_str(), "wb" ) )
		{
			std::fprintf( f, "P6\n%d %d\n255\n", outW, outH );
			for( int y = outH - 1; y >= 0; --y )
				for( int x = 0; x < outW; ++x )
				{
					const uint8_t* p = &pixels[ ( size_t( y ) * size_t( outW ) + size_t( x ) ) * 4 ];
					std::fwrite( p, 1, 3, f );
				}
			std::fclose( f );
			std::printf( "       wrote %s\n", path.c_str() );
		}
	}

	glDeleteBuffers( 1, &vbo );
	glDeleteVertexArrays( 1, &vao );
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &colour );
	glDeleteTextures( 1, &cellTex );

	return pixels;
}

//===========================================================================
// Sequence rendering.
//
// The stills in docs/ and every frame of the project video come from here, so
// that what is published is the real plugin rendering rather than a mock-up or
// a screen capture of something adjacent. A still is just a sequence of one
// frame at a given time.
//===========================================================================

/// Everything a cue can move. All in the 0..1 the host sees, except the four
/// enum-valued ones, which are indices -- the same split the plugin has.
struct Look
{
	float game = 0.0f, palette = 0.0f, fit = 0.0f, aspect = 0.0f;
	float speed = 0.45f, skill = 0.65f, difficulty = 0.5f;
	float grid = 0.415f, seed = 0.25f;
	float round = 0.25f, gap = 0.18f, glow = 0.45f, scan = 0.0f, react = 0.35f;
	float backR = 0.02f, backG = 0.02f, backB = 0.03f, backA = 1.0f;
};

float* LookField( Look& L, const std::string& name )
{
	if( name == "Game" ) return &L.game;
	if( name == "Palette" ) return &L.palette;
	if( name == "Fit" ) return &L.fit;
	if( name == "Aspect" ) return &L.aspect;
	if( name == "Speed" ) return &L.speed;
	if( name == "Skill" ) return &L.skill;
	if( name == "Difficulty" ) return &L.difficulty;
	if( name == "Grid" ) return &L.grid;
	if( name == "Seed" ) return &L.seed;
	if( name == "Round" ) return &L.round;
	if( name == "Gap" ) return &L.gap;
	if( name == "Glow" ) return &L.glow;
	if( name == "Scan" ) return &L.scan;
	if( name == "React" ) return &L.react;
	if( name == "BackR" ) return &L.backR;
	if( name == "BackG" ) return &L.backG;
	if( name == "BackB" ) return &L.backB;
	if( name == "BackA" ) return &L.backA;
	return nullptr;
}

struct Cue
{
	float t0 = 0.0f, t1 = 0.0f;
	std::string name;
	float v0 = 0.0f, v1 = 0.0f;
};

/// `T Name=V` sets at a time; `T0..T1 Name=V0..V1` ramps between two. Ramps are
/// eased, because a parameter that starts and stops abruptly reads as a cut.
bool LoadCues( const char* path, std::vector< Cue >& out )
{
	FILE* f = std::fopen( path, "r" );
	if( !f )
		return false;

	char line[ 512 ];
	while( std::fgets( line, sizeof( line ), f ) )
	{
		std::string s( line );
		const size_t hash = s.find( '#' );
		if( hash != std::string::npos )
			s = s.substr( 0, hash );

		const size_t eq = s.find( '=' );
		if( eq == std::string::npos )
			continue;

		std::string lhs = s.substr( 0, eq );
		std::string rhs = s.substr( eq + 1 );

		// Times.
		size_t p = 0;
		while( p < lhs.size() && std::isspace( (unsigned char)lhs[ p ] ) )
			++p;

		const size_t timeEnd = lhs.find_first_of( " \t", p );
		if( timeEnd == std::string::npos )
			continue;

		std::string times = lhs.substr( p, timeEnd - p );
		std::string name  = lhs.substr( timeEnd );
		name.erase( 0, name.find_first_not_of( " \t" ) );
		name.erase( name.find_last_not_of( " \t\r\n" ) + 1 );

		Cue c;
		const size_t dots = times.find( ".." );
		if( dots != std::string::npos )
		{
			c.t0 = std::stof( times.substr( 0, dots ) );
			c.t1 = std::stof( times.substr( dots + 2 ) );
		}
		else
		{
			c.t0 = c.t1 = std::stof( times );
		}

		rhs.erase( 0, rhs.find_first_not_of( " \t" ) );
		const size_t vdots = rhs.find( ".." );
		if( vdots != std::string::npos )
		{
			c.v0 = std::stof( rhs.substr( 0, vdots ) );
			c.v1 = std::stof( rhs.substr( vdots + 2 ) );
		}
		else
		{
			c.v0 = c.v1 = std::stof( rhs );
		}

		c.name = name;
		Look probe;
		if( LookField( probe, name ) == nullptr )
		{
			std::printf( "  warning: unknown cue parameter '%s'\n", name.c_str() );
			continue;
		}
		out.push_back( c );
	}

	std::fclose( f );
	return true;
}

Look LookAt( const std::vector< Cue >& cues, float t )
{
	Look L;
	for( const Cue& c : cues )
	{
		float* field = LookField( L, c.name );
		if( !field )
			continue;

		if( t < c.t0 )
			continue;

		if( t >= c.t1 || c.t1 <= c.t0 )
		{
			*field = c.v1;
		}
		else
		{
			const float u = ( t - c.t0 ) / ( c.t1 - c.t0 );
			const float e = u * u * ( 3.0f - 2.0f * u );// smoothstep
			*field        = c.v0 + ( c.v1 - c.v0 ) * e;
		}
	}
	return L;
}

void WritePPM( const std::string& path, const std::vector< uint8_t >& px, int w, int h )
{
	FILE* f = std::fopen( path.c_str(), "wb" );
	if( !f )
		return;

	std::fprintf( f, "P6\n%d %d\n255\n", w, h );
	for( int y = h - 1; y >= 0; --y )
		for( int x = 0; x < w; ++x )
			std::fwrite( &px[ ( size_t( y ) * size_t( w ) + size_t( x ) ) * 4 ], 1, 3, f );

	std::fclose( f );
}

/**
	Render a cue-driven sequence to numbered PPMs.

	One `Sim` runs across the whole sequence and is advanced by real elapsed
	time, so the games are genuinely playing rather than being posed. Switching
	the Game cue restarts that game, which is what a montage wants.
*/
int RenderSequence( GLuint prog, const std::vector< Cue >& cues, const char* outDir,
                    int w, int h, float seconds, int fps, const char* singleFrame )
{
	Sim sim;
	Input in;
	int currentGame = -1;

	GLuint cellTex = 0;
	glGenTextures( 1, &cellTex );
	glBindTexture( GL_TEXTURE_2D, cellTex );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	int texW = 0, texH = 0;

	GLuint fbo = 0, colour = 0;
	glGenTextures( 1, &colour );
	glBindTexture( GL_TEXTURE_2D, colour );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0 );

	const float verts[] = {
		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,
	};
	GLuint vao = 0, vbo = 0;
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
	glGenBuffers( 1, &vbo );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ), nullptr );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( float ),
	                       reinterpret_cast< void* >( 2 * sizeof( float ) ) );

	const int frames = singleFrame ? 1 : int( seconds * float( fps ) );
	std::vector< uint8_t > px( size_t( w ) * size_t( h ) * 4 );

	// A still is a moment in the same timeline, so the sim still has to be
	// played up to it -- posing a game by hand would produce arrangements the
	// rules cannot actually reach.
	const float stillAt = singleFrame ? seconds : 0.0f;
	const int simFrames = singleFrame ? int( stillAt * float( fps ) ) : 0;

	for( int i = 0; i <= ( singleFrame ? simFrames : frames - 1 ); ++i )
	{
		const float t = float( i ) / float( fps );
		const Look L  = LookAt( cues, t );

		const int want = std::clamp( int( std::lround( L.game ) ), 0, int( GameId::Count ) - 1 );
		if( want != currentGame )
		{
			sim.SetGame( GameId( want ) );
			currentGame = want;
		}

		GameConfig cfg;
		const Aspect asp = Aspect( std::clamp( int( std::lround( L.aspect ) ), 0,
		                                       int( Aspect::Count ) - 1 ) );
		cfg.gridW      = GridWidth( L.grid );
		cfg.gridH      = GridHeight( cfg.gridW, asp );
		cfg.seed       = SeedValue( L.seed );
		cfg.speed      = L.speed;
		cfg.skill      = L.skill;
		cfg.difficulty = L.difficulty;
		cfg.autopilot  = true;

		sim.Configure( cfg );
		sim.Advance( double( t ), in );

		if( singleFrame && i < simFrames )
			continue;

		const Grid& grid = sim.Playfield();
		glBindTexture( GL_TEXTURE_2D, cellTex );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		if( grid.Width() != texW || grid.Height() != texH )
		{
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8UI, grid.Width(), grid.Height(), 0,
			              GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, grid.Data() );
			texW = grid.Width();
			texH = grid.Height();
		}
		else
		{
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, grid.Width(), grid.Height(),
			                 GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, grid.Data() );
		}

		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, w, h );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		glUseProgram( prog );
		SetF2( prog, "GridSize", float( grid.Width() ), float( grid.Height() ) );
		SetF2( prog, "Resolution", float( w ), float( h ) );
		SetI( prog, "FitMode", std::clamp( int( std::lround( L.fit ) ), 0, 2 ) );
		SetI( prog, "PaletteMode", std::clamp( int( std::lround( L.palette ) ), 0,
		                                       int( Palette::Count ) - 1 ) );
		SetF( prog, "CellRound", L.round );
		SetF( prog, "CellGap", L.gap );
		SetF( prog, "Glow", L.glow );
		SetF( prog, "Scanline", L.scan );
		SetF( prog, "Reactive", L.react );
		SetF( prog, "Intensity", sim.Intensity() );
		SetF4( prog, "BackColor", L.backR, L.backG, L.backB, L.backA );
		SetI( prog, "CellTexture", 0 );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, cellTex );
		glBindVertexArray( vao );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		glFinish();
		glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );

		char name[ 512 ];
		if( singleFrame )
			std::snprintf( name, sizeof( name ), "%s", singleFrame );
		else
			std::snprintf( name, sizeof( name ), "%s/frame-%05d.ppm", outDir, i );

		WritePPM( name, px, w, h );
	}

	glDeleteBuffers( 1, &vbo );
	glDeleteVertexArrays( 1, &vao );
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &colour );
	glDeleteTextures( 1, &cellTex );

	return frames;
}

} // namespace

int main( int argc, char** argv )
{
	bool dump = false;
	const char* seqDir  = nullptr;
	const char* script  = nullptr;
	const char* still   = nullptr;
	int outW = 1920, outH = 1080, fps = 30;
	float seconds = 45.0f;

	for( int i = 1; i < argc; ++i )
	{
		const char* a = argv[ i ];
		if( std::strcmp( a, "--dump" ) == 0 )
			dump = true;
		else if( std::strcmp( a, "--sequence" ) == 0 && i + 1 < argc )
			seqDir = argv[ ++i ];
		else if( std::strcmp( a, "--still" ) == 0 && i + 1 < argc )
			still = argv[ ++i ];
		else if( std::strcmp( a, "--script" ) == 0 && i + 1 < argc )
			script = argv[ ++i ];
		else if( std::strcmp( a, "--seconds" ) == 0 && i + 1 < argc )
			seconds = std::stof( argv[ ++i ] );
		else if( std::strcmp( a, "--fps" ) == 0 && i + 1 < argc )
			fps = std::stoi( argv[ ++i ] );
		else if( std::strcmp( a, "--size" ) == 0 && i + 1 < argc )
		{
			const std::string s = argv[ ++i ];
			const size_t x      = s.find( 'x' );
			if( x != std::string::npos )
			{
				outW = std::stoi( s.substr( 0, x ) );
				outH = std::stoi( s.substr( x + 1 ) );
			}
		}
	}

	if( seqDir || still )
	{
		CGLContextObj ctx = MakeContext();
		if( ctx == nullptr )
		{
			std::printf( "could not create a headless GL context\n" );
			return 1;
		}

		std::string log;
		const GLuint prog = BuildProgram( false, log );
		if( prog == 0 )
		{
			std::printf( "shader would not compile:\n%s\n", log.c_str() );
			CGLDestroyContext( ctx );
			return 1;
		}

		std::vector< Cue > cues;
		if( script && !LoadCues( script, cues ) )
		{
			std::printf( "could not read cue script %s\n", script );
			CGLDestroyContext( ctx );
			return 1;
		}

		const int n = RenderSequence( prog, cues, seqDir, outW, outH, seconds, fps, still );
		std::printf( "%s: %d frame(s) at %dx%d\n", still ? still : seqDir, n, outW, outH );

		glDeleteProgram( prog );
		CGLDestroyContext( ctx );
		return 0;
	}

	std::printf( "coinopgl -- shader and cell-texture harness\n\n" );

	CGLContextObj ctx = MakeContext();
	if( ctx == nullptr )
	{
		std::printf( "  \033[31mFAIL\033[0m could not create a headless GL context\n" );
		return 1;
	}

	const GLubyte* ver = glGetString( GL_VERSION );
	std::printf( "  GL %s\n\n", ver ? reinterpret_cast< const char* >( ver ) : "unknown" );

	// --- The shader compiles, both variants --------------------------------
	std::string log;
	const GLuint sourceProg = BuildProgram( false, log );
	Check( sourceProg != 0, "the source shader compiles and links" );
	if( sourceProg == 0 )
		std::printf( "%s\n", log.c_str() );

	log.clear();
	const GLuint effectProg = BuildProgram( true, log );
	Check( effectProg != 0, "the effect shader compiles and links" );
	if( effectProg == 0 )
		std::printf( "%s\n", log.c_str() );

	if( sourceProg == 0 )
	{
		CGLDestroyContext( ctx );
		std::printf( "\n\033[31m%d checks, %d failed\033[0m\n", gChecks, gFailed );
		return 1;
	}

	// --- Every game actually draws something -------------------------------
	//
	// The check that catches the integer-texture traps. If the sampler were
	// incomplete, or the format wrong, every fetch would come back zero, every
	// cell would read as Empty, and the frame would be a flat background --
	// which is exactly what this measures against.
	for( unsigned i = 0; i < unsigned( GameId::Count ); ++i )
	{
		const GameId id = GameId( i );
		const auto px   = RenderGame( sourceProg, id, 320, 180, dump );

		size_t lit  = 0;
		size_t dark = 0;
		for( size_t p = 0; p < px.size(); p += 4 )
		{
			const int sum = px[ p ] + px[ p + 1 ] + px[ p + 2 ];
			if( sum > 40 )
				++lit;
			else
				++dark;
		}

		const double litFrac = double( lit ) / double( lit + dark );
		Check( lit > 200, std::string( GameName( id ) ) + ": the playfield renders lit cells" );
		Check( litFrac < 0.95, std::string( GameName( id ) ) + ": and is not a solid field" );
		std::printf( "       (%s: %.1f%% of the frame lit)\n", GameName( id ), litFrac * 100.0 );
	}

	// --- Fit puts the playfield exactly where it should be -----------------
	//
	// A 4:3 grid in a 16:9 frame must pillarbox to the middle 75% and use the
	// full height. Measured rather than eyeballed, because a letterbox that is
	// slightly wrong looks entirely plausible in a thumbnail and shows up as a
	// pixel map that is a few fixtures out.
	{
		const int outW = 320;
		const int outH = 180;
		const auto px  = RenderGame( sourceProg, GameId::Snake, outW, outH, false );

		int minX = outW, maxX = -1, minY = outH, maxY = -1;
		for( int y = 0; y < outH; ++y )
		{
			for( int x = 0; x < outW; ++x )
			{
				const size_t i = ( size_t( y ) * size_t( outW ) + size_t( x ) ) * 4;
				if( px[ i ] + px[ i + 1 ] + px[ i + 2 ] > 40 )
				{
					minX = std::min( minX, x );
					maxX = std::max( maxX, x );
					minY = std::min( minY, y );
					maxY = std::max( maxY, y );
				}
			}
		}

		// 32x24 into 16:9: gridAR/outAR = 0.75, so 12.5% of the width is bar on
		// each side. Two pixels of slack for the softened cell edge.
		Check( std::abs( minX - 40 ) <= 2 && std::abs( maxX - 279 ) <= 2,
		       "Fit pillarboxes a 4:3 grid to the middle 75% of a 16:9 frame" );
		Check( minY <= 1 && maxY >= outH - 2,
		       "Fit uses the full height when pillarboxing" );
		std::printf( "       (playfield x %d..%d, y %d..%d)\n", minX, maxX, minY, maxY );
	}

	// --- Errors ------------------------------------------------------------
	GLenum err = glGetError();
	Check( err == GL_NO_ERROR, "no GL error was raised during rendering" );
	if( err != GL_NO_ERROR )
		std::printf( "       (glGetError = 0x%04X)\n", err );

	glDeleteProgram( sourceProg );
	if( effectProg )
		glDeleteProgram( effectProg );
	CGLDestroyContext( ctx );

	std::printf( "\n%s%d checks, %d failed\033[0m\n",
	             gFailed ? "\033[31m" : "\033[32m", gChecks, gFailed );
	return gFailed ? 1 : 0;
}
