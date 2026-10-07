// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "engine/sim/snapshot/WorldSnapshot.h"

TEST_SUITE("TeamLiveLists")
{
TEST_CASE("live slot lists follow every slot assignment and order the snapshot records")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
	auto& game = world.game;
	auto* team = game.teams[0];
	const auto consistent = [&] {
		for (int t = 0; t < 2; ++t)
		{
			CHECK(game.teams[t]->liveUnits.matches(game.teams[t]->myUnits, Unit::MAX_COUNT));
			CHECK(game.teams[t]->liveBuildings.matches(game.teams[t]->myBuildings, Building::MAX_COUNT));
		}
	};
	consistent();
	REQUIRE(world.addBuilding("inn", 4, 4, 0, 0));
	auto* swarm = world.addBuilding("swarm", 12, 4, 0, 0);
	REQUIRE(swarm);
	for (int i = 0; i < 6; ++i) REQUIRE(world.addUnit(WORKER, 8 + i, 12, 0));
	REQUIRE(world.addUnit(EXPLORER, 8, 14, 0));
	REQUIRE(world.addUnit(WORKER, 20, 20, 1));
	consistent();
	CHECK(team->liveUnits.size() == 7); CHECK(team->liveBuildings.size() == 2);
	CHECK(std::is_sorted(team->liveUnits.slots().begin(), team->liveUnits.slots().end()));

	// Editor removal detaches; a slot freed in the middle keeps the order.
	REQUIRE(game.removeUnitAndBuildingAndFlags(9, 12, unsigned(Game::DEL_GROUND_UNIT)));
	consistent(); CHECK(team->liveUnits.size() == 6);
	REQUIRE(game.removeUnitAndBuildingAndFlags(12, 4, unsigned(Game::DEL_BUILDING)));
	consistent(); CHECK(team->liveBuildings.size() == 1);
	// The freed slot is reused by the next unit and reattached in place.
	REQUIRE(world.addUnit(WORKER, 9, 12, 0));
	consistent(); CHECK(team->liveUnits.size() == 7);

	// Death collection in the team step detaches too.
	team->liveUnits.entries()[3]->isDead = true;
	team->syncStep();
	consistent(); CHECK(team->liveUnits.size() == 6);

	// Snapshot records follow the lists in slot order with the same slot indices.
	const auto handle = SimulationSnapshot::capture(game, SimulationSnapshot::captureCatalog(game));
	REQUIRE(handle.entities);
	std::size_t records = 0;
	Uint32 previous = 0;
	for (std::size_t n = 0; n < team->liveUnits.size(); ++n)
	{
		const auto slot = team->liveUnits.slots()[n];
		const auto record = handle.entities->unitSlotIndices[slot];
		REQUIRE(record != SimulationSnapshot::Entities::NoRecord);
		CHECK(handle.entities->units[record].identity.gid == team->liveUnits.entries()[n]->gid);
		if (n) CHECK(record > previous);
		previous = record; ++records;
	}
	std::size_t indexed = 0;
	for (int i = 0; i < Unit::MAX_COUNT; ++i) indexed += handle.entities->unitSlotIndices[i] != SimulationSnapshot::Entities::NoRecord;
	CHECK(indexed == records);

	// Direct slot writes (loaders, fixtures) are reconciled by a rebuild.
	team->liveUnits.clear(); team->liveBuildings.clear();
	CHECK_FALSE(team->liveUnits.matches(team->myUnits, Unit::MAX_COUNT));
	team->rebuildLiveLists();
	consistent();
	CHECK(team->integrity());
}
}
