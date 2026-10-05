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

#include "Glob2Test.h"
#include <string>
#include <cstdio>
#include <memory>
#include <SDL3/SDL.h>
#include "TextStream.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "GameHeader.h"
#include "Version.h"

using namespace GAGCore;

namespace {


void check(bool ok, const char* what)
{
	CHECK_MESSAGE(ok, (what));
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
	header.setResourceGrowthDisabled(true);
	header.setResourceScarcityLevel(2);
	header.setInstantConstructionEnabled(true);
	header.setStockpileStartLevel(3);
	header.setHungerDisabled(true);
	header.setUnitUpgradesDisabled(true);
	header.setGlassCannonLevel(2);
	header.setUnitsFearless(true);
	header.setPermadeathDisabled(true);
	header.setPeacefulModeEnabled(true);
	header.setBuildingHpLevel(1);
	header.getExperiments().set(ExperimentId::GuardAreaBalancing);
	for (int i = 0; i < 4; ++i)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "Player %d", i);
		header.getBasePlayer(i) = BasePlayer(i, name, i % 2, BasePlayer::P_IP);
		header.getBasePlayer(i).playerID = 1000 + i;
		header.setAIConfig(i, "swarmWorkerCap=" + std::to_string(i+4) + "\n");
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
		    || pa.teamNumber != pb.teamNumber || pa.playerID != pb.playerID
		    || a.getAIConfig(i) != b.getAIConfig(i))
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
	for (int team = 0; team < Team::MAX_COUNT; ++team)
		check(loaded.getAllyTeamNumber(team) == original.getAllyTeamNumber(team),
		      "full: indexed alliance slots preserved");
	check(loaded.getRandomSeed() == 0xCAFEBABE, "full: seed preserved");
	check(loaded.isMapDiscovered(), "full: mapDiscovered preserved");
	check(loaded.areAllyTeamsFixed(), "full: allyTeamsFixed preserved");
	check(loaded.isResourceGrowthDisabled(), "full: resourceGrowthDisabled preserved");
	check(loaded.getResourceScarcityLevel() == 2, "full: resourceScarcityLevel preserved");
	check(loaded.isInstantConstructionEnabled(), "full: instantConstruction preserved");
	check(loaded.getStockpileStartLevel() == 3, "full: stockpileStartLevel preserved");
	check(loaded.isHungerDisabled(), "full: hungerDisabled preserved");
	check(loaded.isUnitUpgradesDisabled(), "full: unitUpgradesDisabled preserved");
	check(loaded.getGlassCannonLevel() == 2, "full: glassCannonLevel preserved");
	check(loaded.isUnitsFearless(), "full: unitsFearless preserved");
	check(loaded.isPermadeathDisabled(), "full: permadeathDisabled preserved");
	check(loaded.isPeacefulModeEnabled(), "full: peacefulMode preserved");
	check(loaded.getBuildingHpLevel() == 1, "full: buildingHpLevel preserved");
	check(loaded.hasExperiment(ExperimentId::GuardAreaBalancing), "full: experiments preserved");
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

void testBinaryHeaderFormsAndLegacy()
{
	for (int form=0; form<3; ++form)
	{
		GameHeader original=makeFixtureHeader();
		auto *memory=new MemoryStreamBackend;
		BinaryOutputStream out(memory);
		if (form==0) original.save(&out);
		else if (form==1) original.savePlayerInfo(&out);
		else original.saveWithoutPlayerInfo(&out);
		out.flush();
		memory->seekFromStart(0);
		BinaryInputStream in(new MemoryStreamBackend(*memory));
		GameHeader loaded;
		const bool ok=form==0 ? loaded.load(&in,VERSION_MINOR)
			: form==1 ? loaded.loadPlayerInfo(&in,VERSION_MINOR) : loaded.loadWithoutPlayerInfo(&in,VERSION_MINOR);
		check(ok && loaded.getAIConfig(0)==original.getAIConfig(0)
			&& loaded.getAIConfig(1)==original.getAIConfig(1), "binary full/partial resolved player configuration");
		// Version 100 ended immediately before the new extension. Truncate a
		// current binary fixture at that boundary and require an exact old read.
		size_t extension=4;
		for(int p=0;p<Team::MAX_COUNT;++p) extension+=4+original.getAIConfig(p).size();
		// The custom-game rule bytes (version 102 on) follow it in the full and
		// player-less forms.
		const size_t ruleBytes=5+6; // economy (102), combat (103)
		// The experiments list (version 124) follows the rules; measure it rather
		// than hard-coding its key lengths.
		size_t experimentBytes=0;
		{
			auto *section=new MemoryStreamBackend;
			BinaryOutputStream sectionOut(section);
			original.getExperiments().save(&sectionOut);
			sectionOut.flush();
			experimentBytes=section->getPosition();
		}
		if (form != 1)
			extension += ruleBytes + experimentBytes + 1;
		memory->seekFromEnd(0);
		const size_t legacySize=memory->getPosition()-extension;
		auto *oldBytes=new MemoryStreamBackend(memory->getBuffer(),legacySize);
		oldBytes->seekFromStart(0);
		BinaryInputStream old(oldBytes);
		loaded.setAIConfig(0,"stale");
		const bool legacy=form==0 ? loaded.load(&old,100)
			: form==1 ? loaded.loadPlayerInfo(&old,100) : loaded.loadWithoutPlayerInfo(&old,100);
		check(legacy && loaded.getAIConfig(0).empty() && oldBytes->getPosition()==legacySize,
			"version 100 full/partial header loads without reading extension bytes");
		if (form!=1)
		{
			// Version 101 ended before the custom-game rule bytes: its headers load
			// exactly, with every rule off.
			const size_t v101Size = memory->getPosition() - ruleBytes - experimentBytes - 1;
			auto *v101Bytes=new MemoryStreamBackend(memory->getBuffer(),v101Size);
			v101Bytes->seekFromStart(0);
			BinaryInputStream v101(v101Bytes);
			GameHeader ruled;
			const bool read=form==0 ? ruled.load(&v101,101) : ruled.loadWithoutPlayerInfo(&v101,101);
			check(read && v101Bytes->getPosition()==v101Size && ruled.getAIConfig(0)==original.getAIConfig(0)
				&& !ruled.isResourceGrowthDisabled() && ruled.getResourceScarcityLevel()==0
				&& !ruled.isInstantConstructionEnabled() && ruled.getStockpileStartLevel()==0
				&& !ruled.isHungerDisabled() && ruled.getExperiments().empty(),
				"version 101 header loads without rule bytes, rules off");
		}
	}
}

}  // namespace

TEST_SUITE("GameHeaderTextSaveLoad")
{
	TEST_CASE("FullRoundTrip") { testFullRoundTrip(); }
	TEST_CASE("PlayerInfoRoundTrip") { testPlayerInfoRoundTrip(); }
	TEST_CASE("BinaryHeaderFormsAndLegacy") { testBinaryHeaderFormsAndLegacy(); }
	TEST_CASE(
		"scheduled building configuration survives both header forms and rejects invalid delays")
	{
		for (unsigned delay : {2, 4, 8})
			for (int form : {0, 1})
			{
				GameHeader original;
				original.getExperiments().set(ExperimentId::BuildingGradientPipeline);
				original.setBuildingGradientDelay(delay);
				auto *bytes = new MemoryStreamBackend;
				BinaryOutputStream out(bytes);
				if (form == 0)
					original.save(&out);
				else
					original.saveWithoutPlayerInfo(&out);
				out.flush();
				auto data = bytes->takeContents();
				BinaryInputStream in(new MemoryStreamBackend(std::move(data)));
				GameHeader loaded;
				REQUIRE((form == 0 ? loaded.load(&in, VERSION_MINOR)
								   : loaded.loadWithoutPlayerInfo(&in, VERSION_MINOR)));
				CHECK(loaded.hasExperiment(ExperimentId::BuildingGradientPipeline));
				CHECK(loaded.getBuildingGradientDelay() == delay);
			}
		GameHeader header;
		CHECK_FALSE(header.hasExperiment(ExperimentId::BuildingGradientPipeline));
		CHECK(header.getBuildingGradientDelay() == 4);
		for (unsigned delay : {0, 1, 3, 5, 6, 7, 9, 255})
			CHECK_THROWS_AS(header.setBuildingGradientDelay(delay), std::invalid_argument);
	}
}
