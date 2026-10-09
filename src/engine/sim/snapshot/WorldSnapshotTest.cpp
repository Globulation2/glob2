// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "WorldSnapshot.h"
#include "SnapshotStore.h"
#include "Building.h"
#include "Unit.h"
#include "Team.h"
#include "ai/observation/AIWorldView.h"
#include <limits>
#include <span>
#include <atomic>
#include <thread>
#include <cstring>
#include <nlohmann/json.hpp>
#include <ThreadSupport.h>

TEST_SUITE("WorldSnapshot")
{
    TEST_CASE("shared entity indices preserve empty slots and canonical record addresses")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
        REQUIRE(fixture.addBuilding("inn",4,4,0,0));
        REQUIRE(fixture.addUnit(WORKER,12,12,0));
        REQUIRE(fixture.addUnit(WORKER,14,14,1));
        const auto captured=SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game));
        AIEngine::AIWorldView world(captured);
        for(int team=0;team<2;++team) {
            const auto buildings=world.buildingSlots(team), invalidBuildings=world.buildingSlots(-1);
            const auto units=world.unitSlots(team);
            CHECK(buildings.size()==std::size_t(Building::MAX_COUNT)); CHECK(units.size()==std::size_t(Unit::MAX_COUNT));
            CHECK(invalidBuildings.size()==0); CHECK(world.unitSlots(2).size()==0);
            std::size_t slot=0;
            for(const auto* unit:units) {
                CHECK(unit==world.unitAtSlot(Uint16(team*Unit::MAX_COUNT+slot)));
                CHECK(bool(unit)==bool(fixture.game.teams[team]->myUnits[slot]));
                if(unit) {
                    CHECK(unit==world.unit(unit->identity));
                    CHECK(world.unit({unit->identity.gid,unit->identity.generation+1})==nullptr);
                }
                ++slot;
            }
            CHECK(slot==std::size_t(Unit::MAX_COUNT));
            for(std::size_t i=0;i<buildings.size();++i) {
                const auto* building=buildings[i];
                CHECK(building==world.buildingAtSlot(Uint16(team*Building::MAX_COUNT+i)));
                CHECK(bool(building)==bool(fixture.game.teams[team]->myBuildings[i]));
                if(building) CHECK(building==world.building(building->identity));
            }
        }
        CHECK(world.buildingAtSlot(0xffff)==nullptr); CHECK(world.unitAtSlot(0xffff)==nullptr);
    }
    TEST_CASE("snapshot capability tables share authoritative immutable catalog data")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
        const auto captured=SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game));
        const auto& authoritative=fixture.game.buildingCapabilities();
        CHECK(captured.catalogs->capabilities==authoritative.frozenTables());
        AIEngine::AIWorldView world(captured);
        for(unsigned i=0;i<unsigned(AIPlanning::BuildingIntent::Count);++i) {
            const auto intent=AIPlanning::BuildingIntent(i);
            CHECK(&world.capabilities().providers(intent)==&authoritative.providers(intent));
            CHECK(&world.capabilities().placements(intent)==&authoritative.placements(intent));
            CHECK(&world.capabilities().placementsByCost(intent)==&authoritative.placementsByCost(intent));
            for(std::size_t type=0;type<world.catalog->size();++type) {
                CHECK(world.capabilities().intentMask(type)==authoritative.intentMask(type));
                CHECK(world.capabilities().lineageRoot(type)==authoritative.lineageRoot(type));
                CHECK(world.capabilities().lineagePosition(type)==authoritative.lineagePosition(type));
                for(int unit=-1;unit<NB_UNIT_TYPE;++unit)
                    CHECK(world.capabilities().matches(type,intent,unit)==authoritative.matches(type,intent,unit));
            }
        }
    }
    TEST_CASE("on-demand construction feasibility matches live helpers and remains frozen")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true}};
        REQUIRE(fixture.addBuilding("inn",4,4,1));
        REQUIRE(fixture.addBuilding("school",14,4));
        REQUIRE(fixture.addBuilding("warflag",24,24));
        REQUIRE(fixture.addBuilding("stonewall",26,4));
        for(int phase=0;phase<3;++phase) {
            if(phase==1) fixture.game.map.setResourceByIndex(4,4,STONE,1);
            if(phase==2) fixture.game.gameHeader.setUnitUpgradesDisabled(true);
            const auto captured=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
            std::vector<std::array<bool,3>> expected;
            for(const auto& record:captured->buildings) {
                auto* live=fixture.game.teams[record.team]->myBuildings[Building::GIDtoID(record.identity.gid)];
                REQUIRE(live);
                expected.push_back({live->isUpgradeAvailable(),live->isHardSpaceForBuildingSite(Building::UPGRADE),live->isHardSpaceForBuildingSite(Building::REPAIR)});
                CHECK(captured->isUpgradeAvailable(record)==expected.back()[0]);
                CHECK(captured->isHardSpaceForBuildingSite(record,true)==expected.back()[1]);
                CHECK(captured->isHardSpaceForBuildingSite(record,false)==expected.back()[2]);
            }
            fixture.game.gameHeader.setUnitUpgradesDisabled(!fixture.game.gameHeader.isUnitUpgradesDisabled());
            for(std::size_t i=0;i<captured->buildings.size();++i) {
                CHECK(captured->isUpgradeAvailable(captured->buildings[i])==expected[i][0]);
                CHECK(captured->isHardSpaceForBuildingSite(captured->buildings[i],true)==expected[i][1]);
                CHECK(captured->isHardSpaceForBuildingSite(captured->buildings[i],false)==expected[i][2]);
            }
            fixture.game.gameHeader.setUnitUpgradesDisabled(false);
        }
    }
    TEST_CASE("unit snapshots share authoritative scalar state and remain frozen")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
        auto* unit=fixture.addUnit(WORKER,12,12,0); REQUIRE(unit);
        unit->jobTimer=17; unit->terrainHealthRemainder=9;
        auto captured=SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game));
        REQUIRE(captured.entities->units.size()==1);
        const auto& observed=captured.entities->units.front();
        static_assert(std::is_base_of_v<UnitState,Unit>);
        static_assert(std::is_base_of_v<UnitState,SimulationSnapshot::UnitView>);
        CHECK(observed.typeNum==unit->typeNum); CHECK(observed.posX==unit->posX); CHECK(observed.posY==unit->posY);
        CHECK(observed.action==unit->action); CHECK(observed.needToRecheckMedical==unit->needToRecheckMedical);
        CHECK(observed.serviceResourcesReserved==unit->serviceResourcesReserved);
        CHECK(observed.trigHP==unit->trigHP); CHECK(observed.trigHungryCarrying==unit->trigHungryCarrying);
        CHECK(observed.delta==unit->delta); CHECK(observed.jobTimer==17); CHECK(observed.terrainHealthRemainder==9);
        for(int i=0;i<NB_ABILITY;++i) {
            CHECK(observed.performance[i]==unit->performance[i]); CHECK(observed.level[i]==unit->level[i]);
            CHECK(observed.canLearn[i]==unit->canLearn[i]);
        }
        const int oldLevel=observed.level[WALK]; unit->level[WALK]=oldLevel+1; unit->jobTimer=31;
        CHECK(observed.level[WALK]==oldLevel); CHECK(observed.jobTimer==17);
    }
    TEST_CASE("map snapshots bulk copy authoritative records without translation")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
        auto& map=fixture.game.map;
        SimulationSnapshot::Store store;
        for(Uint8 flag : {Uint8(0),Uint8(1),Uint8(2),Uint8(255)}) {
            map.setResourcesGrow(5,5,flag);
            ++fixture.game.stepCounter;
            const auto observed=store.captureBoundary(fixture.game,SimulationSnapshot::All);
            const auto index=map.coordToIndex(5,5);
            CHECK(observed.resources->cells[index].mayGrow==flag);
            CHECK(std::memcmp(observed.resources->cells.data(),map.resourceState().data(),map.resourceState().size_bytes())==0);
            CHECK(std::memcmp(observed.occupancy->cells.data(),map.occupancyState().data(),map.occupancyState().size_bytes())==0);
            CHECK(std::memcmp(observed.areas->cells.data(),map.areaState().data(),map.areaState().size_bytes())==0);
            CHECK(std::memcmp(observed.terrain->cellRules.data(),map.cellRuleState().data(),map.cellRuleState().size_bytes())==0);
            CHECK(std::equal(observed.terrain->vertices->begin(),observed.terrain->vertices->end(),map.vertexTerrainState().begin()));
            for(std::size_t i=0;i<map.cellCount();++i) {
                CHECK(observed.visibility->discovered[i]==map.mapDiscovered[i]);
                CHECK(observed.visibility->visible[i]==(map.fogOfWar?map.fogOfWar[i]:0));
            }
        }
    }

	TEST_CASE("narrow component queries preserve combined tile values and projection defaults")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		fixture.game.map.setResourceByIndex(20, 20,WHEAT, 1);
		fixture.game.map.addForbidden(20, 20, 0);
		SimulationSnapshot::Store store;
		const auto captured=store.captureBoundary(fixture.game,SimulationSnapshot::All);
		using namespace SimulationSnapshot;
		for (Requirements mask : {All, Requirements(0), bit(Component::Terrain), bit(Component::Resources),
			bit(Component::Occupancy), bit(Component::Areas), bit(Component::Visibility)})
		{
			const auto view=captured.project(mask);
			for (std::size_t i=0;i<1024;++i)
			{
				const auto full=view.tileAt(i); const auto rule=view.cellRuleAt(i);
				const auto resource=view.resourceAt(i); const auto occupancy=view.occupancyAt(i);
				const auto areas=view.areasAt(i); const auto visibility=view.visibilityAt(i);
				CHECK(rule==full.cellRule);
				CHECK(resource.resource.getUint32()==full.resource.getUint32()); CHECK(resource.fertility==full.fertility);
				CHECK(resource.mayGrow==full.resourcesMayGrow); CHECK(view.canPaintFarmAt(i)==full.canPaintFarm);
				CHECK(occupancy.building==full.building); CHECK(occupancy.groundUnit==full.groundUnit);
				CHECK(occupancy.airUnit==full.airUnit); CHECK(occupancy.immobileUnit==full.immobileUnit);
				CHECK(areas.forbidden==full.forbidden); CHECK(areas.guard==full.guard);
				CHECK(areas.clear==full.clear); CHECK(areas.farm==full.farm);
				CHECK(visibility.discovered==full.discovered); CHECK(visibility.visible==full.visible);
			}
			CHECK_THROWS_AS(view.cellRuleAt(1024),std::out_of_range);
			CHECK_THROWS_AS(view.resourceAt(1024),std::out_of_range);
			CHECK_THROWS_AS(view.occupancyAt(1024),std::out_of_range);
			CHECK_THROWS_AS(view.areasAt(1024),std::out_of_range);
			CHECK_THROWS_AS(view.visibilityAt(1024),std::out_of_range);
			CHECK_THROWS_AS(view.canPaintFarmAt(1024),std::out_of_range);
		}
	}
	TEST_CASE("coordinate wrapping preserves signed edges for power of two and general dimensions")
	{
		const std::array coordinates{std::numeric_limits<int>::min(),-4097,-33,-1,0,1,33,std::numeric_limits<int>::max()};
		const auto expected=[](int coordinate,int size){const auto value=coordinate%size;return value<0?value+size:value;};
		for (int width : {1,2,32,12})
		{
			SimulationSnapshot::Handle lease; lease.width=width; lease.height=7;
			AIEngine::AIWorldView view(std::move(lease));
			for (int x : coordinates) for (int y : coordinates)
			{
				CHECK(view.normalizeX(x)==expected(x,width)); CHECK(view.normalizeY(y)==expected(y,7));
				CHECK(view.tileIndex(x,y)==std::size_t(expected(y,7))*width+expected(x,width));
			}
		}
		for (const auto dimensions : {std::pair{0,7},std::pair{7,0},std::pair{-1,7},std::pair{7,-1}})
		{
			SimulationSnapshot::Handle invalid; invalid.width=dimensions.first; invalid.height=dimensions.second;
			CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(invalid)),std::logic_error);
		}
	}
	TEST_CASE("AI boundary validates each captured map array before fast reads")
	{
		glob2test::HeadlessGlobals globals;
		using namespace SimulationSnapshot;
		const auto geometry=[] { Handle h; h.width=32; h.height=32; return h; };
		const auto checkCells=[&]<class Layer>(Component component, std::shared_ptr<const Layer> Handle::*member)
		{
			for (std::size_t count : {1023u,1024u,1025u})
			{
				auto h=geometry(); h.requirements=bit(component);
				auto layer=std::make_shared<Layer>(); layer->cells.resize(count); h.*member=layer;
				if (count==1024) CHECK_NOTHROW(AIEngine::AIWorldView(std::move(h)));
				else CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(h)),std::logic_error);
			}
		};
		for (Component component : {Component::Terrain,Component::Resources,Component::Occupancy,Component::Areas,Component::Visibility})
		{
			auto missing=geometry(); missing.requirements=bit(component);
			CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(missing)),std::logic_error);
		}
		checkCells(Component::Resources,&Handle::resources);
		checkCells(Component::Occupancy,&Handle::occupancy);
		checkCells(Component::Areas,&Handle::areas);
		for (std::size_t count : {1023u,1024u,1025u}) for (bool discovered : {false,true}) {
            auto h=geometry(); h.requirements=bit(Component::Visibility);
            auto layer=std::make_shared<Visibility>(); layer->discovered.resize(discovered?count:1024); layer->visible.resize(discovered?1024:count); h.visibility=layer;
            if (count==1024) CHECK_NOTHROW(AIEngine::AIWorldView(std::move(h)));
            else CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(h)),std::logic_error);
        }
		const auto rules=std::make_shared<const CellRuleTable>(TerrainRegistry::builtins(),ResourceRegistry::builtins());
		for (std::size_t count : {1023u,1024u,1025u}) for (bool malformedVertices : {false,true})
		{
			auto h=geometry(); h.requirements=bit(Component::Terrain);
			auto layer=std::make_shared<Terrain>(); layer->rules=rules;
			layer->vertices=std::make_shared<const std::vector<TerrainType>>(malformedVertices?count:1024,GRASS);
			layer->cellRules.resize(malformedVertices?1024:count); h.terrain=layer;
			if (count==1024) CHECK_NOTHROW(AIEngine::AIWorldView(std::move(h)));
			else CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(h)),std::logic_error);
		}
		auto missingIdentity=geometry(); missingIdentity.requirements=bit(Component::Terrain);
		auto terrain=std::make_shared<Terrain>(); terrain->rules=rules; terrain->cellRules.resize(1024); missingIdentity.terrain=terrain;
		CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(missingIdentity)),std::logic_error);
		auto emptyGrowth=geometry(); emptyGrowth.requirements=bit(Component::Growth);
		emptyGrowth.growth=std::make_shared<const Fertility::GrowthCache>();
		CHECK_THROWS_AS(AIEngine::AIWorldView(std::move(emptyGrowth)),std::logic_error);
		// Partial projections validate only their captured arrays; geometry itself
		// remains usable without invoking a raw reader for an omitted component.
		CHECK_NOTHROW(AIEngine::AIWorldView(geometry()));
	}
	TEST_CASE("AI fast scalar reads preserve checked values and frozen lease isolation")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5,.hDec=5,.teams=1,.clearImmobile=true,.loadDefaultRace=true}};
		auto& game=fixture.game; auto& map=game.map;
		map.setResourceByIndex(20,20,WHEAT,1); map.setResourceAmount(map.coordToIndex(20,20),1); map.setResourcesGrow(20,20,1);
		map.setFertility(20,20,123); map.setGroundUnit(20,20,7); map.setAirUnit(20,20,8);
		map.addForbidden(20,20,0); map.addGuardArea(20,20,0); map.addClearArea(20,20,0);
		map.setMapDiscovered(20,20,1u);
		SimulationSnapshot::Store store;
		const auto captured=store.captureBoundary(game,SimulationSnapshot::All);
		AIEngine::AIWorldView original(captured);
		for (std::size_t i=0;i<1024;++i)
		{
			CHECK(original.cellRuleAt(i)==captured.cellRuleAt(i));
			CHECK(sameTerrainProperties(original.terrainPropertiesAt(i),captured.terrainPropertiesAt(i)));
			const auto resource=original.resourceAt(i), checkedResource=captured.resourceAt(i);
			CHECK(resource.resource.getUint32()==checkedResource.resource.getUint32());
			CHECK(resource.fertility==checkedResource.fertility); CHECK(resource.mayGrow==checkedResource.mayGrow);
			const auto occupancy=original.occupancyAt(i), checkedOccupancy=captured.occupancyAt(i);
			CHECK(occupancy.building==checkedOccupancy.building); CHECK(occupancy.groundUnit==checkedOccupancy.groundUnit);
			CHECK(occupancy.airUnit==checkedOccupancy.airUnit); CHECK(occupancy.immobileUnit==checkedOccupancy.immobileUnit);
			const auto area=original.areasAt(i), checkedArea=captured.areasAt(i);
			CHECK(area.forbidden==checkedArea.forbidden); CHECK(area.guard==checkedArea.guard);
			CHECK(area.clear==checkedArea.clear); CHECK(area.farm==checkedArea.farm);
			const auto visibility=original.visibilityAt(i), checkedVisibility=captured.visibilityAt(i);
			CHECK(visibility.discovered==checkedVisibility.discovered); CHECK(visibility.visible==checkedVisibility.visible);
			CHECK(original.canPaintFarmAt(i)==captured.canPaintFarmAt(i));
		}
		const auto index=original.tileIndex(-12,52);
		CHECK(index==map.coordToIndex(20,20));
		map.setResourceAmount(index,2); map.setFertility(20,20,456); map.setResourcesGrow(20,20,0);
		map.setGroundUnit(20,20,9); map.setAirUnit(20,20,10);
		map.removeForbidden(20,20,0); map.setMapDiscovered(20,20,2u);
		++game.stepCounter;
		AIEngine::AIWorldView changed(store.captureBoundary(game,SimulationSnapshot::All));
		CHECK(original.resourceAt(index).resource.amount==1); CHECK(changed.resourceAt(index).resource.amount==2);
		CHECK(original.resourceAt(index).fertility==123); CHECK(changed.resourceAt(index).fertility==456);
		CHECK(original.resourceAt(index).mayGrow); CHECK_FALSE(changed.resourceAt(index).mayGrow);
		CHECK(original.occupancyAt(index).groundUnit==7); CHECK(changed.occupancyAt(index).groundUnit==9);
		CHECK(original.occupancyAt(index).airUnit==8); CHECK(changed.occupancyAt(index).airUnit==10);
		CHECK((original.areasAt(index).forbidden&1u)==1u); CHECK((changed.areasAt(index).forbidden&1u)==0u);
		CHECK((original.visibilityAt(index).discovered&2u)==0u); CHECK((changed.visibilityAt(index).discovered&2u)==2u);
		CHECK((original.visibilityAt(index).visible&2u)==0u); CHECK((changed.visibilityAt(index).visible&2u)==2u);
	}
	TEST_CASE("component leases and weak control blocks outlive the owning store")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1}};
		SimulationSnapshot::Handle held;
		std::weak_ptr<const SimulationSnapshot::Resources> weak;
		{
			SimulationSnapshot::Store store;
			held = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Resources));
			weak = held.resources;
		}
		REQUIRE(held.resources);
		CHECK(held.resources->cells.size() == 1024);
		held = {};
		CHECK(weak.expired());
		// Consumers own the buffer's control block directly; the pool's own
		// reference died with the store.
		weak.reset();
	}
	TEST_CASE("worker final lease release synchronizes owner buffer reuse")
	{
		if constexpr (GAGCore::ThreadSupport::available)
		{
			SimulationSnapshot::BufferPool<std::vector<int>> pool;
			Uint64 allocations = 0;
			for (int iteration = 0; iteration < 32; ++iteration)
			{
				auto lease = pool.acquire(allocations);
				lease->assign(128, iteration);
				auto* identity = lease.get();
				std::atomic<bool> released{false};
				int sum = 0;
				auto worker = GAGCore::ThreadSupport::launch([lease = std::move(lease), &released, &sum]() mutable {
					for (int value : *lease) sum += value;
					lease.reset();
					released.store(true, std::memory_order_relaxed);
				});
				// Relaxed polling deliberately supplies no read/write barrier. Only
				// the pool's acquire fence orders the worker's reads before this write.
				while (!released.load(std::memory_order_relaxed)) std::this_thread::yield();
				auto reused = pool.acquire(allocations);
				CHECK(reused.get() == identity);
				reused->assign(128, -1);
				worker.join();
				CHECK(sum == 128 * iteration);
			}
			CHECK(allocations == 1);
		}
	}

	TEST_CASE("catalog refresh survives intervening projections and rules reuse independently")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
		auto& game=fixture.game;
		SimulationSnapshot::Store store;
		const auto first=store.captureBoundary(game,SimulationSnapshot::All);
		game.gameHeader.setUnitUpgradesDisabled(true);
		++game.stepCounter;
		store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		++game.stepCounter;
		const auto refreshed=store.captureBoundary(game,SimulationSnapshot::All);
		CHECK(refreshed.catalogs->buildings!=first.catalogs->buildings);
		++game.stepCounter;
		const auto rules=store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Rules));
		CHECK(rules.rules==refreshed.rules);
		++game.stepCounter;
		const auto nextRules=store.captureBoundary(game,SimulationSnapshot::bit(SimulationSnapshot::Component::Rules));
		CHECK(nextRules.rules==rules.rules);
	}
	TEST_CASE("projected terrain lease cannot retain unrelated world components")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto all = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
		std::weak_ptr<const SimulationSnapshot::Entities> entities = all.entities;
		auto terrain = all.project(SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		CHECK_FALSE(terrain.entities); CHECK_FALSE(terrain.visibility); CHECK_FALSE(terrain.teams);
		all = {};
		CHECK(entities.expired()); REQUIRE(terrain.terrain);
		CHECK_THROWS_AS(terrain.project(SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)), std::invalid_argument);
	}
	TEST_CASE("component mutation generations preserve isolation and reuse unchanged arrays")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		const auto catalog = SimulationSnapshot::captureCatalog(game);
		auto initial = SimulationSnapshot::capture(game, catalog);
		auto unchanged = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &initial);
		CHECK(unchanged.resources == initial.resources); CHECK(unchanged.occupancy == initial.occupancy);
		CHECK(unchanged.areas == initial.areas); CHECK(unchanged.visibility == initial.visibility);
		game.map.setResourceByIndex(5, 5,WHEAT, 1);
		auto changed = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &unchanged);
		CHECK(changed.resources != initial.resources); CHECK(changed.areas == initial.areas);
		CHECK(initial.tileAt(game.map.coordToIndex(5, 5)).resource.type == NO_RES_TYPE);
		CHECK(changed.tileAt(game.map.coordToIndex(5, 5)).resource.type == WHEAT);
		game.map.addForbidden(5, 5, 0);
		auto areaChanged = SimulationSnapshot::capture(game, catalog, SimulationSnapshot::All, &changed);
		CHECK(areaChanged.areas != changed.areas); CHECK(areaChanged.resources == changed.resources);
	}
	TEST_CASE("no-op map writes reuse components while changed values remain isolated")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
		auto& game=fixture.game; auto& map=game.map;
		const auto index=map.coordToIndex(20,20);
		map.setResourceByIndex(20,20,WHEAT,1); map.setResourcesGrow(20,20,1);
		map.setMapDiscovered(20,20,1u);
		SimulationSnapshot::Store store;
		const auto next=[&] { ++game.stepCounter; return store.captureBoundary(game,SimulationSnapshot::All); };
		const auto original=next();
		map.setGroundUnit(20,20,map.getGroundUnit(20,20));
		map.setAirUnit(20,20,map.getAirUnit(20,20));
		map.setResourceAmount(index,map.getResource(index).amount);
		map.setFertility(20,20,map.getTile(index).fertility);
		map.setResourcesGrow(20,20,1); // Exact authoritative byte is unchanged.
		auto& inactive=map.fogOfWar==map.fogOfWarA.data()?map.fogOfWarB:map.fogOfWarA;
		inactive[index]=0; // Fixture setup: this plane is not exposed by the snapshot.
		map.setMapDiscovered(20,20,1u);
		const auto unchanged=next();
		CHECK(unchanged.resources==original.resources); CHECK(unchanged.occupancy==original.occupancy);
		CHECK(unchanged.visibility==original.visibility);
		CHECK(map.getTile(index).canResourcesGrow==1); CHECK((inactive[index]&1u)==1u);
		map.setResourcesGrow(20,20,2); // Both values allow growth, but their stored bytes differ.
		const auto rawChanged=next();
		CHECK(rawChanged.resources!=unchanged.resources);
		CHECK(rawChanged.resourceAt(index).mayGrow==2);
		CHECK(original.resourceAt(index).mayGrow==1);
		map.setResourceAmount(index,Uint8(map.getResource(index).amount+1));
		map.setFertility(20,20,Uint16(map.getTile(index).fertility+1));
		map.setResourcesGrow(20,20,0);
		map.setGroundUnit(20,20,7); map.setAirUnit(20,20,8);
		map.setMapDiscovered(20,20,2u);
		const auto changed=next();
		CHECK(changed.resources!=original.resources); CHECK(changed.occupancy!=original.occupancy);
		CHECK(changed.visibility!=original.visibility);
		CHECK(original.resourceAt(index).mayGrow); CHECK_FALSE(changed.resourceAt(index).mayGrow);
		CHECK(original.occupancyAt(index).groundUnit==NOGUID); CHECK(changed.occupancyAt(index).groundUnit==7);
		CHECK(changed.occupancyAt(index).airUnit==8);
		CHECK((original.visibilityAt(index).visible&2u)==0); CHECK((changed.visibilityAt(index).visible&2u)==2u);
		map.switchFogOfWar();
		const auto switched=next();
		CHECK(switched.visibility!=changed.visibility);
		CHECK(switched.visibilityAt(index).visible==map.fogOfWar[index]);
		CHECK(switched.resources==changed.resources); CHECK(switched.occupancy==changed.occupancy);
	}
	TEST_CASE("capture requirements exclude arrays that no scheduled consumer requests")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		SimulationSnapshot::Store store;
		auto terrain = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		REQUIRE(terrain.terrain); CHECK_FALSE(terrain.entities); CHECK_FALSE(terrain.resources); CHECK_FALSE(terrain.growth);
		CHECK(store.metrics.captures == 1); CHECK(store.metrics.bytesCopied == 1024 * sizeof(Uint16));
		store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
		CHECK(store.metrics.captures == 1);
	}
	TEST_CASE("vertex changes preserve retained terrain and reuse unchanged versions")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1}};
		auto& game = fixture.game;
		auto& map = game.map;
		SimulationSnapshot::Store store;
		const auto required = SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain);
		map.setVertexTerrain(4, 5, GRASS);
		const auto original = store.captureBoundary(game, required);
		const auto index = map.coordToIndex(4, 5);
		map.setVertexTerrain(4, 5, WATER);
		store.invalidateBoundary();
		const auto changed = store.captureBoundary(game, required);
		CHECK(original.terrain != changed.terrain);
		CHECK((*original.terrain->vertices)[index] == GRASS);
		CHECK((*changed.terrain->vertices)[index] == WATER);
		CHECK(original.terrain->cellRules[index] == GRASS);
		CHECK(changed.terrain->cellRules[index] != GRASS);
		CHECK_NOTHROW(SimulationSnapshot::verifyCapture(game, changed));
		map.setVertexTerrain(4, 5, WATER);
		store.invalidateBoundary();
		CHECK(store.captureBoundary(game, required).terrain == changed.terrain);
		store.reset();
		CHECK((*original.terrain->vertices)[index] == GRASS);
		CHECK((*changed.terrain->vertices)[index] == WATER);
	}
	TEST_CASE("warm component buffers stop allocating at a fixed population")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		auto* building = fixture.addBuilding("inn", 4, 4, 0, 0);
		auto* worker = fixture.addUnit(WORKER, 12, 12, 0);
		REQUIRE(building); REQUIRE(worker);
		building->unitsWorking.push_back(worker);
		game.map.setResourceByIndex(20, 20,WHEAT, 1);
		game.map.getMaterialGradientSlot(0, materialIndex(MaterialId::Food), 0);
		SimulationSnapshot::Store store;
		auto mutateAndCapture = [&] {
			++game.stepCounter;
			game.map.setFertility(20, 20, Uint16(game.stepCounter));
			game.map.setGroundUnit(25, 25, NOGUID);
			game.map.addGuardArea(20, 20, 0);
			game.map.setMapDiscovered(20, 20, game.teams[0]->me);
			game.map.updateMaterialGradient(0, Uint8(materialIndex(MaterialId::Food)), 0);
			building->priority = int(game.stepCounter);
			return store.captureBoundary(game, SimulationSnapshot::All);
		};
		for (int i = 0; i < 6; ++i) mutateAndCapture();
		const auto warmedAllocations = store.metrics.allocations;
		const auto warmedMemory = store.memoryMetrics();
		REQUIRE(warmedMemory.allocatedBuffers > 0);
		const auto copied = store.metrics.bytesCopied;
		for (int i = 0; i < 24; ++i) {
			auto snapshot = mutateAndCapture();
			CHECK(snapshot.entities->buildings.front().priority == int(game.stepCounter));
			CHECK(store.metrics.allocations == warmedAllocations);
			const auto memory = store.memoryMetrics();
			CHECK(memory.allocatedBuffers == warmedMemory.allocatedBuffers);
			CHECK(memory.capacityBytes == warmedMemory.capacityBytes);
		}
		CHECK(store.metrics.bytesCopied > copied);
	}
	TEST_CASE("pooled captures never overwrite a held worker projection")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		auto* building = fixture.addBuilding("inn", 4, 4, 0, 0);
		REQUIRE(building);
		SimulationSnapshot::Store store;
		const auto required = SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)
			| SimulationSnapshot::bit(SimulationSnapshot::Component::Entities);
		auto full = store.captureBoundary(game, SimulationSnapshot::All);
		auto held = full.project(required);
		full = {};
		const int oldPriority = held.entities->buildings.front().priority;
		const auto index = game.map.coordToIndex(20, 20);
		const auto oldResource = held.resources->cells[index].resource.getUint32();
		for (int i = 0; i < 24; ++i) {
			++game.stepCounter;
			building->priority = oldPriority + i + 1;
			game.map.setResourceByIndex(20, 20,WHEAT, 1);
			auto latest = store.captureBoundary(game, SimulationSnapshot::All);
			CHECK(latest.resources != held.resources);
			CHECK(latest.entities != held.entities);
			CHECK(held.entities->buildings.front().priority == oldPriority);
			CHECK(held.resources->cells[index].resource.getUint32() == oldResource);
		}
		CHECK_FALSE(held.terrain); CHECK_FALSE(held.teams); CHECK_FALSE(held.visibility);
	}
	TEST_CASE("building snapshots copy shared scalar records and freeze selected stock")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto* local = fixture.addBuilding("inn", 4, 4, 0, 0);
		auto* shared = fixture.addBuilding("inn", 12, 12, 0, 0);
		REQUIRE(local); REQUIRE(shared);
		local->localMaterials[WOOD] = 17;
		shared->localMaterials[WOOD] = 19;
		// Exercise the actual runtime binding independently of the catalog flag.
		shared->materials = fixture.game.teams[0]->teamMaterials;
		fixture.game.teams[0]->teamMaterials[WOOD] = 23;
		local->priority = -1; local->maxUnitWorking = 3;
		shared->minWorkerLevelToFlag = 2;
		shared->explorersRequireBombing = true;
		local->locked[4] = true;
		const auto catalog = SimulationSnapshot::captureCatalog(fixture.game);
		auto captured = SimulationSnapshot::capture(fixture.game, catalog);
		AIEngine::AIWorldView held(captured);
		const auto* frozenLocal = held.building(Game::refOf(local));
		const auto* frozenShared = held.building(Game::refOf(shared));
		REQUIRE(frozenLocal); REQUIRE(frozenShared);
		for (const auto& building : held.buildings) {
			const auto* live = fixture.game.resolveBuilding(building.identity);
			REQUIRE(live);
			CHECK(static_cast<const BuildingStateRecord&>(building)
				== static_cast<const BuildingStateRecord&>(*live));
		}
		CHECK_FALSE(frozenLocal->usesTeamResources);
		CHECK(frozenShared->usesTeamResources);
		CHECK(held.buildingResources(*frozenLocal).data() == frozenLocal->localMaterials);
		CHECK(held.buildingResources(*frozenShared).data() == held.teams[0].materials.data());
		CHECK(held.buildingResources(*frozenLocal)[WOOD] == 17);
		CHECK(held.buildingResources(*frozenShared)[WOOD] == 23);
		local->localMaterials[WOOD] = 31;
		fixture.game.teams[0]->teamMaterials[WOOD] = 37;
		local->priority = 1;
		AIEngine::AIWorldView later(SimulationSnapshot::capture(fixture.game, catalog));
		CHECK(later.buildingResources(*later.building(Game::refOf(local)))[WOOD] == 31);
		CHECK(later.buildingResources(*later.building(Game::refOf(shared)))[WOOD] == 37);
		CHECK(frozenLocal->priority == -1);
		CHECK(held.buildingResources(*frozenLocal)[WOOD] == 17);
		CHECK(held.buildingResources(*frozenShared)[WOOD] == 23);
		// Entities-only projections retain local stock without capturing Teams.
		auto partial = SimulationSnapshot::capture(fixture.game, catalog,
			SimulationSnapshot::bit(SimulationSnapshot::Component::Entities));
		CHECK_FALSE(partial.teams);
		AIEngine::AIWorldView entitiesOnly(std::move(partial));
		CHECK(entitiesOnly.buildingResources(*entitiesOnly.building(Game::refOf(local)))[WOOD] == 31);
		CHECK(entitiesOnly.building(Game::refOf(shared))->usesTeamResources);
	}
	TEST_CASE("flat relationship ranges preserve every building list in order")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto* first = fixture.addBuilding("inn", 4, 4, 0, 0);
		auto* second = fixture.addBuilding("inn", 20, 20, 0, 0);
		auto* a = fixture.addUnit(WORKER, 12, 12, 0);
		auto* b = fixture.addUnit(WORKER, 13, 12, 0);
		REQUIRE(first); REQUIRE(second); REQUIRE(a); REQUIRE(b);
		first->unitsWorking.push_back(b); first->unitsWorking.push_back(a);
		first->unitsInside.push_back(a); second->unitsInside.push_back(b);
		auto snapshot = SimulationSnapshot::capture(fixture.game, SimulationSnapshot::captureCatalog(fixture.game));
		std::size_t cursor = 0;
		for (const auto& view : snapshot.entities->buildings) {
			const auto* live = fixture.game.resolveBuilding(view.identity);
			REQUIRE(live);
			CHECK(view.working.offset == cursor);
			CHECK(view.working.count == live->unitsWorking.size());
			for (const auto* unit : live->unitsWorking) CHECK(snapshot.entities->relationships.at(cursor++) == Game::refOf(unit));
			CHECK(view.inside.offset == cursor);
			CHECK(view.inside.count == live->unitsInside.size());
			for (const auto* unit : live->unitsInside) CHECK(snapshot.entities->relationships.at(cursor++) == Game::refOf(unit));
		}
		CHECK(cursor == snapshot.entities->relationships.size());
	}
	TEST_CASE("farm paint queries match frozen growth and resource inputs")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		for (int y = 0; y < game.map.getH(); ++y) {
			for (int x = 0; x < 8; ++x) game.map.paintVertexSquare(x, y, WATER, 1);
			for (int x = 8; x < 16; ++x) game.map.paintVertexSquare(x, y, SAND, 1);
		}
		SimulationSnapshot::Store store;
		auto checkParity = [&] {
			++game.stepCounter;
			auto snapshot = store.captureBoundary(game, SimulationSnapshot::All);
			for (int y = 0; y < game.map.getH(); ++y)
				for (int x = 0; x < game.map.getW(); ++x)
					CHECK(snapshot.tileAt(game.map.coordToIndex(x, y)).canPaintFarm == game.map.canPaintFarmArea(x, y));
			return snapshot;
		};
		auto held = checkParity();
		const auto grassIndex = game.map.coordToIndex(19, 10);
		REQUIRE(held.tileAt(grassIndex).canPaintFarm);
		game.map.setResourceByIndex(19, 10,STONE, 3);
		auto stone = checkParity();
		CHECK_FALSE(stone.tileAt(grassIndex).canPaintFarm);
		CHECK(held.tileAt(grassIndex).canPaintFarm);
		game.map.setResourceByIndex(19, 10,WOOD, 3);
		CHECK(checkParity().tileAt(grassIndex).canPaintFarm);
		game.map.setResourcesGrow(19, 10, 0);
		CHECK_FALSE(checkParity().tileAt(grassIndex).canPaintFarm);
	}

	TEST_CASE("memory metrics distinguish held epochs from reusable high water capacity")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game;
		SimulationSnapshot::Store store;
		const auto resources = SimulationSnapshot::bit(SimulationSnapshot::Component::Resources);
		const auto areas = SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
		auto initial = store.captureBoundary(game, resources | areas);
		auto held = initial.project(resources);
		initial = {};
		const auto first = store.memoryMetrics();
		CHECK(first.allocatedBuffers == 2); CHECK(first.leasedBuffers == 2); CHECK(first.reusableBuffers == 0);
		++game.stepCounter; game.map.setResourceByIndex(20, 20,WHEAT, 1);
		store.captureBoundary(game, resources);
		const auto retained = store.memoryMetrics();
		CHECK(retained.allocatedBuffers == 3); CHECK(retained.leasedBuffers == 2); CHECK(retained.reusableBuffers == 1);
		CHECK(retained.retainedBytes > first.retainedBytes);
		held = {};
		const auto released = store.memoryMetrics();
		CHECK(released.leasedBuffers == 1); CHECK(released.reusableBuffers == 2);
		CHECK(released.retainedBytes == retained.retainedBytes); CHECK(released.capacityBytes == retained.capacityBytes);
		CHECK(released.leasedBytes < retained.leasedBytes);
		CHECK(released.peakLeasedBytes == retained.peakLeasedBytes);
		CHECK(released.peakRetainedBytes == retained.retainedBytes);
	}
	// Trivially copyable cells have no operator==; compare their bytes.
	template<class Cell> bool sameCells(const std::vector<Cell>& captured, std::span<const Cell> live)
	{ return captured.size() == live.size() && (live.empty() || !std::memcmp(captured.data(), live.data(), live.size_bytes())); }
	TEST_CASE("reused buffers copy sparse chunks and refresh dense changes contiguously")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game; auto& map = game.map;
		SimulationSnapshot::Store store;
		store.setVerification(true);
		// Map arrays only: entity and team records are copied whole every capture.
		const auto arrays = SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)
			| SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy) | SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
		const auto next = [&] { ++game.stepCounter; return store.captureBoundary(game, arrays); };
		auto first = next();
		map.setResourceByIndex(5, 5, WHEAT, 1);
		auto second = next(); // first still holds buffer A, so B is filled completely
		CHECK(second.resources != first.resources);
		first = {};
		const auto before = store.metrics.bytesCopied;
		map.setResourceByIndex(6, 6, WHEAT, 1); // same chunk as the first change
		auto third = next(); // reuses A: only the changed chunk moves
		const auto chunkCells = MapState::ChunkGeometry::Side * MapState::ChunkGeometry::Side;
		const auto expected = chunkCells * sizeof(SimulationSnapshot::ResourceCell)
			+ (map.resourceStockIndexState().empty() ? 0 : chunkCells * sizeof(Uint32));
		CHECK(store.metrics.bytesCopied - before == expected);
		CHECK(third.occupancy == second.occupancy); CHECK(third.areas == second.areas);
		CHECK(third.resources != second.resources);
		CHECK(third.resourceAt(map.coordToIndex(5, 5)).resource.type == WHEAT);
		CHECK(third.resourceAt(map.coordToIndex(6, 6)).resource.type == WHEAT);
		CHECK(sameCells(third.resources->cells, map.resourceState()));
		// Buffer B last saw the world at the second capture, so reusing it moves
		// three dirty chunks out of four trigger a contiguous refresh.
		second = {};
		const auto threeBefore = store.metrics.bytesCopied;
		map.setResourceByIndex(20, 20, WOOD, 1); map.setResourceByIndex(5, 20, STONE, 1);
		auto fourth = next();
		CHECK(store.metrics.bytesCopied - threeBefore == 4 * expected);
		CHECK(sameCells(fourth.resources->cells, map.resourceState()));
		CHECK(third.resourceAt(map.coordToIndex(20, 20)).resource.type != WOOD);
		// Keep A held so the next capture must allocate C, then reuse the
		// densely refreshed B. Its stamps must include the whole refresh.
		map.setResourceByIndex(21, 21, WOOD, 1);
		auto fifth = next();
		fourth = {};
		map.setResourceByIndex(22, 22, WOOD, 1);
		const auto sparseBefore = store.metrics.bytesCopied;
		auto sixth = next();
		CHECK(store.metrics.bytesCopied - sparseBefore == expected);
		CHECK(sameCells(sixth.resources->cells, map.resourceState()));
		CHECK(fifth.resourceAt(map.coordToIndex(22, 22)).resource.type != WOOD);
	}
	TEST_CASE("resource copy metrics include multi-material stock sidecars")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .loadDefaultRace=true}};
		auto& game = fixture.game; auto& map = game.map;
		auto resource = nlohmann::json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
		resource["key"] = "snapshot-mixed-crop";
		resource["yields"]["paper"] = resource["yields"]["food"];
		map.installResourceDefinitions(nlohmann::json{{"schemaVersion", 1}, {"resources", {resource}}}.dump());
		const auto type = resourceIndex(*map.resourceRegistry().find("snapshot-mixed-crop"));
		map.setResourceByIndex(5, 5, type, 1);
		REQUIRE_FALSE(map.resourceStockState().empty());
		SimulationSnapshot::Store store;
		store.setVerification(true);
		const auto next = [&] { ++game.stepCounter; return store.captureBoundary(game, SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)); };
		auto first = next();
		CHECK(store.metrics.bytesCopied == map.resourceState().size_bytes()
			+ map.resourceStockIndexState().size_bytes() + map.resourceStockState().size_bytes());
		map.setResourceByIndex(6, 6, type, 1);
		auto second = next();
		first = {};
		map.setResourceByIndex(7, 7, type, 1);
		const auto before = store.metrics.bytesCopied;
		auto third = next();
		const auto chunkCells = MapState::ChunkGeometry::Side * MapState::ChunkGeometry::Side;
		CHECK(store.metrics.bytesCopied - before == chunkCells * (sizeof(SimulationSnapshot::ResourceCell) + sizeof(Uint32))
			+ map.resourceStockState().size_bytes());
		CHECK(sameCells(third.resources->cells, map.resourceState()));
		CHECK(sameCells(third.resources->stockIndices, map.resourceStockIndexState()));
		CHECK(sameCells(third.resources->stocks, map.resourceStockState()));
	}
	TEST_CASE("a retained consumer leaves the newest free buffer to absorb only later changes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game; auto& map = game.map;
		SimulationSnapshot::Store store;
		store.setVerification(true);
		const auto areas = SimulationSnapshot::bit(SimulationSnapshot::Component::Areas);
		const auto next = [&] { ++game.stepCounter; return store.captureBoundary(game, areas); };
		auto held = next();                       // buffer A, retained by a slow consumer
		map.addForbidden(2, 2, 0);
		auto b = next();                          // buffer B, full fill
		map.addGuardArea(20, 20, 0);
		auto c = next();                          // buffer C, full fill (A and B both held)
		b = {};
		map.addClearArea(21, 21, 0);              // same chunk as the guard area
		const auto before = store.metrics.bytesCopied;
		auto d = next();                          // B is the newest free buffer: one chunk since its fill
		const auto chunkBytes = MapState::ChunkGeometry::Side * MapState::ChunkGeometry::Side * sizeof(SimulationSnapshot::AreaCell);
		CHECK(store.metrics.bytesCopied - before == chunkBytes);
		CHECK(sameCells(d.areas->cells, map.areaState()));
		CHECK(held.areasAt(map.coordToIndex(2, 2)).forbidden == 0);
		CHECK(held.areasAt(map.coordToIndex(20, 20)).guard == 0);
		held = {};
		const auto reuseBefore = store.metrics.bytesCopied;
		map.addForbidden(3, 3, 0);
		auto e = next();                          // C is newer than A: chunks since C's fill only
		// Exactly half dirty remains a sparse copy.
		CHECK(store.metrics.bytesCopied - reuseBefore == 2 * chunkBytes);
		CHECK(sameCells(e.areas->cells, map.areaState()));
	}
	TEST_CASE("capture verification rejects writes that bypass the change trackers")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		auto& game = fixture.game; auto& map = game.map;
		SimulationSnapshot::Store store;
		store.setVerification(true);
		const auto next = [&] { ++game.stepCounter; return store.captureBoundary(game, SimulationSnapshot::All); };
		auto first = next();
		map.setFertility(4, 4, 9);
		auto second = next();
		first = {};
		map.resourceCells[map.coordToIndex(4, 4)].fertility = 10; // raw write, no mark
		CHECK_THROWS_WITH_AS(next(), "snapshot verification: resources chunk 0 differs from the live map", std::logic_error);
		map.markResource(map.coordToIndex(4, 4));
		CHECK_NOTHROW(next());
		CHECK(store.captureBoundary(game, SimulationSnapshot::All).resourceAt(map.coordToIndex(4, 4)).fertility == 10);
	}
	TEST_CASE("same tick component expansion preserves narrow leases and captures arrays once")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
		SimulationSnapshot::Store store;
		const auto terrainMask = SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain);
		auto terrain = store.captureBoundary(fixture.game, terrainMask);
		const auto terrainBytes = store.metrics.bytesCopied;
		auto full = store.captureBoundary(fixture.game, SimulationSnapshot::All);
		CHECK(full.terrain == terrain.terrain); CHECK_FALSE(terrain.resources); CHECK_FALSE(terrain.entities);
		CHECK(store.metrics.captures == 2); CHECK(store.metrics.reusedComponents >= 1);
		const auto bytes = store.metrics.bytesCopied;
		CHECK(bytes > terrainBytes);
		auto resources = store.captureBoundary(fixture.game, SimulationSnapshot::bit(SimulationSnapshot::Component::Resources));
		CHECK(resources.resources == full.resources); CHECK_FALSE(resources.terrain); CHECK_FALSE(resources.entities);
		CHECK(store.metrics.bytesCopied == bytes); CHECK(store.metrics.captures == 2);
	}

}

TEST_SUITE("WorldSnapshot")
{
TEST_CASE("explicit observation boundaries refresh paused state and retain old leases")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5, .hDec=5, .teams=1, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
    auto* unit = fixture.addUnit(WORKER,12,12,0);
    REQUIRE(unit);
    auto& store = fixture.game.snapshots();
    constexpr auto needed = SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)
        | SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain);
    const auto before = store.captureBoundary(fixture.game, needed);
    const auto previousHp = before.entities->units.front().hp;
    unit->hp -= 1;
    // Captures within a declared boundary stay coherent, even if a caller
    // prematurely writes. Only the owner's explicit boundary makes it visible.
    CHECK(store.captureBoundary(fixture.game, needed).entities == before.entities);
    store.invalidateBoundary();
    const auto after = store.captureBoundary(fixture.game, needed);
    CHECK(after.tick == before.tick);
    CHECK(after.observationRevision == before.observationRevision + 1);
    CHECK(after.entities->units.front().hp == previousHp - 1);
    CHECK(before.entities->units.front().hp == previousHp);
    CHECK(after.terrain == before.terrain);
    const auto projected = after.project(SimulationSnapshot::bit(SimulationSnapshot::Component::Entities));
    CHECK(projected.observationRevision == after.observationRevision);
    CHECK_FALSE(projected.terrain);
    fixture.game.clearAI();
    // Store reset does not invalidate handles already owned by consumers.
    CHECK(before.entities->units.front().hp == previousHp);
    CHECK(fixture.game.snapshots().captureBoundary(fixture.game, needed).entities->units.front().hp == previousHp - 1);
}
}
