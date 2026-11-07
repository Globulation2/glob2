// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a resource-fetch target must track the gradient
// a walking unit actually follows, not a one-time snapshot from task
// assignment.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <filesystem>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "map/claims/ActiveRouteRuntime.h"
#include "map/claims/ClaimPolicy.h"
#include <fstream>
#include <set>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

// handleMovementGoingToResource() is protected; a subclass gets access the
// same way test/README.md's Map subclass pattern does for Map predicates.
struct TestUnit : Unit
{
	TestUnit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
		: Unit(x, y, gid, typeNum, team, level) {}
	void stepGoingToResource() { handleMovementGoingToResource(); }
    void stepDisplacement() { handleDisplacement(); }
    void resolveDeathForTest() { resolveDeath(); }
};

// Exercise the normal owner admission so assignment purpose, subscription lists,
// and hiring bookkeeping are valid in full-save continuation fixtures.
static void hireClaimWorker(Unit& unit, Building& building)
{
    building.maxUnitWorking=1;
    building.setMaterial(WHEAT,0);
    building.updateCallLists();
    unit.medical=Unit::MED_FREE;
    unit.activity=Unit::ACT_RANDOM;
    unit.hungriness=0;
    glob2test::publishCurrentHiringSuggestions(*unit.owner->game);
    REQUIRE(building.subscribeToBringMaterialsStep());
    REQUIRE(unit.attachedBuilding==&building);
    REQUIRE(unit.jobPurpose==UnitJobPurpose::Transport);
    REQUIRE(unit.destinationPurpose==WHEAT);
    glob2test::discardPrematureHiringSuggestions(*unit.owner->game);
}

static void staleTargetIsRefreshedAfterGradientRebuild(int expectedCost, int swimSpeed)
{
	GameGUI gui;
	Game& game = gui.game;
	const bool swimOnly=expectedCost==WATER_ONLY_CLASS;
	game.map.setSize(5, 5, swimOnly?WATER:GRASS); // 32x32
	game.map.setGame(&game);
	for(int y=0;y<game.map.getH();++y)
		for(int x=0;x<game.map.getW();++x) game.map.clearImmobileUnit(x,y);
	if(swimOnly) {
		// Water-only access has no land travel, so give it a legal aquatic
		// food source while amphibious and land-only cases use land wheat.
		auto aquatic=nlohmann::json::parse(game.map.resourceRegistry().serialize())["resources"][WHEAT];
		aquatic["key"]="swim-only-food";
		aquatic["properties"]["habitatMask"]=ResourceAquatic;
		aquatic["properties"]["requiresGrowthTerrain"]=false;
		game.map.installResourceDefinitions(nlohmann::json{{"schemaVersion",1},{"resources",nlohmann::json::array({aquatic})}}.dump());
	}
	game.addTeam(0);
	Team* team = game.teams[0];
	const int teamNumber = team->teamNumber;

	const int unitX = 16, unitY = 16;
	const int nearX = 20, nearY = 16; // distance 4
	const int farX = 16, farY = 25;   // distance 9
	const int source=swimOnly?resourceIndex(*game.map.resourceRegistry().find("swim-only-food")):WHEAT;
	require(game.map.incResourceByIndex(nearX, nearY, source, 0), "seed the near wheat tile");
	require(game.map.incResourceByIndex(farX, farY, source, 0), "seed the far wheat tile");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->attachUnit(0, unit);
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->destinationPurpose = WHEAT;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->setValidTarget(true);
	unit->setPerformance(WALK,swimOnly?0:10);
	unit->setPerformance(SWIM,swimSpeed);
	const auto access = unit->movementAccess();
	require(Map::swimClass(unit->performance[WALK], unit->performance[SWIM]) == expectedCost,
		"exercise the requested physical movement cost");
	team->updateRoutingRatio();
	require(game.map.preparationCostClass(teamNumber, accessClass(access)) == expectedCost,
		"team preparation costs preserve restricted access and select the sole amphibious unit's ratio");

	const auto* initial = glob2test::awaitGradient(game, [&] { return game.map.getMaterialGradientSlot(teamNumber,WHEAT,access); });
	require(game.map.publishedCostClass(initial) == expectedCost,
		"the shared field retains its selected preparation cost");
	// The planning target at task assignment.
	glob2test::materialPlanningTarget(game.map,teamNumber, WHEAT, access, unit->posX, unit->posY, &unit->targetX, &unit->targetY, NULL);
	require(unit->targetX == nearX && unit->targetY == nearY, "initial target is the nearer wheat tile");
	require(game.map.getGradient(teamNumber, WHEAT, access, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"initial target is the gradient's goal");

	// One action of walking: the target must not move while it is still valid.
	unit->stepGoingToResource();
	require(unit->targetX == nearX && unit->targetY == nearY, "target holds steady while still valid");

	// Another unit fully harvests the near tile. This mutates the resource
	// layer directly, the same way Map::decResource does; the cached
	// gradient is untouched until something rebuilds it.
	game.map.replaceResource(nearX, nearY, Resource{});
	require(game.map.getGradient(teamNumber, WHEAT, access, nearX, nearY) == GRADIENT_AT_GOAL,
		"the cached gradient does not notice the depletion by itself");

	// Request an ordinary refresh; field identity remains stable while its
	// replacement is prepared and published at the fixed boundary.
	const auto* previous=game.map.getMaterialGradientSlot(teamNumber,WHEAT,access);
	game.map.updateMaterialGradient(teamNumber, WHEAT, access);
	// Stale guidance remains visible until the fixed publication boundary.
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,access)==previous,"refresh keeps the published generation");
	glob2test::awaitGradient(game, [&] {
		const auto* field=game.map.getMaterialGradientSlot(teamNumber,WHEAT,access);
		return field && field[game.map.coordToIndex(nearX,nearY)]!=GRADIENT_AT_GOAL ? field : nullptr;
	});
	require(game.map.getGradient(teamNumber, WHEAT, access, nearX, nearY) != GRADIENT_AT_GOAL,
		"the rebuilt gradient no longer marks the depleted tile as the goal");

	// The unit takes its next action. pathfindResource reads the fresh
	// gradient and correctly steps toward the far tile; the stored target
	// must follow, not keep pointing at the now-empty near tile.
	unit->stepGoingToResource();
	require(unit->targetX == farX && unit->targetY == farY,
		"target is refreshed to the far wheat tile once the near one is gone");
	require(game.map.getGradient(teamNumber, WHEAT, access, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"refreshed target is the rebuilt gradient's goal");

	std::puts("PASS resource-fetch target is refreshed when the gradient it was ascended from is rebuilt");
}

// targetX/Y (also the debug path line, hotkey T) are set once, by ascending
// a gradient, when a fetch task starts (Unit.cpp, UnitDisplacement.cpp).
// pathfindMaterial (UnitMovement.cpp) re-reads the resource gradient that
// governs the unit's step fresh every action instead, and that field can be
// rebuilt while the unit is still walking. Fetching is greedy: the unit heads
// for the resource nearest to itself, even when another is a cheaper carry.
static void targetTracksTheGradientTheUnitActuallyFollows()
{
	GameGUI gui;
	Game& game = gui.game;
	game.map.setSize(5, 5, GRASS); // 32x32
	game.map.setGame(&game);
	// setSize leaves immobileUnits[] zeroed, not IMMOBILE_UNIT_NONE (255), so
	// every cell reads as immobile-unit-occupied until cleared; a real game
	// never observes this because something else sweeps it first.
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
			game.map.clearImmobileUnit(x, y);
	game.addTeam(0);
	Team* team = game.teams[0];
	const int teamNumber = team->teamNumber;
	const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
	require(innType >= 0, "inn type exists");
	Building* inn = game.addBuilding(5, 5, innType, 0);
	require(inn != nullptr, "inn placed");
	require(game.map.materialSupplyModesSlot(inn, WHEAT) == 0, "stock permission without a supplier uses natural routing");
	game.map.setBuilding(5, 5, inn->type->width, inn->type->height, inn->gid);

	const int unitX = 16, unitY = 16;
	// A is close to the unit but a long carry from the building; B is a
	// longer fetch but a short carry. Greedy fetching walks to A.
	const int nearUnitX = 20, nearUnitY = 16;
	const int nearBuildingX = 7, nearBuildingY = 7;
	require(game.map.incResourceByIndex(nearUnitX, nearUnitY, WHEAT, 0), "seed the tile near the unit");
	require(game.map.incResourceByIndex(nearBuildingX, nearBuildingY, WHEAT, 0), "seed the tile near the building");

	TestUnit* unit = new TestUnit(unitX, unitY, 0, WORKER, team, 0);
	team->attachUnit(0, unit);
	game.map.setGroundUnit(unitX, unitY, unit->gid);
	unit->attachedBuilding = inn;
	unit->destinationPurpose = WHEAT;
	unit->activity = Unit::ACT_FILLING;
	unit->displacement = Unit::DIS_GOING_TO_RESOURCE;
	unit->setValidTarget(true);
	const auto access = unit->movementAccess();

	glob2test::awaitGradient(game, [&] { return game.map.getMaterialGradientSlot(teamNumber,WHEAT,access); });
	require(game.map.getGlobalGradientDestination(game.map.getMaterialGradientSlot(teamNumber, WHEAT, access), unit->posX, unit->posY, &unit->targetX, &unit->targetY),
		"sanity: ascending the plain gradient reaches an exact goal");
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"sanity: the plain gradient's nearest tile is the one close to the unit, not the building");

	// One action: the unit steps toward the tile nearest to it, and the target follows.
	unit->stepGoingToResource();
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"target follows the nearest resource, not the cheaper carry");

	// Another action while nothing changed: the target must hold steady.
	unit->stepGoingToResource();
	require(unit->targetX == nearUnitX && unit->targetY == nearUnitY,
		"target holds steady while still valid");

	// The near-unit tile gets fully harvested by someone else. The resource
	// gradient does not notice by itself.
	game.map.replaceResource(nearUnitX, nearUnitY, Resource{});
	const auto* previous=game.map.getMaterialGradientSlot(teamNumber,WHEAT,access);
	game.map.updateMaterialGradient(teamNumber, WHEAT, access);
	// Stale guidance remains visible until the fixed publication boundary.
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,access)==previous,"refresh keeps the published generation");
	glob2test::awaitGradient(game, [&] {
		const auto* field=game.map.getMaterialGradientSlot(teamNumber,WHEAT,access);
		return field && field[game.map.coordToIndex(nearUnitX,nearUnitY)]!=GRADIENT_AT_GOAL ? field : nullptr;
	});

	// Next action: only the near-building tile is left on the gradient: the
	// target must be refreshed to it.
	unit->stepGoingToResource();
	require(unit->targetX == nearBuildingX && unit->targetY == nearBuildingY,
		"target is refreshed to the only remaining wheat tile");

	require(game.map.materialRoutingCacheBytes() == 0, "ordinary natural fetch allocates no supplier cache");
	const auto* natural=game.map.getMaterialGradientSlot(teamNumber,WHEAT,access);
	const int marketType=game.buildingsTypes.getTypeNum("market",0,false);
	auto* market=game.addBuilding(24,24,marketType,0);
	require(market!=nullptr, "fruit-only market placed");
	market->setMaterial(CHERRY, 1);
	require(!team->directStockSuppliers.empty(), "fruit-only market is a direct supplier");
	require(game.map.materialSupplyModesSlot(inn,WHEAT)==0, "fruit-only supplier cannot alter wheat routing");
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,access,true,inn)==natural,
		"fruit-only supplier retains the original natural wheat field");
	require(game.map.materialRoutingCacheBytes()==0, "impossible wheat supplier creates no cached field");
	std::puts("PASS resource-fetch target tracks the nearest resource and refreshes when its gradient is rebuilt");
}
}

TEST_SUITE("ResourceFetchTarget")
{
    TEST_CASE("a still stocked old peak does not pin the current pursuit")
    {
        glob2test::HeadlessGlobals globals;
        GameGUI gui;
        auto& game=gui.game;
        auto& map=game.map;
        map.setSize(5,5,GRASS); map.setGame(&game); game.addTeam(0);
        for (int y=0;y<map.getH();++y) for (int x=0;x<map.getW();++x) map.clearImmobileUnit(x,y);
        REQUIRE(map.incResourceByIndex(20,16,WHEAT,0));
        auto* unit=new TestUnit(16,16,0,WORKER,game.teams[0],0);
        game.teams[0]->attachUnit(0,unit); map.setGroundUnit(16,16,unit->gid);
        unit->destinationPurpose=WHEAT; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->setValidTarget(true);
        const auto movement=unit->movementAccess();
        glob2test::awaitGradient(game,[&]{ return map.getMaterialGradientSlot(0,WHEAT,movement); });
        unit->targetX=20; unit->targetY=16;
        REQUIRE(map.incResourceByIndex(16,18,WHEAT,0));
        map.updateMaterialGradient(0,WHEAT,movement);
        glob2test::awaitGradient(game,[&] {
            const auto* field=map.getMaterialGradientSlot(0,WHEAT,movement);
            return field && field[map.coordToIndex(16,18)]==GRADIENT_AT_GOAL ? field : nullptr;
        });
        REQUIRE(glob2test::isGradientPeak(map,map.getMaterialGradientSlot(0,WHEAT,movement),20,16));
        const auto before=unit->entityRandom;
        unit->stepGoingToResource();
        CHECK(unit->targetX==16); CHECK(unit->targetY==18); CHECK(unit->validTarget());
        CHECK(unit->entityRandom==before);

        // A source can disappear while its published peak remains unchanged.
        map.replaceResource(16,18,Resource{});
        unit->stepGoingToResource();
        CHECK_FALSE(unit->validTarget());
    }

    TEST_CASE("material projection uses weighted ranking and validates its terminal source")
    {
        glob2test::HeadlessGlobals globals;
        GameGUI gui;
        auto& game=gui.game; auto& map=game.map;
        map.setSize(5,5,GRASS); map.setGame(&game); game.addTeam(0);
        for (int y=0;y<map.getH();++y) for (int x=0;x<map.getW();++x) map.clearImmobileUnit(x,y);
        REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));
        REQUIRE(map.incResourceByIndex(7,7,WHEAT,0));
        std::vector<Uint16> field(map.getW()*map.getH(),GRADIENT_UNREACHABLE);
        field[map.coordToIndex(8,8)]=64000;
        field[map.coordToIndex(9,8)]=65000;
        const int extra=map.stepCost(-1,-1,map.coordToIndex(7,7),0)-map.stepCost(1,0,map.coordToIndex(9,8),0);
        REQUIRE(extra>1);
        field[map.coordToIndex(7,7)]=65000+extra-1;
        Sint32 x=-1,y=-1;
        CHECK_FALSE(map.getGlobalGradientDestination(field.data(),8,8,&x,&y));
        CHECK(x==7); CHECK(y==7); // Raw ascent picks the more expensive diagonal.
        REQUIRE(map.projectMaterialDestination(field.data(),0,WHEAT,MovementAccess::Land,8,8,&x,&y));
        CHECK(x==9); CHECK(y==8);
        map.replaceResource(9,8,Resource{});
        CHECK_FALSE(map.projectMaterialDestination(field.data(),0,WHEAT,MovementAccess::Land,8,8,&x,&y));
        CHECK_FALSE(map.projectMaterialDestination(field.data(),0,WHEAT,MovementAccess::Land,0,0,&x,&y));
    }

    TEST_CASE("material step projection follows its accepted first step without extra randomness")
    {
        glob2test::HeadlessGlobals globals;
        GameGUI gui;
        auto& game=gui.game; auto& map=game.map;
        map.setSize(5,5,GRASS); map.setGame(&game); game.addTeam(0);
        for (int y=0;y<map.getH();++y) for (int x=0;x<map.getW();++x) map.clearImmobileUnit(x,y);
        REQUIRE(map.incResourceByIndex(12,8,WHEAT,0));
        REQUIRE(map.incResourceByIndex(4,8,WHEAT,0));
        const auto* field=glob2test::awaitGradient(game,[&]{ return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land); });
        // Occupy the eastward choices; the selected step may switch basins.
        for (int y=7;y<=9;++y) map.markImmobileUnit(9,y,0);
        EntityRandom reference, observed;
        int rx=0,ry=0,dx=0,dy=0; bool stop=false,otherStop=false,valid=false;
        const auto expected=map.queryMaterialStep(reference,0,WHEAT,MovementAccess::Land,8,8,&rx,&ry,&stop,nullptr);
        Sint32 x=-1,y=-1,ex=-1,ey=-1;
        const auto actual=map.queryMaterialStep(observed,0,WHEAT,MovementAccess::Land,8,8,&dx,&dy,&otherStop,nullptr,false,&x,&y,&valid);
        REQUIRE(actual==StepResult::Step); CHECK(actual==expected);
        CHECK(dx==rx); CHECK(dy==ry); CHECK(stop==otherStop); CHECK(observed==reference);
        REQUIRE(map.projectMaterialDestination(field,0,WHEAT,MovementAccess::Land,8+dx,8+dy,&ex,&ey));
        REQUIRE(valid); CHECK(x==ex); CHECK(y==ey);
    }

    TEST_CASE("harvesting records the touched pickup rather than the previous projection")
    {
        glob2test::HeadlessGlobals globals;
        GameGUI gui;
        auto& game=gui.game; auto& map=game.map;
        map.setSize(5,5,GRASS); map.setGame(&game); game.addTeam(0);
        const int innType=globalContainer->buildingsTypes.getTypeNum("inn",0,false);
        auto* inn=game.addBuilding(20,20,innType,0); REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(7,8,WHEAT,0));
        auto* unit=new TestUnit(8,8,0,WORKER,game.teams[0],0);
        game.teams[0]->attachUnit(0,unit); map.setGroundUnit(8,8,unit->gid);
        unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
        unit->targetX=15; unit->targetY=15; unit->setValidTarget(true);
        unit->stepDisplacement();
        CHECK(unit->displacement==Unit::DIS_HARVESTING);
        CHECK(unit->targetX==7); CHECK(unit->targetY==8);
        CHECK_FALSE(unit->validTarget()); // This flag denotes movement targets.
    }

	TEST_CASE("a depleted target is refreshed after its gradient rebuild for every ground access and amphibious cost")
	{
		glob2test::HeadlessGlobals globals;
		const int swimSpeeds[] = {0, 20, 14, 10, 7, 5, 3};
		static_assert(std::size(swimSpeeds)==WATER_ONLY_CLASS);
		// Keep all six amphibious costs covered even though every one uses
		// the same Amphibious lookup identity in its independent fixture.
		for (int cost = 0; cost < WATER_ONLY_CLASS; ++cost)
			staleTargetIsRefreshedAfterGradientRebuild(cost, swimSpeeds[cost]);
		staleTargetIsRefreshedAfterGradientRebuild(WATER_ONLY_CLASS,20);
		targetTracksTheGradientTheUnitActuallyFollows();
	}
}



TEST_CASE("flying resource pursuit crosses paint but chooses the farther legal pickup" * doctest::test_suite("ResourceFetchTarget"))
{
    glob2test::HeadlessGlobals globals;
    for (int size:{5,7}) for (bool constrained:{false,true}) {
        CAPTURE(size); CAPTURE(constrained);
        glob2test::HeadlessGame fixture({.wDec=size,.hDec=size,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        auto* inn=fixture.addBuilding("inn",22,22); REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(10,8,WHEAT,0));
        REQUIRE(map.incResourceByIndex(14,8,WHEAT,0));
        map.addForbidden(9,8,0); map.addForbidden(10,8,0);
        if(constrained) map.paintCell(30,30,VOID_HOLE);
        auto* unit=new TestUnit(8,8,0,WORKER,team,0); team->attachUnit(0,unit);
        unit->setPerformance(FLY,32); map.setAirUnit(8,8,unit->gid);
        unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
        unit->targetX=10; unit->targetY=8; unit->setValidTarget(false);
        const auto* field=glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Air);});
        CHECK(field[map.coordToIndex(10,8)]>GRADIENT_UNREACHABLE);
        CHECK(field[map.coordToIndex(10,8)]<GRADIENT_AT_GOAL);
        CHECK(field[map.coordToIndex(14,8)]==GRADIENT_AT_GOAL);
        CHECK(map.isFreeForMovement(9,8,AIR_MOVEMENT_CLASS,team->me));
        CHECK_FALSE(map.gradientGoalValid({GradientFieldFamily::Materials,0,AIR_MOVEMENT_CLASS,WHEAT},10,8));
        unit->stepGoingToResource();
        CHECK(unit->validTarget()); CHECK(unit->targetX==14); CHECK(unit->targetY==8);
        if(constrained) { CHECK(unit->dx==1); CHECK(unit->dy==0); }
        else CHECK(unit->movement==Unit::MOV_FLYING_TARGET);
    }
}

TEST_SUITE("ActiveMaterialClaims")
{
    TEST_CASE("natural tree fruit stone and water algae pickups share exact reservation semantics")
    {
        glob2test::HeadlessGlobals globals;
        for(int material:{WOOD,CHERRY,STONE,ALGA}) {
            CAPTURE(material);
            glob2test::HeadlessGame f({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto& map=f.game.map;map.enableResourceClaims();
            auto* consumer=f.addBuilding("inn",20,20);REQUIRE(consumer);
            auto* a=f.addUnit(WORKER,8,8);auto* b=f.addUnit(WORKER,8,9);REQUIRE(a);REQUIRE(b);
            if(material==ALGA) {
                map.paintCell(9,8,WATER);
                a->setPerformance(SWIM,1);b->setPerformance(SWIM,1);
            }
            const auto access=Uint32(map.coordToIndex(9,8));
            map.setResourceByIndex(9,8,material,1);map.setMaterialAmountSlot(access,material,1);
            map.setMapDiscovered(9,8,a->owner->me);map.fogOfWar[access]|=a->owner->me;
            for(auto* unit:{a,b}) {
                unit->attachedBuilding=consumer;unit->activity=Unit::ACT_FILLING;
                unit->jobPurpose=UnitJobPurpose::Transport;unit->destinationPurpose=material;
                unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->medical=Unit::MED_FREE;
            }
            const auto before=map.materialAmountAtSlot(access,material);REQUIRE(before==1);
            REQUIRE(a->acquireMaterialClaimAt(access));REQUIRE(a->activeMaterialClaimValid());
            CHECK(a->activeMaterialClaim->exclusive==(material==WOOD));
            CHECK(b->acquireMaterialClaimAt(access)==(material==STONE));
            CHECK(map.materialClaimLedger().size()==(material==STONE?2u:1u));
            a->dx=1;a->dy=0;a->displacement=Unit::DIS_HARVESTING;
            REQUIRE(a->takeActiveMaterialClaim());CHECK_FALSE(a->activeMaterialClaim);
            CHECK(map.materialAmountAtSlot(access,material)==(material==STONE?before:0u));
            if(material==STONE) {
                REQUIRE(b->activeMaterialClaim);CHECK(b->activeMaterialClaimValid());
                REQUIRE(a->acquireMaterialClaimAt(access));a->releaseActiveMaterialClaim();
            } else if(material==WOOD)CHECK(map.getResource(access).type==NO_RES_TYPE);
            a->releaseActiveMaterialClaim();b->releaseActiveMaterialClaim();
            CHECK(map.materialClaimLedger().size()==0);
            a->attachedBuilding=nullptr;b->attachedBuilding=nullptr;
        }
    }
    TEST_CASE("active reservations enforce capacity and survive unrelated gradient changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
        auto& map=f.game.map;auto* team=f.game.teams[0];
        map.enableResourceClaims();
        auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
        auto make=[&](int slot) {
            auto* u=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
            team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
            u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;
            return u;
        };
        auto* a=make(0);auto* b=make(1);const auto source=Uint32(map.coordToIndex(12,12));
        REQUIRE(a->acquireMaterialClaimAt(source));CHECK_FALSE(b->acquireMaterialClaimAt(source));
        REQUIRE(a->activeMaterialClaim);const auto task=a->materialClaimTask;
        REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
        a->targetX=15;a->reconcileMaterialClaim();
        REQUIRE(a->activeMaterialClaim);CHECK(a->activeMaterialClaim->source==source);CHECK(a->materialClaimTask==task);
        const auto snapshot=SimulationSnapshot::capture(f.game,SimulationSnapshot::captureCatalog(f.game));
        REQUIRE(snapshot.entities);const auto& record=snapshot.entities->units[snapshot.entities->unitSlotIndices[a->gid]];
        CHECK(record.activeMaterialClaim==a->activeMaterialClaim);CHECK(record.materialClaimTask==task);CHECK(record.activeClaimsEnabled);
        a->medical=Unit::MED_DAMAGED;a->reconcileMaterialClaim();CHECK_FALSE(a->activeMaterialClaim);
        REQUIRE(b->acquireMaterialClaimAt(source));
        b->standardRandomActivity();CHECK_FALSE(b->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
        b->attachedBuilding=inn;b->activity=Unit::ACT_FILLING;b->displacement=Unit::DIS_GOING_TO_RESOURCE;
        REQUIRE(b->acquireMaterialClaimAt(source));
        b->recordLethalDamage(b->hp-UNIT_HP_DEATH_THRESHOLD+1,GameplayMeasurements::COMBAT);
        CHECK_FALSE(b->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
        REQUIRE(b->acquireMaterialClaimAt(source));
        map.disableResourceClaims();
        CHECK_FALSE(map.activeClaimsEnabled());CHECK(a->materialClaimTask==0);CHECK(b->materialClaimTask==0);
        CHECK_FALSE(a->activeMaterialClaim);CHECK_FALSE(b->activeMaterialClaim);
        CHECK_NOTHROW(map.rebuildActiveMaterialClaims());
    }
    TEST_CASE("pooled harvesting consumes its reserved source rather than the ripest neighbor")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::GameOptions options;
        options.wDec=5;options.hDec=5;options.teams=1;options.discovered=true;options.clearImmobile=true;options.loadDefaultRace=true;
        options.header=true;options.experiments.set(ExperimentId::FarmAreas,true);
        glob2test::HeadlessGame f(options);
        auto& map=f.game.map;auto* team=f.game.teams[0];
        map.enableResourceClaims();
        auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
        for(int x:{9,10}) {REQUIRE(map.incResourceByIndex(x,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,8),WHEAT,2);map.addFarmArea(x,8,0);}
        auto make=[&](int slot) {
            auto* u=new TestUnit(8,8+slot,Unit::GIDfrom(slot,0),WORKER,team,0);
            team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
            u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;
            return u;
        };
        auto* a=make(0);auto* b=make(1);const auto access=Uint32(map.coordToIndex(9,8));
        REQUIRE(a->acquireMaterialClaimAt(access));REQUIRE(b->acquireMaterialClaimAt(access));
        CHECK(a->activeMaterialClaim->source==access);REQUIRE(b->activeMaterialClaim->source==map.coordToIndex(10,8));
        b->dx=1;b->dy=-1;b->displacement=Unit::DIS_HARVESTING;
        REQUIRE(b->takeActiveMaterialClaim());CHECK_FALSE(b->activeMaterialClaim);
        CHECK(map.materialAmountAtSlot(map.coordToIndex(10,8),WHEAT)==1);
        CHECK(map.materialAmountAtSlot(access,WHEAT)==2);CHECK(a->activeMaterialClaim.has_value());
    }
}


TEST_CASE("map export projects active ownership out without mutating the live match" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;
    map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);auto* u=f.addUnit(WORKER,8,8);REQUIRE(u);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
    hireClaimWorker(*u,*inn);
    REQUIRE(u->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto checksum=f.game.checkSum();const auto claim=u->activeMaterialClaim;const auto task=u->materialClaimTask;const auto gid=u->gid;
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);f.game.save(&out,true,"active-export");out.flush();
    const auto bytes=backend->takeContents();
    CHECK(f.game.checkSum()==checksum);CHECK(u->activeMaterialClaim==claim);CHECK(u->materialClaimTask==task);
    auto* restoredBackend=new GAGCore::MemoryStreamBackend;
    restoredBackend->write(bytes.data(),bytes.size());restoredBackend->seekFromStart(0);
    GAGCore::BinaryInputStream input(restoredBackend);
    glob2test::HeadlessGame restored;
    // A map starts a new game: claims enabled, but none of the exported claims.
    REQUIRE(restored.game.load(&input));CHECK(restored.game.map.activeClaimsEnabled());
    CHECK(restored.game.map.materialClaimLedger().size()==0);
    const auto* copy=restored.game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(copy);
    CHECK_FALSE(copy->activeMaterialClaim);CHECK(copy->materialClaimTask==0);
}

TEST_CASE("saved rule changes preserve deferred active claim invalidation" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;options.header=true;options.loadDefaultRace=true;options.clearImmobile=true;options.discovered=true;
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame f(options);auto& map=f.game.map;
    f.game.gameHeader.setResourceGrowthDisabled(true);
    map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);auto* u=f.addUnit(WORKER,8,8);REQUIRE(u);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));map.addFarmArea(9,8,0);
    hireClaimWorker(*u,*inn);REQUIRE(u->acquireMaterialClaimAt(Uint32(map.coordToIndex(9,8))));
    f.game.observeCompletedWorld(); // Freeze this tick before the paused rule edit.
    f.game.gameHeader.setResourceGrowthDisabled(false); // Newly protected seed, invalidation still due.
    const auto before=f.game.checkSum();const auto gid=u->gid;
    auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(backend);
    f.game.save(&out,false,"claim-rule-pending");out.flush();const auto bytes=backend->takeContents();
    CHECK(f.game.checkSum()==before);REQUIRE(u->activeMaterialClaim);
    auto* restoredBackend=new GAGCore::MemoryStreamBackend;restoredBackend->write(bytes.data(),bytes.size());restoredBackend->seekFromStart(0);
    GAGCore::BinaryInputStream input(restoredBackend);glob2test::HeadlessGame restored;REQUIRE(restored.game.load(&input));
    auto* copy=restored.game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(copy);REQUIRE(copy->activeMaterialClaim);
    CHECK(restored.game.checkSum()==before);
    for(unsigned tick=0;tick<32;++tick) {
        CAPTURE(tick); f.step(); restored.step();
        REQUIRE(restored.game.checkSum()==f.game.checkSum());
        CHECK(restored.game.map.materialAmountAtSlot(restored.game.map.coordToIndex(9,8),WHEAT)==map.materialAmountAtSlot(map.coordToIndex(9,8),WHEAT));
    }
    CHECK_FALSE(u->activeMaterialClaim);CHECK_FALSE(copy->activeMaterialClaim);
}

TEST_CASE("post-tick incapacitating hits save consistently with either death rule" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    for(bool noPermadeath:{false,true}) {
        CAPTURE(noPermadeath);
        glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=f.game.map;f.game.gameHeader.setPermadeathDisabled(noPermadeath);
        map.enableResourceClaims();
        auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);auto* u=f.addUnit(WORKER,8,8);REQUIRE(u);
        REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
        hireClaimWorker(*u,*inn);REQUIRE(u->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
        const int damage=u->hp-UNIT_HP_DEATH_THRESHOLD+1;
        u->recordLethalDamage(damage,GameplayMeasurements::COMBAT);u->hp-=damage;
        REQUIRE_FALSE(u->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
        const auto before=f.game.checkSum();
        auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(backend);
        f.game.save(&out,false,"post-tick-hit");out.flush();const auto bytes=backend->takeContents();
        CHECK(f.game.checkSum()==before);
        auto* restoredBackend=new GAGCore::MemoryStreamBackend;restoredBackend->write(bytes.data(),bytes.size());restoredBackend->seekFromStart(0);
        GAGCore::BinaryInputStream input(restoredBackend);glob2test::HeadlessGame restored;REQUIRE(restored.game.load(&input));
        CHECK(restored.game.checkSum()==before);CHECK(restored.game.map.materialClaimLedger().size()==0);
    }
}

TEST_CASE("mixed team farming reservations retain the strongest seed floor across saves" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;options.wDec=6;options.hDec=6;options.teams=2;options.header=true;options.loadDefaultRace=true;
    options.clearImmobile=true;options.discovered=true;options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame f(options);auto& map=f.game.map;
    map.enableResourceClaims();
    auto* innA=f.addBuilding("inn",20,20,0,0);auto* innB=f.addBuilding("inn",40,40,0,1);REQUIRE(innA);REQUIRE(innB);
    auto* a=f.addUnit(WORKER,8,8,0);auto* b=f.addUnit(WORKER,8,9,1);REQUIRE(a);REQUIRE(b);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));const auto source=Uint32(map.coordToIndex(9,8));
    map.setMaterialAmountSlot(source,WHEAT,2);map.addFarmArea(9,8,0);
    hireClaimWorker(*a,*innA);hireClaimWorker(*b,*innB);
    REQUIRE(a->acquireMaterialClaimAt(source));CHECK(a->activeMaterialClaim->reserve==1);
    CHECK_FALSE(b->acquireMaterialClaimAt(source));
    a->releaseActiveMaterialClaim();REQUIRE(b->acquireMaterialClaimAt(source));CHECK(b->activeMaterialClaim->reserve==0);
    CHECK_FALSE(a->acquireMaterialClaimAt(source));b->releaseActiveMaterialClaim();
    map.setMaterialAmountSlot(source,WHEAT,3);REQUIRE(a->acquireMaterialClaimAt(source));REQUIRE(b->acquireMaterialClaimAt(source));
    const auto before=f.game.checkSum();const auto gidA=a->gid,gidB=b->gid;
    auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(backend);
    f.game.save(&out,false,"mixed-reserve-claims");out.flush();const auto bytes=backend->takeContents();
    auto* inputBackend=new GAGCore::MemoryStreamBackend;inputBackend->write(bytes.data(),bytes.size());inputBackend->seekFromStart(0);
    GAGCore::BinaryInputStream input(inputBackend);glob2test::HeadlessGame restored;REQUIRE(restored.game.load(&input));
    CHECK(restored.game.checkSum()==before);
    const auto* copyA=restored.game.teams[0]->myUnits[Unit::GIDtoID(gidA)];const auto* copyB=restored.game.teams[1]->myUnits[Unit::GIDtoID(gidB)];
    REQUIRE(copyA->activeMaterialClaim);REQUIRE(copyB->activeMaterialClaim);
    CHECK(copyA->activeMaterialClaim->reserve==1);CHECK(copyB->activeMaterialClaim->reserve==0);
    b->displacement=Unit::DIS_HARVESTING;b->dx=1;b->dy=-1;REQUIRE(b->takeActiveMaterialClaim());
    REQUIRE(a->activeMaterialClaim);CHECK(map.materialAmountAtSlot(source,WHEAT)==2);
    a->displacement=Unit::DIS_HARVESTING;a->dx=1;a->dy=0;REQUIRE(a->takeActiveMaterialClaim());
    CHECK(map.materialAmountAtSlot(source,WHEAT)==1);CHECK(map.materialClaimLedger().size()==0);
}

TEST_CASE("active save rejects invalid deferred map references on a fresh loader" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;
    auto resources=nlohmann::json::parse(map.resourceRegistry().serialize())["resources"];
    auto custom=resources[WHEAT];
    custom["key"]="saved-claim-food";
    resources.push_back(custom);
    map.installResourceDefinitions(nlohmann::json{{"schemaVersion",1},{"resources",resources}}.dump());
    const auto customType=resourceIndex(*map.resourceRegistry().find("saved-claim-food"));
    map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",20,20);auto* unit=f.addUnit(WORKER,8,8);REQUIRE(inn);REQUIRE(unit);
    REQUIRE(map.incResourceByIndex(12,12,customType,0));hireClaimWorker(*unit,*inn);
    REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto valid=*unit->activeMaterialClaim;
    // These saves are corrupted on purpose to exercise the loader, so the
    // save-time audit (which would reject them first) is off here.
    struct Audit { bool saved=Map::claimSaveAudit; ~Audit(){Map::claimSaveAudit=saved;} } restoreAudit;
    Map::claimSaveAudit=false;
    for(unsigned corruption=0;corruption<4;++corruption) {
        CAPTURE(corruption);
        unit->activeMaterialClaim=valid;
        if(corruption==1)unit->activeMaterialClaim->source=Uint32(map.cellCount());
        if(corruption==2)unit->activeMaterialClaim->access=Uint32(map.cellCount());
        if(corruption==3)unit->activeMaterialClaim->resource=NO_RES_TYPE-1;
        auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
        f.game.save(&output,false,"invalid-deferred-claim");output.flush();const auto bytes=backend->takeContents();
        unit->activeMaterialClaim=valid;
        GameGUI restored(false); REQUIRE(restored.game.map.cellCount()==0);
        auto* inputBackend=new GAGCore::MemoryStreamBackend;inputBackend->write(bytes.data(),bytes.size());inputBackend->seekFromStart(0);
        GAGCore::BinaryInputStream input(inputBackend);
        if(corruption) CHECK_THROWS_AS(restored.game.load(&input),std::runtime_error);
        else { REQUIRE(restored.game.load(&input)); CHECK(restored.game.checkSum()==f.game.checkSum()); }
    }
    // With the audit on, the same corruption fails at save time instead.
    Map::claimSaveAudit=true;
    {
        auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
        CHECK_NOTHROW(f.game.save(&output,false,"audited-claim"));
    }
    unit->activeMaterialClaim->source=Uint32(map.cellCount());
    {
        auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
        CHECK_THROWS(f.game.save(&output,false,"audited-claim"));
    }
    unit->activeMaterialClaim=valid;
}

TEST_CASE("a claim worker that finds no claimable stock records demand and gives up its job" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;map.enableResourceClaims();
    auto* a=f.addBuilding("inn",20,20);auto* b=f.addBuilding("inn",24,20);REQUIRE(a);REQUIRE(b);
    auto* holder=f.addUnit(WORKER,8,8);REQUIRE(holder);
    REQUIRE(map.incResourceByIndex(13,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(13,8),WHEAT,1);
    hireClaimWorker(*holder,*a);
    // Admit one free worker at a time: the real hiring planner chooses
    // candidates and need not select an arbitrary fixture pointer.
    auto* seeker=f.addUnit(WORKER,8,9);REQUIRE(seeker);hireClaimWorker(*seeker,*b);
    REQUIRE(holder->acquireMaterialClaimAt(Uint32(map.coordToIndex(13,8))));
    const auto held=holder->activeMaterialClaim;
    const GradientFieldKey demandKey{GradientFieldFamily::Materials,0,Uint8(seeker->movementAccess()),WHEAT};
    const auto queryBucket=f.game.stepCounter&127;
    const auto beforeQueries=map.gradientRuntime->adaptive.enroll(demandKey).queries[queryBucket];
    seeker->handleActiveMaterialMovement();
    // Selection reads are real demand even when every available unit of
    // stock is reserved.
    CHECK(map.gradientRuntime->adaptive.fields.at(demandKey).queries[queryBucket]==beforeQueries+1);
    CHECK(seeker->materialClaimPolicyState.decisionOrdinal==1);
    CHECK_FALSE(seeker->activeMaterialClaim);
    // The worker is freed for work it can do rather than waiting.
    CHECK(seeker->attachedBuilding==nullptr);CHECK(b->unitsWorking.empty());
    CHECK(holder->activeMaterialClaim==held);CHECK(map.materialClaimLedger().size()==1);
}
TEST_CASE("destination refresh keeps the claim when it finds nothing better and survives saves" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    using ResourceClaims::Policy::DestinationRefreshDecisions;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;f.game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
    auto* a=f.addBuilding("inn",20,20);auto* b=f.addBuilding("inn",24,20);REQUIRE(a);REQUIRE(b);
    auto* seeker=f.addUnit(WORKER,8,8);auto* holder=f.addUnit(WORKER,8,10);REQUIRE(seeker);REQUIRE(holder);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));REQUIRE(map.incResourceByIndex(14,12,WHEAT,0));
    const auto near=Uint32(map.coordToIndex(9,8)),far=Uint32(map.coordToIndex(14,12));
    map.setMaterialAmountSlot(near,WHEAT,1);map.setMaterialAmountSlot(far,WHEAT,1);
    // Publish the field first: waiting for it runs whole game ticks.
    REQUIRE(glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
    hireClaimWorker(*seeker,*a);hireClaimWorker(*holder,*b);
    REQUIRE(seeker->acquireMaterialClaimAt(far));REQUIRE(holder->acquireMaterialClaimAt(near));
    const auto oldClaim=seeker->activeMaterialClaim;const auto oldTask=seeker->materialClaimTask;
    f.game.invalidateObservation();f.game.observeCompletedWorld(); // Route submission reads the last completed world.
    auto& cadence=seeker->materialClaimPolicyState;
    REQUIRE(seeker->activeMaterialClaimValid());
    // Not yet due: the held claim and its task are kept without selection.
    cadence.lastGoalAttempt=10;cadence.decisionOrdinal=10+DestinationRefreshDecisions-2;
    seeker->handleActiveMaterialMovement();CHECK(cadence.lastGoalAttempt==10);
    // Due, but the nearer stock is held by another worker: the refresh keeps
    // the old reservation.
    seeker->handleActiveMaterialMovement();CHECK(cadence.lastGoalAttempt==10+DestinationRefreshDecisions);
    CHECK(seeker->activeMaterialClaim==oldClaim);CHECK(seeker->materialClaimTask==oldTask);
    // Once it is free, the next due refresh takes it.
    holder->releaseActiveMaterialClaim();
    seeker->handleActiveMaterialMovement();CHECK(seeker->activeMaterialClaim==oldClaim);
    cadence.decisionOrdinal=cadence.lastGoalAttempt+DestinationRefreshDecisions-1;
    seeker->handleActiveMaterialMovement();REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==near);
    CHECK(map.materialClaimLedger().materialCount(far,WHEAT)==0);
    const auto sameTask=seeker->materialClaimTask;
    seeker->handleActiveMaterialMovement();seeker->handleActiveMaterialMovement();
    CHECK(seeker->materialClaimTask==sameTask);
    f.game.observeCompletedWorld();
    auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
    f.gui.save(&output,"claim-refresh-cadence");output.flush();const auto bytes=backend->takeContents();
    GameGUI resumed;auto* inputBackend=new GAGCore::MemoryStreamBackend;inputBackend->write(bytes.data(),bytes.size());inputBackend->seekFromStart(0);
    GAGCore::BinaryInputStream input(inputBackend);REQUIRE(resumed.load(&input,true));
    REQUIRE(resumed.game.checkSum()==f.game.checkSum());
    const auto* copy=resumed.game.teams[0]->myUnits[Unit::GIDtoID(seeker->gid)];REQUIRE(copy);
    CHECK(copy->materialClaimPolicyState==seeker->materialClaimPolicyState);
    for(unsigned tick=0;tick<64;++tick) {CAPTURE(tick);f.step();resumed.game.step(0);resumed.consumeClientEvents();REQUIRE(resumed.game.checkSum()==f.game.checkSum());}
}
TEST_CASE("natural claim access keeps goal precedence and rechecks visible harvest eligibility" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    for(bool hidden:{false,true}) {
        CAPTURE(hidden);
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=f.game.map;
        if(hidden) {
            auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
            definitions["resources"][WHEAT]["properties"]["visibleToHarvest"]=true;
            map.installResourceDefinitions(definitions.dump());
        }
        map.enableResourceClaims();
        auto* building=f.addBuilding("inn",20,20);auto* unit=f.addUnit(WORKER,8,8);REQUIRE(building);REQUIRE(unit);
        REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
        map.setMapDiscovered(12,12,unit->owner->me); // Discovered fixture terrain is not necessarily current harvest visibility.
        hireClaimWorker(*unit,*building);
        REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
        if(hidden) {
            // A missed callback must still be caught by per-unit validation.
            map.fogOfWar[map.coordToIndex(12,12)]=0;
            CHECK_FALSE(unit->activeMaterialClaimValid());unit->reconcileMaterialClaim();CHECK_FALSE(unit->activeMaterialClaim);
        } else {
            // Natural deposits remain pickup goals despite the source tile's
            // movement blockers: harvesting occurs from an adjacent legal tile.
            map.setBuilding(12,12,1,1,building->gid);
            REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaimValid());
            map.setBuilding(12,12,1,1,NOGBID);
            map.paintCell(12,12,VOID_HOLE);
            map.setResourceByIndex(12,12,WHEAT,1);
            if(!unit->activeMaterialClaim)REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
            CHECK(unit->activeMaterialClaimValid());
            bool legalStance=false;
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)if(dx || dy)
                legalStance|=map.isFreeForMovement(12+dx,12+dy,accessClass(unit->movementAccess()),unit->owner->me);
            CHECK(legalStance);
        }
    }
}

TEST_CASE("farm selection memo preserves pooled source and resets between decisions" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.wDec=5;options.hDec=5;options.discovered=true;options.clearImmobile=true;options.loadDefaultRace=true;
    options.header=true;options.seed=9183;options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame f(options);auto& map=f.game.map;
    map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",20,20);auto* unit=f.addUnit(WORKER,7,11);
    unit->attachedBuilding=inn;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
    unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
    std::vector<ResourceClaims::Owner> holders;
    const auto source=Uint32(map.coordToIndex(15,11));
    for(int y=8;y<16;++y)for(int x=8;x<16;++x) {
        REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));const auto at=Uint32(map.coordToIndex(x,y));
        map.setMaterialAmountSlot(at,WHEAT,2);map.addFarmArea(x,y,0);
    }
    // Pooling allows this physical stock while forbidding it as pickup access.
    map.addForbidden(15,11,0);
    for(int y=8;y<16;++y)for(int x=8;x<16;++x) {
        const auto at=Uint32(map.coordToIndex(x,y));
        const auto holder=ResourceClaims::unitOwner(300,at+1);holders.push_back(holder);
        REQUIRE(map.acquireMaterialClaim(holder,ResourceClaims::Claim{at,at,Uint8(WHEAT),WHEAT,false,1},1,false));
    }
    const auto* field=glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);});
    REQUIRE(field);
    const auto flooded=map.farmFloodGeneration;
    CHECK_FALSE(unit->selectActiveMaterialClaim(field));
    CHECK(map.farmFloodGeneration==flooded+1); // Many distinct accesses, one exhausted component.
    CHECK_FALSE(unit->activeMaterialClaim);
    map.releaseMaterialClaim(ResourceClaims::unitOwner(300,source+1));
    const auto beforeAvailable=map.farmFloodGeneration;
    REQUIRE(unit->selectActiveMaterialClaim(field));
    REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->source==source);
    CHECK(unit->activeMaterialClaim->access==map.coordToIndex(8,11));
    CHECK(unit->activeMaterialClaim->access!=source);CHECK(unit->activeMaterialClaim->reserve==1);
    CHECK(map.farmFloodGeneration==beforeAvailable+1); // Commit reuses the uninterrupted selection result.
    const auto held=unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
    REQUIRE(unit->selectActiveMaterialClaim(field)); // Own reservation must be excluded on a fresh memo epoch.
    CHECK(unit->activeMaterialClaim==held);CHECK(unit->materialClaimTask==task);
    unit->releaseActiveMaterialClaim();unit->attachedBuilding=nullptr;
    for(const auto holder:holders)map.releaseMaterialClaim(holder);
}

TEST_CASE("stationary carriers claim adjacent stock and deliver with saved continuation" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    {
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9721});
        auto& game=f.game;auto& map=game.map;
        auto definitions=nlohmann::json::parse(game.unitCatalog().serialize());
        auto& definition=definitions["units"][WORKER];
        for(const char* capability:{"walk","swim","fly"})definition["behaviors"][capability]=false;
        game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));game.configureBuildingCatalog();
        game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        map.enableResourceClaims();
        auto* inn=f.addBuilding("inn",10,8);REQUIRE(inn);inn->maxUnitWorking=1;inn->setMaterial(WHEAT,0);inn->updateCallLists();
        auto* unit=f.addUnit(WORKER,9,8);REQUIRE(unit);unit->hungriness=0;
        REQUIRE(unit->performance[WALK]==0);REQUIRE(unit->performance[SWIM]==0);REQUIRE(unit->performance[FLY]==0);
        REQUIRE(map.doesUnitTouchBuilding(unit,inn->gid));
        REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
        const auto source=Uint32(map.coordToIndex(8,8));map.setMaterialAmountSlot(source,WHEAT,1);
        REQUIRE(map.incResourceByIndex(15,15,WHEAT,0));
        glob2test::publishCurrentHiringSuggestions(game);REQUIRE(inn->subscribeToBringMaterialsStep());
        REQUIRE(unit->attachedBuilding==inn);glob2test::discardPrematureHiringSuggestions(game);
        // Remote stock needs travel this carrier cannot do; adjacent stock it
        // can claim. (One uncrowded worker would not engage claims by itself.)
        CHECK_FALSE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(15,15))));
        REQUIRE(unit->acquireMaterialClaimAt(source));REQUIRE(unit->activeMaterialClaimValid());
        CHECK(map.materialClaimLedger().materialCount(source,WHEAT)==1);
        game.invalidateObservation();
        auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
        const auto before=game.checkSum();f.gui.save(&output,"stationary claim continuation");output.flush();
        auto* copied=new GAGCore::MemoryStreamBackend(*bytes);copied->seekFromStart(0);
        auto restored=std::make_unique<glob2test::HeadlessGame>();
        restored->game.map.clear();restored->game.map.resourceCells.clear();
        GAGCore::BinaryInputStream input(copied);REQUIRE(restored->gui.load(&input,true));
        REQUIRE(restored->game.checkSum()==before);CHECK(game.checkSum()==before);
        bool observedClaim=true,delivered=false;
        for(unsigned tick=0;tick<512 && !delivered;++tick) {
            game.step(0);restored->game.step(0);REQUIRE(restored->game.checkSum()==game.checkSum());
            CHECK(unit->posX==9);CHECK(unit->posY==8);
            delivered=game.teams[0]->stats.events().delivered[WHEAT]>0;
        }
        REQUIRE(observedClaim);REQUIRE(delivered);REQUIRE(restored);
        CHECK(game.teams[0]->stats.events().harvested[WHEAT]==1);
        CHECK(game.teams[0]->stats.events().delivered[WHEAT]==1);
        CHECK(map.materialAmountAtSlot(source,WHEAT)==0);CHECK(inn->material(WHEAT)==1);
        CHECK(map.materialClaimLedger().size()==0);
    }
}

TEST_CASE("lost mobility releases claims needing source or consumer travel" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    for(bool remoteConsumer:{false,true}) {
        CAPTURE(remoteConsumer);
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=f.game.map;map.enableResourceClaims();
        auto* inn=f.addBuilding("inn",remoteConsumer?20:10,8);REQUIRE(inn);
        auto* unit=f.addUnit(WORKER,9,8);REQUIRE(unit);
        unit->attachedBuilding=inn;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;
        const int x=remoteConsumer?8:16;REQUIRE(map.incResourceByIndex(x,8,WHEAT,0));
        const auto access=Uint32(map.coordToIndex(x,8));REQUIRE(unit->acquireMaterialClaimAt(access));
        unit->setPerformance(WALK,0);unit->setPerformance(SWIM,0);unit->setPerformance(FLY,0);
        CHECK_FALSE(unit->activeMaterialClaimValid());unit->reconcileMaterialClaim();
        CHECK_FALSE(unit->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
        CHECK_FALSE(unit->acquireMaterialClaimAt(access));
        unit->attachedBuilding=nullptr; // This focused lifecycle fixture did not subscribe to a job.
    }
}


#ifndef __EMSCRIPTEN__
TEST_CASE("claim gradient demand records pursuit evidence on the natural material field" * doctest::test_suite("ActiveMaterialClaims"))
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;map.enableResourceClaims();
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        struct Cleanup { Unit& unit; ~Cleanup(){unit.releaseActiveMaterialClaim();unit.attachedBuilding=nullptr;} } cleanup{*unit};
        unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
        unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
        map.setResourceByIndex(14,8,WHEAT,1);map.setMaterialAmountSlot(Uint32(map.coordToIndex(14,8)),WHEAT,3);
        REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(14,8))));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
        const GradientFieldKey key{GradientFieldFamily::Materials,0,Uint8(accessClass(MovementAccess::Land)),Uint8(WHEAT)};
        auto& fields=map.gradientRuntime->adaptive.fields;
        const auto evidence=[&]()->Uint64{const auto found=fields.find(key);return found==fields.end()?0:Uint64(found->second.total[2]);};
        const auto queries=[&]()->Uint64{const auto found=fields.find(key);return found==fields.end()?0:Uint64(found->second.count128);};
        const auto queried=queries();
        // Route computing: the worker takes the ordinary gradient step, and the
        // claim adds field demand.
        unit->handleActiveMaterialMovement();
        CHECK((unit->dx||unit->dy));
        CHECK(queries()>queried);
        map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
        const auto published=queries();const auto before=evidence();
        // Published route: a routed step is also movement evidence on the
        // natural material field, so adaptive refresh keeps serving it.
        unit->handleActiveMaterialMovement();
        CHECK((unit->dx||unit->dy));REQUIRE(unit->activeMaterialClaim);
        CHECK(evidence()==before+1);CHECK(queries()==published+1);
    }
#endif

namespace
{
// Contention fixtures: inns needing wheat and free workers the ordinary
// hiring step admits, in a game with claims on or off.
struct ContentionRun
{
    std::vector<std::vector<int>> units;   // per tick: x,y,targetX,targetY,validTarget,displacement,carried,claimed per unit
    std::vector<Uint32> checksums;
    unsigned maxClaimed=0,maxDistinctClaims=0;
    bool claimedAndHarvested=false,claimFreeAfterHarvest=true;
    int delivered=0;
};
ContentionRun runContentionFixture(bool claims,unsigned participants,int workers,int inns,const std::vector<std::pair<int,int>>& stock,unsigned ticks)
{
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=5311});
    auto& game=f.game;auto& map=game.map;
    map.computeExecutor().configure(participants);
    game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
    // Both arms keep destination planes, so they differ only in claims.
    if(claims)map.enableResourceClaims();else map.setMaterialDestinationsEnabled(true);
    CHECK(map.activeClaimsEnabled()==claims);
    std::vector<Building*> buildings;
    for(int i=0;i<inns;++i) {
        auto* inn=f.addBuilding("inn",20,2+5*i);REQUIRE(inn);
        inn->maxUnitWorking=1;inn->setMaterial(WHEAT,0);inn->updateCallLists();buildings.push_back(inn);
    }
    std::vector<Unit*> units;
    for(int i=0;i<workers;++i) {
        auto* unit=f.addUnit(WORKER,4,6+i);REQUIRE(unit);
        unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;unit->hungriness=0;units.push_back(unit);
    }
    for(const auto& [x,y]:stock) {REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,1);}
    ContentionRun run;
    std::vector<bool> carried(units.size(),false),claimed(units.size(),false);
    for(unsigned tick=0;tick<ticks;++tick) {
        CAPTURE(tick);
        f.step();
        std::vector<int> row;unsigned holding=0;std::set<Uint32> accesses;
        for(std::size_t i=0;i<units.size();++i) {
            auto* u=units[i];REQUIRE(game.teams[0]->myUnits[Unit::GIDtoID(u->gid)]==u);
            const bool has=u->hasCarriedMaterial(WHEAT);
            if(u->activeMaterialClaim){++holding;accesses.insert(u->activeMaterialClaim->access);claimed[i]=true;}
            if(has && !carried[i] && claimed[i])run.claimedAndHarvested=true;
            // Harvest ends claim mode: no claim is held while carrying.
            if(has && u->activeMaterialClaim)run.claimFreeAfterHarvest=false;
            if(has)claimed[i]=false;
            carried[i]=has;
            row.insert(row.end(),{u->posX,u->posY,u->targetX,u->targetY,int(u->validTarget()),int(u->displacement),int(has),int(bool(u->activeMaterialClaim))});
        }
        // The ledger always holds exactly the units' claims.
        REQUIRE_NOTHROW(map.auditActiveMaterialClaims());
        run.maxClaimed=std::max(run.maxClaimed,holding);run.maxDistinctClaims=std::max(run.maxDistinctClaims,unsigned(accesses.size()));
        run.units.push_back(row);run.checksums.push_back(game.checkSum());
    }
    for(auto* b:buildings)run.delivered+=b->material(WHEAT);
    for(auto* u:units)u->releaseActiveMaterialClaim();
    return run;
}
}

TEST_CASE("an uncrowded worker moves exactly as with claims off" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    const std::vector<std::pair<int,int>> stock{{12,8},{12,20},{26,14}};
    const auto off=runContentionFixture(false,1,1,1,stock,3000);
    const auto claims=runContentionFixture(true,1,1,1,stock,3000);
    CHECK(off.delivered>=2);CHECK(claims.delivered==off.delivered);
    CHECK(claims.maxClaimed==0);
    REQUIRE(claims.units.size()==off.units.size());
    for(std::size_t tick=0;tick<off.units.size();++tick){CAPTURE(tick);REQUIRE(claims.units[tick]==off.units[tick]);}
}

TEST_CASE("crowded workers engage claims, spread, harvest and stay deterministic" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    // Five workers and five inns; one tight cluster of single-unit wheat.
    std::vector<std::pair<int,int>> stock;
    for(int y=7;y<=12;++y)for(int x=12;x<=13;++x)stock.emplace_back(x,y);
    const auto off=runContentionFixture(false,1,5,5,stock,1200);
    const auto one=runContentionFixture(true,1,5,5,stock,1200);
    const auto two=runContentionFixture(true,2,5,5,stock,1200);
    CHECK(one.maxClaimed>=3);
    // Claims are exact capacity on single-unit stock: one holder per source.
    CHECK(one.maxDistinctClaims==one.maxClaimed);
    CHECK(one.claimedAndHarvested);CHECK(one.claimFreeAfterHarvest);
    CHECK(one.delivered>=5);
    MESSAGE("delivered "<<one.delivered<<" (claims off "<<off.delivered<<"), max concurrent claims "<<one.maxClaimed);
    REQUIRE(one.checksums.size()==two.checksums.size());
    for(std::size_t tick=0;tick<one.checksums.size();++tick){CAPTURE(tick);REQUIRE(one.checksums[tick]==two.checksums[tick]);REQUIRE(one.units[tick]==two.units[tick]);}
}

TEST_CASE("contention decisions count same-material fetch targets on the torus and keep a held claim" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    using namespace ResourceClaims::Policy;
    static_assert(ContentionMinWorkers==2 && ContentionRadius==3);
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=5311});
    auto& game=f.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",14,14);REQUIRE(inn);
    const int w=map.getW();
    std::vector<Unit*> units;
    struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim();u->attachedBuilding=nullptr;}} } cleanup{units};
    const auto fetcher=[&](int x,int y,int tx,int ty,int material=WHEAT) {
        auto* u=f.addUnit(WORKER,x,y);REQUIRE(u);units.push_back(u);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;
        u->destinationPurpose=material;u->medical=Unit::MED_FREE;u->targetX=tx;u->targetY=ty;u->setValidTarget(true);
        return u;
    };
    // Self targets column 0. A different material on the same cell and the
    // same material ContentionRadius+1 tiles away never count.
    auto* self=fetcher(4,4,0,8);
    auto* wood=fetcher(7,4,0,8,WOOD);auto* far=fetcher(8,4,4,8);(void)wood;(void)far;
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==0);
    CHECK(map.claimContentionOthers(*self,WOOD,0,8)==1);
    CHECK_FALSE(self->materialClaimDecision()); // 1 < 2: ordinary movement
    // A neighbour targeting the wrapped column w-1 counts: 2 >= 2.
    auto* left=fetcher(5,4,w-1,8);
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==1);
    CHECK(self->materialClaimDecision());
    // So does one exactly ContentionRadius tiles away.
    auto* corner=fetcher(6,4,3,11);
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==2);
    // An invalid ordinary target is not counted.
    corner->setValidTarget(false);left->setValidTarget(false);map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==0);
    CHECK_FALSE(self->materialClaimDecision());
    // A held claim counts at its access and keeps its holder in claim mode,
    // even uncrowded, until it is released; then the worker is ordinary.
    const auto stock=Uint32(map.coordToIndex(10,20));
    map.setResourceByIndex(10,20,WHEAT,1);map.setMaterialAmountSlot(stock,WHEAT,1);
    REQUIRE(left->acquireMaterialClaimAt(stock));
    const auto access=left->activeMaterialClaim->access;
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==0);
    CHECK(map.claimContentionOthers(*self,WHEAT,int(access%Uint32(w)),int(access/Uint32(w)))>=1);
    CHECK(left->materialClaimDecision());
    left->releaseActiveMaterialClaim();
    left->targetX=20;left->targetY=2;left->setValidTarget(true);map.prepareClaimContention(0);
    CHECK_FALSE(left->materialClaimDecision());
    CHECK_FALSE(left->usesMaterialClaimPickup());
}

namespace
{
// Hire-time claim check fixtures: more free workers and wheat-needing inn
// places than claimable stock. Two single units of wheat lie within the
// contention radius of each other, so hired workers crowd them; those whose
// claim selection finds nothing give up their jobs, and the hire check must
// then keep them (or an equivalent worker) from being hired straight back.
struct HireRun
{
    std::vector<Uint32> checksums;
    std::vector<unsigned> releases;  // per worker: jobs given up while not carrying wheat
    Uint64 skipped=0,searched=0;     // hire-time searches with no claimable target / in total
    int delivered=0;
};
HireRun runHireFixture(unsigned participants,unsigned ticks)
{
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7411});
    auto& game=f.game;auto& map=game.map;
    map.computeExecutor().configure(participants);
    game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
    map.enableResourceClaims();
    std::vector<Building*> buildings;
    for(int i=0;i<4;++i) {
        auto* inn=f.addBuilding("inn",20,2+5*i);REQUIRE(inn);
        inn->maxUnitWorking=2;inn->setMaterial(WHEAT,0);inn->updateCallLists();buildings.push_back(inn);
    }
    std::vector<Unit*> units;
    for(int i=0;i<8;++i) {
        auto* unit=f.addUnit(WORKER,4,6+i);REQUIRE(unit);
        unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;unit->hungriness=0;units.push_back(unit);
    }
    for(const auto& [x,y]:std::vector<std::pair<int,int>>{{12,8},{13,10}}) {
        REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,1);
    }
    HireRun run;run.releases.assign(units.size(),0);
    std::vector<bool> attached(units.size(),false);
    for(unsigned tick=0;tick<ticks;++tick) {
        CAPTURE(tick);
        f.step();
        const auto& index=map.claimHireTargetIndex();
        if(index.team==0)for(std::size_t i=0;i<index.requests.size();++i) {
            ++run.searched;
            if(index.results[i].state!=ResourceClaims::HireTarget::State::None)continue;
            ++run.skipped;
            // A worker without a claimable target is never hired for it.
            const auto* u=game.teams[0]->myUnits[Unit::GIDtoID(index.requests[i].gid)];REQUIRE(u);
            CHECK_FALSE((u->attachedBuilding && u->activity==Unit::ACT_FILLING && u->destinationPurpose==index.requests[i].material));
        }
        REQUIRE_NOTHROW(map.auditActiveMaterialClaims());
        for(std::size_t i=0;i<units.size();++i) {
            const bool now=units[i]->attachedBuilding!=nullptr;
            if(attached[i] && !now && !units[i]->hasCarriedMaterial(WHEAT))++run.releases[i];
            attached[i]=now;
        }
        run.checksums.push_back(game.checkSum());
    }
    for(auto* b:buildings)run.delivered+=b->material(WHEAT);
    for(auto* u:units)u->releaseActiveMaterialClaim();
    return run;
}
}

TEST_CASE("hiring skips workers with no claimable target and stays deterministic across threads" * doctest::test_suite("ClaimHireTargets"))
{
    glob2test::HeadlessGlobals globals;
    const auto one=runHireFixture(1,600);
    Uint64 releases=0;unsigned most=0;
    for(auto r:one.releases){releases+=r;most=std::max(most,r);}
    MESSAGE("hire searches "<<one.searched<<", skipped "<<one.skipped<<", releases "<<releases<<", delivered "<<one.delivered);
    CHECK(one.searched>0);
    CHECK(one.skipped>0);
    // Without the check a released worker is hired straight back for the
    // same reserved stock and released again, over and over.
    CHECK(most<=2);
    CHECK(one.delivered==2);
    for(unsigned threads:{2u,4u}) {
        CAPTURE(threads);
        const auto other=runHireFixture(threads,600);
        REQUIRE(other.checksums.size()==one.checksums.size());
        for(std::size_t tick=0;tick<one.checksums.size();++tick){CAPTURE(tick);REQUIRE(other.checksums[tick]==one.checksums[tick]);}
        CHECK(other.releases==one.releases);CHECK(other.skipped==one.skipped);CHECK(other.delivered==one.delivered);
    }
}

TEST_CASE("the parallel hire pass gives the serial results at every thread count" * doctest::test_suite("ClaimHireTargets"))
{
    glob2test::HeadlessGlobals globals;
    // The fixture's passes are far below the parallel threshold; lower it so
    // every pass runs on the compute executor.
    struct Threshold {
        std::size_t saved=ResourceClaims::HireTargetIndex::parallelSearches;
        ~Threshold(){ResourceClaims::HireTargetIndex::parallelSearches=saved;}
    } restore;
    const auto serial=runHireFixture(1,600);
    REQUIRE(serial.searched>0);
    ResourceClaims::HireTargetIndex::parallelSearches=1;
    for(unsigned threads:{1u,2u,4u}) {
        CAPTURE(threads);
        const auto parallel=runHireFixture(threads,600);
        REQUIRE(parallel.checksums.size()==serial.checksums.size());
        for(std::size_t tick=0;tick<serial.checksums.size();++tick){CAPTURE(tick);REQUIRE(parallel.checksums[tick]==serial.checksums[tick]);}
        CHECK(parallel.searched==serial.searched);CHECK(parallel.skipped==serial.skipped);CHECK(parallel.delivered==serial.delivered);
    }
}

TEST_CASE("claims can be turned off at setup, also on a map exported mid-match, but not after a tick" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    for(Uint32 start:{0u,5000u}) {
        CAPTURE(start);
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7411});
        auto& game=f.game;auto& map=game.map;
        game.stepCounter=start; // An exported map keeps the tick it was saved at.
        map.enableResourceClaims();
        REQUIRE_NOTHROW(map.disableResourceClaims());
        CHECK_FALSE(map.activeClaimsEnabled());CHECK_FALSE(map.materialDestinationsEnabled());
        map.enableResourceClaims();
        f.step();
        CHECK_THROWS_AS(map.disableResourceClaims(),std::logic_error);
        CHECK(map.activeClaimsEnabled());
    }
}

TEST_CASE("the hire check rejects reserved stock and cargo-blocked workers and accepts claimable stock" * doctest::test_suite("ClaimHireTargets"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7411});
    auto& game=f.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
    auto* inn=f.addBuilding("inn",20,2);REQUIRE(inn);
    auto* holder=f.addUnit(WORKER,8,8);auto* u=f.addUnit(WORKER,4,6);REQUIRE(holder);REQUIRE(u);
    const auto stock=Uint32(map.coordToIndex(12,8));
    REQUIRE(map.incResourceByIndex(12,8,WHEAT,0));map.setMaterialAmountSlot(stock,WHEAT,1);
    REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
    u->medical=Unit::MED_FREE;u->activity=Unit::ACT_RANDOM;
    // No hiring pass ran this tick, so the check searches on the owner.
    REQUIRE_FALSE(map.claimHireTargetIndex().current(0,game.stepCounter));
    CHECK(u->materialClaimHireAllowed(*inn,WHEAT));
    // With the only stock reserved by another worker there is nothing to claim.
    holder->attachedBuilding=inn;holder->activity=Unit::ACT_FILLING;holder->displacement=Unit::DIS_GOING_TO_RESOURCE;
    holder->destinationPurpose=WHEAT;holder->medical=Unit::MED_FREE;
    REQUIRE(holder->acquireMaterialClaimAt(stock));
    CHECK_FALSE(u->materialClaimHireAllowed(*inn,WHEAT));
    holder->releaseActiveMaterialClaim();holder->attachedBuilding=nullptr;holder->activity=Unit::ACT_RANDOM;
    CHECK(u->materialClaimHireAllowed(*inn,WHEAT));
    // Claim resolution requires cargo room, so a worker carrying another
    // material could never claim wheat and would only be released again.
    u->carriedMaterial=WOOD;
    CHECK_FALSE(u->canCarryMaterial(WHEAT));
    CHECK_FALSE(u->materialClaimHireAllowed(*inn,WHEAT));
    u->carriedMaterial=-1;
}

TEST_CASE("the hire check never runs with claims off" * doctest::test_suite("ClaimHireTargets"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7411});
    auto& game=f.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);
    map.disableResourceClaims();
    auto* inn=f.addBuilding("inn",20,2);REQUIRE(inn);
    inn->maxUnitWorking=2;inn->setMaterial(WHEAT,0);inn->updateCallLists();
    auto* u=f.addUnit(WORKER,4,6);auto* w=f.addUnit(WORKER,4,8);REQUIRE(u);REQUIRE(w);
    REQUIRE(map.incResourceByIndex(12,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(12,8),WHEAT,1);
    for(auto* unit:{u,w}){unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;unit->hungriness=0;}
    u->carriedMaterial=WOOD;
    CHECK(u->materialClaimHireAllowed(*inn,WHEAT));
    u->carriedMaterial=-1;
    bool hired=false;
    for(unsigned tick=0;tick<64;++tick) {
        f.step();
        hired|=w->attachedBuilding==inn || u->attachedBuilding==inn;
        CHECK_FALSE(map.claimHireTargetIndex().current(0,game.stepCounter));
        CHECK(map.claimHireTargetIndex().requests.empty());
    }
    CHECK(hired);
    CHECK(u->materialClaimHireAllowed(*inn,WHEAT));CHECK(w->materialClaimHireAllowed(*inn,WHEAT));
}
