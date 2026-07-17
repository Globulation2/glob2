// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière


#include <Toolkit.h>
#include <GAG.h>
#include <GUIBase.h>

#include "FileManager.h"
#include "GameGUIKeyActions.h"
#include "Glob2Style.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "KeyboardManager.h"
#include "LogFileManager.h"
#include "MapEditKeyActions.h"
#include "Race.h"
#include "SoundMixer.h"
#include "render/UnitSkin.h"
#include "VoiceRecorder.h"
#ifndef YOG_SERVER_ONLY
#include "DatasetWriter.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#endif  // !YOG_SERVER_ONLY

#include "YOGConsts.h"


/**
 * The GlobalContainer basically holds all preferences, data,
 * configuration information, etc.
 */
GlobalContainer::GlobalContainer(void)
{
	// Init toolkit
	Toolkit::init("glob2");

	// init virtual filesystem
	fileManager = Toolkit::getFileManager();
	assert(fileManager);
	fileManager->addWriteSubdir("maps");
	fileManager->addWriteSubdir("games");
	fileManager->addWriteSubdir("campaigns");
	fileManager->addWriteSubdir("replays");
	fileManager->addWriteSubdir("thumbnails");
	fileManager->addWriteSubdir(YOG_SERVER_FOLDER);
	fileManager->addWriteSubdir(YOG_SERVER_FOLDER+"gamelog");
	fileManager->addWriteSubdir("logs");
	fileManager->addWriteSubdir("scripts");
	fileManager->addWriteSubdir("videoshots");
	logFileManager = std::make_unique<LogFileManager>(fileManager);

	// load user preference
	settings.load();

#ifndef YOG_SERVER_ONLY
	runNoX = false;
	hostServer = false;
#else
	runNoX = true;
	hostServer = true;
#endif  // !YOG_SERVER_ONLY

	hostRouter = false;
	adminRouter = false;
	
	runTestGames=false;
	runTestGamesCount=0;
	testGamesAIPool.clear();
	testGamesMap.clear();
	testGamesMatchup.clear();
	testGamesSaveGameAs.clear();
	testGamesSeed=0;
	testGamesSeedSet=false;
	runTestMapGeneration=false;
	automaticEndingGame=false;
	automaticEndingSteps=-1;

#ifndef YOG_SERVER_ONLY
	gfx = NULL;

	terrain = NULL;
	terrainShader = NULL;
	terrainBlack = NULL;
	ressources = NULL;
	units = NULL;

	menuFont = NULL;
	standardFont = NULL;
	littleFont = NULL;
#endif  // !YOG_SERVER_ONLY

	automaticGameGlobalEndConditions=false;

	replaying = false;
	replayFileName = "";
	replayFastForward = false;
	replayShowFog = true;
	replayVisibleTeams = 0xFFFFFFFF;
	replayShowAreas = false;
	replayShowFlags = true;

	assert((int)USERNAME_MAX_LENGTH==(int)BasePlayer::MAX_NAME_LENGTH);
}

GlobalContainer::~GlobalContainer(void)
{
#ifndef YOG_SERVER_ONLY
	// unlink GUI style
	if (!runNoX)
		delete Style::style;
	Style::style = &defaultStyle;

	// Release sound and the title surface before Toolkit::close() pulls the
	// underlying graphics/audio backends out from under them.
	mix.reset();
	voiceRecorder.reset();
	title.reset();
#endif  // !YOG_SERVER_ONLY

	// release resources
	Toolkit::close();

	// Remaining owned members (logFileManager, replayReader, replayWriter,
	// datasetWriter) are destroyed by the implicit member destruction that
	// runs after this body — no manual cleanup needed.
}

// parseArgs is defined in GlobalContainerArgs.cpp.

#ifndef YOG_SERVER_ONLY
void GlobalContainer::updateLoadProgressScreen(int value)
{
	unsigned randomSeed = 1;
	unsigned columnCount = gfx->getW() / 32;
	unsigned limit = (value * columnCount) / 100;
	for (int y = 0; y < gfx->getH(); y += 32)
		for (int x = 0; x < gfx->getW(); x += 32)
		{
			randomSeed = randomSeed * 69069;
			unsigned index;
			if (x/32 < (int)limit)
				index = ((randomSeed >> 16) & 0xF);
			else if (x/32 == (int)limit)
				index = ((randomSeed >> 16) & 0x7) + 64;
			else
				index = ((randomSeed >> 16) & 0xF) + 128;
			gfx->drawSprite(x, y, terrain, index);
		}
	gfx->finishDrawingSprite(terrain, 255);
	//gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), Color::black);
	gfx->drawSurface((gfx->getW()-title->getW())>>1, (gfx->getH()-title->getH())>>1, title.get());
	//gfx->drawFilledRect(((gfx->getW()-400)>>1), (gfx->getH()>>1)+11+180, (value)<<2, 20, 10, 50, 255, 80);
	gfx->nextFrame();
}

// glob2-client specific actions here.
void GlobalContainer::loadClient(void)
{
	if (!runNoX)
	{
		// create graphic context
		gfx = Toolkit::initGraphic(settings.screenWidth, settings.screenHeight, settings.screenFlags, "Globulation 2", "glob 2");
		gfx->setMinRes(640, 480);
		//gfx->setQuality((settings.optionFlags & OPTION_LOW_SPEED_GFX) != 0 ? GraphicContext::LOW_QUALITY : GraphicContext::HIGH_QUALITY);
		
		// load data required for drawing progress screen
		title = std::make_unique<DrawableSurface>("data/gfx/title.png");
		terrain = Toolkit::getSprite("data/gfx/terrain");
		updateLoadProgressScreen(0);

		// create mixer
		mix = std::make_unique<SoundMixer>(settings.musicVolume, settings.voiceVolume, settings.mute);
		// Track slots must match the MusicTrack enum order. Engine::run may
		// later overwrite the InGame* slots with a randomly chosen music dir.
		mix->loadTrack("data/zik/intro.ogg",            MusicTrack::Intro);
		mix->loadTrack("data/zik/menu.ogg",             MusicTrack::Menu);
		mix->loadTrack("data/zik/original/a1.ogg",      MusicTrack::InGameDefault);
		mix->loadTrack("data/zik/original/a2.ogg",      MusicTrack::BuildingEvent);
		mix->loadTrack("data/zik/original/a3.ogg",      MusicTrack::WarEvent);
		mix->setNextTrack(MusicTrack::Intro);
		mix->setNextTrack(MusicTrack::Menu);
		
		// create voice recorder
		voiceRecorder = std::make_unique<VoiceRecorder>();
		
		updateLoadProgressScreen(15);
	}
	
	// initialize building types: resolve sprite pointers and prev/next-level
	// links for the static table baked into game/entities/buildings*.cpp.
	buildingsTypes.init();
	IntBuildingType::init();
	
	if (!runNoX)
	{
		updateLoadProgressScreen(35);
	}

	// initiate keyboard actions
	GameGUIKeyActions::init();
	MapEditKeyActions::init();

	if (settings.version < 1)
	{
		KeyboardManager game(GameGUIShortcuts);
		game.loadDefaultShortcuts();
		game.saveKeyboardLayout();

		KeyboardManager edit(MapEditShortcuts);
		edit.loadDefaultShortcuts();
		edit.saveKeyboardLayout();
	}

	if (!runNoX)
	{
		updateLoadProgressScreen(40);
		
		// load fonts
		std::string fontfile = "data/fonts/";
		fontfile+=+PRIMARY_FONT;
		Toolkit::loadFont(fontfile.c_str(), 20, "menu");
		Toolkit::loadFont(fontfile.c_str(), 13, "standard");
		Toolkit::loadFont(fontfile.c_str(), 10, "little");
		menuFont = Toolkit::getFont("menu");
		menuFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		standardFont = Toolkit::getFont("standard");
		standardFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		littleFont = Toolkit::getFont("little");
		littleFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));

		updateLoadProgressScreen(50);
		// load terrain data
		//terrain = Toolkit::getSprite("data/gfx/terrain"); // terrain is already loaded as it is required to display progress screen
		terrainWater = Toolkit::getSprite("data/gfx/water");
		terrainCloud = Toolkit::getSprite("data/gfx/cloud");
		
		// black for unexplored terrain
		terrainBlack = Toolkit::getSprite("data/gfx/black");

		// load shader for invisible terrain
		terrainShader = Toolkit::getSprite("data/gfx/shade");
		
		updateLoadProgressScreen(60);
		// load resources
		ressources = Toolkit::getSprite("data/gfx/ressource");
		ressourceMini = Toolkit::getSprite("data/gfx/ressourcemini");
		areaClearing = Toolkit::getSprite("data/gfx/area-clearing");
		areaForbidden = Toolkit::getSprite("data/gfx/area-forbidden");
		areaGuard = Toolkit::getSprite("data/gfx/area-guard");
		bullet = Toolkit::getSprite("data/gfx/bullet");
		bulletExplosion = Toolkit::getSprite("data/gfx/explosion");
		deathAnimation = Toolkit::getSprite("data/gfx/death"); 

		updateLoadProgressScreen(70);
		// load units
		units = Toolkit::getSprite("data/gfx/unit");
		initUnitSkins();

		updateLoadProgressScreen(90);
		// load graphics for gui
		unitmini = Toolkit::getSprite("data/gfx/unitmini");
		gamegui = Toolkit::getSprite("data/gfx/gamegui");
		brush = Toolkit::getSprite("data/gfx/brush");
		magiceffect = Toolkit::getSprite("data/gfx/magiceffect");
		particles = Toolkit::getSprite("data/gfx/particle");
		
		// use custom style
		Style::style = new Glob2Style;

		updateLoadProgressScreen(100);
	}
}
#endif  // !YOG_SERVER_ONLY

void GlobalContainer::load(void)
{
	// load texts
	if (!Toolkit::getStringTable()->load("data/texts.list.txt"))
	{
		std::cerr << "Fatal error : while loading \"data/texts.list.txt\"" << std::endl;
		assert(false);
		exit(-1);
	}
	// load texts
	if (!Toolkit::getStringTable()->loadIncompleteList("data/texts.incomplete.txt"))
	{
		std::cerr << "Fatal error : while loading \"data/texts.incomplete.txt\"" << std::endl;
		assert(false);
		exit(-1);
	}
	
	Toolkit::getStringTable()->setLang(Toolkit::getStringTable()->getLangCode(settings.language));
	// load default unit types
	Race::loadDefault();
	// Resource types are now a compile-time const table (see
	// src/game/entities/resources.cpp); nothing to load here.

#ifndef YOG_SERVER_ONLY
	loadClient();
#endif  // !YOG_SERVER_ONLY
}
