#include "GameEvent.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Scene extraction: entities by gid, the selection's map-view contribution, and
// the overlay map's refresh cadence.
#include "EngineFixtures.h"
#include "OverlayAreas.h"
#include "SceneExtract.h"
#include "render/UnitMotion.h"
#include "UnitTiming.h"
#include <nlohmann/json.hpp>

TEST_SUITE("SceneExtract")
{
	TEST_CASE("script area snapshots are opt-in and independent of later map edits")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		world.game.map.setPoint(8, 3, 4);
		Scene scene; SceneRequest request;
		extractScene(world.game, request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
		request.includeScriptAreas=true;
		extractScene(world.game, request, scene);
		world.game.map.unsetPoint(8,3,4);
		CHECK(scene.map.isPointSet(8,3,4));
		CHECK_FALSE(scene.map.isPointSet(7,3,4));
		request.includeScriptAreas=false;
		extractScene(world.game, request, scene);
		CHECK_FALSE(scene.map.isPointSet(8,3,4));
	}

	TEST_CASE("units and buildings resolve by gid with their drawn fields and the selection")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .discovered = true, .loadDefaultRace = true});
		Building *inn = world.addBuilding("inn", 4, 4);
		Unit *worker = world.addUnit(WORKER, 10, 10);
		Unit *other = world.addUnit(WORKER, 12, 10, 1);
		worker->hp = 3;

		SceneRequest request;
		request.selectedBuilding = Game::refOf(inn);
		request.selectedUnit = Game::refOf(worker);
		Scene scene;
		SceneExtractor().extract(world.game, request, scene);

		const SceneUnit *u = scene.entities.unit(worker->gid);
		REQUIRE(u);
		CHECK((u->posX == 10 && u->posY == 10 && u->hp == 3 && u->team == 0 && u->typeNum == WORKER));
		CHECK(scene.entities.owner(*scene.entities.unit(other->gid)).teamNumber == 1);
		CHECK(scene.entities.isSelected(*u));
		CHECK_FALSE(scene.entities.isSelected(*scene.entities.unit(other->gid)));

		const SceneBuilding *b = scene.entities.building(inn->gid);
		REQUIRE(b);
		CHECK((b->posX == 4 && b->posY == 4 && b->type == inn->type && b->effectiveMaxHp == inn->getEffectiveMaxHp()));
		CHECK(scene.entities.isSelected(*b));
		CHECK(scene.entities.selectedBuilding.ref == request.selectedBuilding);
		CHECK(scene.map.getGroundUnit(10, 10) == worker->gid);
		CHECK(scene.map.getBuilding(4, 4) == inn->gid);
		CHECK(scene.entities.unit(NOGUID) == nullptr);
	}

	TEST_CASE("overlay maps match a direct computation and refresh per 25-tick window or type change")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 1, .discovered = true, .loadDefaultRace = true});
		Unit *wounded = world.addUnit(WORKER, 10, 10);
		wounded->medical = Unit::MED_DAMAGED;

		SceneExtractor extractor;
		SceneRequest request;
		request.view.overlay = OverlayArea::Damage;
		Scene scene;
		world.game.stepCounter = 26; // window 1 starts at tick 26
		extractor.extract(world.game, request, scene);
		REQUIRE(scene.overlay);
		OverlayArea direct;
		direct.compute(world.game, OverlayArea::Damage, 0);
		CHECK(scene.overlay->getMaximum() == direct.getMaximum());
		CHECK(scene.overlay->getValue(10, 10) == direct.getValue(10, 10));
		CHECK(scene.overlay->getValue(10, 10) > 0);

		// Same window: the snapshot is reused even though the game changed.
		const auto first = scene.overlay;
		wounded->medical = Unit::MED_FREE;
		world.game.stepCounter = 50;
		extractor.extract(world.game, request, scene);
		CHECK(scene.overlay == first);

		// Next window (ticks 51..75): recomputed.
		world.game.stepCounter = 51;
		extractor.extract(world.game, request, scene);
		CHECK(scene.overlay != first);
		CHECK(scene.overlay->getValue(10, 10) == 0);

		// A type change recomputes at once; None drops the overlay.
		const auto damage = scene.overlay;
		request.view.overlay = OverlayArea::Defence;
		extractor.extract(world.game, request, scene);
		CHECK((scene.overlay != damage && scene.overlay->getOverlayType() == OverlayArea::Defence));
		request.view.overlay = OverlayArea::None;
		extractor.extract(world.game, request, scene);
		CHECK(!scene.overlay);
	}
    TEST_CASE("published scene retains old entity fields while subsequent scenes reflect changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; Scene before,after;
        const auto gid=unit->gid; const auto hp=unit->hp;
        extractor.extract(world.game,request,before);
        unit->hp=1;
        extractor.extract(world.game,request,after);
        REQUIRE(before.entities.unit(gid)); REQUIRE(after.entities.unit(gid));
        CHECK(before.entities.unit(gid)->hp==hp); CHECK(after.entities.unit(gid)->hp==1);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        extractor.extract(world.game,request,after);
        CHECK(after.entities.unit(gid)==nullptr);
        CHECK(before.entities.unit(gid)->hp==hp);
    }

    TEST_CASE("stale selection generation never selects a replacement occupying the same gid")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.clearImmobile=true,.loadDefaultRace=true});
        auto* unit=world.addUnit(WORKER,10,10);
        SceneExtractor extractor; SceneRequest request; Scene scene;
        request.selectedUnit=Game::refOf(unit); const auto gid=unit->gid;
        extractor.extract(world.game,request,scene);
        REQUIRE(scene.entities.unit(gid)); CHECK(scene.entities.isSelected(*scene.entities.unit(gid)));
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        auto* replacement=world.addUnit(WORKER,10,10);
        REQUIRE(replacement->gid==gid);
        extractor.extract(world.game,request,scene);
        REQUIRE(scene.entities.unit(gid)); CHECK_FALSE(scene.entities.isSelected(*scene.entities.unit(gid)));
    }


	TEST_CASE("smooth unit motion advances the drawn delta within the current action only")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 1, .discovered = true, .loadDefaultRace = true});
		Unit *walker = world.addUnit(WORKER, 10, 10);
		walker->action = WALK;
		walker->dx = 1;
		walker->dy = 1;
		walker->delta = 100;

		SceneRequest request;
		request.tickTime = 1000;
		request.tickInterval = 40;
		Scene scene;
		SceneExtractor().extract(world.game, request, scene);
		CHECK((scene.tickTime == 1000 && scene.tickInterval == 40));
		const SceneUnit *u = scene.entities.unit(walker->gid);
		REQUIRE(u);
		CHECK(u->stepSpeed == unitActionStepSpeed(walker->speed, WALK, 1, 1));
		REQUIRE(u->stepSpeed > 0);

		// Off (motion 0) draws exactly the simulated delta.
		CHECK(drawnUnitDelta(*u, 0.f) == 100);
		CHECK(unitMotionFraction(scene, 1000) == 0.f);
		CHECK(unitMotionFraction(scene, 1020) == doctest::Approx(0.5f));
		CHECK(unitMotionFraction(scene, 5000) == 1.f);
		CHECK(drawnUnitDelta(*u, 0.5f) == 100 + int(0.5f * float(u->stepSpeed)));
		// A full interval reaches what the next tick computes, without crossing the action.
		CHECK(drawnUnitDelta(*u, 1.f) == std::min(UNIT_DELTA_MAX, 100 + u->stepSpeed));
		SceneUnit ending = *u;
		ending.delta = UNIT_DELTA_MAX - 1;
		CHECK(drawnUnitDelta(ending, 1.f) == UNIT_DELTA_MAX);

		// Uncapped simulation: no interval, no motion between ticks.
		scene.tickInterval = 0;
		CHECK(unitMotionFraction(scene, 1020) == 0.f);
	}
    TEST_CASE("connected overlays use configured groups and scene retains its catalog")
    {
        glob2test::HeadlessGlobals globals;
        Scene scene;
        const SceneBuilding* extracted=nullptr;
        std::string key;
        {
        glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true});
        const int firstId=world.game.buildingsTypes.getFinishedTypeNum("warflag");
        const int otherId=world.game.buildingsTypes.getFinishedTypeNum("explorationflag");
        auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        for (int id : {firstId,otherId})
        {
            catalog["variants"][id]["properties"]["crossConnectMultiImage"]=1;
            catalog["variants"][id]["presentation"]["connectionGroup"]="joined-canopies";
        }
        world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
        world.game.configureBuildingCatalog();
        Building* first=world.addBuilding("warflag",4,4);
        world.addBuilding("explorationflag",4+first->type->width,4);
        world.addBuilding("explorationflag",4,4+first->type->height,0,1);
        SceneRequest request;
        extractScene(world.game,request,scene);
        extracted=scene.entities.building(first->gid);
        REQUIRE(extracted);
        CHECK(extracted->connectionMask==1);
        CHECK(scene.map.getBuilding(4,4)==NOGBID);
        key=extracted->type->key;
        }
        CHECK(extracted->type->key==key);
        CHECK(extracted->type->presentation.connectionGroup=="joined-canopies");
    }

    TEST_CASE("building events preserve concrete catalog IDs and key-only display names")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams=1,.loadDefaultRace=true});
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& variants=snapshot["variants"];
        const auto prototype=variants[3];
        while (variants.size()<=256)
        {
            auto variant=prototype;
            variant["id"]=variants.size();
            variant["key"]="fixture.event."+std::to_string(variants.size());
            variant["properties"].erase("type");
            variant["properties"].erase("shortTypeNum");
            variant["previous"]=""; variant["next"]="";
            variant["semantics"]["repairable"]=false;
            variant["presentation"]["displayName"]="Custom refuge";
            variants.push_back(std::move(variant));
        }
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        world.game.configureBuildingCatalog();
        for (const auto event : {GameEvent::buildingUnderAttack(0,1,2,256),GameEvent::buildingCompleted(0,1,2,256)})
        {
            CHECK(event.getTypeNum()==256);
            CHECK(event.formatMessage(world.game).find("Custom refuge")!=std::string::npos);
        }
    }

}
