// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for GameHeader section handling on text streams.
// GameHeader::load and ::loadPlayerInfo used to call
// stream->readLeaveSection(i) inside their per-player loops, passing the
// loop index into readLeaveSection(size_t count) — which pops `count`
// nesting levels, not "section number i". BinaryInputStream's no-op
// readLeaveSection masked this; on a TextInputStream the section stack
// corrupts (i==0 pops nothing, i>=2 pops levels never entered) and the
// count <= levels.size() assert fires at i==3. Post-fix, both loaders pop
// exactly one level per iteration and a text round-trip succeeds.
// Exercised here:
//   1. save() -> TextOutputStream -> TextInputStream -> load() preserves
//      every field (players, allies, seed, winning-condition count)
//   2. savePlayerInfo() -> loadPlayerInfo() round-trip stays aligned
// Links libgag_server.a for TextStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "TextStream.h"
#include "StreamBackend.h"
#include "GameHeader.h"
#include "Version.h"

using namespace GAGCore;

// Same direct sha1.c inclusion as BasePlayerSaveLoadTest.cpp: libgag_server.a
// objects reference C++-mangled SHA1 names with no extern "C" wrapper.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
}

std::unique_ptr<TextInputStream> makeInputStream(const MemoryStreamBackend& written)
{
	// TextInputStream parses the whole backend in its constructor and does
	// not retain (or own) it, so a stack copy is enough.
	MemoryStreamBackend copy(written);
	copy.seekFromStart(0);
	return std::make_unique<TextInputStream>(&copy);
}

GameHeader makeFixtureHeader()
{
	GameHeader header;
	header.setNumberOfPlayers(4);
	header.setGameLatency(12);
	header.setOrderRate(3);
	header.setRandomSeed(0xCAFEBABE);
	header.setMapDiscovered(true);
	header.setAllyTeamsFixed(true);
	for (int i = 0; i < 4; ++i)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "Player %d", i);
		header.getBasePlayer(i) = BasePlayer(i, name, i % 2, BasePlayer::P_IP);
		header.getBasePlayer(i).playerID = 1000 + i;
	}
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		header.setAllyTeamNumber(i, (i % 3) + 1);
	return header;
}

bool playersMatch(GameHeader& a, GameHeader& b, int count)
{
	for (int i = 0; i < count; ++i)
	{
		BasePlayer& pa = a.getBasePlayer(i);
		BasePlayer& pb = b.getBasePlayer(i);
		if (pa.type != pb.type || pa.number != pb.number || pa.name != pb.name
		    || pa.teamNumber != pb.teamNumber || pa.playerID != pb.playerID)
			return false;
	}
	return true;
}

void testFullRoundTrip()
{
	GameHeader original = makeFixtureHeader();

	MemoryStreamBackend backend;
	{
		// TextOutputStream deletes its backend; give it a private copy and
		// read the written bytes back out of it before it dies.
		MemoryStreamBackend* owned = new MemoryStreamBackend;
		TextOutputStream ostream(owned);
		original.save(&ostream);
		ostream.flush();
		backend = *owned;
	}

	auto istream = makeInputStream(backend);
	GameHeader loaded;
	check(loaded.load(istream.get(), VERSION_MINOR), "full: load succeeds");

	check(loaded.getNumberOfPlayers() == 4, "full: numberOfPlayers preserved");
	check(loaded.getGameLatency() == 12, "full: gameLatency preserved");
	check(loaded.getOrderRate() == 3, "full: orderRate preserved");
	check(loaded.getRandomSeed() == 0xCAFEBABE, "full: seed preserved");
	check(loaded.isMapDiscovered(), "full: mapDiscovered preserved");
	check(loaded.areAllyTeamsFixed(), "full: allyTeamsFixed preserved");
	check(playersMatch(original, loaded, 4), "full: players preserved");
	// allyTeamNumbers values are NOT asserted: save() writes all 32 entries
	// under the single repeated key "allyTeamNumber" (no per-index section),
	// so the text table collapses them to the last write. Pre-existing text
	// serialization defect, separate from the section-stack bug under test;
	// binary streams are unaffected because they ignore field names.
	check(loaded.getWinningConditions().size() == original.getWinningConditions().size(),
	      "full: winning-condition count preserved");
}

void testPlayerInfoRoundTrip()
{
	GameHeader original = makeFixtureHeader();

	MemoryStreamBackend backend;
	{
		MemoryStreamBackend* owned = new MemoryStreamBackend;
		TextOutputStream ostream(owned);
		original.savePlayerInfo(&ostream);
		ostream.flush();
		backend = *owned;
	}

	auto istream = makeInputStream(backend);
	GameHeader loaded;
	check(loaded.loadPlayerInfo(istream.get(), VERSION_MINOR),
	      "playerInfo: load succeeds");
	check(loaded.getNumberOfPlayers() == 4, "playerInfo: numberOfPlayers preserved");
	check(playersMatch(original, loaded, 4), "playerInfo: players preserved");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testFullRoundTrip();
	testPlayerInfoRoundTrip();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
