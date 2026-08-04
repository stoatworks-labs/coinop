#pragma once

#include "Controls.h"
#include "Sim.h"

#include <FFGLSDK.h>

/**
	The FFGL side: parameters in, cells out.

	Everything that is actually a *game* lives in `coinop_sim`, which links with
	no graphics API present. This file is the adapter -- host clock, host
	parameters, a texture upload and a draw -- and it is deliberately the only
	place that knows FFGL exists.

	## Two things here are not obvious

	**Button presses are latched in `SetFloatParameter`, not read in
	`ProcessOpenGL`.** The host sets parameters on its own thread when the MIDI
	lands; rendering happens at frame rate. Polling the parameter at draw time
	loses any press that started and ended between two frames, which at 50 fps
	is a 20 ms window and a drummer on a pad hits inside it constantly. See
	Input.h -- the whole file is about this.

	**The host clock is normalised before the sim ever sees it.** FFGL's header
	never says what unit `SetTime` is in and hosts disagree: Resolume hands over
	milliseconds, the offline harness sends seconds. `UpdateClock` decides from
	the first plausible frame delta and sticks. This is lifted from orrery,
	where it was measured against the real host.
*/
namespace coinop
{

class CoinopPlugin : public CFFGLPlugin
{
public:
	explicit CoinopPlugin( bool overInput );

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	void UpdateClock();
	void UploadCells();
	void Render( int width, int height, GLuint inputTexture, float maxU, float maxV );

	/// Rising edge on an FF_TYPE_EVENT parameter, queued for the next tick.
	void LatchButton( unsigned int index, float oldValue, float newValue );

	const bool overInput;

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	/// The playfield, as GL_RGBA8UI. Integer and not normalised -- see
	/// Grid.h. Reallocated only when the grid size changes.
	GLuint cellTexture = 0;
	int cellTexW       = 0;
	int cellTexH       = 0;

	Sim sim;
	Input input;

	float params[ PT_COUNT ] = {};

	//-----------------------------------------------------------------------
	// Host clock units.
	//
	// Resolume hands over MILLISECONDS (measured live in orrery: 20.0 per frame
	// at its 50 fps, and the SDK's own Particles sample divides by 1000), while
	// the offline harness sends seconds. UpdateClock decides from the first
	// plausible frame delta and sticks: 0.001..0.5 is a seconds-host frame,
	// 2..500 is a milliseconds-host frame, anything else keeps waiting.
	//-----------------------------------------------------------------------
	double clockScale  = 0.0;///< 0 until decided; then 1.0 or 0.001.
	double lastRawTime = -1.0;
	double hostSeconds = 0.0;

	/// Guards the one-shot Reset event so holding the parameter at 1 does not
	/// restart the game every frame.
	bool resetLatched = false;
};

} // namespace coinop
