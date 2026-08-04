#pragma once

#include <cstdint>

/**
	A deterministic pseudo-random source, owned by the game rather than by the
	process.

	## Why not `rand()`, and why not `std::mt19937`

	The Seed parameter has to mean something. "Same seed, same game" is what
	makes `coinoptest` able to assert that the snake is 14 long after 300 ticks,
	and it is what lets an operator find a run they liked and get it back.

	`rand()` shares one global state with the host and with every other plugin
	in the process, so two Snake instances on two layers would draw from the
	same stream and neither would be reproducible. `std::mt19937` is
	per-instance and fine, but its output is only guaranteed identical across
	implementations for the raw engine -- the moment you put a
	`std::uniform_int_distribution` in front of it the mapping is
	implementation-defined, so macOS and Windows disagree on where the food
	lands. Both are real reproducibility holes and neither shows up as a
	failure; they show up as a test that passes on one machine.

	So: sixteen lines of xorshift64*, and a rejection-sampled bound. Fixed
	behaviour on every platform, forever.
*/
namespace coinop
{

class Rng
{
public:
	explicit Rng( uint64_t seed )
	{
		Reseed( seed );
	}

	/// A zero state is xorshift's one fixed point -- it emits zero forever.
	/// Seed 0 is exactly the seed a user leaves the slider on, so it is
	/// remapped rather than left to produce a game where the food never moves.
	void Reseed( uint64_t seed )
	{
		mState = seed ? seed : 0x9E3779B97F4A7C15ull;
	}

	uint64_t Next()
	{
		mState ^= mState >> 12;
		mState ^= mState << 25;
		mState ^= mState >> 27;
		return mState * 0x2545F4914F6CDD1Dull;
	}

	/// Uniform in [0, n). Rejection rather than `% n`, because modulo is biased
	/// toward low values whenever n does not divide 2^32 -- and n here is a
	/// grid width, which never does. The bias is small, but it is a bias in
	/// *where the food appears*, which is the one thing a player would notice
	/// over a long run.
	uint32_t Below( uint32_t n )
	{
		if( n == 0 )
			return 0;

		const uint32_t limit = uint32_t( -int32_t( n ) ) % n;// 2^32 mod n
		uint32_t v;
		do
		{
			v = uint32_t( Next() >> 32 );
		} while( v < limit );

		return v % n;
	}

	/// Uniform in [0, 1). 24 bits, which is every value a float can hold in
	/// that range without repeats.
	float Unit()
	{
		return float( Next() >> 40 ) * ( 1.0f / 16777216.0f );
	}

	float Range( float lo, float hi ) { return lo + ( hi - lo ) * Unit(); }

	bool Chance( float p ) { return Unit() < p; }

private:
	uint64_t mState = 0;
};

} // namespace coinop
