#ifndef __EMSCRIPTEN__
#include <SDL3_net/SDL_net.h>
#endif
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière

#include <GameplayRecording.h>
#include <Toolkit.h>
#include <GAG.h>
#include <GUIBase.h>
#ifndef __EMSCRIPTEN__
#include <SDL3_net/SDL_net.h>
#endif

#include "FileManager.h"
#include "GameGUIKeyActions.h"
#include "Glob2Style.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "KeyboardManager.h"
#include "MapEditKeyActions.h"
#include "Race.h"
#include "SoundMixer.h"
#include "render/UnitSkin.h"
#include "VoiceRecorder.h"
#include "DatasetWriter.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "ScrollTuning.h"

#include <ApplicationHost.h>


/**
 * The GlobalContainer basically holds all preferences, data,
 * configuration information, etc.
 */
GlobalContainer::GlobalContainer(const char *profileName)
{
	// Init toolkit
	Toolkit::init(profileName);

	// init virtual filesystem
	fileManager = Toolkit::getFileManager();
	assert(fileManager);
	fileManager->addWriteSubdir("maps");
	fileManager->addWriteSubdir("games");
	fileManager->addWriteSubdir("campaigns");
	fileManager->addWriteSubdir("replays");
	fileManager->addWriteSubdir("thumbnails");
	fileManager->addWriteSubdir("logs");
	fileManager->addWriteSubdir("scripts");
	fileManager->addWriteSubdir("videoshots");

#ifdef __EMSCRIPTEN__
	// Start browser profiles quietly and without clouds. Saved preferences win.
	settings.mute = 1;
#endif
	// load user preference
	settings.load();

	applyScrollTuning(settings, reducedMotion);
	runNoX = false;
	
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

	gfx = NULL;

	terrain = NULL;
	terrainShader = NULL;
	terrainBlack = NULL;
	resources = NULL;
	units = NULL;

	menuFont = NULL;
	standardFont = NULL;
	littleFont = NULL;

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
	GAGCore::Recording::recorder().shutdown();
	// unlink GUI style
	if (!runNoX)
		delete Style::style;
	Style::style = &defaultStyle;

	// Release sound and the title surface before Toolkit::close() pulls the
	// underlying graphics/audio backends out from under them.
	mix.reset();
	voiceRecorder.reset();
	title.reset();

	// SDL_net owns resolver threads and conditions. Join them before the
	// graphics backend calls SDL_Quit and destroys SDL thread resources.
#ifndef __EMSCRIPTEN__
	if (networkInitialized)
		NET_Quit();
#endif

	// release resources
#ifndef __EMSCRIPTEN__
	// Join SDL_net's resolver threads before GraphicContext calls SDL_Quit().
	// Waiting until the atexit fallback is too late: SDL has already cleared
	// its thread registry, so NET_Quit cannot join those threads safely.
	NET_Quit();
#endif
	Toolkit::close();

	// Remaining owned members (replayReader, replayWriter, datasetWriter) are destroyed by the implicit member destruction that
	// runs after this body — no manual cleanup needed.
}

// parseArgs is defined in GlobalContainerArgs.cpp.

void GlobalContainer::updateLoadProgressScreen(int value)
{
	// The terrain tiles come with the game sprites, which the browser installs
	// after the main menu. Its canvas is hidden until then anyway.
	if (terrain)
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
	}
	gfx->drawSurface((gfx->getW()-title->getW())>>1, (gfx->getH()-title->getH())>>1, title.get());
	gfx->nextFrame();
}

// glob2-client specific actions here.
void GlobalContainer::loadClient(void)
{
	// Native builds have every data package; the browser installs game sprites
	// before startup and menu music afterward (scons/web_assets.py).
	const bool gameData = GAGCore::ApplicationHost::assetPackageReady("game");
	if (!runNoX)
	{
		// create graphic context
		GraphicContext::setRequestedUiScale(settings.uiScale / 100.0f);
		gfx = Toolkit::initGraphic(settings.screenWidth, settings.screenHeight, settings.screenFlags, "Globulation 2", "glob 2");
#if !defined(GLOB2_MOBILE) && !defined(__EMSCRIPTEN__)
		gfx->setDisplayPreferenceCallback([this](int w, int h, bool fullscreen)
		{
			const bool changed = bool(settings.screenFlags & GraphicContext::FULLSCREEN) != fullscreen;
			settings.screenWidth=w; settings.screenHeight=h;
			if (gfx->getOptionFlags() & GraphicContext::RESIZABLE) settings.screenFlags |= GraphicContext::RESIZABLE;
			if (fullscreen) settings.screenFlags |= GraphicContext::FULLSCREEN;
			else settings.screenFlags &= ~GraphicContext::FULLSCREEN;
			// Resizes are kept in memory and saved by the normal settings/shutdown flow.
			// F11 is a preference change and persists immediately.
			if (changed && !settings.save()) std::cerr << "Could not save display preferences." << std::endl;
		});
#endif
		gfx->setCompactWindowAllowed(true);
        gfx->refreshPresentation();
		gfx->setMinRes(640, 480);
		
		// load data required for drawing progress screen
		title = std::make_unique<DrawableSurface>("data/gfx/loading-wordmark.png");
		if (gameData)
			terrain = Toolkit::getSprite("data/gfx/terrain");
		updateLoadProgressScreen(0);

		// create mixer
		mix = std::make_unique<SoundMixer>(settings.musicVolume, settings.voiceVolume, settings.mute);
		// Track slots must match the MusicTrack enum order. Engine::run may
		// later overwrite the InGame* slots with a randomly chosen music dir.
		loadMenuMusic();
		mix->loadTrack("data/zik/original/a1.ogg",      MusicTrack::InGameDefault);
		mix->loadTrack("data/zik/original/a2.ogg",      MusicTrack::BuildingEvent);
		mix->loadTrack("data/zik/original/a3.ogg",      MusicTrack::WarEvent);
		mix->setNextTrack(MusicTrack::Intro);
		mix->setNextTrack(MusicTrack::Menu);
		
		// create voice recorder
		voiceRecorder = std::make_unique<VoiceRecorder>();
		
		updateLoadProgressScreen(15);
	}
	
	// initialize building types: resolve prev/next-level links for the static
	// table baked into game/entities/buildings*.cpp (sprites come with the
	// game graphics below).
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
		loadGameFonts();
        // Separate frontend aliases avoid changing gameplay/editor font metrics.
        Toolkit::loadFont(fontfile.c_str(), 16, "frontend-body");
        Toolkit::loadFont(fontfile.c_str(), 14, "frontend-support");
		// Frontend title/caption roles used by the declarative menus.
		Toolkit::loadFont(fontfile.c_str(), 30, "front-title");
		Toolkit::loadFont(fontfile.c_str(), 12, "front-caption");
		menuFont = Toolkit::getFont("menu");
		menuFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		standardFont = Toolkit::getFont("standard");
		standardFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));
		littleFont = Toolkit::getFont("little");
		littleFont->setStyle(Font::Style(Font::STYLE_NORMAL, GAGGUI::Style::style->textColor));

		updateLoadProgressScreen(50);
		if (gameData)
			loadGameGraphics(true);
		
		// use custom style
		Style::style = new Glob2Style;

		updateLoadProgressScreen(100);
	}
}

void GlobalContainer::loadGameGraphics(bool showProgress)
{
	const auto sprite = [](const std::string& path) {
		auto* result = Toolkit::getSprite(path);
		if (!result) throw std::runtime_error("Cannot load game sprite: " + path);
		return result;
	};
	// load terrain data
	if (!terrain)
		terrain = sprite("data/gfx/terrain");
	terrainWater = sprite("data/gfx/water");
	terrainCloud = sprite("data/gfx/cloud");
	
	// black for unexplored terrain
	terrainBlack = sprite("data/gfx/black");

	// load shader for invisible terrain
	terrainShader = sprite("data/gfx/shade");
	
	if (showProgress)
		updateLoadProgressScreen(60);
	// load resources
	resources = sprite("data/gfx/ressource");
	resources->createTextureAtlas(true);
	resourceMini = sprite("data/gfx/ressourcemini");
	mapIcons = sprite("data/gfx/mapicon");
	areaClearing = sprite("data/gfx/area-clearing");
	areaForbidden = sprite("data/gfx/area-forbidden");
	areaGuard = sprite("data/gfx/area-guard");
	areaFarm = sprite("data/gfx/area-farm");
	bullet = sprite("data/gfx/bullet");
	bulletExplosion = sprite("data/gfx/explosion");
	deathAnimation = sprite("data/gfx/death");

	if (showProgress)
		updateLoadProgressScreen(70);
	// load units
	units = sprite("data/gfx/unit");
	initUnitSkins();

	if (showProgress)
		updateLoadProgressScreen(90);
	// load graphics for gui
	unitmini = sprite("data/gfx/unitmini");
	gamegui = sprite("data/gfx/gamegui");
	brush = sprite("data/gfx/brush");
	magiceffect = sprite("data/gfx/magiceffect");
	particles = sprite("data/gfx/particle");

	// building artwork used by the game, the editor and the settings
	buildingsTypes.loadSprites();
	gameGraphics = true;
}

bool GlobalContainer::ensureGameGraphics(void)
{
	if (runNoX || gameGraphics)
		return true;
	if (!GAGCore::ApplicationHost::assetPackageReady("game"))
		return false;
	loadGameGraphics(false);
	return true;
}

GAGCore::CooperativeTask GlobalContainer::gameGraphicsTask(void)
{
	while (!ensureGameGraphics())
		co_await GAGCore::CooperativeTask::checkpoint("[Loading game graphics]");
	co_return true;
}

bool GlobalContainer::loadMenuMusic(void)
{
	if (menuMusic)
		return true;
	if (!mix || !GAGCore::ApplicationHost::assetPackageReady("menu-music"))
		return false;
	mix->loadTrack("data/zik/intro.ogg",            MusicTrack::Intro);
	mix->loadTrack("data/zik/menu.ogg",             MusicTrack::Menu);
	menuMusic = true;
	return true;
}

void GlobalContainer::load(void)
{
	// Automated runs use explicitly requested checkpoints/final saves. Keep
	// this override in memory; a headless run must not change GUI preferences.
	// Harnesses exercising autosave can opt in after loading.
	if (runNoX || runTestGames || structuredHeadless)
		settings.autosaveGames = false;

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

	loadClient();
}

void GlobalContainer::loadGameFonts()
{
	const std::string font = std::string("data/fonts/") + PRIMARY_FONT;
	Toolkit::loadFont(font, 13, "standard");
	Toolkit::loadFont(font, 10, "little");
	standardFont = Toolkit::getFont("standard"); littleFont = Toolkit::getFont("little");
}
void GlobalContainer::loadOffscreenGraphics()
{
	if (!gfx) gfx = Toolkit::initGraphic(640, 480, 0, "Glob2 export", "glob2");
	if (!standardFont || !littleFont) loadGameFonts();
	if (!gameGraphics) loadGameGraphics(false);
}
