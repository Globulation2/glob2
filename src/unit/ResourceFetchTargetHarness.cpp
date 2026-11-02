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
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <filesystem>
#include <zlib.h>
#include "map/claims/ResourceClaimProbe.h"

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

static void staleTargetIsRefreshedAfterGradientRebuild(int expectedClass, int swimSpeed)
{
	GameGUI gui;
	Game& game = gui.game;
	const bool swimOnly=expectedClass==WATER_ONLY_CLASS;
	game.map.setSize(5, 5, swimOnly?WATER:GRASS); // 32x32
	game.map.setGame(&game);
	for(int y=0;y<game.map.getH();++y)
		for(int x=0;x<game.map.getW();++x) game.map.clearImmobileUnit(x,y);
	if(swimOnly) {
		// The historical seven classes walk to land wheat. The eighth has
		// no land travel, so give it a legal aquatic food source instead.
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
	const int swimClass = unit->swimClass();
	require(swimClass == expectedClass, "exercise the requested swim class");

	glob2test::awaitGradient(game, [&] { return game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass); });
	// Task assignment: ascend the resource gradient once, same call as
	// Unit.cpp/UnitDisplacement.cpp.
	game.map.materialAvailableUpdateSlot(teamNumber, WHEAT, swimClass, unit->posX, unit->posY, &unit->targetX, &unit->targetY, NULL);
	require(unit->targetX == nearX && unit->targetY == nearY, "initial target is the nearer wheat tile");
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
		"initial target is the gradient's goal");

	// One action of walking: the target must not move while it is still valid.
	unit->stepGoingToResource();
	require(unit->targetX == nearX && unit->targetY == nearY, "target holds steady while still valid");

	// Another unit fully harvests the near tile. This mutates the resource
	// layer directly, the same way Map::decResource does; the cached
	// gradient is untouched until something rebuilds it.
	game.map.replaceResource(nearX, nearY, Resource{});
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, nearX, nearY) == GRADIENT_AT_GOAL,
		"the cached gradient does not notice the depletion by itself");

	// Simulate the periodic rebuild every cached gradient gets from
	// Map::syncStep once per its round-robin turn.
	const auto* previous=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
	game.map.updateMaterialGradient(teamNumber, WHEAT, swimClass);
	// Stale guidance remains visible until the fixed publication boundary.
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass)==previous,"refresh keeps the published generation");
	glob2test::awaitGradient(game, [&] {
		const auto* field=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
		return field && field[game.map.coordToIndex(nearX,nearY)]!=GRADIENT_AT_GOAL ? field : nullptr;
	});
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, nearX, nearY) != GRADIENT_AT_GOAL,
		"the rebuilt gradient no longer marks the depleted tile as the goal");

	// The unit takes its next action. pathfindResource reads the fresh
	// gradient and correctly steps toward the far tile; the stored target
	// must follow, not keep pointing at the now-empty near tile.
	unit->stepGoingToResource();
	require(unit->targetX == farX && unit->targetY == farY,
		"target is refreshed to the far wheat tile once the near one is gone");
	require(game.map.getGradient(teamNumber, WHEAT, swimClass, unit->targetX, unit->targetY) == GRADIENT_AT_GOAL,
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
	const int swimClass = unit->swimClass();

	glob2test::awaitGradient(game, [&] { return game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass); });
	require(game.map.getGlobalGradientDestination(game.map.getMaterialGradientSlot(teamNumber, WHEAT, swimClass), unit->posX, unit->posY, &unit->targetX, &unit->targetY),
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
	const auto* previous=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
	game.map.updateMaterialGradient(teamNumber, WHEAT, swimClass);
	// Stale guidance remains visible until the fixed publication boundary.
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass)==previous,"refresh keeps the published generation");
	glob2test::awaitGradient(game, [&] {
		const auto* field=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
		return field && field[game.map.coordToIndex(nearUnitX,nearUnitY)]!=GRADIENT_AT_GOAL ? field : nullptr;
	});

	// Next action: only the near-building tile is left on the gradient: the
	// target must be refreshed to it.
	unit->stepGoingToResource();
	require(unit->targetX == nearBuildingX && unit->targetY == nearBuildingY,
		"target is refreshed to the only remaining wheat tile");

	require(game.map.materialRoutingCacheBytes() == 0, "ordinary natural fetch allocates no supplier cache");
	const auto* natural=game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass);
	const int marketType=game.buildingsTypes.getTypeNum("market",0,false);
	auto* market=game.addBuilding(24,24,marketType,0);
	require(market!=nullptr, "fruit-only market placed");
	market->setMaterial(CHERRY, 1);
	require(!team->directStockSuppliers.empty(), "fruit-only market is a direct supplier");
	require(game.map.materialSupplyModesSlot(inn,WHEAT)==0, "fruit-only supplier cannot alter wheat routing");
	require(game.map.getMaterialGradientSlot(teamNumber,WHEAT,swimClass,true,inn)==natural,
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
        const int movement=unit->movementClass();
        glob2test::awaitGradient(game,[&]{ return map.getMaterialGradientSlot(0,WHEAT,movement); });
        unit->targetX=20; unit->targetY=16;
        REQUIRE(map.incResourceByIndex(16,18,WHEAT,0));
        map.updateMaterialGradient(0,WHEAT,movement);
        glob2test::awaitGradient(game,[&] {
            const auto* field=map.getMaterialGradientSlot(0,WHEAT,movement);
            return field && field[map.coordToIndex(16,18)]==GRADIENT_AT_GOAL ? field : nullptr;
        });
        REQUIRE(map.isGradientPeak(map.getMaterialGradientSlot(0,WHEAT,movement),20,16));
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
        REQUIRE(map.projectMaterialDestination(field.data(),0,WHEAT,0,8,8,&x,&y));
        CHECK(x==9); CHECK(y==8);
        map.replaceResource(9,8,Resource{});
        CHECK_FALSE(map.projectMaterialDestination(field.data(),0,WHEAT,0,8,8,&x,&y));
        CHECK_FALSE(map.projectMaterialDestination(field.data(),0,WHEAT,0,0,0,&x,&y));
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
        const auto* field=glob2test::awaitGradient(game,[&]{ return map.getMaterialGradientSlot(0,WHEAT,0); });
        // Occupy the eastward choices; the selected step may switch basins.
        for (int y=7;y<=9;++y) map.markImmobileUnit(9,y,0);
        EntityRandom reference, observed;
        int rx=0,ry=0,dx=0,dy=0; bool stop=false,otherStop=false,valid=false;
        const auto expected=map.queryMaterialStep(reference,0,WHEAT,0,8,8,&rx,&ry,&stop,nullptr);
        Sint32 x=-1,y=-1,ex=-1,ey=-1;
        const auto actual=map.queryMaterialStep(observed,0,WHEAT,0,8,8,&dx,&dy,&otherStop,nullptr,false,&x,&y,&valid);
        REQUIRE(actual==StepResult::Step); CHECK(actual==expected);
        CHECK(dx==rx); CHECK(dy==ry); CHECK(stop==otherStop); CHECK(observed==reference);
        REQUIRE(map.projectMaterialDestination(field,0,WHEAT,0,8+dx,8+dy,&ex,&ey));
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

	TEST_CASE("a depleted target is refreshed after its gradient rebuild for every swim class")
	{
		glob2test::HeadlessGlobals globals;
		const int swimSpeeds[] = {0, 20, 14, 10, 7, 5, 3};
		static_assert(std::size(swimSpeeds)==WATER_ONLY_CLASS);
		for (int swimClass = 0; swimClass < WATER_ONLY_CLASS; ++swimClass)
			staleTargetIsRefreshedAfterGradientRebuild(swimClass, swimSpeeds[swimClass]);
		staleTargetIsRefreshedAfterGradientRebuild(WATER_ONLY_CLASS,20);
		targetTracksTheGradientTheUnitActuallyFollows();
	}
}

TEST_SUITE("ResourceClaimLifecycle")
{
    TEST_CASE("shadow claims track retargeting interruptions depletion and destruction without gameplay mutation")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
        REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
        const auto first=map.coordToIndex(12,12), second=map.coordToIndex(15,12);
        auto make=[&](int slot) {
            auto* unit=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
            team->attachUnit(slot,unit); map.setGroundUnit(unit->posX,unit->posY,unit->gid);
            unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
            unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
            unit->targetX=12; unit->targetY=12; unit->setValidTarget(true);
            return unit;
        };
        auto* a=make(0); auto* b=make(1);
        a->reconcileMaterialClaim(); CHECK(map.materialClaimLedger().size()==0);
        const auto checksum=game.checkSum(); const auto random=a->entityRandom;
        map.setMaterialClaimTrackingEnabled(true);
        CHECK(map.materialClaimState().at(first)==2); CHECK(game.checkSum()==checksum); CHECK(a->entityRandom==random);
        map.reconcileMaterialClaims(); CHECK(map.materialClaimState().at(first)==2);
        b->targetX=15; b->reconcileMaterialClaim();
        CHECK(map.materialClaimState().at(first)==1); CHECK(map.materialClaimState().at(second)==1);
        a->medical=Unit::MED_DAMAGED; a->reconcileMaterialClaim(); CHECK(map.materialClaimState().at(first)==0);
        a->medical=Unit::MED_FREE; a->reconcileMaterialClaim(); CHECK(map.materialClaimState().at(first)==1);
        a->standardRandomActivity(); CHECK(map.materialClaimState().at(first)==0);
        map.replaceResource(15,12,Resource{}); map.reconcileMaterialClaims(); CHECK(map.materialClaimLedger().size()==0);
        b->targetX=12; b->setValidTarget(true); b->reconcileMaterialClaim();
        REQUIRE(map.materialClaimState().at(first)==1);
        const auto owner=b->scriptIdentity;
        delete team->detachUnit(1); CHECK(map.materialClaimLedger().find(owner)==nullptr);
        CHECK(map.materialClaimState().at(first)==0);
        a->attachedBuilding=inn; a->activity=Unit::ACT_FILLING; a->displacement=Unit::DIS_GOING_TO_RESOURCE;
        a->setValidTarget(true); a->reconcileMaterialClaim(); REQUIRE(map.materialClaimState().at(first)==1);
        // Diagnostic reconstruction after a load uses current unit state only.
        map.setMaterialClaimTrackingEnabled(false); CHECK(map.materialClaimLedger().size()==0);
        map.setMaterialClaimTrackingEnabled(true); REQUIRE(map.materialClaimState().at(first)==1);
        a->hp=-10; a->resolveDeathForTest(); CHECK(a->isDead); CHECK(map.materialClaimState().at(first)==0);
        map.setMaterialClaimTrackingEnabled(false); CHECK(map.materialClaimLedger().size()==0);
    }

    TEST_CASE("harvest animation retains a claim and delivery releases it")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));
        auto* unit=new TestUnit(8,8,0,WORKER,team,0); team->attachUnit(0,unit);
        map.setGroundUnit(8,8,unit->gid); unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
        unit->targetX=9; unit->targetY=8; unit->setValidTarget(true);
        map.setMaterialClaimTrackingEnabled(true);
        unit->stepDisplacement(); REQUIRE(unit->displacement==Unit::DIS_HARVESTING);
        REQUIRE_FALSE(unit->validTarget());
        const auto* claim=map.materialClaimLedger().find(unit->scriptIdentity); REQUIRE(claim);
        CHECK(claim->source==map.coordToIndex(9,8)); CHECK(claim->access==claim->source);
        unit->displacement=Unit::DIS_GOING_TO_BUILDING; unit->reconcileMaterialClaim();
        CHECK(map.materialClaimLedger().size()==0);
        unit->displacement=Unit::DIS_HARVESTING; unit->reconcileMaterialClaim(); REQUIRE(map.materialClaimLedger().size()==1);
        map.addForbidden(9,8,0); unit->setPerformance(FLY,10); unit->reconcileMaterialClaim();
        CHECK(map.materialClaimLedger().size()==0); // Harvest access forbids air too.
    }

    TEST_CASE("pooled farms distinguish the touched access tile from the consumed stock")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::GameOptions options;
        options.header=true; options.loadDefaultRace=true; options.clearImmobile=true;
        options.experiments.set(ExperimentId::FarmAreas,true);
        glob2test::HeadlessGame fixture(options);
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
        for (int x=9;x<=11;++x) { REQUIRE(map.incResourceByIndex(x,8,WHEAT,0)); map.addFarmArea(x,8,0); }
        map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,2);
        map.setMaterialAmountSlot(map.coordToIndex(10,8),WHEAT,2);
        map.setMaterialAmountSlot(map.coordToIndex(11,8),WHEAT,4);
        auto* unit=new TestUnit(8,8,0,WORKER,team,0); team->attachUnit(0,unit);
        unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING; unit->destinationPurpose=WHEAT;
        unit->displacement=Unit::DIS_HARVESTING; unit->dx=1; unit->dy=0; unit->setValidTarget(false);
        map.setMaterialClaimTrackingEnabled(true);
        const auto* claim=map.materialClaimLedger().find(unit->scriptIdentity); REQUIRE(claim);
        CHECK(claim->access==map.coordToIndex(9,8)); CHECK(claim->source==map.coordToIndex(11,8));
        const auto hits=map.materialClaimTelemetry.farmCacheHits;
        unit->reconcileMaterialClaim(); CHECK(map.materialClaimTelemetry.farmCacheHits==hits+1);
        map.setMaterialAmountSlot(map.coordToIndex(11,8),WHEAT,1);
        unit->reconcileMaterialClaim();
        claim=map.materialClaimLedger().find(unit->scriptIdentity); REQUIRE(claim);
        CHECK(claim->source==map.coordToIndex(9,8)); // Stock mutations invalidate the memo.
        map.setMaterialAmountSlot(map.coordToIndex(11,8),WHEAT,4);
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->targetX=9; unit->targetY=8;
        unit->posX=4; unit->setValidTarget(true); unit->reconcileMaterialClaim();
        claim=map.materialClaimLedger().find(unit->scriptIdentity); REQUIRE(claim);
        CHECK(claim->source==map.coordToIndex(11,8));
    }
}

TEST_CASE("sampled claim oracle freezes the actual decision and leaves checksums and RNG untouched" * doctest::test_suite("ResourceClaimProbe"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
    auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
    REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    for (int slot=0;slot<2;++slot) {
        auto* unit=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,unit); map.setGroundUnit(unit->posX,unit->posY,unit->gid);
        unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
        unit->targetX=12; unit->targetY=12; unit->setValidTarget(true);
    }
    const auto* field=glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,team->myUnits[0]->movementClass());});
    map.setMaterialClaimTrackingEnabled(true);
    const auto path=(glob2test::artifactDir()/"resource-claim-probe-test.jsonl.gz").string();
    std::filesystem::remove(path);
    ResourceClaims::Probe::Config config; config.sampleEvery=1; config.maxPending=1; config.maxSamples=2;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);
    map.setMaterialClaimProbe(probe);
    const auto before=game.checkSum(); const auto random=team->myUnits[0]->entityRandom;
    map.observeMaterialClaimDecision(*team->myUnits[0],field);
    REQUIRE(probe->metrics().submitted==1);
    CHECK(probe->metrics().completed==0); CHECK(game.checkSum()==before); CHECK(team->myUnits[0]->entityRandom==random);
    map.observeMaterialClaimDecision(*team->myUnits[0],field);
    CHECK(probe->metrics().excludedQueueLimit==1);
    // The background oracle must still see the source that existed at capture.
    map.replaceResource(15,12,Resource{});
    probe->publish(game.stepCounter); CHECK(probe->metrics().completed==0);
    probe->publish(game.stepCounter+1); REQUIRE(probe->metrics().completed==1);
    probe->finish();
    gzFile input=gzopen(path.c_str(),"rb"); REQUIRE(input);
    std::string text; char buffer[4096]; int count;
    while ((count=gzread(input,buffer,sizeof(buffer)))>0) text.append(buffer,count);
    REQUIRE(count==0); REQUIRE(gzclose(input)==Z_OK);
    std::istringstream lines(text); std::string line; unsigned decisions=0;
    while (std::getline(lines,line)) {
        const auto row=nlohmann::json::parse(line);
        if (row["kind"]=="decision") {
            ++decisions; REQUIRE_FALSE(row["optimal"].is_null());
            CHECK(row["optimal"]["source"]==map.coordToIndex(15,12));
            CHECK(row["patterns"].size()==480);
        }
    }
    CHECK(decisions==1);
}

TEST_CASE("shadow collection preserves every tick of a real worker fetch trace" * doctest::test_suite("ResourceClaimProbe"))
{
    glob2test::HeadlessGlobals globals;
    const auto run=[&](bool shadow) {
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183});
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
        REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
        REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
        for(int slot=0;slot<2;++slot) {
            auto* unit=fixture.addUnit(WORKER,8+slot,8);
            unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
            unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
            unit->targetX=12; unit->targetY=12; unit->setValidTarget(true);
            inn->unitsWorking.push_back(unit);
        }
        glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,team->myUnits[0]->movementClass());});
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(shadow) {
            map.setMaterialClaimTrackingEnabled(true);
            const auto path=(glob2test::artifactDir()/"resource-claim-shadow-trace.jsonl.gz").string();
            std::filesystem::remove(path);
            ResourceClaims::Probe::Config config; config.sampleEvery=1; config.maxSamples=256;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);
            map.setMaterialClaimProbe(probe);
        }
        std::vector<Uint32> trace;
        for(unsigned tick=0;tick<256;++tick) {
            fixture.step(1);
            trace.push_back(game.checkSum());
        }
        if(probe) {
            probe->finish();
            CHECK(probe->metrics().submitted>0);
            CHECK(probe->metrics().completed==probe->metrics().submitted);
        }
        return trace;
    };
    const auto without=run(false);
    const auto with=run(true);
    REQUIRE(with.size()==without.size());
    for(std::size_t tick=0;tick<with.size();++tick) {
        CAPTURE(tick);
        REQUIRE(with[tick]==without[tick]);
    }
}

TEST_CASE("seed-only farm pickup decisions remain eligible oracle failure samples" * doctest::test_suite("ResourceClaimProbe"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options; options.header=true; options.loadDefaultRace=true; options.clearImmobile=true;
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame fixture(options);
    auto& game=fixture.game; auto& map=game.map;
    auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0)); map.addFarmArea(9,8,0);
    map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,1);
    auto* unit=fixture.addUnit(WORKER,8,8);
    unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
    unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
    unit->targetX=9; unit->targetY=8; unit->setValidTarget(true);
    glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,unit->movementClass());});
    map.setMaterialClaimTrackingEnabled(true);
    CHECK(map.materialClaimLedger().size()==0);
    const auto path=(glob2test::artifactDir()/"resource-claim-exhausted-farm.jsonl.gz").string();
    std::filesystem::remove(path);
    ResourceClaims::Probe::Config config; config.sampleEvery=1;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config); map.setMaterialClaimProbe(probe);
    map.observeMaterialClaimPickupDecision(*unit,9,8);
    REQUIRE(probe->metrics().contested==1); REQUIRE(probe->metrics().submitted==1);
    probe->finish(); CHECK(probe->metrics().completed==1);
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
        const auto* field=glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,AIR_MOVEMENT_CLASS);});
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
