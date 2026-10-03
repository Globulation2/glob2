// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "online/ReplayAppearance.h"
#include "online/OnlineServices.h"
#include "online/SkinDownloads.h"
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
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "OrderValidation.h"
#include "TurnLockstep.h"
#include "gui/ConnectionOverlay.h"
#include "gui/TurnMatchPresenter.h"

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>


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
    initializationDiagnostic.clear();
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
    MapHeader map;
    GameHeader players;
    auto stream = openGameInput(filename, map, players);
    if (!stream) co_return false;
    if (players.getNumberOfPlayers() == 0) players = prepareCampaign(map, gui.localPlayer, gui.localTeamNo);
    else { gui.localPlayer = 0; gui.localTeamNo = players.getBasePlayer(0).teamNumber; }
    if (campaign) players.getBasePlayer(0).name = campaign->getPlayerName();
    // Missions and the tutorial play as authored: never with experiments.
    if (!map.getIsSavedGame()) players.getExperiments().clear();
    const bool loaded = co_await initGameFromStreamTask(map, players, std::move(stream), false);
    if (loaded && campaign) gui.setCampaignGame(*campaign, mission);
    co_return loaded;
}
void Engine::applyLocalExperiments(GameHeader& header, const MapHeader& map)
{
    if (!map.getIsSavedGame())
        header.setExperiments(globalContainer->settings.experiments);
}
int Engine::initCustom(MapHeader& map, GameHeader& players, int localTeam, const std::string& sourceFileName)
{
    const bool loaded = initCustomTask(map, players, localTeam, -1, sourceFileName).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
GAGCore::CooperativeTask Engine::initCustomTask(MapHeader map, GameHeader players, int localTeam, int speed, std::string sourceFileName)
{
    initializationDiagnostic.clear();
    gui.localPlayer = 0;
    gui.localTeamNo = localTeam;
    // Restored by ~Engine(); a negative speed means the caller doesn't offer
    // a match-speed choice (e.g. the sync MapHeader/GameHeader overload).
    if (speed >= 0)
    {
        previousCustomSpeed = globalContainer->settings.gameSpeed;
        globalContainer->settings.gameSpeed = speed;
    }
    // Without this, a generated map falls back to a name-based library
    // lookup ("Random map" -> maps/Random_map.map) that never exists; a
    // premade map's on-disk path can also legitimately differ from its
    // declared map name (user libraries, duplicate names).
    co_return co_await initGameTask(map, players, true, false, false, sourceFileName);
}
int Engine::initCustom(const std::string& filename)
{
    const bool loaded = initCustomTask(filename).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}
GAGCore::CooperativeTask Engine::initCustomTask(std::string filename)
{
    initializationDiagnostic.clear();
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
    MapHeader map;
    GameHeader players;
    auto stream = openGameInput(filename, map, players);
    if (!stream) co_return false;
    for (int p = 0; p < players.getNumberOfPlayers(); ++p)
        if (players.getBasePlayer(p).type == BasePlayer::P_IP) players.getBasePlayer(p).makeItAI(AI::toggleAI);
    applyLocalExperiments(players, map);
    if (players.getNumberOfPlayers() == 0) co_return false;
    co_return co_await initGameFromStreamTask(map, players, std::move(stream), true);
}


int Engine::initTurnMatch(TurnMatchStart start)
{
    const bool loaded = initTurnMatchTask(std::move(start)).run();
    if (!loaded) showMapLoadError();
    return loaded ? EE_NO_ERROR : EE_CANT_LOAD_MAP;
}

GAGCore::CooperativeTask Engine::initTurnMatchTask(TurnMatchStart start)
{
    initializationDiagnostic.clear();
    if (!start.transport) co_return false;
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");
    TurnMatchState state;
    state.mapFile = start.mapFile;
    state.localSeat = start.localSeat;
    state.simVersion = start.setup.simVersion.key();
    state.networkKind = start.networkKind;
    state.relayId = start.relayId;
    state.relayRegion = start.relayRegion;
    state.map = loadMapHeader(start.mapFile);
    try { state.header = start.setup.toGameHeader(state.map); }
    catch (const Online::MatchSetupError& error)
    {
        initializationDiagnostic = error.what();
        co_return false;
    }
    const int players = state.header.getNumberOfPlayers();
    if (start.localSeat >= players) co_return false;
    int viewSeat = start.localSeat;
    if (viewSeat < 0)
    {
        viewSeat = 0;
        for (const auto& seat : start.setup.seats)
            if (seat.human) { viewSeat = seat.seat; break; }
    }
    state.localPlayer = viewSeat;
    state.localTeam = state.header.getBasePlayer(viewSeat).teamNumber;
    gui.localPlayer = state.localPlayer;
    gui.localTeamNo = state.localTeam;

    // ignoreGUIData: a saved game's own local player and viewport do not apply.
    const bool loaded = co_await initGameTask(state.map, state.header, true, true, false, state.mapFile);
    if (!loaded) co_return false;
    // A match without human seats would otherwise start as a live-spectated
    // offline game, whose local AI orders would be submitted as human orders.
    globalContainer->liveSpectating = false;
    gui.localPlayer = state.localPlayer;
    gui.localTeamNo = state.localTeam;

    // Replaces the NetEngine finishGameInit created; nothing has used it yet.
    auto session = std::make_unique<Turn::TurnLockstepSession>(players, start.transport, start.config);
    // Every human order is checked against this game before it executes, on every
    // client and in the verifier alike (OrderValidation.h).
    session->validator = [this](int player, Order& order) { return OrderValidation::validate(gui.game, player, order); };
    session->onLocalQuit = [this] { leaveTurnMatch(); };
    if (start.setup.pauseLimit)
        session->setPauseLimit({static_cast<std::uint32_t>(start.setup.pauseLimit->pauses),
                                static_cast<std::uint32_t>(start.setup.pauseLimit->seconds)});
    session->onPauseNotice = [this](Turn::PauseNotice notice, int seat) {
        auto& strings = *Toolkit::getStringTable();
        if (notice == Turn::PauseNotice::Refused)
        {
            if (seat == gui.localPlayer)
                gui.addNotice(strings.getString("[turn no pauses left]"));
        }
        else if (seat >= 0 && seat < gui.game.gameHeader.getNumberOfPlayers() && gui.game.players[seat])
            gui.addNotice(GAGCore::FormattableString(strings.getString("[turn pause time used %0]"))
                              .arg(gui.game.players[seat]->name));
    };
    turn = session.get();
    // What the menus and the Paused label show of the pause limit.
    gui.pauseState = [this] {
        GameGUI::PauseState state;
        if (!turn)
            return state;
        const auto period = std::max<std::uint64_t>(1, turn->turn().tickPeriodMicros());
        const auto secondsLeft = [&](int seat) {
            const std::uint64_t budget = std::uint64_t(turn->currentPauseLimit()->seconds) * 1000000;
            const std::uint64_t used = std::uint64_t(turn->pauseTicksUsed(seat)) * period;
            return int((budget > used ? budget - used : 0) / 1000000);
        };
        state.pausedBy = turn->pausedBy();
        if (const auto& limit = turn->currentPauseLimit())
        {
            state.limited = true;
            const int seat = gui.localPlayer;
            state.pausesLeft = int(limit->pauses > turn->pausesUsed(seat) ? limit->pauses - turn->pausesUsed(seat) : 0);
            state.secondsLeft = secondsLeft(seat);
            if (state.pausedBy >= 0)
                state.pauserSecondsLeft = secondsLeft(state.pausedBy);
        }
        return state;
    };
    net = std::move(session);
    const char* envReplayPath = getenv("GLOB2_REPLAY_PATH");
    state.replayPath = envReplayPath ? envReplayPath : "replays/last_game.replay";
    turnMatch = std::move(state);
    // The connection HUD replaces the "waiting for players" notice.
    gui.networkMatch.active = true;
    gui.connectionOverlay = std::make_unique<ConnectionOverlay>();
    turnPresenter = std::make_unique<TurnMatchPresenter>();
    gui.connectionOverlay->source = [this] {
        return turn ? turnPresenter->snapshot(turn->turn(), gui.game, turnNowMicros) : ConnectionSnapshot();
    };
    gui.connectionOverlay->leave = [this] { gui.isRunning = false; };
    gui.connectionOverlay->notice = [this](const std::string& line) { gui.addNotice(line); };
    gui.connectionNotice = [this] {
        return turn ? TurnMatchPresenter::notice(turn->turn(), gui.game) : std::vector<std::string>();
    };
    co_return true;
}


Turn::TurnSession* Engine::turnSession()
{
    return turn ? &turn->turn() : nullptr;
}

bool Engine::turnFastForwarding()
{
    if (!turn || !session || !gui.isRunning)
        return false;
    const Turn::TurnSession& s = turn->turn();
    return s.state() == Turn::TurnSession::State::Running && s.catchingUp() && !s.needsReload() && s.bufferedTicks() > 0;
}


namespace
{
	// GLOB2_TEST_RULES turns custom-game rules on for -test-games(-nox) matches, as
	// comma-separated name=value pairs (docs/development/headless-replays.md), so AI matches can exercise
	// the rules without the lobby. An unknown name or out-of-range value stops the run.
	void applyTestRules(GameHeader& header)
	{
		const char* environment = getenv("GLOB2_TEST_RULES");
		if (!environment || !*environment)
			return;
		struct Rule
		{
			const char* name;
			long maximum;
			std::function<void(GameHeader&, int)> apply;
		};
		std::vector<Rule> rules = {
			{"noGrowth", 1, [](GameHeader& h, int v) { h.setResourceGrowthDisabled(v); }},
			{"scarcity", 3, [](GameHeader& h, int v) { h.setResourceScarcityLevel(v); }},
			{"instantConstruction", 1, [](GameHeader& h, int v) { h.setInstantConstructionEnabled(v); }},
			{"stockpile", 3, [](GameHeader& h, int v) { h.setStockpileStartLevel(v); }},
			{"noHunger", 1, [](GameHeader& h, int v) { h.setHungerDisabled(v); }},
			{"noUpgrades", 1, [](GameHeader& h, int v) { h.setUnitUpgradesDisabled(v); }},
			{"glassCannon", 2, [](GameHeader& h, int v) { h.setGlassCannonLevel(v); }},
			{"fearless", 1, [](GameHeader& h, int v) { h.setUnitsFearless(v); }},
			{"noPermadeath", 1, [](GameHeader& h, int v) { h.setPermadeathDisabled(v); }},
			{"peaceful", 1, [](GameHeader& h, int v) { h.setPeacefulModeEnabled(v); }},
			{"fortress", 2, [](GameHeader& h, int v) { h.setBuildingHpLevel(v); }},
			{"suddenDeathTick", 100000000, [](GameHeader& h, int v)
				{
					WinningCondition::setSuddenDeathWinCondition(h.getWinningConditions(),
						v ? std::optional<Uint32>(v) : std::nullopt);
				}},
			{"winProbabilityPermille", 1000, [](GameHeader& h, int v)
				{
					WinningCondition::setWinProbabilityWinCondition(h.getWinningConditions(),
						v ? std::optional<Uint32>(v) : std::nullopt);
				}},
		};
		// One 0/1 rule per experiment, named by its key (ExperimentalFeatures.cpp).
		for (const auto& definition : experimentDefinitions())
			rules.push_back({definition.key, 1, [id = definition.id](GameHeader& h, int v) { h.getExperiments().set(id, v != 0); }});
		std::stringstream list(environment);
		std::string item;
		while (std::getline(list, item, ','))
		{
			const size_t equals = item.find('=');
			const std::string name = item.substr(0, equals);
			const Rule* rule = nullptr;
			for (const Rule& candidate : rules)
				if (name == candidate.name)
					rule = &candidate;
			char* end = nullptr;
			errno = 0;
			const long value = equals == std::string::npos ? -1 : strtol(item.c_str() + equals + 1, &end, 10);
			if (!rule || equals == std::string::npos || errno || *end || end == item.c_str() + equals + 1
				|| value < 0 || value > rule->maximum
				|| (name == "winProbabilityPermille" && value != 0 && value < 501))
			{
				std::cerr << "GLOB2_TEST_RULES: invalid entry \"" << item << "\"" << std::endl;
				exit(1);
			}
			rule->apply(header, static_cast<int>(value));
			std::cout << "GLOB2_TEST_RULES: " << name << "=" << value << std::endl;
		}
	}
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
	applyLocalExperiments(game, map);
	applyTestRules(game);
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
	const std::string gzipPath = glob2GzipWritePath(path);
	const bool saved = Toolkit::getFileManager()->writeGzipAtomically(gzipPath,
		[&](OutputStream &stream) { gui.save(&stream, mapName); });
	if (!saved)
	{
		std::cerr << label << ": cannot open " << gzipPath << " for writing" << std::endl;
		exit(1);
	}
	std::cout << label << ": wrote " << gzipPath << std::endl;
}



bool Engine::haveMap(const MapHeader& mapHeader)
{
	FileManager& files = *Toolkit::getFileManager();
	const std::string resolved = glob2PreferGzipReadPath(files, mapHeader.getFileName());
	if (!files.exists(resolved))
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
	initializationDiagnostic.clear();
	bool error = false;
	try
	{
		error = !(co_await gui.loadFromHeadersTask(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI, sourceFileName));
	}
	catch (std::exception &e)
	{
		initializationDiagnostic = e.what();
		std::cerr << "Failed to load the map: " << initializationDiagnostic << std::endl;
		error = true;
	}
	if (error) {
		co_return false;
	}
	finishGameInit();
	co_return true;
}

GAGCore::CooperativeTask Engine::initGameFromStreamTask(MapHeader map, GameHeader players, std::unique_ptr<InputStream> stream, bool saveAI)
{
    bool loaded = false;
    try
    {
        loaded = co_await gui.loadFromStreamTask(map, players, true, false, saveAI, stream.get());
    }
    catch (const std::exception& error)
    {
        initializationDiagnostic = error.what();
        std::cerr << "Failed to load the map: " << initializationDiagnostic << std::endl;
    }
    stream.reset(); // release the snapshot before replay/network setup
    if (loaded) finishGameInit();
    co_return loaded;
}

void Engine::finishGameInit()
{
    const bool offlineSetup = !globalContainer->replaying && !gui.game.gameHeader.hasNetworkPlayer();
    globalContainer->liveSpectating = offlineSetup;
    if(offlineSetup) {
        for(int i=0;i<gui.game.gameHeader.getNumberOfPlayers();++i)
            if(gui.game.gameHeader.getBasePlayer(i).type==BasePlayer::P_LOCAL) {
                globalContainer->liveSpectating=false;
                gui.localPlayer=i;
                gui.localTeamNo=gui.game.gameHeader.getBasePlayer(i).teamNumber;
                break;
            }
    }
    if(globalContainer->liveSpectating) {
        gui.localPlayer=0;gui.localTeamNo=gui.game.gameHeader.getBasePlayer(0).teamNumber;
        globalContainer->replayVisibleTeams=REPLAY_VISIBLE_TEAMS_ALL;
        globalContainer->replayShowFog=false;
        globalContainer->replayShowAreas=false;
        globalContainer->replayShowFlags=true;
    }
	gui.game.clearingUncontrolledTeams();
	finalAdjustments();
	if(globalContainer->liveSpectating) gui.configureLiveSpectatorView();

	net = std::make_unique<NetEngine>(gui.game.gameHeader.getNumberOfPlayers(), gui.localPlayer);

	// Initialise the replay writer, unless we're showing a replay.
	// GLOB2_REPLAY_PATH overrides the default output path (used by the
	// AI-trainer pipeline to keep per-game replays without overwriting,
	// and to allow concurrent headless instances to write to distinct files).
	const char* envReplayPath = getenv("GLOB2_REPLAY_PATH");
	std::string replayPath = envReplayPath ? envReplayPath : "replays/last_game.replay";
	if (!globalContainer->replaying && (!globalContainer->structuredHeadless || globalContainer->headlessReplay))
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
}

GAGCore::CooperativeTask Engine::initCustomFromBytesTask(MapHeader map, GameHeader players, int localTeam, int speed, std::shared_ptr<std::string> bytes)
{
    initializationDiagnostic.clear();
    gui.localPlayer = 0;
    gui.localTeamNo = localTeam;
    if (speed >= 0)
    {
        previousCustomSpeed = globalContainer->settings.gameSpeed;
        globalContainer->settings.gameSpeed = speed;
    }
    // BinaryInputStream owns and deletes its backend; the copy the backend takes here is
    // the same one loadTask's file-based sibling pays for on the read side of a real file.
    auto *backend = new GAGCore::MemoryStreamBackend(bytes->data(), bytes->size());
    // The memory backend copies its input through write(), leaving its cursor at
    // the end. Start at the map header, as file-backed streams do.
    backend->seekFromStart(0);
    GAGCore::BinaryInputStream stream(backend);
    bool error = false;
    try
    {
        error = !(co_await gui.loadFromStreamTask(map, players, true, false, false, &stream));
    }
    catch (std::exception &e)
    {
        initializationDiagnostic = e.what();
        std::cerr << "Failed to load the generated map: " << initializationDiagnostic << std::endl;
        error = true;
    }
    if (error) {
        co_return false;
    }
    finishGameInit();
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
	BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), filename));
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
    initializationDiagnostic.clear();
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
	bool loaded = co_await initGameTask(mapHeader, gameHeader, true, false, true, fileName);
	if (!loaded)
	{
		clearReplayState();
		co_return false;
	}

    if (!globalContainer->runNoX)
    {
        auto &online = Online::services();
        if (const auto appearance = Online::readReplayAppearance(*Toolkit::getFileManager(),fileName,online.config))
            setColonySkins(std::make_unique<Online::SkinDownloads>(online.storage,appearance->origin,
                appearance->matchId,std::vector<Online::SkinDownloads::Ticket>{}));
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
	// Interactive flows run the task through GameLoadScreen, which reports the
	// failure on the screen stack; the synchronous wrappers only log it.
	std::cerr << Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]") << std::endl;
	if (!initializationDiagnostic.empty())
		std::cerr << initializationDiagnostic << std::endl;
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
