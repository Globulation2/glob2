/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GLOBALCONTAINER_H
#define __GLOBALCONTAINER_H

#include "BuildingsTypes.h"
#include "Header.h"
#include "RessourcesTypes.h"
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
class UnitsSkins;
class ReplayReader;
class ReplayWriter;

class GlobalContainer
{
public:
	enum { USERNAME_MAX_LENGTH=32 };
	enum { OPTION_LOW_SPEED_GFX=0x1 };
	enum { OPTION_MAP_EDIT_USE_USL=0x2 };

private:
	void updateLoadProgressScreen(int value);

public:
	GlobalContainer(void);
	virtual ~GlobalContainer(void);

	void parseArgs(int argc, char *argv[]);
	void load(void);

	//void setUsername(const std::string &name);
	//const std::string &getUsername(void) { return settings.getUsername(); }
	const char *getComputerHostName(void);

public:
	FileManager *fileManager;
	LogFileManager *logFileManager;

	GraphicContext *gfx;
	SoundMixer *mix;
	VoiceRecorder *voiceRecorder;
	
	DrawableSurface *title;
	
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
	
	UnitsSkins *unitsSkins;
	
	Font *menuFont;
	Font *standardFont;
	Font *littleFont;
	
	Settings settings;

	BuildingsTypes buildingsTypes;
	RessourcesTypes ressourcesTypes;

	std::string videoshotName; //!< the name of videoshot to record. If empty, do not record videoshot
	bool runNoX;
	std::string runNoXGameName;
	int runNoXCountRuns; //!< The number of runs you want to repeat the no X run
	bool automaticEndingGame;
	int automaticEndingSteps;
	bool automaticGameGlobalEndConditions; //! Set false if the automatic game will end if the local team wins/loses, true to wait for the entire game to finish
	
	bool runTestGames; //! runs test games
	
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

	ReplayReader *replayReader; //!< Reads and processes replay files, and outputs orders
	ReplayWriter *replayWriter; //!< Writes orders into replay files

public:
	Uint32 getConfigCheckSum();
};

extern GlobalContainer *globalContainer;

#endif

