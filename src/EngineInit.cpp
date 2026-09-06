// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <BinaryStream.h>

#include "AINames.h"
#include "ChecksumSidecar.h"
#include "CustomGameScreen.h"
#include "DatasetWriter.h"
#include "Engine.h"
#include <vector>
#include "EngineTiming.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GUIMessageBox.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"

#include <iostream>


int Engine::initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = loadGameHeader(mapName);
	if(gameHeader.getNumberOfPlayers() == 0)
	{
		gameHeader = prepareCampaign(mapHeader, gui.localPlayer, gui.localTeamNo);
	}
	else
	{
		gui.localPlayer = 0;
		gui.localTeamNo = gameHeader.getBasePlayer(0).teamNumber;
	}

	gameHeader.getBasePlayer(0).name = campaign.getPlayerName();

	int end=initGame(mapHeader, gameHeader);
	gui.setCampaignGame(campaign, missionName);
	return end;
}



int Engine::initCampaign(const std::string &mapName)
{
	MapHeader mapHeader = loadMapHeader(mapName);
	GameHeader gameHeader = loadGameHeader(mapName);
	if(gameHeader.getNumberOfPlayers() == 0)
	{
		gameHeader = prepareCampaign(mapHeader, gui.localPlayer, gui.localTeamNo);
	}
	else
	{
		gui.localPlayer = 0;
		gui.localTeamNo = gameHeader.getBasePlayer(0).teamNumber;
	}
	int end=initGame(mapHeader, gameHeader);
	return end;
}



int Engine::initCustom(void)
{
	CustomGameScreen customGameScreen;

	int cgs=customGameScreen.execute(globalContainer->gfx, GAME_TICK_MS);

	if (cgs==CustomGameScreen::CANCEL)
		return EE_CANCEL;
	if (cgs==-1)
		return -1;

	int teamColor=customGameScreen.getSelectedColor(0);
	gui.localPlayer=0;
	gui.localTeamNo=teamColor;

	int ret = initGame(customGameScreen.getMapHeader(), customGameScreen.getGameHeader());
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;
	else if(ret == -1)
		return -1;

	return EE_NO_ERROR;
}

int Engine::initCustom(const std::string &gameName)
{
	MapHeader mapHeader = loadMapHeader(gameName);
	GameHeader gameHeader = loadGameHeader(gameName);

	// If the game is a network saved game, we need to toggle net players to ai players:
	for (int p=0; p<gameHeader.getNumberOfPlayers(); p++)
	{
		if (verbose)
			printf("Engine::initCustom::player[%d].type=%d.\n", p, gameHeader.getBasePlayer(p).type);
		if (gameHeader.getBasePlayer(p).type==BasePlayer::P_IP)
		{
			gameHeader.getBasePlayer(p).makeItAI(AI::toggleAI);
			if (verbose)
				printf("Engine::initCustom::net player (id %d) was made ai.\n", p);
		}
	}

	int ret = initGame(mapHeader, gameHeader, true, false, true);
	if(ret != EE_NO_ERROR)
		return EE_CANT_LOAD_MAP;
	else if(ret == -1)
		return -1;

	return EE_NO_ERROR;
}

int Engine::initLoadGame()
{
	ChooseMapScreen loadGameScreen("games", "game", true, "replays", "replay", false);
	int lgs = loadGameScreen.execute(globalContainer->gfx, GAME_TICK_MS);
	if (lgs == ChooseMapScreen::CANCEL)
		return EE_CANCEL;
	else if(lgs == -1)
		return -1;

	assert(loadGameScreen.getSelectedType() != ChooseMapScreen::NONE);
	assert(loadGameScreen.getSelectedType() != ChooseMapScreen::MAP);

	if (loadGameScreen.getSelectedType() == ChooseMapScreen::GAME)
		return initCustom(loadGameScreen.getMapHeader().getFileName());
	else if (loadGameScreen.getSelectedType() == ChooseMapScreen::REPLAY)
		return loadReplay(loadGameScreen.getMapHeader().getFileName(false,true));
	else
		assert(false);
}

int Engine::initMultiplayer(std::shared_ptr<MultiplayerGame> multiplayerGame, std::shared_ptr<YOGClient> client, int localPlayer)
{
	gui.localPlayer = localPlayer;
	gui.localTeamNo = multiplayerGame->getGameHeader().getBasePlayer(localPlayer).teamNumber;

	// On failure, initGame has not created `net`; propagate the error before
	// touching it, and leave `multiplayer` unset so the engine is not left
	// half-initialised (mirrors the clean state teardownSession leaves).
	int ret = initGame(multiplayerGame->getMapHeader(), multiplayerGame->getGameHeader(), true, true);
	if (ret != EE_NO_ERROR)
		return ret;

	multiplayer = multiplayerGame;
	multiplayer->setNetEngine(net.get());

	for (int p=0; p<multiplayerGame->getGameHeader().getNumberOfPlayers(); p++)
	{
		if (multiplayerGame->getGameHeader().getBasePlayer(p).type==BasePlayer::P_IP)
		{
			net->prepareForLatency(p, multiplayerGame->getGameHeader().getGameLatency());
		}
	}

	net->setNetworkInfo(multiplayerGame->getGameHeader().getOrderRate(), client->getGameConnection());

	return Engine::EE_NO_ERROR;
}



void Engine::createRandomGame()
{
	MapHeader map;

	if (!globalContainer->testGamesMap.empty())
	{
		// --map: try once, fail loudly. The legacy retry loop below would
		// spin forever on a typo'd map name. loadMapHeader does NOT throw
		// on a missing file (it logs to stderr and returns a default-
		// constructed MapHeader with numberOfTeams=0), so we detect failure
		// by checking the team count rather than catching an exception.
		std::optional<MapHeader> chosen;
		try
		{
			chosen = chooseRandomMap();
		}
		catch (std::ios_base::failure &e)
		{
			std::cerr << "--map: cannot load maps/"
				<< globalContainer->testGamesMap << ".map: "
				<< e.what() << std::endl;
			exit(1);
		}
		// With --map set, chooseRandomMap never returns nullopt (the
		// override path either loads or throws), but defend against it
		// anyway so a future refactor doesn't reintroduce undefined state.
		if (!chosen || chosen->getNumberOfTeams() <= 0)
		{
			std::cerr << "--map: cannot load maps/"
				<< globalContainer->testGamesMap << ".map "
				<< "(missing or invalid; numberOfTeams=0)" << std::endl;
			exit(1);
		}
		map = *chosen;
	}
	else
	{
		bool validMapChosen = false;
		while (!validMapChosen)
		{
			try
			{
				std::optional<MapHeader> chosen = chooseRandomMap();
				if (!chosen)
				{
					// Empty or unreadable maps/ directory. Previously this
					// path produced syncRand() % 0 (UB / SIGFPE) inside
					// chooseRandomMap; now we exit cleanly so the user
					// gets an actionable message instead of a crash or a
					// retry loop that can never succeed.
					std::cerr << "createRandomGame: no maps available in "
						<< "maps/ directory (empty or unreadable). "
						<< "Cannot pick a random map." << std::endl;
					exit(1);
				}
				map = *chosen;
				validMapChosen = true;
			}
			catch (std::ios_base::failure &e)
			{
				validMapChosen = false;
			}
		}
	}

	std::cout<<"Randomly Chosen Map: "<<map.getMapName()<<std::endl;

	// Validate matchup-vs-map team count now that we know how many teams
	// the loaded map has. Self-contained matchup validation already
	// happened in GlobalContainer::parseArgs; this is the deferred check.
	if (!globalContainer->testGamesMatchup.empty()
		&& (int)globalContainer->testGamesMatchup.size() != map.getNumberOfTeams())
	{
		std::cerr << "--matchup has " << globalContainer->testGamesMatchup.size()
			<< " entries but map " << map.getMapName() << " has "
			<< map.getNumberOfTeams() << " teams" << std::endl;
		exit(1);
	}

	GameHeader game = createRandomGame(map.getNumberOfTeams());
	// Mirror the syncRand seed (captured at runTestGames entry) into the
	// GameHeader so a saved .game file reloads with the same syncRand
	// state. GameHeader's ctor defaults seed to time(NULL) at header-
	// construction time, which won't match GLOB2_TEST_SEED (and even
	// without that env var, can drift seconds away from the time(NULL)
	// runTestGames already used for setSyncRandSeed). Without this mirror,
	// --save-game-as / GLOB2_DUMP_GAME produce .game files that diverge
	// from the original run when reloaded via --nox.
	if (globalContainer->testGamesSeedSet)
	{
		game.setRandomSeed(globalContainer->testGamesSeed);
	}
	std::cout<<"Random Seed gameheader: "<<game.getRandomSeed();
	for (int p=0; p<game.getNumberOfPlayers(); p++)
	{
		std::cout<<"    Player: "<<game.getBasePlayer(p).name<<" for team "<<game.getBasePlayer(p).teamNumber<<std::endl;
	}

	gui.localPlayer=0;
	gui.localTeamNo=0;

	initGame(map, game);

	// Capture the fully-initialised tick-0 game state to a .game file so
	// the same scenario can later be replayed deterministically via --nox.
	// Uses gui.save() for the complete game-state format (matching the GUI
	// Custom-Game save path), not just the headers — partial dumps fail to
	// load because loadFromHeaders re-reads numberOfTeams from the saved
	// state. Used to bootstrap checked-in regression baselines.
	//
	// Two entry points: GLOB2_DUMP_GAME (legacy env var, used by tooling)
	// and --save-game-as (CLI flag). They are independent — if both are
	// set, both files are written. Pair either with GLOB2_TEST_SEED for a
	// fully reproducible scenario; the seed is mirrored into GameHeader
	// above before save.
	const char* dumpPath = getenv("GLOB2_DUMP_GAME");
	if (dumpPath)
		saveInitialGameStateOrExit(dumpPath, "GLOB2_DUMP_GAME", map.getMapName());
	if (!globalContainer->testGamesSaveGameAs.empty())
		saveInitialGameStateOrExit(globalContainer->testGamesSaveGameAs, "--save-game-as", map.getMapName());
	if (getenv("GLOB2_MAP_STATS"))
	{
		printMapStats();
		exit(0);
	}
}

// GLOB2_MAP_STATS: print start-position asymmetry of the loaded map (swarm
// positions, resources and terrain around each start, pairwise distances,
// a coarse ASCII map) and exit. Analysis aid for AI-vs-AI results.
void Engine::printMapStats()
{
	Game& game = gui.game;
	Map& m = game.map;
	int w = m.getW(), h = m.getH();
	int nbTeams = game.mapHeader.getNumberOfTeams();
	std::cout << "MAPSTATS map=\"" << game.mapHeader.getMapName() << "\" size=" << w << "x" << h << " teams=" << nbTeams << std::endl;
	std::vector<int> sx(nbTeams, -1), sy(nbTeams, -1);
	for (int t = 0; t < nbTeams; t++)
	{
		Team* team = game.teams[t];
		if (!team) continue;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b && b->shortTypeNum == IntBuildingType::SWARM_BUILDING && sx[t] < 0)
			{
				sx[t] = b->posX;
				sy[t] = b->posY;
			}
		}
		std::cout << "MAPSTATS team=" << t << " swarm=" << sx[t] << "," << sy[t] << " allies=0x" << std::hex << team->allies << std::dec << std::endl;
	}
	const char* resNames[8] = {"wood", "corn", "papyrus", "stone", "alga", "cherry", "orange", "prune"};
	for (int t = 0; t < nbTeams; t++)
	{
		if (sx[t] < 0) continue;
		for (int radius : {12, 24, 40})
		{
			int res[8] = {0}, resAmount[8] = {0}, water = 0, sand = 0, grass = 0;
			for (int dy = -radius; dy <= radius; dy++)
				for (int dx = -radius; dx <= radius; dx++)
				{
					int x = (sx[t] + dx) & m.getMaskW(), y = (sy[t] + dy) & m.getMaskH();
					const Resource& r = m.getResource(x, y);
					if (r.type != NO_RES_TYPE && r.type < 8) { res[r.type]++; resAmount[r.type] += r.amount; }
					if (m.isWater(x, y)) water++;
					else if (m.isSand(x, y)) sand++;
					else grass++;
				}
			std::cout << "MAPSTATS team=" << t << " radius=" << radius << " water=" << water << " sand=" << sand << " grass=" << grass;
			for (int i = 0; i < 8; i++)
				std::cout << " " << resNames[i] << "=" << res[i] << "/" << resAmount[i];
			std::cout << std::endl;
		}
		// Nearest tile of each resource type.
		std::cout << "MAPSTATS team=" << t << " nearest";
		for (int i = 0; i < 8; i++)
		{
			int best = -1;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					if (m.getResource(x, y).type == i)
					{
						int d = m.warpDistMax(x, y, sx[t], sy[t]);
						if (best < 0 || d < best) best = d;
					}
			std::cout << " " << resNames[i] << "=" << best;
		}
		std::cout << std::endl;
	}
	for (int a = 0; a < nbTeams; a++)
		for (int b = a + 1; b < nbTeams; b++)
			if (sx[a] >= 0 && sx[b] >= 0)
				std::cout << "MAPSTATS dist team" << a << "-team" << b << "=" << m.warpDistMax(sx[a], sy[a], sx[b], sy[b]) << std::endl;
	// Coarse ASCII map, 2x2 cells per character.
	for (int y = 0; y < h; y += 2)
	{
		std::string line;
		for (int x = 0; x < w; x += 2)
		{
			char c = '.';
			bool water = false, sand = false;
			int resType = -1;
			for (int dy = 0; dy < 2; dy++)
				for (int dx = 0; dx < 2; dx++)
				{
					int xx = x + dx, yy = y + dy;
					for (int t = 0; t < nbTeams; t++)
						if (sx[t] >= 0 && abs(xx - sx[t]) <= 1 && abs(yy - sy[t]) <= 1)
							c = '0' + t;
					if (m.isWater(xx, yy)) water = true;
					else if (m.isSand(xx, yy)) sand = true;
					int rt = m.getResource(xx, yy).type;
					if (rt != NO_RES_TYPE && (resType < 0 || rt < resType)) resType = rt;
				}
			if (c == '.')
			{
				if (resType == WOOD) c = 'T';
				else if (resType == CORN) c = 'c';
				else if (resType == STONE) c = '#';
				else if (resType == ALGA) c = 'a';
				else if (resType >= CHERRY) c = 'f';
				else if (resType == PAPYRUS) c = 'p';
				else if (water) c = '~';
				else if (sand) c = ':';
			}
			line += c;
		}
		std::cout << "MAPROW " << line << std::endl;
	}
}

void Engine::saveInitialGameStateOrExit(const std::string& path, const std::string& label, const std::string& mapName)
{
	BinaryOutputStream stream(Toolkit::getFileManager()->openOutputStreamBackend(path));
	if (stream.isEndOfStream())
	{
		std::cerr << label << ": cannot open " << path << " for writing" << std::endl;
		exit(1);
	}
	gui.save(&stream, mapName);
	std::cout << label << ": wrote " << path << std::endl;
}



bool Engine::haveMap(const MapHeader& mapHeader)
{
	if (!Toolkit::getFileManager()->exists(mapHeader.getFileName()))
		return false;
	MapHeader mh = loadMapHeader(mapHeader.getFileName());
	return mh == mapHeader;
}



int Engine::initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI)
{
	bool error = false;
	try
	{
		error = !gui.loadFromHeaders(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI);
	}
	catch (std::exception &e)
	{
		std::cerr << "Failed to load the map: exception received." << std::endl;
		error = true;
	}
	if (error) {
		showMapLoadError();
		return EE_CANT_LOAD_MAP;
	}

	gui.game.clearingUncontrolledTeams();
	finalAdjustments();

	net = std::make_unique<NetEngine>(gui.game.gameHeader.getNumberOfPlayers(), gui.localPlayer);

	// Initialise the replay writer, unless we're showing a replay.
	// GLOB2_REPLAY_PATH overrides the default output path (used by the
	// AI-trainer pipeline to keep per-game replays without overwriting,
	// and to allow concurrent headless instances to write to distinct files).
	const char* envReplayPath = getenv("GLOB2_REPLAY_PATH");
	std::string replayPath = envReplayPath ? envReplayPath : "replays/last_game.replay";
	if (!globalContainer->replaying)
	{
		assert(globalContainer->replayWriter == nullptr);
		globalContainer->replayWriter = std::make_unique<ReplayWriter>();
		globalContainer->replayWriter->init(replayPath, gui);
	}

	// Initialise checksum sidecar writer if requested
	if (getenv("GLOB2_CHECKSUM_SIDECAR"))
	{
		std::string sidecarBase = globalContainer->replaying
			? globalContainer->replayFileName
			: replayPath;
		checksumSidecar = std::make_unique<ChecksumSidecarWriter>();
		if (!checksumSidecar->open(sidecarBase, gui.game))
		{
			std::cerr << "GLOB2_CHECKSUM_SIDECAR: failed to open checksum sidecar for "
				<< sidecarBase << std::endl;
			checksumSidecar.reset();
		}
	}

	// Initialise dataset writer if GLOB2_DATASET_PATH is set. Writes
	// one (state, action) record per executed order — see DatasetWriter.h.
	// Skipped when replaying (no orders fire that the trainer cares about).
	const char* envDatasetPath = getenv("GLOB2_DATASET_PATH");
	if (envDatasetPath && !globalContainer->replaying)
	{
		assert(globalContainer->datasetWriter == nullptr);
		globalContainer->datasetWriter = std::make_unique<DatasetWriter>();
		if (!globalContainer->datasetWriter->open(envDatasetPath))
		{
			std::cerr << "GLOB2_DATASET_PATH: failed to open dataset file "
				<< envDatasetPath << std::endl;
			globalContainer->datasetWriter.reset();
		}
	}

	return EE_NO_ERROR;
}



GameHeader Engine::prepareCampaign(MapHeader& mapHeader, int& localPlayer, int& localTeam)
{
	GameHeader gameHeader;

	// We make a player for each team in the mapHeader
	int playerNumber=0;
	// Incase there are multiple "humans" selected, only the first will actually become human
	bool wasHuman=false;
	// Each team has a variable, type, that designates whether it is a human or an AI in
	// a campaign match.
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (mapHeader.getBaseTeam(i).type==BaseTeam::T_HUMAN && !wasHuman)
		{
			localPlayer = playerNumber;
			localTeam = i;
			std::string name = FormattableString("Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_LOCAL);
			wasHuman=true;
		}
		else if (mapHeader.getBaseTeam(i).type==BaseTeam::T_AI || wasHuman)
		{
			std::string name = FormattableString("AI Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_AI);
		}
		playerNumber+=1;
	}
	if(!wasHuman)
	{
		localPlayer = 0;
		localTeam = gameHeader.getBasePlayer(0).teamNumber;
	}

	gameHeader.setNumberOfPlayers(playerNumber);

	return gameHeader;
}



bool Engine::loadGame(const std::string &filename)
{
	BinaryInputStream stream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream.isEndOfStream())
	{
		std::cerr << "Engine::loadGame(\"" << filename << "\") : error, can't open file." << std::endl;
		return false;
	}
	if (!gui.load(&stream))
	{
		std::cerr << "Engine::loadGame(\"" << filename << "\") : error, can't load game." << std::endl;
		return false;
	}

	if (verbose)
		std::cout << "Engine::loadGame(\"" << filename << "\") : game successfully loaded." << std::endl;
	return true;
}



int Engine::loadReplay(const std::string &fileName)
{
	// Parse the replay file before committing any global state, so a failed
	// load leaves globalContainer as if no replay had been requested.
	auto replayReader = std::make_unique<ReplayReader>();
	bool replayLoaded = replayReader->loadReplay(fileName);

	if (!replayLoaded)
	{
		showMapLoadError();
		clearReplayState();
		return EE_CANT_LOAD_MAP;
	}

	assert(replayReader->isValid());

	// The replay parsed: let globalContainer know we are now replaying.
	// initGame below branches on `replaying` (it skips the replay writer
	// and dataset writer, and keys the checksum sidecar off replayFileName).
	globalContainer->replaying = true;
	globalContainer->replayFileName = fileName;
	globalContainer->replayReader = std::move(replayReader);

	// Reset the replay's options
	gui.localPlayer = 0;
	gui.localTeamNo = 0;
	globalContainer->replayVisibleTeams = REPLAY_VISIBLE_TEAMS_ALL;
	globalContainer->replayFastForward = false;

	MapHeader mapHeader = loadMapHeader(fileName);
	GameHeader gameHeader = loadGameHeader(fileName);

	// A replay drives players from recorded orders, so no live AI runs.
	for (int p=0; p<gameHeader.getNumberOfPlayers(); p++)
	{
		gameHeader.getBasePlayer(p).makeItAI(AI::NONE);
	}

	// Finally, initialise the Game. If the map embedded in the replay fails
	// to load, drop the replay state committed above so the next game
	// session starts as a normal game.
	int ret = initGame(mapHeader, gameHeader, true, false, true);
	if(ret != EE_NO_ERROR)
	{
		clearReplayState();
		return EE_CANT_LOAD_MAP;
	}

	return EE_NO_ERROR;
}

void Engine::clearReplayState()
{
	globalContainer->replaying = false;
	globalContainer->replayFileName.clear();
	globalContainer->replayReader.reset();
}

void Engine::showMapLoadError()
{
	if (!globalContainer->runNoX)
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));
}

void Engine::finalAdjustments(void)
{
	gui.adjustLocalTeam();
	if (!globalContainer->runNoX)
	{
		gui.adjustInitialViewport();
	}
	gui.game.setAlliances();
}
