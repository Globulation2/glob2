// SPDX-License-Identifier: GPL-3.0-or-later
// Scene extraction: entities by gid, the selection's map-view contribution, and
// the overlay map's refresh cadence.
#include "EngineFixtures.h"
#include "OverlayAreas.h"
#include "scene/SceneExtract.h"

TEST_SUITE("SceneExtract")
{
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
}
