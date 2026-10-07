// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression: a building's fetch gradient counts the team's
// stocked markets as goals, so a worker hired for a resource no tile holds is
// led to a market, takes the resource at its door and carries it home; a
// nearer tile still wins; a market that runs dry stops being a goal.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <cstdio>
#include <cstdlib>
#include "Version.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>

namespace
{

static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

struct TestUnit : Unit
{
	using Unit::Unit;
	void arrive() { handleDisplacement(); }
};

struct Bed
{
	GameGUI gui;
	Game& game;
	Team* team;
	Building* inn;
	Building* market;
	TestUnit* unit;
	Bed(int unitX, int unitY) : game(gui.game)
	{
		game.gameHeader.getExperiments().set(ExperimentId::MarketsV2);
        game.configureBuildingCatalog();
		game.map.setSize(6, 6, GRASS); // 64x64
		game.map.setGame(&game);
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				game.map.clearImmobileUnit(x, y);
		game.addTeam(0);
		team = game.teams[0];
		team->race.loadDefault();
		// Fruit is only a goal while seen: give the team vision of the whole bed.
		game.map.setMapDiscovered(0, 0, game.map.getW(), game.map.getH(), team->me);
		// A level-2 inn wants 80 of each fruit.
		int innType = game.buildingsTypes.getTypeNum("inn", 1, false);
		require(innType >= 0, "inn type exists");
		inn = game.addBuilding(6, 6, innType, 0);
		require(inn != nullptr, "inn placed");
		game.map.setBuilding(6, 6, inn->type->width, inn->type->height, inn->gid);
		inn->maxUnitWorking = 2;
		inn->materials[WHEAT] = inn->type->maxMaterial[WHEAT];
		inn->updateCallLists();
		int marketType = game.buildingsTypes.getTypeNum("market", 0, false);
		require(marketType >= 0, "market type exists");
		market = game.addBuilding(40, 40, marketType, 0);
		require(market != nullptr, "market placed");
		game.map.setBuilding(40, 40, market->type->width, market->type->height, market->gid);
		unit = new TestUnit(unitX, unitY, Unit::GIDfrom(0, 0), WORKER, team, 1);
		team->myUnits[0] = unit;
		game.map.setGroundUnit(unitX, unitY, unit->gid);
		unit->activity = Unit::ACT_RANDOM;
		unit->medical = Unit::MED_FREE;
	}
	void hire() { team->updateAllBuildingTasks(); }
	bool marketTile(int x, int y) const { return game.map.getBuilding(x, y) == market->gid; }
};

TEST_CASE("MarketFetch/stocked markets are resource goals, depleted markets are not")
{
	glob2test::HeadlessGlobals globals;
	globals->settings.rememberUnit = false;

	{
		// No cherries on the map, ten in the market four tiles from the worker.
		Bed bed(36, 36);
		bed.market->materials[CHERRY] = 10;
		int dist = 0;
		require(bed.game.map.materialAvailableSlot(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "the with-markets gradient reaches the stocked market");
		require(!bed.game.map.materialAvailableSlot(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, false), "the plain gradient knows no cherries");
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->destinationPurpose == CHERRY, "inn hires the worker for cherries held by the market");
		require(bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "walking the fetch gradient");
		require(bed.marketTile(bed.unit->targetX, bed.unit->targetY), "the gradient's goal is the market");
		std::printf("market fetch: hired via the market, distance %d tiles\n", dist);
	}
	{
		// Standing at the market's door: the take happens on arrival. A fruit
		// delivery is ten units (multiplierResource), so is a take.
		Bed bed(39, 39);
		bed.market->materials[CHERRY] = 20;
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "hired and walking");
		bed.unit->arrive();
		require(bed.unit->carriedMaterial == CHERRY, "took a cherry out of the market");
		require(bed.market->materials[CHERRY] == 10, "market stock went down by one take");
		require(bed.unit->displacement == Unit::DIS_GOING_TO_BUILDING && bed.unit->targetBuilding == bed.inn, "carrying it to the inn");
		std::puts("market fetch: the resource is taken at the market's door");
	}
	{
		// Another market shares the same pool and can exhaust it after hiring.
		Bed bed(39, 39);
		auto *other=bed.game.addBuilding(50,50,bed.market->typeNum,0);
		REQUIRE(other);
		bed.market->materials[CHERRY]=20;
		CHECK(other->materials[CHERRY]==20);
		other->removeMaterialFromBuilding(CHERRY);
		CHECK(bed.market->materials[CHERRY]==10);
		bed.hire();
		REQUIRE(bed.unit->displacement==Unit::DIS_GOING_TO_RESOURCE);
		other->removeMaterialFromBuilding(CHERRY);
		bed.unit->arrive();
		CHECK(bed.market->materials[CHERRY]==0);
		CHECK(bed.unit->carriedMaterial==-1);
		CHECK(bed.unit->displacement==Unit::DIS_GOING_TO_RESOURCE);
		other->addMaterialIntoBuilding(CHERRY);
		bed.unit->arrive();
		CHECK(bed.unit->carriedMaterial==CHERRY);
		CHECK(other->materials[CHERRY]==0);
		CHECK(bed.unit->targetBuilding==bed.inn);
	}
	{
		// Cherries two tiles from the worker beat the market.
		Bed bed(36, 36);
		bed.market->materials[CHERRY] = 10;
		require(bed.game.map.incResourceByIndex(34, 36, CHERRY, 0), "seed a cherry tile near the worker");
		bed.hire();
		require(bed.inn->unitsWorking.size() == 1 && bed.unit->displacement == Unit::DIS_GOING_TO_RESOURCE, "hired for cherries");
		require(bed.unit->targetX == 34 && bed.unit->targetY == 36, "the nearer tile is the goal, not the market");
		std::puts("market fetch: a nearer tile wins over the market");
	}
	{
		// The market runs dry: after the rebuild it is no goal any more.
		Bed bed(36, 36);
		bed.market->materials[CHERRY] = 1;
		int dist = 0;
		require(bed.game.map.materialAvailableSlot(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "stocked market is a goal");
		bed.market->removeMaterialFromBuilding(CHERRY);
		bed.game.map.updateMaterialGradient(0, CHERRY, bed.unit->swimClass(), true);
		require(!bed.game.map.materialAvailableSlot(0, CHERRY, bed.unit->swimClass(), 36, 36, &dist, true), "an empty market is no goal");
		bed.hire();
		require(bed.inn->unitsWorking.empty() && bed.inn->unitsFailingRequirements[Building::UnitCantAccessFruit] >= 1, "nobody hired, counted as no fruit reachable");
		std::puts("market fetch: an empty market is no source");
	}
	{
		// Market levels: a level-1 market never hands out wood even though the
		// team pool holds some; a level-3 market does.
		Bed bed(36, 36);
		require(bed.game.buildingsTypes.getTypeNum("market", 1, false) >= 0 && bed.game.buildingsTypes.getTypeNum("market", 2, false) >= 0, "market levels 2 and 3 exist");
		bed.market->materials[WOOD] = 20;
		int dist = 0;
		require(!bed.game.map.materialAvailableSlot(0, WOOD, bed.unit->swimClass(), 36, 36, &dist, true), "a level-1 market is no wood goal");
		int top = bed.game.buildingsTypes.getTypeNum("market", 2, false);
        auto* supplier=bed.game.addBuilding(50,50,top,0);
        REQUIRE(supplier);
        bed.game.map.setBuilding(50,50,supplier->type->width,supplier->type->height,supplier->gid);
		bed.game.map.updateMaterialGradient(0, WOOD, bed.unit->swimClass(), true);
		require(bed.game.map.materialAvailableSlot(0, WOOD, bed.unit->swimClass(), 36, 36, &dist, true), "a level-3 market hands out wood");
		std::puts("market levels: only a level that takes the resource hands it out");
	}
	std::puts("PASS stocked markets are goals of the fetch gradients");
}

TEST_CASE("MarketFetch/deliverer stays attached and fetches the next market delivery")
{
	glob2test::HeadlessGlobals globals;
	globals->settings.rememberUnit = false;
	Bed bed(39,39);
	bed.market->materials[CHERRY]=30;
	bed.hire();
	bed.unit->arrive();
	require(bed.unit->carriedMaterial==CHERRY, "first take from market");
	bed.game.map.setGroundUnit(39,39,NOGUID);
	bed.unit->posX=5; bed.unit->posY=5;
	bed.game.map.setGroundUnit(5,5,bed.unit->gid);
	bed.unit->displacement=Unit::DIS_FILLING_BUILDING;
	bed.unit->needToRecheckMedical=true;
	// An idle majority would release this deliverer under #254's hiring gate.
	for (int i=0; i<4; ++i)
	{
		auto *idle=bed.game.addUnit(20+i,20,0,WORKER,0,0,0,0);
		require(idle!=nullptr, "place idle worker");
		idle->medical=Unit::MED_FREE;
		idle->activity=Unit::ACT_RANDOM;
	}
	bed.unit->arrive();
	require(bed.inn->materials[CHERRY]==10, "first delivery deposited");
	require(bed.unit->attachedBuilding==bed.inn && bed.unit->activity==Unit::ACT_FILLING,
	        "master hiring behavior keeps the worker attached after depositing");
	require(bed.unit->displacement==Unit::DIS_GOING_TO_RESOURCE,
	        "next delivery uses the resource gradient");
	require(bed.marketTile(bed.unit->targetX,bed.unit->targetY), "next target is the stocked market");
}

std::string saveMap(Map &map, bool text)
{
	auto *backend=new GAGCore::MemoryStreamBackend;
	std::unique_ptr<GAGCore::OutputStream> out(text
		? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(backend))
		: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(backend)));
	map.saveRuntimeState(out.get()); out->flush();
	return backend->takeContents();
}

TEST_CASE("MarketFetch/market fields and pending publications survive binary and text saves [save-format]")
{
	glob2test::HeadlessGlobals globals;
	for (bool text : {false, true})
	{
		Bed source(36,36), restored(36,36);
		for (auto *bed : {&source, &restored})
		{
			bed->game.gameHeader.setResourceGrowthDisabled(true);
			bed->market->materials[CHERRY]=10;
			bed->game.map.getMaterialGradientSlot(0,CHERRY,0,true);
		}
		source.game.map.configureGradientPipeline(2,3);
		// Capture each queue phase, including a market job's publication deadline.
		for (int tick=1; tick<=12; ++tick)
		{
			source.game.map.advanceGradientPipeline();
			source.game.map.syncStep(tick);
			const auto bytes=saveMap(source.game.map,text);
			auto *backend=new GAGCore::MemoryStreamBackend;
			backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
			std::unique_ptr<GAGCore::InputStream> in(text
				? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(backend))
				: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(backend)));
			restored.game.map.loadRuntimeState(in.get(),VERSION_MINOR);
			CHECK(saveMap(restored.game.map,text)==bytes);
			if (tick==6)
			{
				source.market->removeMaterialFromBuilding(CHERRY);
				restored.market->removeMaterialFromBuilding(CHERRY);
			}
			source.game.map.advanceGradientPipeline();
			source.game.map.syncStep(tick+1);
			restored.game.map.advanceGradientPipeline();
			restored.game.map.syncStep(tick+1);
			CHECK(saveMap(restored.game.map,text)==saveMap(source.game.map,text));
		}
	}
}

TEST_CASE("MarketFetch/upgraded market IDs and stock survive binary and text saves [save-format]")
{
	glob2test::HeadlessGlobals globals;
	for (int level=0; level<3; ++level) for (bool text : {false,true})
	{
		glob2test::GameOptions options{.header=true};
		options.experiments.set(ExperimentId::MarketsV2);
		glob2test::HeadlessGame source(options);
		auto *market=source.addBuilding("market",8,8,level);
		REQUIRE(market);
		CHECK(market->typeNum==(level ? 50+2*level : 50));
		CHECK(source.game.buildingsTypes.getTypeNum("market",level,true)==(level ? 49+2*level : 49));
		CHECK((market->type->maxMaterial[WOOD]>0)==(level>=1));
		CHECK((market->type->maxMaterial[WHEAT]>0)==(level>=1));
		CHECK((market->type->maxMaterial[STONE]>0)==(level>=2));
		market->materials[CHERRY]=20;
		if (level) market->materials[WOOD]=30;
		auto *storage=new GAGCore::MemoryStreamBackend;
		std::unique_ptr<GAGCore::OutputStream> out(text
			? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(storage))
			: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(storage)));
		source.game.save(out.get(),false,"Market upgrades"); out->flush();
		const auto bytes=storage->takeContents();
		auto *backend=new GAGCore::MemoryStreamBackend;
		backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
		std::unique_ptr<GAGCore::InputStream> in(text
			? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(backend))
			: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(backend)));
		GameGUI restored;
		REQUIRE(restored.game.load(in.get()));
		auto *loaded=restored.game.teams[0]->myBuildings[Building::GIDtoID(market->gid)];
		REQUIRE(loaded);
		CHECK(loaded->typeNum==market->typeNum);
		CHECK(loaded->type->level==level);
		CHECK(loaded->materials[CHERRY]==20);
		if (level) CHECK(loaded->materials[WOOD]==30);
	}
}

}
