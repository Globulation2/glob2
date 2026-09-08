// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <BinaryStream.h>

#include "AINames.h"
#include "ChecksumSidecar.h"
#include "DatasetWriter.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GUIMessageBox.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"

#include <iostream>


int Engine::initCampaign(const std::string& filename, Campaign& campaign, const std::string& mission)
{
    const bool loaded = initCampaignTask(filename, &campaign, mission).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
int Engine::initCampaign(const std::string& filename)
{
    const bool loaded = initCampaignTask(filename).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
GAGCore::CooperativeTask Engine::initCampaignTask(std::string filename, Campaign* campaign, std::string mission)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
    auto map = loadMapHeader(filename);
    auto players = loadGameHeader(filename);
    if (players.getNumberOfPlayers() == 0) players = prepareCampaign(map, gui.localPlayer, gui.localTeamNo);
    else { gui.localPlayer = 0; gui.localTeamNo = players.getBasePlayer(0).teamNumber; }
    if (campaign) players.getBasePlayer(0).name = campaign->getPlayerName();
    const bool loaded = co_await initGameTask(map, players);
    if (loaded && campaign) gui.setCampaignGame(*campaign, mission);
    co_return loaded;
}
int Engine::initCustom(MapHeader& map, GameHeader& players, int localTeam)
{
    const bool loaded = initCustomTask(map, players, localTeam).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
GAGCore::CooperativeTask Engine::initCustomTask(MapHeader map, GameHeader players, int localTeam)
{
    gui.localPlayer = 0;
    gui.localTeamNo = localTeam;
    co_return co_await initGameTask(map, players);
}
int Engine::initCustom(const std::string& filename)
{
    const bool loaded = initCustomTask(filename).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
GAGCore::CooperativeTask Engine::initCustomTask(std::string filename)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
    auto map = loadMapHeader(filename);
    auto players = loadGameHeader(filename);
    for (int p = 0; p < players.getNumberOfPlayers(); ++p)
        if (players.getBasePlayer(p).type == BasePlayer::P_IP) players.getBasePlayer(p).makeItAI(AI::toggleAI);
    co_return co_await initGameTask(map, players, true, false, true, filename);
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



int Engine::initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, const std::string& sourceFileName)
{
    const bool loaded = initGameTask(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI, sourceFileName).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}

GAGCore::CooperativeTask Engine::initGameTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, std::string sourceFileName)
{
	bool error = false;
	try
	{
		error = !(co_await gui.loadFromHeadersTask(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI, sourceFileName));
	}
	catch (std::exception &e)
	{
		std::cerr << "Failed to load the map: exception received." << std::endl;
		error = true;
	}
	if (error) {
		co_return false;
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

	co_return true;
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



int Engine::loadReplay(const std::string& filename)
{
    const bool loaded = loadReplayTask(filename).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}

GAGCore::CooperativeTask Engine::loadReplayTask(std::string fileName)
{
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
	// Parse the replay file before committing any global state, so a failed
	// load leaves globalContainer as if no replay had been requested.
	auto replayReader = std::make_unique<ReplayReader>();
	bool replayLoaded = replayReader->loadReplay(fileName);

	if (!replayLoaded)
	{
		clearReplayState();
		co_return false;
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
	bool loaded = co_await initGameTask(mapHeader, gameHeader, true, false, true);
	if (!loaded)
	{
		clearReplayState();
		co_return false;
	}

	co_return true;
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

void Engine::cancelInitialization()
{
    teardownSession();
    clearReplayState();
}
