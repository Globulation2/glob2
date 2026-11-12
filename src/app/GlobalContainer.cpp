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
#include "FrontendUI.h"
#include "render/ResourceSprites.h"
#include "TerrainPresentation.h"
#include "ResourceRegistry.h"
#include "UnitCatalog.h"
#include "render/terrain/TerrainCatalogIO.h"
#include "render/terrain/TerrainCompositor.h"
#include "ui/ThemeCatalog.h"
#include "IntBuildingType.h"
#include "KeyboardManager.h"
#include "MapEditKeyActions.h"
#include "Race.h"
#include "SoundMixer.h"
#include "SystemLanguage.h"
#include "render/UnitSkin.h"
#include "VoiceRecorder.h"
#include "DatasetWriter.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "ScrollTuning.h"
#include "field/GradientDeviceService.h"

#include <ApplicationHost.h>
#include <algorithm>
#include <chrono>
#include <cmath>


/**
 * The GlobalContainer basically holds all preferences, data,
 * configuration information, etc.
 */
GlobalContainer::GlobalContainer(const char *profileName, const std::string& buildingCatalog)
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
	fileManager->addWriteSubdir("themes");

#ifdef __EMSCRIPTEN__
	// Start browser profiles quietly and without clouds. Saved preferences win.
	settings.mute = 1;
#endif
	// Catalog experiments must be known before preferences parse enabled keys.
	if (buildingCatalog.empty()) buildingsTypes.init();
	else buildingsTypes.loadManifest(buildingCatalog);
	std::vector<CatalogExperimentDefinition> buildingExperiments;
	for (const auto& definition : buildingsTypes.experiments())
		buildingExperiments.push_back({definition.key, definition.label, definition.help});
	for (const auto& definition : ResourceRegistry::availableDefaults()->experiments())
		buildingExperiments.push_back(definition);
	for (const auto& definition : UnitCatalog::availableDefaults()->experiments())
		buildingExperiments.push_back(definition);
	registerCatalogExperiments(buildingExperiments);

	// load user preference
	settings.load();
	Glob2UI::applyThemes(settings.menuTheme, settings.gameTheme);

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
	// Games have already drained their executors. Native drivers may register
	// process exit handlers during background initialization, after the broker's
	// static destructor; join here before those handlers or SDL teardown run.
	if (gradientDeviceShutdown) gradientDeviceShutdown();
	GAGCore::Recording::recorder().shutdown();
	// Commands and interrupted loading can finish before a heap-owned style exists.
	if (!runNoX && Style::style != &defaultStyle)
		delete Style::style;
	Style::style = &defaultStyle;

	// Release sound and the title surface before Toolkit::close() pulls the
	// underlying graphics/audio backends out from under them.
	mix.reset();
	voiceRecorder.reset();
	title.reset();
    ResourceSprites::clear();
	customTerrainCompositor.reset();
    compositorAssets.reset();
	terrainCompositor_.reset();

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

// applyCommand is defined in GlobalContainerArgs.cpp.

namespace
{
// Share of the bar for the synchronous startup steps (texts, music, fonts),
// measured at about a sixth of startup on warm and cold disks. Sprite loading
// has the rest: it advances only while the startup loop polls it, after
// these steps, and Toolkit::assetProgress() weights it by decoded files.
// finishLoadingStep() fractions likewise follow each step's measured time.
constexpr float LOADING_STEPS_SHARE = 0.16f;
// How quickly the drawn bar catches up with measured progress, which advances
// in bursts as the main thread publishes decoded sprites.
constexpr float LOADING_EASE_SECONDS = 0.06f;
// Fade the terrain in when it arrives after the wordmark is already showing.
constexpr float LOADING_TERRAIN_FADE_SECONDS = 0.25f;
// Presentation-thread time per startup turn for queuing and publishing
// sprites. Only the splash shares a native frame, so it gets most of it; the
// browser keeps its short turns (docs/browser/adr-004-cooperative-loading.md).
#ifdef __EMSCRIPTEN__
constexpr unsigned STARTUP_ASSET_BUDGET_MS = 4;
#else
constexpr unsigned STARTUP_ASSET_BUDGET_MS = 12;
#endif
}

float GlobalContainer::loadingProgress() const
{
	const float assets = gameGraphics ? 1.0f : gameGraphicsRequested ? Toolkit::assetProgress() : 0.0f;
	return LOADING_STEPS_SHARE * loadingSteps + (1 - LOADING_STEPS_SHARE) * assets;
}

void GlobalContainer::finishLoadingStep(float fraction)
{
	loadingSteps = std::max(loadingSteps, fraction);
	if (gfx && title)
		drawLoadingScreen();
}

void GlobalContainer::drawLoadingScreen(bool settle)
{
	using Clock = std::chrono::steady_clock;
	const auto now = Clock::now();
	const bool first = loadingDrawn == Clock::time_point{};
	const float target = loadingProgress();
	float shown = target;
	if (!settle && !first)
	{
		const float seconds = std::chrono::duration<float>(now - loadingDrawn).count();
		shown = loadingShown + (target - loadingShown) * (1 - std::exp(-seconds / LOADING_EASE_SECONDS));
	}
	loadingShown = std::clamp(std::max(loadingShown, shown), 0.0f, 1.0f);
	loadingDrawn = now;

	// The terrain sheet loads with the other sprites; the wordmark shows
	// on black until it arrives.
	if (!terrain)
	{
		terrain = Toolkit::findSprite("data/gfx/terrain");
		if (terrain)
			loadingTerrainShown = first ? Clock::time_point{} : now;
	}
	gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), Color(0, 0, 0));
	if (terrain)
	{
		const float fade = std::min(1.0f, std::chrono::duration<float>(now - loadingTerrainShown).count() / LOADING_TERRAIN_FADE_SECONDS);
		const Uint8 alpha = Uint8(255 * fade);
		// Loaded columns are grass, the edge column is the shore and the
		// rest is water. The edge moves by fractions of a column: the next
		// tiles blend in over the current ones.
		const int columns = (gfx->getW() + 31) / 32;
		const float position = loadingShown * columns;
		const int edge = std::min(int(position), columns);
		const Uint8 blend = Uint8(alpha * (position - edge));
		const auto drawTiles = [&](int edgeColumn, int firstColumn, int lastColumn, Uint8 tileAlpha)
		{
			unsigned randomSeed = 1;
			for (int y = 0; y < gfx->getH(); y += 32)
				for (int x = 0, column = 0; x < gfx->getW(); x += 32, ++column)
				{
					randomSeed = randomSeed * 69069;
					if (column < firstColumn || column > lastColumn)
						continue;
					unsigned index;
					if (column < edgeColumn)
						index = ((randomSeed >> 16) & 0xF);
					else if (column == edgeColumn)
						index = ((randomSeed >> 16) & 0x7) + 64;
					else
						index = ((randomSeed >> 16) & 0xF) + 128;
					gfx->drawSprite(x, y, terrain, index, tileAlpha);
				}
			gfx->finishDrawingSprite(terrain, tileAlpha);
		};
		drawTiles(edge, 0, columns, alpha);
		if (blend)
			drawTiles(edge + 1, edge, edge + 1, blend);
	}
	// Keep the splash wordmark inside portrait viewports without stretching it.
	const float titleWidth = std::min(float(title->getW()), gfx->getW() * 0.8f);
	const float titleHeight = title->getH() * (titleWidth / title->getW());
	gfx->drawSurface((gfx->getW() - titleWidth) / 2, (gfx->getH() - titleHeight) / 2,
					 titleWidth, titleHeight, title.get());
	gfx->nextFrame();
}

void GlobalContainer::openLoadingScreen(void)
{
	// create graphic context
	GraphicContext::setRequestedUiScale(settings.uiScale / 100.0f);
	std::optional<GAGCore::RendererBackend> environmentBackend;
#ifndef __EMSCRIPTEN__
	if (const char *selected = SDL_getenv_unsafe("GLOB2_RENDERER"))
		environmentBackend = GAGCore::parseRendererBackend(selected);
#endif
	GraphicContext::setInitialRendererBackend(GAGCore::resolveRendererPreference(
		settings.rendererBackend, rendererLaunchOverride, environmentBackend));
	gfx = Toolkit::initGraphic(settings.screenWidth, settings.screenHeight, settings.screenFlags, "Globulation 2", "glob 2");
	gfx->setBackendPreferenceCallback([this](GAGCore::RendererBackend selected) {
		settings.rendererBackend = selected;
		if (!settings.save()) std::cerr << "Could not save renderer preference." << std::endl;
	});
	gfx->setTargetRenderFps(settings.targetRenderFps);
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

	// The wordmark is the only image the first frame waits for.
	title = std::make_unique<DrawableSurface>("data/gfx/loading-wordmark.webp");
	drawLoadingScreen();

	// Native builds have every data package; the browser installs game sprites
	// before startup and menu music afterward (buildsys/web_assets.py). Start
	// decoding now so it overlaps the texts, music and fonts.
	if (GAGCore::ApplicationHost::assetPackageReady("game"))
		requestGameGraphics();
	finishLoadingStep(0.17f);
}

// glob2-client specific actions here.
void GlobalContainer::loadClient(void)
{
	IntBuildingType::init();
	if (!runNoX)
	{
		// No loading/menu music may escape before the mobile audio prompt.
#ifdef GLOB2_MOBILE
		settings.applyAudioStartupPolicy(true);
#else
		settings.applyAudioStartupPolicy(Glob2UI::touchPresentation());
#endif
		// create mixer
		mix = std::make_unique<SoundMixer>(settings.musicVolume, settings.voiceVolume, settings.mute);
		// Track slots must match the MusicTrack enum order. Engine::run may
		// later overwrite the InGame* slots with a randomly chosen music dir.
		mix->loadTracks({{"data/zik/original/a1.opus", MusicTrack::InGameDefault},
			{"data/zik/original/a2.opus", MusicTrack::BuildingEvent}, {"data/zik/original/a3.opus", MusicTrack::WarEvent}});
		loadMenuMusic();
		mix->setNextTrack(MusicTrack::Intro);
		mix->setNextTrack(MusicTrack::Menu);

		// create voice recorder
		voiceRecorder = std::make_unique<VoiceRecorder>();

		finishLoadingStep(0.91f);
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
		finishLoadingStep(0.96f);

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

		finishLoadingStep(1.0f);
		if (GAGCore::ApplicationHost::assetPackageReady("game") && !deferAssetLoading) {
			while (!ensureGameGraphics(STARTUP_ASSET_BUDGET_MS)) {
				drawLoadingScreen();
				SDL_PumpEvents();
#ifndef __EMSCRIPTEN__
				SDL_Delay(1);
#endif
			}
		}

		// use custom style
		Style::style = new Glob2Style;
		drawLoadingScreen(gameGraphics);
	}
}

void GlobalContainer::loadGameGraphics()
{
	const auto sprite = [](const std::string& path) {
		auto* result = Toolkit::getSprite(path);
		if (!result) throw std::runtime_error("Cannot load game sprite: " + path);
		return result;
	};
	// load terrain data
	if (!terrain)
		terrain = sprite("data/gfx/terrain");
	terrainCompositor(); // Validate all registered material sources before drawing.
	terrainCloud = sprite("data/gfx/cloud");

	// black for unexplored terrain
	terrainBlack = sprite("data/gfx/black");

	// load shader for invisible terrain
	terrainShader = sprite("data/gfx/shade");

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

	// load units
	units = sprite("data/gfx/unit");
	initUnitSkins();

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

void GlobalContainer::requestGameGraphics()
{
    if (gameGraphicsRequested) return;
    for (const char *name : {"terrain", "cloud", "black", "shade", "ressource", "ressourcemini",
        "mapicon", "area-clearing", "area-forbidden", "area-guard", "area-farm", "bullet", "explosion", "death",
        "unit", "unitmini", "gamegui", "brush", "magiceffect", "particle", "guitheme"})
        Toolkit::requestSprite(std::string("data/gfx/") + name, std::string(name) == "ressource");
	for (const auto &material : TerrainVisual::loadCatalog().materials)
	{
		if (material.variantBlocks > 1) Sprite::deferHighResolution(material.sprite);
		Toolkit::requestSprite(material.sprite);
		if (!material.decor.sprite.empty())
		{
			// The shared decor sheet holds every material's decor, thousands of
			// HD frames; the compositor streams those of the materials in view.
			Sprite::deferHighResolution(material.decor.sprite);
			Toolkit::requestSprite(material.decor.sprite);
		}
		if (!material.overlay.sprite.empty())
			Toolkit::requestSprite(material.overlay.sprite);
	}
	for (size_t i = 0; i < buildingsTypes.size(); ++i) {
        const auto *type = buildingsTypes.get(i);
        if (type->type == "null") continue;
        Toolkit::requestSprite(type->gameSprite);
        if (type->miniSpriteImage >= 0) Toolkit::requestSprite(type->miniSprite);
    }
    gameGraphicsRequested = true;
}
bool GlobalContainer::finishAssetLoading()
{
    return ensureGameGraphics(STARTUP_ASSET_BUDGET_MS);
}
void GlobalContainer::drawAssetLoading()
{
    if (gfx->beginRenderFrame())
        drawLoadingScreen(gameGraphics);
}

bool GlobalContainer::ensureGameGraphics(unsigned budgetMs)
{
	if (runNoX || gameGraphics)
		return true;
	if (!GAGCore::ApplicationHost::assetPackageReady("game"))
		return false;
    requestGameGraphics();
    if (!Toolkit::pollAssets(budgetMs)) return false;
    loadGameGraphics();
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
    mix->loadTracks({{"data/zik/intro.opus", MusicTrack::Intro}, {"data/zik/menu.opus", MusicTrack::Menu}});
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
	// Show the splash before the slower setup below.
	if (!runNoX)
		openLoadingScreen();

	// load texts: every language, about half of the synchronous steps
	const auto textProgress = [this](float fraction)
	{
		// Redraw at most every 30ms so the splash does not slow the reads.
		if (!runNoX && std::chrono::steady_clock::now() - loadingDrawn > std::chrono::milliseconds(30))
			finishLoadingStep(0.17f + 0.53f * fraction);
	};
	if (!Toolkit::getStringTable()->load("data/texts.list.txt", textProgress))
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

	// A profile without a language (or with one this build no longer ships) follows
	// the operating system's or browser's preferred languages.
	StringTable *strings = Toolkit::getStringTable();
	if (!strings->hasLanguage(settings.language))
		settings.language = SystemLanguage::choose(SystemLanguage::preferred(),
			[strings](const std::string &code) { return strings->hasLanguage(code); });
	strings->setLang(strings->getLangCode(settings.language));
	if (!runNoX)
		finishLoadingStep(0.70f);
	// load default unit types
	Race::loadDefault();

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
	if (!gameGraphics)
	{
		// Queue every sprite before waiting on any, as ensureGameGraphics does, so the
		// asset loader decodes them in parallel; loadGameGraphics then finds each ready.
		requestGameGraphics();
		while (!Toolkit::pollAssets(4))
			SDL_Delay(1);
		loadGameGraphics();
	}
}

TerrainVisual::Compositor &GlobalContainer::terrainCompositor()
{
	if (!terrainCompositor_)
		terrainCompositor_ =
			std::make_shared<TerrainVisual::Compositor>(TerrainVisual::loadCatalog());
	return *terrainCompositor_;
}

TerrainVisual::Compositor &GlobalContainer::terrainCompositor(std::shared_ptr<const MapAssetBundle> assets)
{
    return *terrainCompositorOwner(std::move(assets));
}

std::shared_ptr<TerrainVisual::Compositor> GlobalContainer::terrainCompositorOwner(
    std::shared_ptr<const MapAssetBundle> assets)
{
    if (!assets || assets->isEmpty())
    {
        terrainCompositor();
        return terrainCompositor_;
    }
    if (compositorAssets != assets)
    {
        customTerrainCompositor = std::make_shared<TerrainVisual::Compositor>(
            TerrainVisual::loadCatalog(assets), assets);
        compositorAssets = std::move(assets);
    }
    return customTerrainCompositor;
}
