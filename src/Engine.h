// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Header.h"
#include "GameGUI.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Campaign.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "LockstepSession.h"
#include "MatchSetup.h"
#include "NetEngine.h"
#include "TurnSession.h"
#include "MultiplayerGame.h"
#include "ChecksumSidecar.h"
#include "ConnectionOverlay.h"


class MultiplayersJoin;
class NetGame;
namespace Turn { class TurnLockstepSession; }
namespace Online { class OnlineMatchResult; }

using std::shared_ptr;

/// Engine is the backend of the game. It is responsible for loading and setting up games and players,
/// and its run function is meant to run the game that has been loaded.
class Engine
{
	friend struct CustomGameSetupHarness;
	friend struct HeadlessRunner;
	friend struct MatchVerifier;
	friend struct TurnClient;
	std::string headlessOutput;
	std::string initializationDiagnostic;
	int headlessSaveInterval = 0;
	int previousCustomSpeed = -1;
	friend class HighResolutionIntegrationHarness;
public:
	//! Constructor
	Engine();
	//! Destructor. Also finalizes globalContainer->replayWriter (which
	//! initGame allocated), writing the replay file's NullOrder terminator.
	~Engine();

	// Engine uniquely owns its net engine and checksum sidecar (unique_ptr
	// members already make the class non-copyable); deleted explicitly for
	// documentation value.
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	/// Initiates a campaign map. This first loads the MapHeader, and then generates a GameHeader for
	/// the campaign map. It then informs GameGUI that this map is a campaign, and if the player wins
	/// it, the given Campaign should be informed. 
	int initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName);

	/// Initiates a campaign game that isn't part of a campaign. One example is the tutorial, which
	/// is a lone map that runs with campaign semantics
	int initCampaign(const std::string &mapName);

	/// Initialize a custom game from the selected map, players and local team.
	int initCustom(MapHeader& map, GameHeader& players, int localTeam, const std::string& sourceFileName = {});

	/// Initiate a custom game from the provided game, without adjusting settings from the user
	int initCustom(const std::string &gameName);
    GAGCore::CooperativeTask initCustomTask(MapHeader map, GameHeader players, int localTeam, int speed = -1, std::string sourceFileName = {});
    GAGCore::CooperativeTask initCustomTask(std::string filename);
    /// Same as initCustomTask, but for a game already generated in this process: the bytes
    /// (produced by Game::save, kept in memory) are read directly, without a round trip
    /// through a temporary file. Keeps `bytes` alive for the whole coroutine.
    GAGCore::CooperativeTask initCustomFromBytesTask(MapHeader map, GameHeader players, int localTeam, int speed, std::shared_ptr<std::string> bytes);
    GAGCore::CooperativeTask initCampaignTask(std::string filename, Campaign* campaign = nullptr, std::string mission = {});
    GAGCore::CooperativeTask loadReplayTask(std::string filename);
    void cancelInitialization();
    const std::string& getInitializationDiagnostic() const { return initializationDiagnostic; }
    void suspendInput() { gui.suspendInput(); }
    void viewportResized(int oldWidth, int oldHeight, int width, int height) { gui.viewportResized(oldWidth, oldHeight, width, height); }



	/// Initiate a game with the given MultiplayerGame
	int initMultiplayer(std::shared_ptr<MultiplayerGame> multiplayerGame, std::shared_ptr<YOGClient> client, int localPlayer);
	GAGCore::CooperativeTask initMultiplayerTask(std::shared_ptr<MultiplayerGame> multiplayerGame, std::shared_ptr<YOGClient> client, int localPlayer);

	/// Everything a client needs to start a game on the turn protocol (online and LAN):
	/// the validated setup, the map file whose content hash matches setup.map.hash
	/// (Online::resolveMatchMap), the seat this client plays, and an open or opening
	/// transport to the relay. The caller keeps its own reference to the transport and
	/// closes it after the engine has finished with the session, so frames queued by
	/// quit() can still be delivered.
	struct TurnMatchStart
	{
		Online::MatchSetup setup;
		std::string mapFile;
		/// The seat this client controls. -1 for an observer that sends nothing (the
		/// verifier); it views the first human seat, or seat 0.
		int localSeat = -1;
		std::shared_ptr<Turn::TurnTransport> transport;
		Turn::TurnSessionConfig config;
		/// Classifies the connection in the ClientNetworkSummary: "online" or "lan",
		/// and for online games the relay's id and region when known.
		std::string networkKind = "online";
		std::string relayId;
		std::string relayRegion;
	};

	/// Starts a turn-protocol game: builds the GameHeader from the setup (every human
	/// seat P_IP), loads the map, and installs a TurnSession as the lockstep session.
	/// Then run() (or beginSession/stepSession) plays it: the loop pumps the session,
	/// paces ticks by TurnSession::tickIntervalMicros(), fast-forwards while it is 0,
	/// reloads the initial state when the session asks for it, and calls quit() when
	/// the game is left. Fails, with getInitializationDiagnostic(), if the setup does not
	/// fit the map.
	GAGCore::CooperativeTask initTurnMatchTask(TurnMatchStart start);
	int initTurnMatch(TurnMatchStart start);
	/// The running turn session, or null for every other kind of game.
	Turn::TurnSession* turnSession();
	Turn::TurnLockstepSession* turnLockstep() { return turn; }
	/// The in-game connection lines for a turn game (GameGUI::connectionNotice).
	std::vector<std::string> turnConnectionNotice();
	/// Online matches: what the results screen shows (outcome, verification, rating).
	void setOnlineResult(std::shared_ptr<Online::OnlineMatchResult> result) { onlineResult = std::move(result); }
	const std::shared_ptr<Online::OnlineMatchResult>& getOnlineResult() const { return onlineResult; }
	/// A team of the loaded game, or null.
	Team* gameTeam(int team);
	/// True while the game loop waits for the network rather than pacing: a turn
	/// game with its horizon used up, or a legacy game waiting on a peer's orders.
	/// Hosts time the following wait as pacing.network_sleep instead of pacing.sleep.
	bool waitingOnNetwork() const;
	/// The ClientNetworkSummary (docs/development/network-telemetry.md) of the running
	/// turn game; null when this is not a turn game with a local seat.
	nlohmann::json turnNetworkSummary(bool includeSeries = true) const;

	//! This function creates a game with a random map and random AI for every team
	void createRandomGame();

	/// Load a replay. Commits the global "we are replaying" state
	/// (globalContainer->replaying, replayFileName, replayReader) only after
	/// the replay file has been successfully parsed; on any failure the
	/// global replay state is cleared so the next game starts as a normal
	/// game. Returns EE_NO_ERROR or EE_CANT_LOAD_MAP.
	int loadReplay(const std::string &fileName);
	
	///Tells whether a map matching mapHeader is located on this system
	bool haveMap(const MapHeader& mapHeader);

	//! Run game. A valid gui and netGame must exists
	int run();
    void prepareRun();
    std::unique_ptr<GAGGUI::Screen> endRunScreen();
    void restoreCursor();

    // Incremental session API. Requires an initialized game; the host owns
    // scheduling. GUI input and modal flows remain transitional legacy code.
    void beginSession(Uint64 now);
    bool stepSession(Uint64 now);
    bool stepSession(Uint64 now, const std::vector<SDL_Event>& events);
    // Stop a failed session before control returns to its host.
    void abortSession() noexcept;
    void drawSession();
    Uint32 sessionDelay(Uint64 now);
    /// Turn games: reads the relay connection between steps (see TURN_POLL_MS), so
    /// bundles are timed when they arrive rather than at the next frame. Nothing runs.
    void pollTurnSession(Uint64 now);
    /// The host's sleep before the next stepSession or pollTurnSession.
    Uint32 sessionPollDelay(Uint64 now);
    struct PendingLoad { std::string filename; bool replay; };
    // Finalize without loading another game or entering a UI loop. The host
    // schedules a returned request, or presents the end screen when absent.
    std::optional<PendingLoad> finishSessionForHost();
    // Synchronous adapter for native command-line/headless hosts.
    bool finishSession();


	//! Type of error the engine init function can return
	enum EngineError
	{
		//! success
		EE_NO_ERROR=1,
		//! user canceled init
		EE_CANCEL=2,
		//! can't load a valid map
		EE_CANT_LOAD_MAP=3,
		//! no suitable player found in the map
		EE_CANT_FIND_PLAYER=4
	};

	///This will load the map header of the game with the given filename
	static MapHeader loadMapHeader(const std::string &filename);

	///This will load the game header of the game with the given filename
	static GameHeader loadGameHeader(const std::string &filename);

	/// Copies the player's Settings > Experiments into the header of a new game.
	/// A saved game keeps the set it was started with, so this leaves one alone.
	/// The one place the new-game rule lives: the custom lobby, map files, headless
	/// test games and hosted multiplayer call it; campaign missions and joined
	/// multiplayer games deliberately do not (docs/features/experimental-features.md).
	static void applyLocalExperiments(GameHeader &header, const MapHeader &map);
	
private:
    bool stepSessionImpl(Uint64 now, const std::vector<SDL_Event>& events);
	/// Initiates a game, provided the map and game header. This initiates the net
	/// as well. When setGameHeader is true, the gameHeader given will replace the
	/// one loaded with the map. When ignore GUI info is set, the game will ignore
	/// GameGUI data in the file, such as viewport position and localTeam. This is
	/// needed for when your loading a save game over the internet
	int initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader=true, bool ignoreGUIData=false, bool saveAI=false, const std::string& sourceFileName=std::string());
	GAGCore::CooperativeTask initGameTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader=true, bool ignoreGUIData=false, bool saveAI=false, std::string sourceFileName=std::string());
	/// The net engine/replay/checksum/dataset setup shared by every initGameTask variant,
	/// once gui.game holds a fully loaded game, regardless of where it was loaded from.
	void finishGameInit();

	/// Reset globalContainer's replay state (replaying flag, replay file name,
	/// replay reader) so the next game session starts as a normal game.
	/// Called on every loadReplay failure path — including when the caller
	/// (e.g. the -replay command line path) set `replaying` before calling.
	void clearReplayState();

	/// Prepares a GameHeader for the given mapHeader as a campaign map
	/// Campaign maps have one player per team, and the player can be
	/// either a human or an AI. AI's are all AINull. When the human
	/// is found, the player number is put in localPlayer, and the
	/// team number is put in localTeam
	GameHeader prepareCampaign(MapHeader& mapHeader, int& localPlayer, int& localTeam);

	//! Load a game. Return true on success
	bool loadGame(const std::string &filename);
	//! Do the final adjustments, like setting local teams and viewport, rendering minimap
	void finalAdjustments(void);
	void showMapLoadError();
	void saveInitialGameStateOrExit(const std::string& path, const std::string& label, const std::string& mapName);

	/// Choose a random map from the available maps. Returns std::nullopt
	/// if maps/ is empty or unreadable (caller must surface this as a
	/// fatal config error). Throws std::ios_base::failure if a randomly
	/// selected .map file is malformed (caller's retry loop picks again).
	/// See definition in EngineLoaders.cpp for the full behavior contract.
	std::optional<MapHeader> chooseRandomMap();
	
	///This function prepares a random set of AI's in a GameHeader, first player is always human + ai team
	GameHeader createRandomGame(int numberOfTeams);

	/// Body of the outer "play one game and possibly load another" loop in run().
	/// Sets doRunOnceAgain=true to loop again (e.g. user picked a new save), false to return.
	void runOneGameSession(bool& doRunOnceAgain);

	// runOneGameSession phases

	struct MainLoopState
	{
		int speed;
		int nextGuiStep;      ///< Fast-forward draw countdown
		Sint64 needToBeTime;  ///< Expected elapsed time for pacing, in ms
		Uint64 startTime;
		unsigned frameNumber;
		bool wasReadyLastTick;
		bool adjustableGameSpeed; ///< Speed presets apply; live network games stay at GAME_TICK_MS
	};

	void updateTickSpeedAndDrawCadence(MainLoopState& st, Uint64 now);

	/// Headless / scripted-test polling: under --nox automaticEndingGame, flip
	/// gui.isRunning=false once a local end condition fires. Records
	/// automaticGameEndTick.
	void pollAutomaticEndingConditions(Uint64 now);

	/// Push this tick's local + AI orders into the net layer and (if the
	/// previous tick committed) call advanceStep + write the checksum sidecar.
	/// Called only from inside the !hardPause branch.
	void gatherAndAdvanceOrders(bool wasReadyLastTick);

	/// Once tickReady() is true for this tick, validate checksums,
	/// execute the matched orders, pump the replay reader, and run
	/// game.syncStep. Called only from inside the !hardPause branch.
	void executeOrdersAndStep(bool readyNow);

	void drawFrame(MainLoopState& st);

	/// Turn games: pumps the session each frame and handles its requests (reload,
	/// desync flag). Called first in stepSessionImpl.
	void pumpTurnSession(Uint64 now);
	/// What the connection HUD shows (ConnectionOverlay), from the turn session.
	ConnectionSnapshot turnConnectionSnapshot();
	Uint64 turnNowMicros = 0;
	std::uint32_t catchupFrom = 0;
	bool catchupActive = false;
	Uint64 connectionLostMicros = 0;
	Uint64 catchupStartedMicros = 0;
	/// Reloads the turn game's initial state in place, keeping the session, after
	/// TurnSession::needsReload(); the session then replays the log from tick 0.
	void reloadTurnInitialState();
	/// Tells the relay this client is leaving, once.
	void leaveTurnMatch();
	/// Network telemetry output (EngineTurnTelemetry.cpp): GLOB2_NET_* records with
	/// GLOB2_TEAM_TIMELINE, and the ClientNetworkSummary next to the replay.
	void printTurnTelemetrySession();
	void printTurnTelemetrySamples();
	void exportTurnTelemetry();
    std::optional<MainLoopState> session;
    /// A turn game draws only after a step: polls between steps change nothing visible.
    bool turnDrawPending = true;
    int sessionEndingTarget = 0;
    std::vector<SDL_Event> sessionInput;

	/// If the GUI requested a clean exit, drain remaining local orders and
	/// flush the net layer. Returns true if the engine loop should break.
	bool handleExitRequest();

	/// Print the headless end-of-game summary plus the GLOB2_GAME_END
	/// key=value line that the AI-trainer pipeline scrapes. Caller checks
	/// automaticEndingGame.
	void printAutomaticEndingSummary();

	/// Dump each team's 512-tick economic/military timeline plus a final
	/// detailed snapshot. Gated by GLOB2_TEAM_TIMELINE; used to compare two
	/// AIs' trajectories after a single headless game.
	void printTeamTimeline();

	/// Remember the tick at which each team was eliminated (Team::isAlive cleared). Called from
	/// pollAutomaticEndingConditions under automaticEndingGame; only reads game state.
	void trackTeamEliminations();

	/// One GLOB2_TEAM_RESULT line per team: outcome, elimination tick, start position and final
	/// prestige and forces. Gated by GLOB2_TEAM_RESULTS; tools/map_fairness_tournament.py scrapes it.
	void printTeamResults();

	/// Tell the YOG multiplayer session how this match ended (won, lost,
	/// quit). Caller checks `multiplayer` is non-null.
	void reportMultiplayerResult();

	/// Close cross-replay sinks (sidecar, dataset) and tear down the network
	/// + multiplayer state. The Engine itself stays alive for a possible
	/// reload (see finishSessionForHost).
	void teardownSession();

	//! The GUI, contains the whole game also
	GameGUI gui;
	//! The lockstep session: queues, exchanges and dispatches orders. A
	//! NetEngine for single player, replays and legacy YOG/LAN games.
	std::unique_ptr<LockstepSession> net;
	//! Checksum sidecar writer for cross-replay debugging. Destroying it
	//! closes the sidecar file (see ~ChecksumSidecarWriter), so the file is
	//! flushed even when run() is never reached after initGame allocated it.
	std::unique_ptr<ChecksumSidecarWriter> checksumSidecar;
	//! The MultiplayerGame, receives orders from across a network
	shared_ptr<MultiplayerGame> multiplayer;
	//! Non-owning view of `net` when it is a turn-protocol session; null otherwise.
	Turn::TurnLockstepSession* turn = nullptr;
	//! What a turn game reloads after TurnSession::needsReload().
	struct TurnMatchState
	{
		MapHeader map;
		GameHeader header;
		std::string mapFile;
		int localPlayer = 0;
		int localTeam = 0;
		std::string replayPath;
		bool flagReported = false;
		// Network telemetry context and output progress.
		int localSeat = -1;
		std::string simVersion;
		std::string networkKind;
		std::string relayId;
		std::string relayRegion;
		std::size_t printedNetPoints = 0;
		bool netExported = false;
	};
	std::optional<TurnMatchState> turnMatch;
	std::shared_ptr<Online::OnlineMatchResult> onlineResult;

	Uint64 automaticGameStartTick, automaticGameEndTick;
	//! Tick at which each team was eliminated, -1 while alive (see trackTeamEliminations).
	std::vector<Sint32> teamEliminatedTick;

	static const bool verbose = false;
};
