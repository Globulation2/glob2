// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>

/// 32-bit MT19937 Mersenne Twister (Matsumoto & Nishimura), used as the
/// simulation RNG behind syncRand().
///
/// Determinism note: this replaces boost::mt19937 and must stay bit-identical
/// to it, since saved games and menu-colony snapshots store the state and
/// lockstep games depend on every draw. That covers:
/// - the output sequence, for any seed;
/// - seed(value), including the normalisation of the low bits of the first
///   state word, which is visible in the saved state;
/// - the text form written by operator<< and read by operator>>: the 624 most
///   recent state words, oldest first, separated by single spaces. Reading
///   takes 624 whitespace-separated words and resumes from them.
class MersenneTwister
{
public:
	typedef std::uint32_t result_type;

	static constexpr std::size_t state_size = 624;
	static constexpr result_type default_seed = 5489u;

	MersenneTwister() { seed(); }
	explicit MersenneTwister(result_type value) { seed(value); }

	static constexpr result_type min() { return 0; }
	static constexpr result_type max() { return 0xffffffffu; }

	void seed() { seed(default_seed); }

	void seed(result_type value)
	{
		state[0] = value;
		for (std::size_t j = 1; j < n; ++j)
			state[j] = 1812433253u * (state[j-1] ^ (state[j-1] >> 30)) + result_type(j);
		next = n;
		normalizeFirstWord();
	}

	result_type operator()()
	{
		if (next == n)
			twist();
		result_type z = state[next++];
		z ^= (z >> 11);
		z ^= (z << 7) & 0x9d2c5680u;
		z ^= (z << 15) & 0xefc60000u;
		z ^= (z >> 18);
		return z;
	}

	friend bool operator==(const MersenneTwister& a, const MersenneTwister& b)
	{
		for (std::size_t j = 0; j < n; ++j)
			if (a.recentWord(j) != b.recentWord(j))
				return false;
		return true;
	}

	friend bool operator!=(const MersenneTwister& a, const MersenneTwister& b) { return !(a == b); }

	template<typename CharT, typename Traits>
	friend std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const MersenneTwister& mt)
	{
		os << mt.recentWord(0);
		for (std::size_t j = 1; j < n; ++j)
			os << ' ' << mt.recentWord(j);
		return os;
	}

	template<typename CharT, typename Traits>
	friend std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, MersenneTwister& mt)
	{
		for (std::size_t j = 0; j < n; ++j)
			is >> mt.state[j] >> std::ws;
		mt.next = n;
		return is;
	}

private:
	static constexpr std::size_t n = state_size;
	static constexpr std::size_t m = 397;
	static constexpr result_type matrixA = 0x9908b0dfu;
	static constexpr result_type upperMask = 0x80000000u;
	static constexpr result_type lowerMask = 0x7fffffffu;

	/// Current block of state words; words [0, next) have been used.
	result_type state[n];
	/// The block before the current one. Together with the used part of
	/// state it holds the 624 most recent words, which form the text state.
	result_type previous[n];
	std::size_t next;

	/// j-th of the 624 most recent state words, oldest first
	result_type recentWord(std::size_t j) const
	{
		return j < n - next ? previous[next + j] : state[j - (n - next)];
	}

	static result_type twistWord(result_type upper, result_type lower, result_type distant)
	{
		result_type y = (upper & upperMask) | (lower & lowerMask);
		return distant ^ (y >> 1) ^ ((lower & 1u) ? matrixA : 0u);
	}

	void twist()
	{
		std::memcpy(previous, state, sizeof(state));
		std::size_t j = 0;
		for (; j < n - m; ++j)
			state[j] = twistWord(state[j], state[j+1], state[j+m]);
		for (; j < n - 1; ++j)
			state[j] = twistWord(state[j], state[j+1], state[j+m-n]);
		state[n-1] = twistWord(state[n-1], state[0], state[m-1]);
		next = 0;
	}

	/// Only the top bit of the oldest word feeds future output, so its low
	/// bits are set to what running the recurrence backwards gives. This
	/// makes a freshly seeded state print the same as one reached by twisting.
	void normalizeFirstWord()
	{
		// state[n-1] = state[m-1] ^ twist(upper(word before state[0]) | lower(state[0]))
		result_type t = state[m-1] ^ state[n-1];
		result_type y = (t & upperMask) ? (((t ^ matrixA) << 1) | 1u) : (t << 1);
		state[0] = (state[0] & upperMask) | (y & lowerMask);
		for (std::size_t j = 0; j < n; ++j)
			if (state[j] != 0)
				return;
		state[0] = upperMask;
	}
};

/// Uniform integer in [0, bound] drawn from a 32-bit engine, using the same
/// bucket-and-reject scheme as boost::random::uniform_int_distribution so that
/// seeded map generation keeps producing the same maps. Draws nothing when
/// bound is 0.
template<typename Engine>
std::uint32_t uniformInt(Engine& engine, std::uint32_t bound)
{
	static_assert(Engine::min() == 0 && Engine::max() == 0xffffffffu, "needs a full-range 32-bit engine");
	if (bound == 0)
		return 0;
	if (bound == 0xffffffffu)
		return engine();
	std::uint32_t bucketSize = 0xffffffffu / (bound + 1);
	if (0xffffffffu % (bound + 1) == bound)
		++bucketSize;
	while (true)
	{
		std::uint32_t result = engine() / bucketSize;
		if (result <= bound)
			return result;
	}
}
