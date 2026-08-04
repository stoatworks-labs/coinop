#pragma once

#include <cstdint>
#include <mutex>

/**
	Controls, and the one FFGL trap that shapes this whole file.

	## FFGL has no input events. It has parameters, and you poll them.

	There is no keyboard, no mouse and no gamepad in the FFGL ABI. Every control
	a game needs has to arrive as a plugin parameter -- which Resolume will then
	happily MIDI-map or OSC-map, so a launchpad button really can be the fire
	button. That part works.

	What does not work is reading the parameter at render time and calling it a
	button press:

		if( params[ PT_FIRE ] > 0.5f )   // WRONG
			Fire();

	An `FF_TYPE_EVENT` parameter goes to 1 and back to 0 as the host sees fit.
	The host sets parameters on its own thread, whenever the MIDI message lands;
	`ProcessOpenGL` runs at frame rate. **A press and release that both happen
	between two frames is invisible to the poll.** At 50 fps that is a 20 ms
	window, and a drummer on a pad hits inside 20 ms constantly. The failure is
	not a crash: it is a fire button that misses roughly one press in five, feels
	broken, and cannot be reproduced on demand.

	So presses are latched *when they arrive*, on whichever thread delivers them,
	and drained once per simulation tick.

	## Order matters, so this is a queue and not a counter

	A counter per button would fix the dropped press and introduce a subtler
	bug. Snake taking `Left` then `Up` inside one tick must turn left and then
	up; if all the sim learns is "one Left, one Up" it is free to apply them the
	other way round and drive into a wall the player never steered at. The queue
	keeps the order the host delivered.

	## Why a mutex, in a render path, on purpose

	The lock-free version of this is an SPSC ring, and it is only correct if
	exactly one thread ever pushes. FFGL does not promise that. The critical
	section here is a handful of instructions on a queue that sees a few pushes
	per second, taken on a thread with a 20 ms frame budget -- this is not an
	audio callback, and there is no realtime constraint to protect. Correctness
	that is obvious beats cleverness that depends on a guarantee the SDK never
	made.
*/
namespace coinop
{

enum class Button : uint8_t
{
	Left = 0,
	Right,
	Up,
	Down,
	Fire, ///< Launch the ball, shoot, thrust -- whatever the game calls it.
	Reset,
	Count
};

class Input
{
public:
	static constexpr size_t kQueueSize = 64;

	/// Called from the host's parameter thread on a rising edge. Safe from any
	/// thread, and safe to call more often than the sim drains.
	void Press( Button b )
	{
		std::lock_guard< std::mutex > lock( mMutex );

		// A full queue drops the *oldest*, not the newest. If a stuck MIDI
		// controller floods us, the recent intent is what the player still
		// means; a sixty-press backlog from four seconds ago is not.
		if( mCount == kQueueSize )
		{
			mHead = ( mHead + 1 ) % kQueueSize;
			--mCount;
		}

		mQueue[ ( mHead + mCount ) % kQueueSize ] = b;
		++mCount;
	}

	/// Sim thread. Returns false when the queue is empty.
	bool Pop( Button& out )
	{
		std::lock_guard< std::mutex > lock( mMutex );

		if( mCount == 0 )
			return false;

		out   = mQueue[ mHead ];
		mHead = ( mHead + 1 ) % kQueueSize;
		--mCount;
		return true;
	}

	void Clear()
	{
		std::lock_guard< std::mutex > lock( mMutex );
		mCount = 0;
		mHead  = 0;
	}

	/// Held state, for games that want "is thrust down right now" rather than
	/// an edge. Separate from the queue on purpose: a level is not an event,
	/// and polling it at tick time is exactly right.
	void SetHeld( Button b, bool down )
	{
		const uint32_t bit = 1u << unsigned( b );
		std::lock_guard< std::mutex > lock( mMutex );
		mHeld = down ? ( mHeld | bit ) : ( mHeld & ~bit );
	}

	bool Held( Button b ) const
	{
		std::lock_guard< std::mutex > lock( mMutex );
		return ( mHeld & ( 1u << unsigned( b ) ) ) != 0;
	}

	/// The continuous control: paddle position, 0..1 across the playfield.
	/// A fader or an OSC float maps to this directly and it is by some way the
	/// nicest way to play Bricks or Rally.
	void SetAxis( float v )
	{
		std::lock_guard< std::mutex > lock( mMutex );
		mAxis = v;
	}

	float Axis() const
	{
		std::lock_guard< std::mutex > lock( mMutex );
		return mAxis;
	}

private:
	mutable std::mutex mMutex;
	Button mQueue[ kQueueSize ] = {};
	size_t mHead                = 0;
	size_t mCount               = 0;
	uint32_t mHeld              = 0;
	float mAxis                 = 0.5f;
};

} // namespace coinop
