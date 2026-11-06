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
#include <zlib.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "map/claims/ResourceClaimProbe.h"
#include "map/claims/ActiveRouteRuntime.h"
#include "map/claims/ClaimPolicyConfig.h"
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
	// Task assignment: ascend the resource gradient once, same call as
	// Unit.cpp/UnitDisplacement.cpp.
	game.map.materialAvailableUpdateSlot(teamNumber, WHEAT, access, unit->posX, unit->posY, &unit->targetX, &unit->targetY, NULL);
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
        REQUIRE(a->scriptIdentity==b->scriptIdentity); // Per-slot generations intentionally match.
        REQUIRE(ResourceClaims::unitOwner(a->gid,a->scriptIdentity)!=ResourceClaims::unitOwner(b->gid,b->scriptIdentity));
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
        const auto owner=ResourceClaims::unitOwner(b->gid,b->scriptIdentity);
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
        const auto* claim=map.materialClaimLedger().find(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity)); REQUIRE(claim);
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
        const auto* claim=map.materialClaimLedger().find(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity)); REQUIRE(claim);
        CHECK(claim->access==map.coordToIndex(9,8)); CHECK(claim->source==map.coordToIndex(11,8));
        const auto hits=map.materialClaimTelemetry.farmCacheHits;
        unit->reconcileMaterialClaim(); CHECK(map.materialClaimTelemetry.farmCacheHits==hits+1);
        map.setMaterialAmountSlot(map.coordToIndex(11,8),WHEAT,1);
        unit->reconcileMaterialClaim();
        claim=map.materialClaimLedger().find(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity)); REQUIRE(claim);
        CHECK(claim->source==map.coordToIndex(9,8)); // Stock mutations invalidate the memo.
        map.setMaterialAmountSlot(map.coordToIndex(11,8),WHEAT,4);
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->targetX=9; unit->targetY=8;
        unit->posX=4; unit->setValidTarget(true); unit->reconcileMaterialClaim();
        claim=map.materialClaimLedger().find(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity)); REQUIRE(claim);
        CHECK(claim->source==map.coordToIndex(11,8));
    }
}

TEST_CASE("sampled claim oracle freezes the actual decision and leaves checksums and RNG untouched" * doctest::test_suite("ResourceClaimProbe"))
{
    glob2test::HeadlessGlobals globals;
    for(bool metadata:{false,true}) for(bool depleted:{false,true}) {
    if(depleted && !metadata)continue;
    CAPTURE(metadata);CAPTURE(depleted);
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
    map.setMaterialDestinationsEnabled(metadata);
    auto* inn=fixture.addBuilding("inn",20,20); REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));
    REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    // Adjacent paint alone is not evidence of protected food donors: include
    // empty paint, a wood deposit, and exactly one requested-material donor.
    map.addForbidden(11,11,0);
    REQUIRE(map.incResourceByIndex(12,11,WOOD,0)); map.addForbidden(12,11,0);
    REQUIRE(map.incResourceByIndex(13,11,WHEAT,0)); map.addForbidden(13,11,0);
    map.setMaterialAmountSlot(map.coordToIndex(13,11),WHEAT,3);
    for (int slot=0;slot<2;++slot) {
        auto* unit=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,unit); map.setGroundUnit(unit->posX,unit->posY,unit->gid);
        unit->attachedBuilding=inn; unit->activity=Unit::ACT_FILLING;
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE; unit->destinationPurpose=WHEAT;
        unit->targetX=12; unit->targetY=12; unit->setValidTarget(true);
    }
    const auto* field=glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,team->myUnits[0]->movementAccess());});
    if(metadata)REQUIRE(map.materialGradientDestinations(field));
    map.setMaterialClaimTrackingEnabled(true);
    const auto path=(glob2test::artifactDir()/(std::string("resource-claim-probe-")+(metadata?"xy":"scalar")+(depleted?"-depleted":"")+".jsonl.gz")).string();
    std::filesystem::remove(path);
    ResourceClaims::Probe::Config config; config.sampleEvery=1; config.maxPending=1; config.maxSamples=2;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);
    map.setMaterialClaimProbe(probe);
    if(depleted) {
        map.replaceResource(12,12,Resource{}); // Publication is deliberately still stale.
        REQUIRE(map.materialAmountAtSlot(map.coordToIndex(12,12),WHEAT)==0);
    }
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
            CHECK(row["features"]["nearby_forbidden_count"]==3);
            CHECK(row["features"]["nearby_forbidden_material_count"]==1);
            CHECK(row["features"]["nearby_forbidden_material_stock"]==3);
            CHECK(row["features"]["nearby_forbidden_growing_material_count"]==1);
            CHECK(row["features"]["nearby_forbidden_growing_material_stock"]==3);
            CHECK(row["reason"]==(depleted?"stale_goal":"fully_claimed"));
            if(depleted) {
                CHECK(row["capacity"]==0);
                CHECK(row["features"]["resource_type"].is_null());
                CHECK(row["features"]["yield_consumption"].is_null());
            }
            CHECK(row["material_destinations_enabled"]==metadata);
            CHECK(row["destination_xy_missing"]==!metadata);
            CHECK(row["destination_lookup"]==(metadata?"published-xy":"weighted-scalar-projection"));
            ++decisions; REQUIRE_FALSE(row["optimal"].is_null());
            CHECK(row["optimal"]["source"]==map.coordToIndex(15,12));
            CHECK(row["patterns"].size()==480);
        }
    }
    CHECK(decisions==1);
    }
}

TEST_CASE("shadow collection preserves every tick of a real worker fetch trace" * doctest::test_suite("ResourceClaimProbe"))
{
    glob2test::HeadlessGlobals globals;
    for(bool metadata:{false,true}) {
    CAPTURE(metadata);
    const auto run=[&](bool shadow) {
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183});
        auto& game=fixture.game; auto& map=game.map; auto* team=game.teams[0];
        map.setMaterialDestinationsEnabled(metadata);
        CHECK(map.materialDestinationsEnabled()==metadata);
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
        glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,team->myUnits[0]->movementAccess());});
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(shadow) {
            map.setMaterialClaimTrackingEnabled(true);
            const auto path=(glob2test::artifactDir()/(metadata?"resource-claim-shadow-trace-xy.jsonl.gz":"resource-claim-shadow-trace-scalar.jsonl.gz")).string();
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
    glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,unit->movementAccess());});
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
            auto& map=f.game.map;map.setMaterialDestinationsEnabled(true);
            map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
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
                REQUIRE(a->acquireMaterialClaimAt(access));a->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
            } else if(material==WOOD)CHECK(map.getResource(access).type==NO_RES_TYPE);
            a->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);b->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
            CHECK(map.materialClaimLedger().size()==0);
            a->attachedBuilding=nullptr;b->attachedBuilding=nullptr;
        }
    }
    TEST_CASE("active reservations enforce capacity and survive unrelated gradient changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
        auto& map=f.game.map;auto* team=f.game.teams[0];
        map.setMaterialDestinationsEnabled(true);
        map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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
        CHECK_THROWS_AS(map.setMaterialClaimTrackingEnabled(true),std::invalid_argument);
        REQUIRE(b->acquireMaterialClaimAt(source));
        map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::Disabled);
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
        map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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

TEST_CASE("active observer samples acquisition without rebuilding authoritative ownership" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& map=f.game.map;auto* team=f.game.teams[0];
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    auto make=[&](int slot) {
        auto* u=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;return u;
    };
    auto* a=make(0);auto* b=make(1);
    const auto* field=glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,a->movementAccess());});
    REQUIRE(a->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto path=(glob2test::artifactDir()/"active-claim-acquisition-probe.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=1;
#ifndef __EMSCRIPTEN__
    // Deep campaign files enforce an 8 GiB disk reserve; browser MEMFS is not
    // a campaign store. Browser coverage retains the ordinary active observer.
    const auto deepDirectory=glob2test::artifactDir()/"resource-claims-deep";
    std::filesystem::remove_all(deepDirectory);
    config.deepContext=nlohmann::json{{"source_revision","diagnostic-working-tree"},
        {"bundle_id","diagnostic-engine-harness"},{"cohort_id","deep-wire-fixture"},
        {"game_id","two-natural-sources"}}.dump();
#endif
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    const auto checksum=f.game.checkSum();const auto claim=a->activeMaterialClaim;
    map.observeMaterialClaimDecision(*b,field);
    REQUIRE(probe->metrics().submitted==1);CHECK(f.game.checkSum()==checksum);
    CHECK(a->activeMaterialClaim==claim);CHECK_FALSE(b->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==1);
    CHECK(map.materialClaimTelemetry.sweeps==0);CHECK_FALSE(map.materialClaimTrackingEnabled());
    probe->finish();CHECK(probe->metrics().completed==1);
#ifndef __EMSCRIPTEN__
    REQUIRE(std::filesystem::is_regular_file(deepDirectory/"event.bin"));
    std::ifstream manifestFile(deepDirectory/"manifest.json");
    const auto manifest=nlohmann::json::parse(manifestFile);
    CHECK(manifest.at("complete")==true); CHECK(manifest.at("censored")==false);
    CHECK(manifest.at("eligible")==1);
#endif
}

TEST_CASE("stratified observer captures acquisition without changing authoritative ownership" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& map=f.game.map;auto* team=f.game.teams[0];
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    auto make=[&](int slot) {
        auto* u=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;return u;
    };
    auto* a=make(0);auto* b=make(1);
    const auto* field=glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,a->movementAccess());});
    REQUIRE(a->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto path=(glob2test::artifactDir()/"active-claim-acquisition-probe.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=1;
#ifndef __EMSCRIPTEN__
    config.deepStratified=true;
#endif
#ifndef __EMSCRIPTEN__
    // Deep campaign files enforce an 8 GiB disk reserve; browser MEMFS is not
    // a campaign store. Browser coverage retains the ordinary active observer.
    const auto deepDirectory=glob2test::artifactDir()/"resource-claims-deep";
    std::filesystem::remove_all(deepDirectory);
    config.deepContext=nlohmann::json{{"source_revision","diagnostic-working-tree"},
        {"bundle_id","diagnostic-engine-harness"},{"cohort_id","deep-wire-fixture"},
        {"game_id","two-natural-sources"}}.dump();
#endif
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    const auto checksum=f.game.checkSum();const auto claim=a->activeMaterialClaim;
    map.observeMaterialClaimDecision(*b,field);
    REQUIRE(probe->metrics().submitted==1);CHECK(f.game.checkSum()==checksum);
    CHECK(a->activeMaterialClaim==claim);CHECK_FALSE(b->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==1);
    CHECK(map.materialClaimTelemetry.sweeps==0);CHECK_FALSE(map.materialClaimTrackingEnabled());
    probe->finish();CHECK(probe->metrics().completed==1);
#ifndef __EMSCRIPTEN__
    REQUIRE(std::filesystem::is_regular_file(deepDirectory/"event-00.bin"));
    std::ifstream manifestFile(deepDirectory/"manifest.json");
    const auto manifest=nlohmann::json::parse(manifestFile);
    CHECK(manifest.at("complete")==true); CHECK(manifest.at("censored")==false);
    CHECK(manifest.at("version")==2);
    REQUIRE(manifest.at("strata").size()==16);
    CHECK(manifest.at("strata")[0].at("eligible")==1);
    CHECK(manifest.at("peak_input_payload").get<Uint64>()<=manifest.at("peak_input_reservations").get<Uint64>());
#endif
}

#ifndef __EMSCRIPTEN__
TEST_CASE("top-four observer captures four of five acquisitions without changing ownership" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& map=f.game.map;auto* team=f.game.teams[0];
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    auto make=[&](int slot) {
        auto* u=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;return u;
    };
    auto* a=make(0);auto* b=make(1);
    const auto* field=glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,a->movementAccess());});
    REQUIRE(a->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto path=(glob2test::artifactDir()/"active-claim-acquisition-probe.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=5;
#ifndef __EMSCRIPTEN__
    config.deepStratified=true;config.deepTopFour=true;
#endif
#ifndef __EMSCRIPTEN__
    // Deep campaign files enforce an 8 GiB disk reserve; browser MEMFS is not
    // a campaign store. Browser coverage retains the ordinary active observer.
    const auto deepDirectory=glob2test::artifactDir()/"resource-claims-deep";
    std::filesystem::remove_all(deepDirectory);
    config.deepContext=nlohmann::json{{"source_revision","diagnostic-working-tree"},
        {"bundle_id","diagnostic-engine-harness"},{"cohort_id","deep-wire-fixture"},
        {"game_id","two-natural-sources"}}.dump();
#endif
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    const auto checksum=f.game.checkSum();const auto claim=a->activeMaterialClaim;
    for(unsigned i=0;i<5;++i)map.observeMaterialClaimDecision(*b,field);
    REQUIRE(probe->metrics().submitted==5);CHECK(f.game.checkSum()==checksum);
    CHECK(a->activeMaterialClaim==claim);CHECK_FALSE(b->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==1);
    CHECK(map.materialClaimTelemetry.sweeps==0);CHECK_FALSE(map.materialClaimTrackingEnabled());
    probe->finish();CHECK(probe->metrics().completed==5);
#ifndef __EMSCRIPTEN__
    REQUIRE(std::filesystem::is_regular_file(deepDirectory/"event-00-0.bin"));
    std::ifstream manifestFile(deepDirectory/"manifest.json");
    const auto manifest=nlohmann::json::parse(manifestFile);
    CHECK(manifest.at("complete")==true); CHECK(manifest.at("censored")==false);
    CHECK(manifest.at("version")==3);
    REQUIRE(manifest.at("strata").size()==16);
    CHECK(manifest.at("strata")[0].at("eligible")==5);
    CHECK(manifest.at("strata")[0].at("retained")==4);
    CHECK(manifest.at("strata")[0].at("weight_numerator")==5);
    CHECK(manifest.at("strata")[0].at("weight_denominator")==4);
    CHECK(manifest.at("peak_input_payload").get<Uint64>()<=manifest.at("peak_input_reservations").get<Uint64>());
#endif
}

#endif

TEST_CASE("map export projects active ownership out without mutating the live match" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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
    REQUIRE(restored.game.load(&input));CHECK_FALSE(restored.game.map.activeClaimsEnabled());
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
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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
        map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* innA=f.addBuilding("inn",20,20,0,0);auto* innB=f.addBuilding("inn",40,40,0,1);REQUIRE(innA);REQUIRE(innB);
    auto* a=f.addUnit(WORKER,8,8,0);auto* b=f.addUnit(WORKER,8,9,1);REQUIRE(a);REQUIRE(b);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));const auto source=Uint32(map.coordToIndex(9,8));
    map.setMaterialAmountSlot(source,WHEAT,2);map.addFarmArea(9,8,0);
    hireClaimWorker(*a,*innA);hireClaimWorker(*b,*innB);
    REQUIRE(a->acquireMaterialClaimAt(source));CHECK(a->activeMaterialClaim->reserve==1);
    CHECK_FALSE(b->acquireMaterialClaimAt(source));
    a->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);REQUIRE(b->acquireMaterialClaimAt(source));CHECK(b->activeMaterialClaim->reserve==0);
    CHECK_FALSE(a->acquireMaterialClaimAt(source));b->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
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

TEST_CASE("active fetch moves harvests and delivers across threads and pending saves" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    const auto run=[&](unsigned participants) {
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=7183});
        auto& game=f.game; auto& map=game.map;
        map.computeExecutor().configure(participants);
        game.gameHeader.setResourceGrowthDisabled(true);
        game.gameHeader.getWinningConditions().clear(); // Let CLI parity runs continue through delivery.
        map.setMaterialDestinationsEnabled(true);
        map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
        auto* inn=f.addBuilding("inn",18,8); REQUIRE(inn);
        inn->maxUnitWorking=1; inn->setMaterial(WHEAT,0); inn->updateCallLists();
        auto* unit=f.addUnit(WORKER,8,8); REQUIRE(unit);
        unit->medical=Unit::MED_FREE; unit->activity=Unit::ACT_RANDOM; unit->hungriness=0;
        const auto gid=unit->gid; const auto buildingGid=inn->gid;
        REQUIRE(map.incResourceByIndex(13,8,WHEAT,0));
        const auto source=Uint32(map.coordToIndex(13,8)); map.setMaterialAmountSlot(source,WHEAT,1);
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(inn->subscribeToBringMaterialsStep());
        REQUIRE(unit->attachedBuilding==inn); REQUIRE(inn->unitsWorking.size()==1);
        glob2test::discardPrematureHiringSuggestions(game);
        REQUIRE_FALSE(unit->activeMaterialClaim);
        std::unique_ptr<glob2test::HeadlessGame> pendingCopy, cachedCopy;
        const auto copyGame=[&](const char* label) {
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);
            const auto before=game.checkSum(); f.gui.save(&output,label); output.flush();
            const auto bytes=backend->takeContents(); CHECK(game.checkSum()==before);
            if(participants==1) {
                std::ofstream saved(glob2test::artifactDir()/(std::string(label)+".game"),std::ios::binary);
                saved.write(bytes.data(),std::streamsize(bytes.size())); REQUIRE(saved.good());
            }
            auto result=std::make_unique<glob2test::HeadlessGame>();
            result->game.map.clear();
            // clear() retains cell storage. Remove the old extent so loading
            // cannot validate a saved claim against the fixture's preset map.
            result->game.map.resourceCells.clear();
            REQUIRE(result->game.map.cellCount()==0);
            result->game.map.computeExecutor().configure(participants);
            auto* inputBackend=new GAGCore::MemoryStreamBackend;
            inputBackend->write(bytes.data(),bytes.size()); inputBackend->seekFromStart(0);
            GAGCore::BinaryInputStream input(inputBackend); REQUIRE(result->gui.load(&input,true));
            REQUIRE(result->game.checkSum()==before);
            return result;
        };
        std::vector<Uint32> trace;
        std::ofstream evidence(glob2test::artifactDir()/("active-fetch-"+std::to_string(participants)+"-participants.tsv"));
        REQUIRE(evidence.good()); evidence<<"tick\tchecksum\tx\ty\tclaim\tstock\tdelivered\n";
        bool moved=false, harvested=false; Uint64 pendingTick=0;
        for(unsigned tick=0;tick<2048;++tick) {
            CAPTURE(participants); CAPTURE(tick);
            f.step();
            for(auto* copy:{pendingCopy.get(),cachedCopy.get()}) if(copy) {
                copy->step(); REQUIRE(copy->game.checkSum()==game.checkSum());
                CHECK(copy->game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)]->material(WHEAT)==inn->material(WHEAT));
            }
            REQUIRE(game.teams[0]->myUnits[Unit::GIDtoID(gid)]==unit);
            CHECK(unit->materialClaimPolicyState==ResourceClaims::MaterialClaimPolicyState{});
            const bool traveling=unit->activeMaterialClaim && unit->displacement==Unit::DIS_GOING_TO_RESOURCE;
            if(traveling && !pendingCopy && map.materialClaimRoutes && !map.materialClaimRoutes->states.empty()) {
                const auto& state=map.materialClaimRoutes->states.begin()->second;
                REQUIRE(state.pending); CHECK(state.due==game.stepCounter);
                CHECK(unit->posX==8); CHECK(unit->posY==8);
                CHECK(map.materialClaimState().at(source)==1);
                pendingTick=game.stepCounter; pendingCopy=copyGame("active-fetch-pending");
            }
            if(traveling && pendingCopy && !cachedCopy && map.materialClaimRoutes && !map.materialClaimRoutes->states.empty()) {
                const auto& state=map.materialClaimRoutes->states.begin()->second;
                if(!state.pending) {
                    CHECK(game.stepCounter>pendingTick); REQUIRE(state.route.status==ResourceClaims::RouteStatus::Found);
                    cachedCopy=copyGame("active-fetch-cached");
                }
            }
            moved|=unit->posX!=8 || unit->posY!=8;
            harvested|=unit->hasCarriedMaterial(WHEAT);
            if(unit->hasCarriedMaterial(WHEAT)) CHECK_FALSE(unit->activeMaterialClaim);
            trace.push_back(game.checkSum());
            evidence<<game.stepCounter<<'\t'<<trace.back()<<'\t'<<unit->posX<<'\t'<<unit->posY<<'\t'
                <<bool(unit->activeMaterialClaim)<<'\t'<<map.materialAmountAtSlot(source,WHEAT)<<'\t'<<inn->material(WHEAT)<<'\n';
            if(inn->material(WHEAT)==1)break;
        }
        REQUIRE(pendingCopy); REQUIRE(cachedCopy); CHECK(moved); CHECK(harvested);
        CHECK(inn->material(WHEAT)==1); CHECK(map.materialAmountAtSlot(source,WHEAT)==0);
        CHECK(map.materialClaimLedger().size()==0); CHECK(evidence.good());
        return trace;
    };
    const auto ownerOnly=run(1), parallel=run(3);
    REQUIRE(ownerOnly.size()==parallel.size());
    for(std::size_t tick=0;tick<ownerOnly.size();++tick) {CAPTURE(tick); REQUIRE(ownerOnly[tick]==parallel[tick]);}
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
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);auto* unit=f.addUnit(WORKER,8,8);REQUIRE(inn);REQUIRE(unit);
    REQUIRE(map.incResourceByIndex(12,12,customType,0));hireClaimWorker(*unit,*inn);
    REQUIRE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));
    const auto valid=*unit->activeMaterialClaim;
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
}

TEST_CASE("experimental fallback choices preserve or release the actual assignment" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    using Config=ResourceClaims::ClaimPolicyConfig;
    for(auto fallback:{Config::Fallback::Wait,Config::Fallback::Roam,Config::Fallback::ReleaseJob,Config::Fallback::BoundedWaitRelease}) {
        CAPTURE(unsigned(fallback));
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=f.game.map;map.setMaterialDestinationsEnabled(true);
        Config config;config.fallback=fallback;config.fallbackAfterDecisions=2;map.setMaterialClaimPolicyConfig(config);
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
        // stock is reserved and this worker must wait instead of descending.
        CHECK(map.gradientRuntime->adaptive.fields.at(demandKey).queries[queryBucket]==beforeQueries+1);
        CHECK(seeker->materialClaimPolicyState.decisionOrdinal==1);
        CHECK(seeker->materialClaimPolicyState.fallbackCount==1);
        CHECK_FALSE(seeker->activeMaterialClaim);
        if(fallback==Config::Fallback::ReleaseJob) {CHECK(seeker->attachedBuilding==nullptr);CHECK(b->unitsWorking.empty());}
        else {
            CHECK(seeker->attachedBuilding==b);
            CHECK(seeker->movement==(fallback==Config::Fallback::Roam?Unit::MOV_RANDOM_GROUND:Unit::MOV_GOING_DX_DY));
            seeker->handleActiveMaterialMovement();
            CHECK(seeker->materialClaimPolicyState.decisionOrdinal==2);
            CHECK((seeker->attachedBuilding==nullptr)==(fallback==Config::Fallback::BoundedWaitRelease));
        }
        CHECK(holder->activeMaterialClaim==held);CHECK(map.materialClaimLedger().size()==1);
    }
}

TEST_CASE("experimental destination cadence retains failed refresh and same claim identity" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=f.game.map;map.setMaterialDestinationsEnabled(true);
    ResourceClaims::ClaimPolicyConfig config;config.destinationRefreshJ=2;
    config.pattern="spiral:k1:unit:s1:every:forward:d0:seed1";map.setMaterialClaimPolicyConfig(config);
    auto* a=f.addBuilding("inn",20,20);auto* b=f.addBuilding("inn",24,20);REQUIRE(a);REQUIRE(b);
    auto* seeker=f.addUnit(WORKER,8,8);auto* holder=f.addUnit(WORKER,8,10);REQUIRE(seeker);REQUIRE(holder);
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));REQUIRE(map.incResourceByIndex(24,24,WHEAT,0));
    const auto near=Uint32(map.coordToIndex(9,8)),far=Uint32(map.coordToIndex(24,24));
    map.setMaterialAmountSlot(near,WHEAT,1);
    hireClaimWorker(*seeker,*a);hireClaimWorker(*holder,*b);
    REQUIRE(seeker->acquireMaterialClaimAt(far));REQUIRE(holder->acquireMaterialClaimAt(near));
    const auto oldClaim=seeker->activeMaterialClaim;const auto oldTask=seeker->materialClaimTask;
    f.game.observeCompletedWorld(); // Route submission reads the last completed world.
    seeker->handleActiveMaterialMovement();CHECK(seeker->materialClaimPolicyState.lastGoalAttempt==0);
    seeker->handleActiveMaterialMovement();CHECK(seeker->materialClaimPolicyState.lastGoalAttempt==2);
    CHECK(seeker->activeMaterialClaim==oldClaim);CHECK(seeker->materialClaimTask==oldTask);
    holder->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
    seeker->handleActiveMaterialMovement();CHECK(seeker->activeMaterialClaim==oldClaim);
    seeker->handleActiveMaterialMovement();REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==near);
    const auto sameTask=seeker->materialClaimTask;const auto sameProbe=seeker->materialClaimPolicyState.probe;
    seeker->handleActiveMaterialMovement();seeker->handleActiveMaterialMovement();
    CHECK(seeker->materialClaimTask==sameTask);CHECK(seeker->materialClaimPolicyState.probe==sameProbe);
    CHECK(seeker->materialClaimPolicyState.lastGoalAttempt==6);
    f.game.observeCompletedWorld();
    auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(backend);
    f.gui.save(&output,"experimental-cadence");output.flush();const auto bytes=backend->takeContents();
    GameGUI resumed;auto* inputBackend=new GAGCore::MemoryStreamBackend;inputBackend->write(bytes.data(),bytes.size());inputBackend->seekFromStart(0);
    GAGCore::BinaryInputStream input(inputBackend);REQUIRE(resumed.load(&input,true));
    REQUIRE(resumed.game.checkSum()==f.game.checkSum());
    CHECK(resumed.game.map.materialClaimPolicyConfig()==config);
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
        map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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

TEST_CASE("experimental farm selection memo preserves pooled source and resets between decisions" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.wDec=5;options.hDec=5;options.discovered=true;options.clearImmobile=true;options.loadDefaultRace=true;
    options.header=true;options.seed=9183;options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame f(options);auto& map=f.game.map;
    map.setMaterialDestinationsEnabled(true);
    ResourceClaims::ClaimPolicyConfig config;config.selection=ResourceClaims::ClaimPolicyConfig::Selection::ClosestGeometric;
    map.setMaterialClaimPolicyConfig(config);
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
    unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;
    for(const auto holder:holders)map.releaseMaterialClaim(holder);
}

TEST_CASE("stationary carriers claim adjacent stock and deliver with saved continuation" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    for(bool experimental:{false,true}) {
        CAPTURE(experimental);
        glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9721});
        auto& game=f.game;auto& map=game.map;
        auto definitions=nlohmann::json::parse(game.unitCatalog().serialize());
        auto& definition=definitions["units"][WORKER];
        for(const char* capability:{"walk","swim","fly"})definition["behaviors"][capability]=false;
        game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));game.configureBuildingCatalog();
        game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        map.setMaterialDestinationsEnabled(true);
        if(experimental)map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
        else map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
        auto* inn=f.addBuilding("inn",10,8);REQUIRE(inn);inn->maxUnitWorking=1;inn->setMaterial(WHEAT,0);inn->updateCallLists();
        auto* unit=f.addUnit(WORKER,9,8);REQUIRE(unit);unit->hungriness=0;
        REQUIRE(unit->performance[WALK]==0);REQUIRE(unit->performance[SWIM]==0);REQUIRE(unit->performance[FLY]==0);
        REQUIRE(map.doesUnitTouchBuilding(unit,inn->gid));
        REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
        const auto source=Uint32(map.coordToIndex(8,8));map.setMaterialAmountSlot(source,WHEAT,1);
        REQUIRE(map.incResourceByIndex(15,15,WHEAT,0));
        glob2test::publishCurrentHiringSuggestions(game);REQUIRE(inn->subscribeToBringMaterialsStep());
        REQUIRE(unit->attachedBuilding==inn);glob2test::discardPrematureHiringSuggestions(game);
        CHECK_FALSE(unit->acquireMaterialClaimAt(Uint32(map.coordToIndex(15,15))));
        std::unique_ptr<glob2test::HeadlessGame> restored;
        bool observedClaim=false,delivered=false;
        for(unsigned tick=0;tick<512 && !delivered;++tick) {
            game.step(0);
            if(restored) {restored->game.step(0);REQUIRE(restored->game.checkSum()==game.checkSum());}
            CHECK(unit->posX==9);CHECK(unit->posY==8);
            if(unit->activeMaterialClaim && !observedClaim) {
                observedClaim=true;REQUIRE(unit->activeMaterialClaimValid());
                CHECK(map.materialClaimLedger().materialCount(source,WHEAT)==1);
                const auto snapshot=SimulationSnapshot::capture(game,SimulationSnapshot::captureCatalog(game));
                REQUIRE(snapshot.claims);CHECK(snapshot.claims->counts.cells[source]==1);
                auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
                const auto before=game.checkSum();f.gui.save(&output,"stationary claim continuation");output.flush();
                auto* copied=new GAGCore::MemoryStreamBackend(*bytes);copied->seekFromStart(0);
                restored=std::make_unique<glob2test::HeadlessGame>();
                restored->game.map.clear();restored->game.map.resourceCells.clear();
                GAGCore::BinaryInputStream input(copied);REQUIRE(restored->gui.load(&input,true));
                REQUIRE(restored->game.checkSum()==before);CHECK(game.checkSum()==before);
            }
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
        auto& map=f.game.map;map.setMaterialDestinationsEnabled(true);
        map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
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

TEST_CASE("shadow tracks stationary adjacent harvesting without changing execution" * doctest::test_suite("ResourceClaimLifecycle"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=f.game;auto& map=game.map;
    auto* inn=f.addBuilding("inn",10,8);REQUIRE(inn);inn->maxUnitWorking=1;inn->setMaterial(WHEAT,0);inn->updateCallLists();
    auto* unit=f.addUnit(WORKER,9,8);REQUIRE(unit);unit->hungriness=0;
    unit->setPerformance(WALK,0);unit->setPerformance(SWIM,0);unit->setPerformance(FLY,0);
    REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));const auto source=Uint32(map.coordToIndex(8,8));
    glob2test::publishCurrentHiringSuggestions(game);REQUIRE(inn->subscribeToBringMaterialsStep());
    REQUIRE(unit->attachedBuilding==inn);glob2test::discardPrematureHiringSuggestions(game);
    map.setMaterialClaimTrackingEnabled(true);REQUIRE_FALSE(map.activeClaimsEnabled());
    bool harvesting=false;
    for(unsigned tick=0;tick<128;++tick) {
        game.step(0);
        if(unit->displacement==Unit::DIS_HARVESTING) {
            harvesting=true;const auto before=game.checkSum();const auto random=unit->entityRandom;
            unit->reconcileMaterialClaim();
            CHECK(map.materialClaimLedger().materialCount(source,WHEAT)==1);
            CHECK(game.checkSum()==before);CHECK(unit->entityRandom==random);
            break;
        }
    }
    REQUIRE(harvesting);
    unit->stopAttachedForBuilding(false);unit->reconcileMaterialClaim();
    CHECK(map.materialClaimLedger().size()==0);
}

#ifndef __EMSCRIPTEN__
TEST_CASE("missing destination observer distinguishes absent fields from stale invalid labels" * doctest::test_suite("ActiveMaterialClaims"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& map=f.game.map;
    { Map::TerrainEditBatch edit(map);for(int y=0;y<32;++y)for(int x:{0,1,16,17})map.setVertexTerrain(x,y,WATER); }
    REQUIRE(map.incResourceByIndex(24,8,WHEAT,0)); // Seed only the other disconnected island.
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);auto* u=f.addUnit(WORKER,8,8);REQUIRE(u);u->setPerformance(SWIM,0);u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;
    const auto* field=glob2test::awaitGradient(f.game,[&]{const auto* value=map.getMaterialGradientSlot(0,WHEAT,u->movementAccess());return map.materialGradientDestinations(value)?value:nullptr;});REQUIRE(field);
    const auto labels=map.materialGradientDestinations(field);REQUIRE(labels);
    REQUIRE(labels->x[map.coordToIndex(8,8)]>=map.getW());
    const auto root=glob2test::artifactDir();const auto path=(root/"missing-observer.jsonl.gz").string();std::filesystem::remove(path);
    std::filesystem::remove_all(root/"resource-claims-deep");std::filesystem::remove_all(root/"resource-claims-missing");
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.deepStratified=true;config.deepTopFour=true;config.deepTopK=16;config.deepMissing=true;config.missingSampleEvery=1;
    config.deepContext=nlohmann::json{{"source_revision","diagnostic"},{"bundle_id","diagnostic"},{"cohort_id","missing-wire"},{"game_id","missing-finite"}}.dump();
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    const auto before=f.game.checkSum();probe->missingDecision(f.game,*u,nullptr,UINT32_MAX,0,true);CHECK(f.game.checkSum()==before);CHECK(probe->metrics().decisions==0);
    probe->publish(f.game.stepCounter+1); // diagnostic deadline only; no simulation tick advanced
    REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));
    const auto withStock=f.game.checkSum();map.observeMaterialClaimDecision(*u,field);CHECK(f.game.checkSum()==withStock);
    probe->finish();CHECK(f.game.checkSum()==withStock);CHECK_FALSE(u->activeMaterialClaim);CHECK(map.materialClaimTelemetry.sweeps==0);
    std::ifstream file(root/"resource-claims-missing/manifest.json");auto manifest=nlohmann::json::parse(file);
    CHECK(manifest.at("complete")==true);CHECK(manifest.at("censored")==false);CHECK(manifest.at("calls")==2);
    CHECK(manifest.at("strata")[0].at("eligible")==1);CHECK(manifest.at("strata")[4].at("eligible")==1);
    REQUIRE(std::filesystem::is_regular_file(root/"resource-claims-missing/event-00.bin"));
    REQUIRE(std::filesystem::is_regular_file(root/"resource-claims-missing/event-04.bin"));
}
#endif

#ifndef __EMSCRIPTEN__
TEST_CASE("capture-only preserves bottom-k membership while skipping nonretained work" * doctest::test_suite("ActiveMaterialClaims"))
{
    // Legacy V4 retains capacity 16; V5 extends retention to 64. Each runs in
    // an independent fixture (the registry runs whole test cases, not subcases).
    for (const unsigned capacity:{16u,64u}) {
    CAPTURE(capacity);
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& map=f.game.map;auto* team=f.game.teams[0];
    map.setMaterialDestinationsEnabled(true);map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
    auto* inn=f.addBuilding("inn",20,20);REQUIRE(inn);
    REQUIRE(map.incResourceByIndex(12,12,WHEAT,0));REQUIRE(map.incResourceByIndex(15,12,WHEAT,0));
    auto make=[&](int slot) {
        auto* u=new TestUnit(8+slot,8,Unit::GIDfrom(slot,0),WORKER,team,0);
        team->attachUnit(slot,u);map.setGroundUnit(u->posX,u->posY,u->gid);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;u->destinationPurpose=WHEAT;return u;
    };
    auto* a=make(0);auto* b=make(1);
    const auto* field=glob2test::awaitGradient(f.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,a->movementAccess());});
    REQUIRE(a->acquireMaterialClaimAt(Uint32(map.coordToIndex(12,12))));

    nlohmann::json manifests[2];Uint64 submitted[2]{};
    const auto checksum=f.game.checkSum();
    for(unsigned mode=0;mode<2;++mode) {
        const auto directory=glob2test::artifactDir()/(mode?"capture-only":"capture-all");
        std::filesystem::remove_all(directory);std::filesystem::create_directories(directory);
        ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=96;
        config.deepStratified=true;config.deepTopFour=true;config.deepTopK=capacity;config.captureOnly=mode;
        config.deepContext=nlohmann::json{{"source_revision","fixture"},{"bundle_id","fixture"},
            {"cohort_id","retained-parity"},{"game_id","same-frozen-world"}}.dump();
        auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),(directory/"trace.jsonl.gz").string(),config);
        map.setMaterialClaimProbe(probe);
        for(unsigned event=0;event<96;++event) {
            map.observeMaterialClaimDecision(*b,field);
            probe->publish(f.game.stepCounter+1);
            CHECK(f.game.checkSum()==checksum);
            CHECK(a->activeMaterialClaim.has_value());CHECK_FALSE(b->activeMaterialClaim);
        }
        probe->finish();CHECK(probe->metrics().sampled==96);CHECK(probe->metrics().contested==96);
        submitted[mode]=probe->metrics().submitted;CHECK(probe->metrics().completed==submitted[mode]);
        std::ifstream input(directory/"resource-claims-deep/manifest.json");manifests[mode]=nlohmann::json::parse(input);
        CHECK(manifests[mode]["censored"]==false);
        CHECK(manifests[mode]["version"]==(capacity>16?5:4));
        CHECK(manifests[mode]["top_k"]==capacity);
        map.setMaterialClaimProbe(nullptr);
    }
    CHECK(submitted[0]==96);CHECK(submitted[1]>=capacity);CHECK(submitted[1]<submitted[0]);
    CHECK(manifests[1]["work_scope"]=="retained-replacements-only-v1");
    for(unsigned stratum=0;stratum<16;++stratum) {
        auto left=manifests[0]["strata"][stratum],right=manifests[1]["strata"][stratum];
        for(auto* row:{&left,&right})for(auto& record:(*row)["records"])record.erase("file_sha256"); // Worker elapsed timing is deliberately non-identical.
        CHECK(left==right);
    }
    }
}
TEST_CASE("claim gradient demand records pursuit evidence on the natural material field" * doctest::test_suite("ActiveMaterialClaims"))
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        for(bool demand:{false,true}) {
            CAPTURE(demand);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
            Config config;config.claimGradientDemand=demand;map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            struct Cleanup { Unit& unit; ~Cleanup(){unit.releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit.attachedBuilding=nullptr;} } cleanup{*unit};
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
            const auto before=evidence(),queried=queries();
            unit->handleActiveMaterialMovement(); // route computing: demand only
            CHECK(unit->dx==0);CHECK(unit->dy==0);
            CHECK(evidence()==before);CHECK(queries()==queried+(demand?1:0));
            map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
            const auto published=queries();
            unit->handleActiveMaterialMovement(); // published route: a real step
            CHECK((unit->dx||unit->dy));REQUIRE(unit->activeMaterialClaim);
            CHECK(evidence()==before+(demand?1:0));CHECK(queries()==published+(demand?1:0));
        }
    }
#endif

namespace
{
// claim_trigger "contention" fixtures: inns needing wheat and free workers the
// ordinary hiring step admits; the policy is the production v5 policy with the
// contention trigger, optionally with the combined (combo A) options.
ResourceClaims::ClaimPolicyConfig contentionPolicy(bool combined,unsigned minWorkers=3,unsigned radius=1)
{
    using Config=ResourceClaims::ClaimPolicyConfig;
    Config c;c.sourceVersion=5;c.pattern="final-r32-exact24-assembled-v1";
    c.patternFrame=Config::PatternFrame::NearestD4;c.destinationRefreshJ=12;c.opportunisticAdjacentPickup=true;
    c.claimTrigger=Config::ClaimTrigger::Contention;c.contentionMinWorkers=minWorkers;c.contentionRadius=radius;
    if(combined) {
        c.refreshSwitchMarginPermille=250;c.fallback=Config::Fallback::ReleaseJob;c.claimGradientDemand=true;
        c.pendingStep=Config::PendingStep::Gradient;c.maxClaimDistanceFactorPermille=2000;
        c.trafficSidestep=true;c.trafficWaitLimit=8;c.terrainComponentPrefilter=true;
    }
    c.validate();
    return c;
}
struct ContentionRun
{
    std::vector<std::vector<int>> units;   // per tick: x,y,targetX,targetY,validTarget,displacement,carried,claimed per unit
    std::vector<Uint32> checksums;
    unsigned maxClaimed=0,maxDistinctClaims=0;
    bool claimedAndHarvested=false,claimFreeAfterHarvest=true;
    int delivered=0;
};
enum class ContentionMode { Off, Contention, Combined };
ContentionRun runContentionFixture(ContentionMode mode,unsigned participants,int workers,int inns,const std::vector<std::pair<int,int>>& stock,unsigned ticks)
{
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=5311});
    auto& game=f.game;auto& map=game.map;
    map.computeExecutor().configure(participants);
    game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
    map.setMaterialDestinationsEnabled(true);
    if(mode!=ContentionMode::Off)map.setMaterialClaimPolicyConfig(contentionPolicy(mode==ContentionMode::Combined));
    CHECK(map.claimContentionEnabled()==(mode!=ContentionMode::Off));
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
        run.maxClaimed=std::max(run.maxClaimed,holding);run.maxDistinctClaims=std::max(run.maxDistinctClaims,unsigned(accesses.size()));
        run.units.push_back(row);run.checksums.push_back(game.checkSum());
    }
    for(auto* b:buildings)run.delivered+=b->material(WHEAT);
    for(auto* u:units)u->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
    return run;
}
}

TEST_CASE("an uncrowded contention worker moves exactly as with claims off" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    const std::vector<std::pair<int,int>> stock{{12,8},{12,20},{26,14}};
    const auto off=runContentionFixture(ContentionMode::Off,1,1,1,stock,3000);
    for(auto mode:{ContentionMode::Contention,ContentionMode::Combined}) {
        CAPTURE(int(mode));
        const auto contention=runContentionFixture(mode,1,1,1,stock,3000);
        CHECK(off.delivered>=2);CHECK(contention.delivered==off.delivered);
        CHECK(contention.maxClaimed==0);
        REQUIRE(contention.units.size()==off.units.size());
        for(std::size_t tick=0;tick<off.units.size();++tick){CAPTURE(tick);REQUIRE(contention.units[tick]==off.units[tick]);}
    }
}

TEST_CASE("crowded contention workers engage claims, spread, harvest and stay deterministic" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    // Five workers and five inns; one tight cluster of single-unit wheat.
    std::vector<std::pair<int,int>> stock;
    for(int y=7;y<=12;++y)for(int x=12;x<=13;++x)stock.emplace_back(x,y);
    const auto off=runContentionFixture(ContentionMode::Off,1,5,5,stock,1200);
    for(auto mode:{ContentionMode::Contention,ContentionMode::Combined}) {
        CAPTURE(int(mode));
        const auto one=runContentionFixture(mode,1,5,5,stock,1200);
        const auto two=runContentionFixture(mode,2,5,5,stock,1200);
        CHECK(one.maxClaimed>=3);
        // Claims are exact capacity on single-unit stock: one holder per source.
        CHECK(one.maxDistinctClaims==one.maxClaimed);
        CHECK(one.claimedAndHarvested);CHECK(one.claimFreeAfterHarvest);
        CHECK(one.delivered>=5);
        MESSAGE("mode "<<int(mode)<<" delivered "<<one.delivered<<" (claims off "<<off.delivered<<"), max concurrent claims "<<one.maxClaimed);
        REQUIRE(one.checksums.size()==two.checksums.size());
        for(std::size_t tick=0;tick<one.checksums.size();++tick){CAPTURE(tick);REQUIRE(one.checksums[tick]==two.checksums[tick]);REQUIRE(one.units[tick]==two.units[tick]);}
    }
}

TEST_CASE("contention decisions count same-material fetch targets on the torus and keep a held claim" * doctest::test_suite("ClaimContention"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame f({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=5311});
    auto& game=f.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
    map.setMaterialClaimPolicyConfig(contentionPolicy(false,3,1));
    auto* inn=f.addBuilding("inn",14,14);REQUIRE(inn);
    const int w=map.getW();
    std::vector<Unit*> units;
    struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);u->attachedBuilding=nullptr;}} } cleanup{units};
    const auto fetcher=[&](int x,int y,int tx,int ty,int material=WHEAT) {
        auto* u=f.addUnit(WORKER,x,y);REQUIRE(u);units.push_back(u);
        u->attachedBuilding=inn;u->activity=Unit::ACT_FILLING;u->displacement=Unit::DIS_GOING_TO_RESOURCE;
        u->destinationPurpose=material;u->medical=Unit::MED_FREE;u->targetX=tx;u->targetY=ty;u->setValidTarget(true);
        return u;
    };
    // Self targets column 0; neighbours target the wrapped column w-1 and row +1.
    auto* self=fetcher(4,4,0,8);auto* left=fetcher(5,4,w-1,8);
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==1);
    CHECK_FALSE(self->materialClaimDecision()); // 2 < 3: ordinary
    auto* below=fetcher(6,4,1,9);
    // Different material and out-of-radius targets never count.
    auto* wood=fetcher(7,4,0,8,WOOD);auto* far=fetcher(8,4,3,8);(void)wood;(void)far;
    map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==2);
    CHECK(map.claimContentionOthers(*self,WOOD,0,8)==1);
    CHECK(self->materialClaimDecision());       // 3 >= 3: claim path this decision
    // An invalid ordinary target is not counted.
    below->setValidTarget(false);map.prepareClaimContention(0);
    CHECK(map.claimContentionOthers(*self,WHEAT,0,8)==1);
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
    left->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Harvest);
    left->targetX=20;left->targetY=2;map.prepareClaimContention(0);
    CHECK_FALSE(left->materialClaimDecision());
    CHECK_FALSE(left->usesMaterialClaimPickup());
}
