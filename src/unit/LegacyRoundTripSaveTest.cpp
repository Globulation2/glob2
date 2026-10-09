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
std::vector<Uint32> simulation(Game &game)
{
	std::vector<Uint32> state, buildings, units;
	game.checkSum(&state, &buildings, &units, true);
	state.erase(state.begin()); // file version changes on save, not simulation
	state.insert(state.end(), buildings.begin(), buildings.end());
	state.insert(state.end(), units.begin(), units.end());
	return state;
}

Uint32 digest(Game &game)
{
	Uint32 hash=2166136261u;
	for (Uint32 value : simulation(game)) hash=(hash ^ value)*16777619u;
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
TEST_CASE("a format-143 save with live round-trip fields plays on greedily [golden][save-format]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.header=true});
	auto &g = world.game;
	const auto bytes = glob2test::readFile(glob2test::inflated("greedy-fetching/round-trip-143.game.gz"));
	REQUIRE(load(g, bytes, false));
	CHECK(g.mapHeader.getVersionMinor() == FILE_FORMAT_VERSION_AI_PIPELINE);
	CHECK(g.mapHeader.getVersionMinor() >= FILE_FORMAT_VERSION_ROUND_TRIP_FIELDS);
	CHECK(g.mapHeader.getVersionMinor() < FILE_FORMAT_VERSION_GREEDY_FETCHING);
	REQUIRE(g.stepCounter == 1200);
	// Older save state becomes a deterministic PCG stream once, never a fallback.
	for (int t=0; t<g.teamsCount(); ++t) {
		for (auto* unit : g.teams[t]->liveUnits.entries()) {
			EntityRandom expected;
			expected.initialize(g.gameHeader.getRandomSeed(), EntityRandom::Kind::Unit, unit->gid, unit->scriptIdentity);
			CHECK(unit->entityRandom == expected);
		}
		for (auto* building : g.teams[t]->liveBuildings.entries()) {
			EntityRandom expected;
			expected.initialize(g.gameHeader.getRandomSeed(), EntityRandom::Kind::Building, building->gid, building->scriptIdentity);
			CHECK(building->entityRandom == expected);
		}
	}

    WorldRandomStreams expectedWorld;
    expectedWorld.initialize(g.gameHeader.getRandomSeed());
    CHECK(g.map.worldRandom.streams == expectedWorld.streams);


	std::ostringstream trace;
	trace << "loaded " << std::hex << digest(g) << std::dec << '\n';
	std::string checkpoint[2];
	std::vector<Uint32> atCheckpoint;
	for (int tick=1200; tick<2200; ++tick)
	{
		world.step();
		trace << tick+1 << ' ' << std::hex << digest(g) << std::dec << '\n';
		if (tick+1 == 1700)
		{
			atCheckpoint = simulation(g);
			checkpoint[0] = save(g, false);
			checkpoint[1] = save(g, true);
		}
	}
	glob2test::expectGolden("greedy-fetching/round-trip-143-checksums.txt", trace.str());

	// The re-saved game uses the current format, which no longer carries
	// round-trip fields, and continues exactly as the uninterrupted run did.
	const std::string expected = trace.str().substr(trace.str().find("\n1701 "));
	for (bool text : {false, true})
	{
		glob2test::HeadlessGame resumed({.header=true});
		REQUIRE(load(resumed.game, checkpoint[text], text));
		CHECK(resumed.game.mapHeader.getVersionMinor() == VERSION_MINOR);
		CHECK(simulation(resumed.game) == atCheckpoint);
		std::ostringstream continued;
		for (int tick=1700; tick<2200; ++tick)
		{
			resumed.step();
			continued << '\n' << tick+1 << ' ' << std::hex << digest(resumed.game) << std::dec;
		}
		CHECK(continued.str() + "\n" == expected);
	}
}
}
