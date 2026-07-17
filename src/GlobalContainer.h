// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <memory>
#include <vector>

#include "BuildingType.h"
#include "RessourceType.h"
#include "Settings.h"

namespace GAGCore
{
	class FileManager;
	class GraphicContext;
	class Sprite;
	class DrawableSurface;
	class Font;
}
using namespace GAGCore;

class SoundMixer;
class VoiceRecorder;
class LogFileManager;
class ReplayReader;
class ReplayWriter;
class DatasetWriter;

class GlobalContainer
{
public:
	enum { USERNAME_MAX_LENGTH=32 };
	enum { OPTION_LOW_SPEED_GFX=0x1 };
	enum { OPTION_MAP_EDIT_USE_USL=0x2 };

#ifndef YOG_SERVER_ONLY
private:
	void updateLoadProgressScreen(int value);
#endif  // !YOG_SERVER_ONLY

public:
	GlobalContainer(void);
	virtual ~GlobalContainer(void);

	void parseArgs(int argc, char *argv[]);
#ifndef YOG_SERVER_ONLY
	void loadClient(void);
#endif  // !YOG_SERVER_ONLY
	void load(void);

public:
	FileManager *fileManager; //!< Borrowed from Toolkit; not owned by GlobalContainer.
	std::unique_ptr<LogFileManager> logFileManager; //!< Owned.

#ifndef YOG_SERVER_ONLY
	GraphicContext *gfx; //!< Borrowed from Toolkit; not owned by GlobalContainer.
	std::unique_ptr<SoundMixer> mix; //!< Owned.
	std::unique_ptr<VoiceRecorder> voiceRecorder; //!< Owned.

	std::unique_ptr<DrawableSurface> title; //!< Owned.
	
	Sprite *terrain;
	Sprite *terrainWater;
	Sprite *terrainCloud;
	Sprite *terrainBlack;
	Sprite *terrainShader;
	Sprite *ressources;
	Sprite *ressourceMini;
	Sprite *areaClearing;
	Sprite *areaForbidden;
	Sprite *areaGuard;
	Sprite *bullet;
	Sprite *bulletExplosion;
	Sprite *deathAnimation;
	Sprite *units;
	Sprite *unitmini;
	Sprite *gamegui;
	Sprite *brush;
	Sprite *magiceffect;
	Sprite *particles;

	Font *menuFont;
	Font *standardFont;
	Font *littleFont;
#endif  // !YOG_SERVER_ONLY
	Settings settings;

#ifndef YOG_SERVER_ONLY
	BuildingsTypes buildingsTypes;
#endif  // !YOG_SERVER_ONLY
	RessourcesTypes ressourcesTypes;

	std::string videoshotName; //!< the name of videoshot to record. If empty, do not record videoshot
	bool runNoX;
	std::string runNoXGameName;
	int runNoXCountRuns; //!< The number of runs you want to repeat the no X run
	bool automaticEndingGame;
	int automaticEndingSteps;
	bool automaticGameGlobalEndConditions; //! Set false if the automatic game will end if the local team wins/loses, true to wait for the entire game to finish
	
	bool runTestGames; //! runs test games
	int runTestGamesCount; //! number of test games to run (0 = infinite)
	//! AI implementation IDs (AI::ImplementitionID values) eligible for random
	//! AI assignment in createRandomGame. Empty means "all AIs allowed" (legacy
	//! behavior: NUMBI..NICOWAR uniformly). Set via --ai-types.
	//! Mutually exclusive with testGamesMatchup.
	std::vector<int> testGamesAIPool;

	//! Bare map name (no .map extension, no path) to pin createRandomGame to.
	//! Empty means "pick a random map from maps/" (legacy). Set via --map.
	std::string testGamesMap;

	//! Per-team AI implementation IDs for createRandomGame. testGamesMatchup[k]
	//! is the AI assigned to team k. Empty means "use testGamesAIPool or random
	//! default" (legacy). Set via --matchup. Validated against the loaded map's
	//! getNumberOfTeams() at game creation time. Requires testGamesMap to be
	//! set (else we'd have no team count to validate against).
	std::vector<int> testGamesMatchup;

	//! Path for --save-game-as: write the fully-initialised tick-0 game state
	//! to this .game file before running, so the same scenario can later be
	//! replayed deterministically via --nox. Empty means "do not save". Only
	//! the -test-games / -test-games-nox flow honors this (the save happens
	//! inside createRandomGame). Pair with GLOB2_TEST_SEED for full
	//! reproducibility — the seed mirrored into GameHeader::seed is the
	//! one captured in testGamesSeed below.
	std::string testGamesSaveGameAs;

	//! Seed actually passed to setSyncRandSeed() at the top of runTestGames().
	//! createRandomGame() mirrors this into GameHeader::seed so the saved
	//! .game file (via --save-game-as or GLOB2_DUMP_GAME) loads with the same
	//! syncRand state. Without this mirror, GameHeader's constructor default
	//! (time(NULL) at header-construction time) wins and the loaded game
	//! diverges from the original -test-games-nox run.
	Uint32 testGamesSeed;
	bool testGamesSeedSet;

	bool runTestMapGeneration; //! runs test map generation
	
	bool hostServer;
	bool hostRouter;
	bool adminRouter;
	//! hostname for YOG, can be set by cmd line to override default
	std::string yogHostName;

	// Variables related to the showing of replays:
	bool replaying; //!< Whether the current game is a replay or a usual game
	std::string replayFileName; //!< The name of the replay file.
	bool replayFastForward; //!< If set to true, the replay will play faster.
	bool replayShowFog; //!< Draw the fog of war or draw the entire map. Can be edited real-time.
	Uint32 replayVisibleTeams; //!< A mask of which teams can be seen in the replay. Can be edited real-time.
	bool replayShowAreas; //!< Show areas of gui.localPlayer or not. Can be edited real-time.
	bool replayShowFlags; //!< Show all flags or show none. Can be edited real-time.

#ifndef YOG_SERVER_ONLY
	std::unique_ptr<ReplayReader> replayReader; //!< Owned. Reads and processes replay files, and outputs orders.
	std::unique_ptr<ReplayWriter> replayWriter; //!< Owned. Writes orders into replay files.
	std::unique_ptr<DatasetWriter> datasetWriter; //!< Owned. Writes (state, action) records for AI training (GLOB2_DATASET_PATH).
#endif  // !YOG_SERVER_ONLY

};

extern GlobalContainer *globalContainer;

