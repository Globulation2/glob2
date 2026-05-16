// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Header.h"
#include "GameGUI.h"
#include <optional>
#include <string>
#include "Campaign.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "NetEngine.h"
#include "MultiplayerGame.h"
#include "CPUStatisticsManager.h"
#include "ChecksumSidecar.h"


class MultiplayersJoin;
class NetGame;

using std::shared_ptr;

/// Engine is the backend of the game. It is responsible for loading and setting up games and players,
/// and its run function is meant to run the game that has been loaded.
class Engine
{
public:
	//! Constructor
	Engine();
	//! Destructor
	~Engine();

	/// Initiates a campaign map. This first loads the MapHeader, and then generates a GameHeader for
	/// the campaign map. It then informs GameGUI that this map is a campaign, and if the player wins
	/// it, the given Campaign should be informed. 
	int initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName);

	/// Initiates a campaign game that isn't part of a campaign. One example is the tutorial, which
	/// is a lone map that runs with campaign semantics
	int initCampaign(const std::string &mapName);

	/// Displays the CustomMap dialogue, and initiates a game from the settings it recieves
	int initCustom();

	/// Initiate a custom game from the provided game, without adjusting settings from the user
	int initCustom(const std::string &gameName);

	/// Show the load/save dialoge, and use initCustom(gameName) to load the game
	int initLoadGame();

	/// Initiate a game with the given MultiplayerGame
	int initMultiplayer(std::shared_ptr<MultiplayerGame> multiplayerGame, std::shared_ptr<YOGClient> client, int localPlayer);

	//! This function creates a game with a random map and random AI for every team
	void createRandomGame();

	/// Load a replay
	int loadReplay(const std::string &fileName);
	
	///Tells whether a map matching mapHeader is located on this system
	bool haveMap(const MapHeader& mapHeader);

	//! Run game. A valid gui and netGame must exists
	int run();

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
	
private:
	/// Initiates a game, provided the map and game header. This initiates the net
	/// as well. When setGameHeader is true, the gameHeader given will replace the
	/// one loaded with the map. When ignore GUI info is set, the game will ignore
	/// GameGUI data in the file, such as viewport position and localTeam. This is
	/// needed for when your loading a save game over the internet
	int initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader=true, bool ignoreGUIData=false, bool saveAI=false);

	/// Prepares a GameHeader for the given mapHeader as a campaign map
	/// Campaign maps have one player per team, and the player can be
	/// either a human or an AI. AI's are all AINull. When the human
	/// is found, the player number is put in localPlayer, and the
	/// team number is put in localTeam
	GameHeader prepareCampaign(MapHeader& mapHeader, int& localPlayer, int& localTeam);

	//! Load a game. Return true on success
	bool loadGame(const std::string &filename);
	//! Do the final adjustements, like setting local teams and viewport, rendering minimap
	void finalAdjustements(void);

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

	// --- runOneGameSession phase helpers ---
	//
	// The single 380-line body was decomposed into the helpers below. Each
	// helper is one phase of the main loop or the post-loop teardown. They
	// must be called in the order they appear here; the comments at each
	// definition site name preconditions and which caller state each one
	// mutates. See EngineRun.cpp.

	/// Choose this tick's sim interval (GAME_TICK_MS / REPLAY_FAST_FORWARD_MS)
	/// and the GUI-draw cadence. Caller has already decremented nextGuiStep.
	void selectReplaySpeed(int& speed, int& nextGuiStep);

	/// Headless / scripted-test polling: under --nox automaticEndingGame, flip
	/// gui.isRunning=false once a local end condition fires. Records
	/// automaticGameEndTick.
	void pollAutomaticEndingConditions();

	/// Push this tick's local + AI orders into the net layer and (if the
	/// previous tick committed) call advanceStep + write the checksum sidecar.
	/// Called only from inside the !hardPause branch.
	void gatherAndAdvanceOrders(bool wasReadyLastTick);

	/// Once allOrdersRecieved() is true for this tick, validate checksums,
	/// execute the matched orders, pump the replay reader, and run
	/// game.syncStep. Called only from inside the !hardPause branch.
	void executeOrdersAndStep(bool readyNow);

	/// Draw the frame (subject to the fast-forward cadence), save a videoshot
	/// if requested, then SDL_Delay to maintain wall-clock pacing and feed
	/// cpuStats. Updates needToBeTime + frameNumber across iterations.
	void frameTimingAndDraw(int speed, int nextGuiStep, Sint64& needToBeTime,
	                        unsigned& frameNumber, Uint64 startTime, bool readyNow);

	/// If the GUI requested a clean exit, drain remaining local orders and
	/// flush the net layer. Returns true if the engine loop should break.
	bool flushOutgoingAndExit();

	/// Print the headless end-of-game summary plus the GLOB2_GAME_END
	/// key=value line that the AI-trainer pipeline scrapes. Caller checks
	/// automaticEndingGame.
	void printAutomaticEndingSummary();

	/// Tell the YOG multiplayer session how this match ended (won, lost,
	/// quit). Caller checks `multiplayer` is non-null.
	void reportMultiplayerResult();

	/// Close cross-replay sinks (sidecar, dataset) and tear down the network
	/// + multiplayer state. The Engine itself stays alive for a possible
	/// reload (see armReloadOrExit).
	void teardownSession();

	/// Decide whether run() should loop back into runOneGameSession (a
	/// load-game request was armed in the GUI) or return to the menu. Always
	/// clears toLoadGameFileName so a follow-up pass doesn't re-trigger it.
	void armReloadOrExit(bool& doRunOnceAgain);

	//! The GUI, contains the whole game also
	GameGUI gui;
	//! The netGame, take care of order queuing and dispatching
	NetEngine *net;
	//! Checksum sidecar writer for cross-replay debugging
	ChecksumSidecarWriter *checksumSidecar;
	//! The MultiplayerGame, recieves orders from across a network
	shared_ptr<MultiplayerGame> multiplayer;

	CPUStatisticsManager cpuStats;

	Uint64 automaticGameStartTick, automaticGameEndTick;

	FILE *logFile;

	static const bool verbose = false;
};

