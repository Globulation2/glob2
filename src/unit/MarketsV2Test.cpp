// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MapInternal.h"
#include "Order.h"
#include "OrderValidation.h"
#include "Version.h"
#include "render/scene/Scene.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <Environment.h>
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include "GameGUITouch.h"
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
std::string save(Game &game, bool text)
{
	auto *backend = new GAGCore::MemoryStreamBackend;
	std::unique_ptr<GAGCore::OutputStream> out(text
		? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(backend))
		: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(backend)));
	game.save(out.get(), false, "Markets V2 test"); out->flush();
	return backend->takeContents();
}
bool load(Game &game, const std::string &bytes, bool text)
{
	auto *backend = new GAGCore::MemoryStreamBackend;
	backend->write(bytes.data(), bytes.size()); backend->seekFromStart(0);
	std::unique_ptr<GAGCore::InputStream> in(text
		? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(backend))
		: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(backend)));
	return game.load(in.get());
}
}

TEST_SUITE("MarketsV2")
{
// This case also compiles against pre-integration master to generate the baseline.
std::string marketTrace(bool enabled, bool report=false)
{
	glob2test::HeadlessGlobals globals;
	std::ostringstream trace;
	for (bool pipeline : {false, true})
	{
		glob2test::HeadlessGame world({.wDec=6, .hDec=6, .teams=2, .discovered=true,
			.clearImmobile=true, .loadDefaultRace=true, .header=true, .seed=271});
		auto &g = world.game;
#if VERSION_MINOR >= 134
		g.gameHeader.getExperiments().set(ExperimentId::MarketsV2, enabled);
#else
		(void)enabled;
#endif
		g.gameHeader.setResourceGrowthDisabled(true);
		g.map.configureGradientPipeline(pipeline ? 2 : 0, 3);
		auto *inn = world.addBuilding("inn", 6, 6, 1);
		auto *market = world.addBuilding("market", 20, 20);
		auto *second = world.addBuilding("market", 40, 40, 0, 1);
		REQUIRE(inn); REQUIRE(market); REQUIRE(second);
		g.teams[0]->sharedVisionExchange |= g.teams[1]->me;
		g.teams[1]->sharedVisionExchange |= g.teams[0]->me;
		g.teams[0]->sharedVisionOther = g.teams[0]->sharedVisionExchange;
		g.teams[1]->sharedVisionOther = g.teams[1]->sharedVisionExchange;
		inn->maxUnitWorking = 3;
		inn->resources[WHEAT] = inn->type->maxResource[WHEAT] - 1;
		market->resources[CHERRY] = 100;
		second->resources[ORANGE] = 100;
		g.map.setResource(5, 10, WHEAT, 0);
		for (int i=0; i<6; ++i) world.addUnit(WORKER, 4+i, 4, 0, 1);
		inn->updateCallLists();
		int exchangeTicks = 0;
		for (int tick=0; tick<1500; ++tick)
		{
			// Identical scripted deliveries on both builds exercise stock transitions.
			if (tick==500) market->addResourceIntoBuilding(ORANGE);
			if (tick==1000) market->addResourceIntoBuilding(PRUNE);
			for (int i=0; i<6; ++i)
			{
				auto *u = world.team->myUnits[i];
				u->hungry = Unit::HUNGRY_MAX; u->medical = Unit::MED_FREE;
				exchangeTicks += u->ownExchangeBuilding != nullptr;
			}
			world.step();
			Uint32 hash=2166136261u;
			for (Uint32 value : simulation(g)) hash=(hash ^ value)*16777619u;
			trace << pipeline << ' ' << tick+1 << ' ' << std::hex << hash << std::dec << '\n';
			if (tick==749)
			{
				const auto state=simulation(g);
				const auto bytes=save(g,pipeline);
				if (!glob2test::artifactDir().empty()) glob2test::writeFile(glob2test::artifactDir()/(pipeline ? "midgame.txt" : "midgame.bin"),bytes);
				const auto marketId=market->gid, secondId=second->gid;
				REQUIRE(load(g,bytes,pipeline));
				CHECK(simulation(g)==state);
				world.team=g.teams[0];
				market=world.team->myBuildings[Building::GIDtoID(marketId)];
				second=g.teams[1]->myBuildings[Building::GIDtoID(secondId)];
			}
		}
		if (!enabled) CHECK(exchangeTicks > 0);
		CHECK(market->resources[CHERRY] < 100);
#if VERSION_MINOR >= 134
		std::size_t fields=0;
		for (int t=0;t<Team::MAX_COUNT;++t) for (int r=0;r<MAX_NB_RESOURCES;++r) for (int sw=0;sw<SWIM_CLASS_COUNT;++sw)
			fields += g.map.marketResourcesGradient[t][r][sw]!=nullptr;
		if (!enabled) CHECK(fields==0);
		if (report) std::printf("markets-v2=%d pipeline=%d: %zu resource fields, %zu bytes in resource fields\n",enabled,pipeline,fields,fields*g.map.getW()*g.map.getH()*sizeof(Uint16));
#else
		(void)report;
#endif
	}
	if (!glob2test::artifactDir().empty()) glob2test::writeFile(glob2test::artifactDir()/"checksums.txt",trace.str());
	return trace.str();
}

TEST_CASE("disabled fruit deliveries match master per tick [golden][save-format][artifacts]")
{
	glob2test::expectGolden("markets-v2/disabled-checksums.txt", marketTrace(false));
}

#if VERSION_MINOR >= 134
TEST_CASE("legacy workers already travelling to markets continue after migration [golden][save-format]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.header=true});
	REQUIRE(load(world.game,glob2test::readFile(glob2test::fixture("markets-v2/legacy-133-market.bin")),false));
	world.team=world.game.teams[0];
	CHECK_FALSE(world.game.gameHeader.hasExperiment(ExperimentId::MarketsV2));
	CHECK(world.team->myUnits[0]->ownExchangeBuilding!=nullptr);
	std::ostringstream trace;
	for (int tick=750;tick<1000;++tick)
	{
		for (int i=0;i<6;++i) { world.team->myUnits[i]->hungry=Unit::HUNGRY_MAX; world.team->myUnits[i]->medical=Unit::MED_FREE; }
		world.step();
		Uint32 hash=2166136261u;
		for (Uint32 value : simulation(world.game)) hash=(hash^value)*16777619u;
		trace << "0 " << tick+1 << ' ' << std::hex << hash << std::dec << '\n';
	}
	std::istringstream full(glob2test::readFile(glob2test::fixture("markets-v2/disabled-checksums.txt")));
	std::ostringstream expected; std::string line;
	for (int tick=0;tick<1000;++tick) { std::getline(full,line); if (tick>=750) expected << line << '\n'; }
	CHECK(trace.str()==expected.str());
}
TEST_CASE("enabled supply networks repeat deterministically with save continuation [golden][save-format][artifacts]")
{
	const auto trace=marketTrace(true);
	CHECK(trace==marketTrace(true));
	glob2test::expectGolden("markets-v2/enabled-checksums.txt",trace);
}
TEST_CASE("identical market workloads report routing cost [benchmark]")
{
	for (bool enabled : {false,true})
	{
		const auto start=std::chrono::steady_clock::now();
		marketTrace(enabled,true);
		const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
		std::printf("markets-v2=%d: 3000 ticks, heavy per-tick checksums and two saves: %.6f seconds\n",enabled,elapsed);
	}
}
TEST_CASE("identical gradient refresh workloads report CPU and memory [benchmark]")
{
	glob2test::HeadlessGlobals globals;
	for (bool enabled : {false,true})
	{
		glob2test::GameOptions options{.wDec=6,.hDec=6,.discovered=true,.clearImmobile=true,.header=true};
		options.experiments.set(ExperimentId::MarketsV2,enabled);
		glob2test::HeadlessGame world(options);
		auto *market=world.addBuilding("market",8,8);
		market->resources[CHERRY]=100;
		for (int r=0;r<MAX_RESOURCES;++r)
		{
			world.game.map.setResource(20+r*2,20,r,0);
			for (int sw=0;sw<SWIM_CLASS_COUNT;++sw) world.game.map.getResourceGradient(0,r,sw,true);
		}
		const auto wall=std::chrono::steady_clock::now(); const auto cpu=std::clock();
		for (int repeat=0;repeat<16;++repeat) for (int r=0;r<MAX_RESOURCES;++r) for (int sw=0;sw<SWIM_CLASS_COUNT;++sw)
			world.game.map.updateResourcesGradient(0,r,sw,true);
		std::printf("markets-v2=%d: %d 64x64 resource refreshes, CPU=%.6fs wall=%.6fs market-field-bytes=%d\n",
			enabled,16*MAX_RESOURCES*SWIM_CLASS_COUNT,double(std::clock()-cpu)/CLOCKS_PER_SEC,
			std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count(),
			enabled ? MAX_RESOURCES*SWIM_CLASS_COUNT*64*64*int(sizeof(Uint16)) : 0);
	}
}
TEST_CASE("disabled gate blocks explicit fetch APIs and upgrade execution")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.discovered=true, .clearImmobile=true, .header=true});
	auto &g=world.game;
	auto *market=world.addBuilding("market",8,8);
	REQUIRE(market);
	market->resources[CHERRY]=20;
	CHECK_FALSE(g.map.marketsV2Enabled());
	CHECK_FALSE(market->isUpgradeAvailable());
	for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
	{
		CHECK(g.map.getResourceGradient(0,CHERRY,sw,true)==g.map.getResourceGradient(0,CHERRY,sw));
		CHECK(g.map.marketResourcesGradient[0][CHERRY][sw]==nullptr);
		g.map.dirtyMarketGradients(0,CHERRY);
		CHECK_FALSE(g.map.marketGradientDirty[0][CHERRY][sw]);
	}
	CHECK_FALSE(g.map.isStockedMarketTile(market->gid,0,CHERRY));
	CHECK_FALSE(g.isBuildingTypeAvailable(51));
	CHECK(g.addBuilding(20,20,52,0)==nullptr);
	OrderConstruction order(market->gid,1,1);
	CHECK(OrderValidation::validate(g,0,order).verdict==OrderValidation::Verdict::Rejected);
	market->launchConstruction(1,1);
	CHECK(market->typeNum==50);
	CHECK(market->constructionResultState==Building::NO_CONSTRUCTION);
	const auto &scene=glob2test::sceneOf(g, Game::ViewState{.selectedBuilding=market});
	CHECK_FALSE(scene.panels.building.hardSpaceForUpgrade);
	CHECK_FALSE(scene.panels.building.showLevel);
}

TEST_CASE("each market level accepts only its resources across all swim classes")
{
	glob2test::HeadlessGlobals globals;
	for (int level=0; level<3; ++level)
	{
		glob2test::GameOptions options{.teams=2, .discovered=true, .clearImmobile=true, .header=true};
		options.experiments.set(ExperimentId::MarketsV2);
		glob2test::HeadlessGame world(options);
		auto &g=world.game;
		auto *market=world.addBuilding("market",8,8,level);
		REQUIRE(market);
		for (int r=0; r<MAX_NB_RESOURCES; ++r) market->resources[r]=100;
		CHECK_FALSE(market->fetchesFromMarkets());
		for (int r=0; r<MAX_RESOURCES; ++r)
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				bool accepts=market->type->maxResource[r]>0;
				CHECK(g.map.isStockedMarketTile(market->gid,0,r)==accepts);
				CHECK_FALSE(g.map.isStockedMarketTile(market->gid,1,r));
				CHECK_FALSE(g.map.resourceAvailable(1,r,sw,6,6,true));
				CHECK(g.map.resourceAvailable(0,r,sw,6,6,true)==accepts);
				CHECK_FALSE(g.map.resourceAvailable(0,r,sw,6,6));
			}
	}
}

TEST_CASE("market routes respect forbidden ground and water across swim classes")
{
	glob2test::HeadlessGlobals globals;
	glob2test::GameOptions options{.discovered=true, .clearImmobile=true, .header=true};
	options.experiments.set(ExperimentId::MarketsV2);
	glob2test::HeadlessGame world(options);
	auto *market=world.addBuilding("market",8,8);
	market->resources[CHERRY]=20;
	for (int y=7;y<=8+market->type->height;++y) for (int x=7;x<=8+market->type->width;++x) world.game.map.addForbidden(x,y,0);
	for (int sw=0;sw<SWIM_CLASS_COUNT;++sw) CHECK_FALSE(world.game.map.resourceAvailable(0,CHERRY,sw,5,5,true));
	for (int y=7;y<=8+market->type->height;++y) for (int x=7;x<=8+market->type->width;++x)
	{ world.game.map.removeForbidden(x,y,0); world.game.map.setTerrain(x,y,256); }
	world.game.map.bumpTopologyGeneration();
	for (int sw=0;sw<SWIM_CLASS_COUNT;++sw)
	{
		world.game.map.updateResourcesGradient(0,CHERRY,sw,true);
		CHECK(world.game.map.resourceAvailable(0,CHERRY,sw,5,5,true)==(sw>0));
	}
}
TEST_CASE("upgrading cancelling and completing preserve shared stock and gate")
{
	glob2test::HeadlessGlobals globals;
	glob2test::GameOptions options{.discovered=true, .clearImmobile=true, .header=true};
	options.experiments.set(ExperimentId::MarketsV2);
	glob2test::HeadlessGame world(options);
	auto &g=world.game;
	auto *market=world.addBuilding("market",8,8);
	REQUIRE(market);
	market->resources[CHERRY]=100;
	for (bool text : {false,true})
	{
		market->launchConstruction(1,1);
		REQUIRE(market->tryToBuildingSiteRoom());
		CHECK(market->type->isBuildingSite);
		CHECK(market->type->level==1);
		CHECK(world.team->teamResources[CHERRY]==100);
		GameGUI resumed;
		REQUIRE(load(resumed.game,save(g,text),text));
		CHECK(resumed.game.gameHeader.hasExperiment(ExperimentId::MarketsV2));
		CHECK(simulation(resumed.game)==simulation(g));
		market->cancelConstruction(1);
		CHECK(market->typeNum==50);
		CHECK(market->resources[CHERRY]==100);
	}
	for (int level=1; level<=2; ++level)
	{
		market->launchConstruction(1,1);
		REQUIRE(market->tryToBuildingSiteRoom());
		for (int r=0; r<MAX_NB_RESOURCES; ++r) market->resources[r]=market->type->maxResource[r];
		market->updateBuildingSite();
		CHECK(market->type->level==level);
		CHECK(market->resources[CHERRY]==100);
		CHECK(market->isUpgradeAvailable()==(level==1));
		market->hp=market->getEffectiveMaxHp(); // complete the provisional level's repair before upgrading again
	}
}
TEST_CASE("market repair materials do not overwrite shared stock")
{
	glob2test::HeadlessGlobals globals;
	glob2test::GameOptions options{.discovered=true, .clearImmobile=true, .header=true};
	options.experiments.set(ExperimentId::MarketsV2);
	glob2test::HeadlessGame world(options);
	auto *market=world.addBuilding("market",8,8,1);
	market->resources[WHEAT]=57; market->resources[CHERRY]=91;
	market->hp=market->getEffectiveMaxHp()/2;
	market->launchConstruction(1,1);
	REQUIRE(market->tryToBuildingSiteRoom());
	CHECK(world.team->teamResources[WHEAT]==57);
	CHECK(world.team->teamResources[CHERRY]==91);
	market->cancelConstruction(1);
	CHECK(market->resources[WHEAT]==57);
	CHECK(market->resources[CHERRY]==91);
}
TEST_CASE("market panels show gated upgrades and shared stock [display][artifacts]")
{
	glob2test::HeadlessGlobals globals({.display=true, .width=800, .height=600});
	for (bool enabled : {false,true})
	{
		glob2test::GameOptions options{.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true};
		options.experiments.set(ExperimentId::MarketsV2,enabled);
		glob2test::HeadlessGame world(options);
		world.gui.localTeamNo=world.gui.localPlayer=0;
		world.gui.adjustLocalTeam();
		world.gui.init();
		world.addBuilding("school",20,20,2);
		world.addUnit(WORKER,18,18,0,2);
		auto *market=world.addBuilding("market",8,8,enabled ? 1 : 0);
		REQUIRE(market);
		for (int r=0;r<MAX_NB_RESOURCES;++r) market->resources[r]=market->type->maxResource[r]/2;
		world.gui.setSelection(GameGUI::BUILDING_SELECTION,market);
		for (bool touch : {false,true})
		{
			// The runner gives this case its own process and disposable profile.
			GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI",touch ? "1" : "0",1);
			globals->gfx->setResponsiveViewport(touch,800,600);
			world.gui.touch->panelOpen=touch;
			world.gui.drawAll(0);
			CHECK(world.gui.drawnScene().panels.building.hardSpaceForUpgrade==enabled);
			const std::string name=std::string("market-")+(enabled ? "v2-" : "legacy-")+(touch ? "touch.bmp" : "desktop.bmp");
			globals->gfx->printScreen(name);
			globals->gfx->nextFrame();
			const auto path=std::filesystem::path(SDL_getenv_unsafe("GLOB2_USER_DATA_DIR"))/name;
			REQUIRE(std::filesystem::exists(path));
			std::filesystem::copy_file(path,glob2test::artifactDir()/name,std::filesystem::copy_options::overwrite_existing);
		}
	}
	GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI","0",1);
}
#endif
}
