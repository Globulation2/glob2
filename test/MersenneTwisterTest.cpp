// SPDX-License-Identifier: GPL-3.0-or-later
//
// MersenneTwister replaced boost::mt19937 as the simulation RNG and must stay
// bit-identical to it: saved games store its text state and lockstep games
// replay its draws. The expected values below were produced by Boost 1.83's
// boost::mt19937.

#include "../src/MersenneTwister.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok)
	{
		std::cerr << "FAIL: " << what << '\n';
		++failures;
	}
}

std::string text(const MersenneTwister& mt)
{
	std::ostringstream out;
	out << mt;
	return out.str();
}

std::uint64_t fnv1a(const std::string& s)
{
	std::uint64_t h = 1469598103934665603ULL;
	for (unsigned char c : s)
	{
		h ^= c;
		h *= 1099511628211ULL;
	}
	return h;
}

struct StateCase
{
	std::uint32_t seed;
	int draws;
	std::uint64_t textHash;
	const char* firstWord;
};

// Text state after seeding and drawing, hashed; the first word shows the
// seed-time normalisation (seed 5489 starts with 5489, but prints 621461756).
const StateCase stateCases[] = {
	{5489u, 0, 0xddb8e3a483b07a67ULL, "621461756"},
	{5489u, 1, 0xfb7686f8d4a26adaULL, "1301868182"},
	{5489u, 623, 0x29380d0f6d84b9c2ULL, "79981964"},
	{5489u, 624, 0x882060c3ce9cd68eULL, "2601187879"},
	{5489u, 625, 0x5488e9dd0b12f601ULL, "3919438689"},
	{5489u, 1000, 0x97f2e0c33c066a32ULL, "761095935"},
	{0u, 0, 0x7163e381a176a59fULL, "788430048"},
	{0u, 1, 0x86cdc9c490393338ULL, "1"},
	{0u, 623, 0xc0b80b54e20eafbdULL, "1796872496"},
	{0u, 624, 0x5d4e4681a14feb1fULL, "2443250962"},
	{0u, 625, 0x6b60037118189d99ULL, "1093594115"},
	{0u, 1000, 0x91046c1dc39ff387ULL, "183005637"},
	{20260929u, 0, 0x4d2e606ea3bca389ULL, "609591260"},
	{20260929u, 1, 0xaf0854e551524f2cULL, "1917495974"},
	{20260929u, 623, 0x3738a4ef0449028fULL, "1566862605"},
	{20260929u, 624, 0x77fc6f33fa7f5ef3ULL, "1849922895"},
	{20260929u, 625, 0x3c3c8e722aff0160ULL, "2038446162"},
	{20260929u, 1000, 0xc666f9c7851c710eULL, "4124495213"},
};
}

int main()
{
	// Reference value of the MT19937 definition: 10000th output of the default seed.
	{
		MersenneTwister mt;
		std::uint32_t value = 0;
		for (int i = 0; i < 10000; ++i)
			value = mt();
		check(value == 4123659995u, "10000th output of the default seed");
	}

	for (const StateCase& c : stateCases)
	{
		MersenneTwister mt(c.seed);
		for (int i = 0; i < c.draws; ++i)
			mt();
		const std::string state = text(mt);
		const std::string label = "seed " + std::to_string(c.seed) + " after " + std::to_string(c.draws) + " draws";
		check(fnv1a(state) == c.textHash, label + ": text state");
		check(state.substr(0, state.find(' ')) == c.firstWord, label + ": first state word");

		// Reloading the text state resumes the same sequence and prints the same.
		MersenneTwister reloaded(12345u);
		std::istringstream in(state + " ");
		in >> reloaded;
		check(bool(in), label + ": state reads back");
		check(text(reloaded) == state && reloaded == mt, label + ": reloaded state is equal");
		for (int i = 0; i < 1500; ++i)
			if (reloaded() != mt())
			{
				check(false, label + ": reloaded sequence diverges");
				break;
			}
		check(text(reloaded) == text(mt), label + ": texts agree after further draws");
	}

	// Seeding resets the whole state, whatever was drawn before.
	{
		MersenneTwister a(99u), b;
		for (int i = 0; i < 700; ++i)
			a();
		a.seed(20260929u);
		b.seed(20260929u);
		check(a == b && text(a) == text(b), "reseeding");
	}

	if (failures)
		return EXIT_FAILURE;
	std::cout << "MersenneTwister matches boost::mt19937 outputs and text state\n";
}
