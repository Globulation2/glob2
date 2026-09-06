// SPDX-License-Identifier: GPL-3.0-or-later

// Harness for the per-team Map::exploredArea section of saved games:
//   1. saveExploredArea -> loadExploredArea(keep=true) restores every byte of
//      every team and leaves the stream positioned after the section
//   2. loadExploredArea(keep=false) consumes the section without allocating,
//      for loads that have no game to attach the arrays to
// Uses the Map-subclass fixture from test/README.md so no Sector, Team or
// globalContainer is linked. Links libgag_server.a for BinaryStream +
// MemoryStreamBackend.

#include <cstdio>
#include <cstring>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Map.h"

using namespace GAGCore;

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream references C++-mangled SHA1 names.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
}

constexpr int kMapDec = 3;   // 8x8
constexpr int kTeams = 3;
constexpr Uint32 kSentinel = 0xC0FFEE42u;

// Sized Map with no Sector array; see test/README.md. exploredArea is owned
// here because Map::clear() only frees it on the arraysBuilt path.
struct TeamMap : Map
{
	TeamMap()
	{
		wDec = kMapDec;
		hDec = kMapDec;
		w = 1 << kMapDec;
		h = 1 << kMapDec;
		wMask = w - 1;
		hMask = h - 1;
		size = static_cast<size_t>(w * h);
	}
	~TeamMap()
	{
		for (int t = 0; t < Team::MAX_COUNT; t++)
		{
			delete[] exploredArea[t];
			exploredArea[t] = NULL;
		}
		w = h = 0;
		wMask = hMask = 0;
		wDec = hDec = 0;
		size = 0;
	}

	// Distinct per-team ramp that hits 0, EXPLORED_BY_BUILDING_MIN (2) and 255.
	void fillTeam(int t)
	{
		exploredArea[t] = new Uint8[size];
		for (size_t i = 0; i < size; i++)
			exploredArea[t][i] = static_cast<Uint8>((i * 37 + t * 101) % 256);
		exploredArea[t][0] = 0;
		exploredArea[t][1] = 2;
		exploredArea[t][2] = 255;
	}
};

std::unique_ptr<BinaryInputStream> makeInputStream(const MemoryStreamBackend& written)
{
	// BinaryInputStream takes ownership of the backend.
	MemoryStreamBackend* copy = new MemoryStreamBackend(written);
	copy->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(copy);
}

// Writes the section plus a trailing sentinel and hands back a reader over a
// copy of the bytes; the output stream owns and frees the backend it wrote.
std::unique_ptr<BinaryInputStream> writeSection(TeamMap& original)
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.saveExploredArea(&ostream, kTeams);
	ostream.writeUint32(kSentinel, "sentinel");
	return makeInputStream(*backend);
}

void testRoundTripRestoresEveryTeam()
{
	TeamMap original;
	for (int t = 0; t < kTeams; t++)
		original.fillTeam(t);

	auto istream = writeSection(original);

	TeamMap loaded;
	loaded.loadExploredArea(istream.get(), kTeams, true);

	bool allAllocated = true;
	bool allEqual = true;
	for (int t = 0; t < kTeams; t++)
	{
		if (!loaded.exploredArea[t])
		{
			allAllocated = false;
			allEqual = false;
			continue;
		}
		if (std::memcmp(loaded.exploredArea[t], original.exploredArea[t], original.getW() * original.getH()) != 0)
			allEqual = false;
	}
	check(allAllocated, "roundTrip: every team array allocated");
	check(allEqual, "roundTrip: every team array byte-identical");
	check(loaded.exploredArea[kTeams] == NULL, "roundTrip: teams past the count stay unallocated");
	check(istream->readUint32("sentinel") == kSentinel, "roundTrip: stream positioned after the section");
}

void testDiscardConsumesSectionWithoutAllocating()
{
	TeamMap original;
	for (int t = 0; t < kTeams; t++)
		original.fillTeam(t);

	auto istream = writeSection(original);

	TeamMap loaded;
	loaded.loadExploredArea(istream.get(), kTeams, false);

	bool noneAllocated = true;
	for (int t = 0; t < Team::MAX_COUNT; t++)
		if (loaded.exploredArea[t])
			noneAllocated = false;
	check(noneAllocated, "discard: no team array allocated");
	check(istream->readUint32("sentinel") == kSentinel, "discard: stream positioned after the section");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTripRestoresEveryTeam();
	testDiscardConsumesSectionWithoutAllocating();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
