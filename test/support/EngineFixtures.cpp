// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "HiringPipeline.h"
#include "render/scene/SceneExtract.h"

#include "FileManager.h"
#include "AI.h"
#include "Player.h"
#include "Order.h"
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "MapEditKeyActions.h"
#include "PerformanceTelemetry.h"
#include "Race.h"
#include "Utilities.h"

namespace glob2test
{
	std::vector<std::string> stepAI(Game &game)
	{
		std::vector<std::string> orders;
		std::vector<std::shared_ptr<Order>> pending;
		for (int p = 0; p < game.gameHeader.getNumberOfPlayers(); ++p)
			if (game.players[p] && game.players[p]->ai)
			{
				auto order = game.players[p]->ai->getOrder(false);
				std::string wire(1, char(order->getOrderType()));
				if (order->getDataLength())
					wire.append(reinterpret_cast<const char *>(order->getData()), order->getDataLength());
				orders.push_back(std::move(wire));
				order->sender = p;
				pending.push_back(std::move(order));
			}
		// Production decisions all observe the same completed simulation tick.
		for (const auto &order : pending) game.executeOrder(order, order->sender);
		game.step(0);
		return orders;
	}

	const PresentationFrame &sceneOf(const Game &game, const Game::ViewState &view, int localTeam, PresentationFrame *into)
	{
		static PresentationFrame shared;
		PresentationFrame &scene = into ? *into : shared;
        SceneRequest request=game.gui ? game.gui->sceneRequest() : SceneRequest{};
		request.localTeam = localTeam;
        if (view.selectedBuilding) request.selectedBuilding=Game::refOf(view.selectedBuilding);
        if (view.selectedUnit) request.selectedUnit=Game::refOf(view.selectedUnit);
		game.snapshots().invalidateBoundary(); // Fixture helpers may directly edit public state.
		SceneExtractor().prepare(game.captureReadBoundary(SceneExtractor::captureOptions(request)),request,scene);
		return scene;
	}

	const PresentationFrame &sceneOf(const Game &game)
	{
		static const Game::ViewState none;
		return sceneOf(game, none);
	}

    void drawGUI(GameGUI& gui,int team)
    {
        if (!gui.simulationThreaded) {
            gui.game.snapshots().invalidateBoundary(); // Test scripts can edit public records.
            gui.prepareLocalPresentation();
        }
        gui.drawAll(team);
    }

    void observeMap(const Map& map,SceneMap& view)
    {
        REQUIRE(map.game);
        SceneRequest request;request.includePanels=false;
        const auto world=map.game->captureReadBoundary(SceneExtractor::captureOptions(request));
        view.bindSnapshot(world,map.displayViewportW,map.displayViewportH,map.displayedTeam);
        view.prepareChunk(0,size_t(world.width)*world.height);
    }

	HeadlessGlobals::HeadlessGlobals(Options options)
		: globals(options.profileName.c_str())
	{
		REQUIRE_MESSAGE(globalContainer == nullptr, "a HeadlessGlobals is already live in this process");
		globalContainer = &globals;
		if (options.addSourceRoot)
			globals.fileManager->addDir(sourceRoot().string());
		globals.runNoX = !options.display;
		globals.settings.rememberUnit = false;
		globals.settings.mute = 1;
		globals.settings.screenWidth = options.width;
		globals.settings.screenHeight = options.height;
		globals.settings.screenFlags = options.screenFlags;
		// Tests read English text unless they choose otherwise, whatever the host's locale.
		if (globals.settings.language.empty())
			globals.settings.language = "en";
		if (options.beforeLoad)
			options.beforeLoad(globals);
		if (options.display || options.loadStrings)
			globals.load();
		else
		{
			globals.buildingsTypes.init();
			IntBuildingType::init();
			Race::loadDefault();
			GameGUIKeyActions::init();
			MapEditKeyActions::init();
		}
		if (options.seed != 0)
			setSyncRandSeed(options.seed);
	}

	HeadlessGlobals::~HeadlessGlobals()
	{
		PerformanceTelemetry::collector().reset();
		globalContainer = nullptr;
	}

	HeadlessGame::HeadlessGame(Options options)
		: gui(false), game(gui.game)
	{
		REQUIRE_MESSAGE(globalContainer != nullptr, "HeadlessGame needs a live HeadlessGlobals");
		random.emplace(game);
		game.gameHeader.setRandomSeed(options.seed);
		game.map.setSize(options.wDec, options.hDec, options.terrain);
		game.map.setGame(&game);
		if (options.clearImmobile)
			for (int y = 0; y < game.map.getH(); ++y)
				for (int x = 0; x < game.map.getW(); ++x)
					game.map.clearImmobileUnit(x, y);
		for (int i = 0; i < options.teams; ++i)
		{
			game.addTeam(i);
			if (options.loadDefaultRace)
				game.teams[i]->race.loadDefault();
		}
		team = game.teams[0];
		if (options.header)
		{
			GameHeader header;
			header.setNumberOfPlayers(options.teams);
			for (int i = 0; i < options.teams; ++i)
				header.getBasePlayer(i) = BasePlayer(i, ("test " + std::to_string(i)).c_str(), i, BasePlayer::P_LOCAL);
			header.setRandomSeed(options.seed);
			header.setExperiments(options.experiments);
			game.setGameHeader(header, true);
		}
		if (options.discovered)
			game.map.setMapDiscovered();
	}

	Building* HeadlessGame::addBuilding(const char* typeName, int x, int y, int level, int teamNumber)
	{
		const int typeNum = game.buildingsTypes.getTypeNum(typeName, level, false);
		REQUIRE_MESSAGE(typeNum >= 0, "building type exists: " << typeName);
		Building* building = game.addBuilding(x, y, typeNum, teamNumber);
		REQUIRE_MESSAGE(building != nullptr, "building placed: " << typeName << " at " << x << "," << y);
		if (building->type->semantics.occupiesGround)
			game.map.setBuilding(x, y, building->type->width, building->type->height, building->gid);
		return building;
	}

	Unit* HeadlessGame::addUnit(int typeNum, int x, int y, int teamNumber, int level)
	{
		if (x < 0)
		{
			x = 20 + parked % 10;
			y = 20 + parked / 10;
			++parked;
		}
		Unit* unit = game.addUnit(x, y, teamNumber, typeNum, level, 0, 0, 0);
		REQUIRE_MESSAGE(unit != nullptr, "unit placed at " << x << "," << y);
		return unit;
	}

	void HeadlessGame::step(int ticks)
	{
		for (int i = 0; i < ticks; ++i)
		{
			game.step(0);  // advances stepCounter itself
			gui.consumeClientEvents();  // as the engine does after every tick
		}
	}

	Uint32 HeadlessGame::checksum()
	{
		return game.checkSum();
	}
}

class HiringPipelineHarness
{
  public:
    static void discardPremature(Game &game)
    {
        if(!game.hiringPipeline)return;
        game.drainHiring();
        for(unsigned team=0;team<unsigned(game.teamsCount());++team)
            if(const auto &o=game.hiringPipeline->published[team];o &&
               (o->tick+HiringPipeline::Delay>game.stepCounter || o->tick%HiringPipeline::Refresh!=team%HiringPipeline::Refresh))
                game.hiringPipeline->published[team].reset();
    }
	static void publish(Game &game)
	{
		game.drainHiring();
		game.snapshotStore().invalidateBoundary();
		const auto setup = game.captureReadBoundary();
		// Immediate-ranking fixtures establish the same published inputs that real
		// matches obtain over their normal ticks; no unit actions run during setup.
		for (const auto& record : setup.entities->buildings)
		{
			if (!HiringPlanner::needsCandidates(record)) continue;
			auto* building = game.resolveBuilding(record.identity);
			if (!building) continue;
			const auto& type = setup.catalogs->buildings->at(record.typeNum).resolvedType;
			const bool footprint = std::any_of(std::begin(type.maxMaterial),std::begin(type.maxMaterial)+MaterialCount,[](int value){return value>0;}) ||
				std::any_of(std::begin(record.wishedMaterials),std::begin(record.wishedMaterials)+MaterialCount,[](int value){return value>0;});
			for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
				if (setup.entities->availableGroundClasses[record.team] & (1u<<swim))
				{
					for (auto route : {BuildingRoute::Footprint,BuildingRoute::Clearing,BuildingRoute::Combat})
					{
						if ((route==BuildingRoute::Footprint && !footprint) ||
							(route==BuildingRoute::Clearing && !type.attractsRole(0)) ||
							(route==BuildingRoute::Combat && !type.attractsRole(1) && !type.attractsRole(2))) continue;
						glob2test::awaitGradient(game,[&] {
							game.map.requestBuildingField(building,swim,route);
							return game.map.lookupBuildingField(building,swim,route);
						});
					}
					for (int material=0; material<MaterialCount; ++material)
						if (building->materialDeliveryNeed(material)>0)
							glob2test::awaitGradient(game,[&] { return game.map.getMaterialGradientSlot(record.team,material,swim,false,building); });
				}
		}
		game.snapshotStore().invalidateBoundary();
		auto input = HiringPipeline::bindRoutes(game, game.captureReadBoundary());
		if (!game.hiringPipeline)
			game.hiringPipeline = std::make_unique<HiringPipeline>(game.map.computeExecutor());
		for (unsigned team = 0; team < unsigned(game.teamsCount()); ++team)
		{
			HiringPlanner::Planner cache;
			auto output = std::make_shared<HiringPlanner::Output>(cache.rank(input, team, 128));
			game.hiringPipeline->published[team] = std::move(output);
		}
	}
};
namespace glob2test
{
void discardPrematureHiringSuggestions(Game &game)
{
    HiringPipelineHarness::discardPremature(game);
}
void publishCurrentHiringSuggestions(Game &game)
{
	HiringPipelineHarness::publish(game);
}
} // namespace glob2test
#include "TurretTargeting.h"
namespace glob2test {
void prepareTurretTargets(Building& turret)
{
    auto& game=*turret.owner->game;
    if (std::find(turret.owner->turrets.begin(),turret.owner->turrets.end(),&turret)==turret.owner->turrets.end())
        turret.owner->addToStaticAbilitiesLists(&turret);
    game.snapshots().invalidateBoundary();
    for (unsigned phase=0;phase<4;++phase) {
        const auto observed=game.captureReadBoundary();
        game.turretTargeting()->admit(game,observed,false);
        ++game.stepCounter;
        game.turretTargeting()->publish(game);
        if (!game.turretTargeting()->entries(turret).empty()) return;
    }
}
}
