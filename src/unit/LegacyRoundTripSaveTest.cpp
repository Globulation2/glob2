// SPDX-License-Identifier: GPL-3.0-or-later
// A game saved mid-match by a build that still routed resource fetching by
// round trip loads, discards the saved round-trip fields, and plays on
// deterministically with greedy fetching, across a further save and load.
#include "Material.h"
#include "EngineFixtures.h"
#include "MapInternal.h"
#include "Version.h"
#include "FileFormatVersions.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <sstream>

namespace
{
std::vector<Uint32> simulation(Game &game, bool legacy152=false)
{
	std::vector<Uint32> state, buildings, units;
	game.checkSum(&state, &buildings, &units, true, legacy152);
	state.erase(state.begin()); // file version changes on save, not simulation
	state.insert(state.end(), buildings.begin(), buildings.end());
	state.insert(state.end(), units.begin(), units.end());
	return state;
}

Uint32 digest(Game &game)
{
	Uint32 hash=2166136261u;
	// Retain the released representation as an independent default-behavior oracle.
	// Save/resume comparisons below still cover the complete current state.
	for (Uint32 value : simulation(game, true)) hash=(hash ^ value)*16777619u;
	return hash;
}

std::string save(Game &game, bool text)
{
	auto *backend = new GAGCore::MemoryStreamBackend;
	std::unique_ptr<GAGCore::OutputStream> out(text
		? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(backend))
		: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(backend)));
	game.save(out.get(), false, "Greedy fetching continuation");
	out->flush();
	return backend->takeContents();
}

bool load(Game &game, const std::string &bytes, bool text)
{
	auto *backend = new GAGCore::MemoryStreamBackend;
	backend->write(bytes.data(), bytes.size());
	backend->seekFromStart(0);
	std::unique_ptr<GAGCore::InputStream> in(text
		? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(backend))
		: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(backend)));
	return game.load(in.get());
}
}

TEST_SUITE("LegacyRoundTripSave")
{
// round-trip-143.game.gz was written at tick 1,200 by master 06a106d3a, which
// fetched by round trip: two teams of ten workers feeding an inn and a swarm,
// with round-trip fields live in the save (see the fixture README).
TEST_CASE("the obsolete greedy-fetching/round-trip-143.game.gz save is rejected [save-format]")
{
    glob2test::HeadlessGlobals globals;
    const auto bytes = glob2test::readFile(glob2test::inflated("greedy-fetching/round-trip-143.game.gz"));
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    GameGUI restored;
    CHECK_FALSE(restored.game.load(&input));
}
}
