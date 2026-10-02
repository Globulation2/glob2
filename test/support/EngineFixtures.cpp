// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"

#include "FileManager.h"
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "MapEditKeyActions.h"
#include "PerformanceTelemetry.h"
#include "Race.h"
#include "Utilities.h"

namespace glob2test
{
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
		const int typeNum = globalContainer->buildingsTypes.getTypeNum(typeName, level, false);
		REQUIRE_MESSAGE(typeNum >= 0, "building type exists: " << typeName);
		Building* building = game.addBuilding(x, y, typeNum, teamNumber);
		REQUIRE_MESSAGE(building != nullptr, "building placed: " << typeName << " at " << x << "," << y);
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
			game.syncStep(0);  // advances stepCounter itself
			gui.consumeClientEvents();  // as the engine does after every tick
		}
	}

	Uint32 HeadlessGame::checksum()
	{
		return game.checkSum();
	}
}
