#include <SDL3/SDL_main.h>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include <GameplayRecording.h>

#ifdef __APPLE__
#	include <CoreFoundation/CoreFoundation.h>
#	include <sys/param.h>
#endif

#include <ApplicationHost.h>
#ifndef __EMSCRIPTEN__
#include <SDL3_net/SDL_net.h>
#endif
#include <cstdlib>
#ifdef GLOB2_MOBILE
#include "mobile/MobilePaths.h"
#include <exception>
#include <SDL3/SDL_log.h>
#endif
#include "Glob2.h"
#include "CommandLine.h"
#include "Version.h"
#include "InviteLink.h"
#include "GlobalContainer.h"


#include "CampaignMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "Engine.h"
#include "Headless.h"
#include "scripting/javascript/ScriptCommand.h"
#include "hive/HiveWorker.h"
#include "hive/HiveClient.h"
#include "Application.h"
#include "SinglePlayerFlow.h"
#include "Game.h"
#include "Building.h"
#include "Unit.h"
#include "FormatableString.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GeneratorPackage.h"
#include "LANMenuScreen.h"
#include "MainMenuScreen.h"
#include "SettingsScreen.h"
#include <StringTable.h>
#include "Utilities.h"


#include <Stream.h>
#include <BinaryStream.h>
#include <Toolkit.h>
#include <FileManager.h>
#include "map/Map.h"
#include "team/Team.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "ai/cortex/CortexFoodSources.h"

#include <stdio.h>
#include <stdlib.h>


#ifndef WIN32
#	include <unistd.h>
#else
#	include <time.h>
#endif

#include "FrontendTheme.h"
#include "MapCommand.h"

using std::shared_ptr;

/*!	\mainpage Globulation 2 Reference documentation

	\section intro Introduction
	This is the documentation of Globulation 2, a free
	software game. It covers Glob2 itself and
	libgag (graphic and widget).
	\section feedback Feedback
	This documentation is not yet complete, but should help to have an
	overview of Globulation's 2 code. If you have any comments or suggestions,
	do not hesitate to contact the development team at
	http://www.globulation2.org
*/

GlobalContainer *globalContainer=NULL;



int Glob2::runNoX()
{
	printf("nox::running %d times %d steps:\n", globalContainer->runNoXCountRuns, globalContainer->automaticEndingSteps);
	for (int runNoXCount = 0; runNoXCount < globalContainer->runNoXCountRuns; runNoXCount++)
	{
		Engine engine;
		if (engine.initCustom(globalContainer->runNoXGameName) != Engine::EE_NO_ERROR)
			return 3;
		engine.run();
	}
	return 0;
}



int Glob2::runTestGames()
{
	// GLOB2_TEST_MAX_TICKS overrides the 90,000-tick cap for tooling that
	// trades game length for throughput (tools/map_fairness_tournament.py).
	// The cap only decides when the driver stops the game; it never changes
	// how a tick is simulated.
	const char* envMaxTicks = getenv("GLOB2_TEST_MAX_TICKS");
	if(globalContainer->automaticEndingSteps < 0)
        globalContainer->automaticEndingSteps = (envMaxTicks && atoi(envMaxTicks) > 0) ? atoi(envMaxTicks) : Cli::DefaultTicks;
	int maxRuns = globalContainer->runTestGamesCount;
	int run = 0;
	while(maxRuns == 0 || run < maxRuns)
	{
		// GLOB2_TEST_SEED overrides the wall-clock seed for deterministic
		// regression testing. With a fixed seed (and unchanged maps/), two
		// runs produce byte-identical replays — the basis for the
		// behavior-preservation harness used by C++ cleanup work.
		const char* envSeed = getenv("GLOB2_TEST_SEED");
		Uint32 t = envSeed ? static_cast<Uint32>(strtoull(envSeed, nullptr, 10)) : static_cast<Uint32>(time(NULL));
		// Capture the seed so createRandomGame can mirror it into
		// GameHeader::seed — otherwise a saved .game file (from
		// --save-game-as or GLOB2_DUMP_GAME) would carry the wall-clock
		// time(NULL) that GameHeader's default ctor wrote, not the seed
		// that actually drove this run, and reloading via --nox would
		// diverge from the original.
		globalContainer->testGamesSeed = (Uint32)t;
		globalContainer->testGamesSeedSet = true;
		std::cout<<"Random Seed initial: "<<t<<std::endl;
		Engine engine;
		engine.createRandomGame();
		engine.run();
		run++;
	}
	return 0;
}



int Glob2::runTestMapGeneration()
{
	long t = time(NULL);
	EntityRandom random;
	random.initializeOwner(Uint32(t), unsigned(RandomDomain::GenerationExercise));
	while(true)
	{
		GenerationRequest descriptor;
		
		using D = GenerationRequest;
		const auto methods = GeneratorRegistry::active().methods(false);
		auto method=methods[random.nextU32()%methods.size()];
		descriptor.setMethodDefaults(method);
		auto controls = D::sharedControls();
		const auto& specific = D::controls(method);
		controls.insert(controls.end(), specific.begin(), specific.end());
		for (const auto& control : controls)
		{
			int choices = (control.maximum - control.minimum) / control.step + 1;
			control.set(descriptor, control.minimum + (random.nextU32() % choices) * control.step);
		}
		if (!descriptor.hasTerrainWeight())
			continue;

		std::cout<<"Generating Map"<<std::endl;		
		GenerationService generator;
		Game game(NULL);
		descriptor.seed=random.nextU32();
		auto result=generator.generate(game, descriptor);
		if(!result) std::cerr << result.diagnostic() << std::endl;
	}
	return 0;
}


// Headless tooling: dump a map's food-source layout and team start positions as
// ASCII, to sanity-check AI food-protection field geometry. Reuses the real
// Game::load path so the data matches what the engine sees. Not a gameplay feature.
static int dumpResources(const std::string& mapName)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-resources: cannot open " << mapName << std::endl;
		delete stream;
		return 3;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok)
	{
		std::cerr << "dump-resources: failed to load " << mapName << std::endl;
		return 3;
	}

	Map& map = game.map;
	const int w = map.getW();
	const int h = map.getH();
	int foodSourceCount = 0;
	int minX = w, minY = h, maxX = -1, maxY = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food) > 0)
			{
				foodSourceCount++;
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}

	const int teamCount = game.mapHeader.getNumberOfTeams();
	std::cout << "Map " << mapName << " : " << w << "x" << h
	          << ", teams=" << teamCount << ", food source tiles=" << foodSourceCount;
	if (foodSourceCount > 0)
		std::cout << ", food source bbox=(" << minX << "," << minY << ")-(" << maxX << "," << maxY << ")";
	std::cout << std::endl;
	for (int t = 0; t < teamCount; t++)
		if (game.teams[t])
			std::cout << "  team " << t << " start=(" << game.teams[t]->startPosX
			          << "," << game.teams[t]->startPosY << ")" << std::endl;
	std::cout << "  legend: C=food source ~=water #=non-walkable .=land  digit=team start" << std::endl;

	for (int y = 0; y < h; y++)
	{
		std::string row;
		for (int x = 0; x < w; x++)
		{
			char c;
			if (map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food) > 0)      c = 'C';
			else if (map.isWater(x, y))                    c = '~';
			else if (!map.isFreeForGroundUnitNoForbidden(x, y, false)) c = '#';
			else                                           c = '.';
			for (int t = 0; t < teamCount; t++)
				if (game.teams[t] && game.teams[t]->startPosX == x && game.teams[t]->startPosY == y)
					c = (char)('0' + t);
			row += c;
		}
		std::cout << row << std::endl;
	}
	return 0;
}

// Headless tooling (AI wheat-protection eyeball): run the Cortex wheat scan over
// one team's territory on a freshly-loaded map and print the checkerboard it
// WOULD paint, swept over the open-margin range N=0..2. No Orders are emitted —
// this is the isolated geometry/reconcile core (ai/cortex/CortexFoodSources.*).
//
// A loaded .map has no colony and is fully fogged, so this differs from the live
// path in two debug-only ways, both documented inline: fog is bypassed
// (ignoreFOW), and the territory region is faked as the start/colony bounding box
// padded generously (the live path uses the real colony bbox + margin).
static int dumpWheatPlan(const std::string& mapName, int team)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-wheat: cannot open " << mapName << std::endl;
		delete stream;
		return 3;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok)
	{
		std::cerr << "dump-wheat: failed to load " << mapName << std::endl;
		return 3;
	}

	Map& map = game.map;
	const int w = map.getW();
	const int h = map.getH();
	const int teamCount = game.mapHeader.getNumberOfTeams();
	if (team < 0 || team >= teamCount || game.teams[team] == NULL)
	{
		std::cerr << "dump-wheat: team " << team << " out of range (teams=" << teamCount << ")" << std::endl;
		return 3;
	}

	Team* tm = game.teams[team];
	const Uint32 teamMask = Team::teamNumberToMask(team);

	// Consumer seeds = feeding-building (inn) tiles; the colony bounding box grows
	// over every real building. A freshly-loaded .map usually has no buildings, so
	// fall back to the team start position as the single consumer seed.
	std::vector<int> seeds;
	std::vector<bool> seedBit(static_cast<size_t>(w) * h, false);
	int bbMinX = w, bbMinY = h, bbMaxX = -1, bbMaxY = -1;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = tm->myBuildings[i];
		if (b == NULL || b->buildingState == Building::DEAD)
			continue;
		if (b->posX < bbMinX) bbMinX = b->posX;
		if (b->posX > bbMaxX) bbMaxX = b->posX;
		if (b->posY < bbMinY) bbMinY = b->posY;
		if (b->posY > bbMaxY) bbMaxY = b->posY;
		if (b->type && b->type->canFeedUnit)
		{
			const int idx = static_cast<int>(map.coordToIndex(b->posX, b->posY));
			seeds.push_back(idx);
			seedBit[idx] = true;
		}
	}
	const int startX = tm->startPosX;
	const int startY = tm->startPosY;
	if (startX < bbMinX) bbMinX = startX;
	if (startX > bbMaxX) bbMaxX = startX;
	if (startY < bbMinY) bbMinY = startY;
	if (startY > bbMaxY) bbMaxY = startY;
	if (seeds.empty())
	{
		const int idx = static_cast<int>(map.coordToIndex(startX, startY));
		seeds.push_back(idx);
		seedBit[idx] = true;
	}

	// Fake territory region: the start/colony bbox padded enough to reach a
	// starter field across its land gap (live path uses the colony bbox + a
	// smaller WHEAT_REGION_MARGIN instead).
	const int DEBUG_REGION_HALF = 18;
	int boxMinX = bbMinX - DEBUG_REGION_HALF;
	int boxMinY = bbMinY - DEBUG_REGION_HALF;
	int boxMaxX = bbMaxX + DEBUG_REGION_HALF;
	int boxMaxY = bbMaxY + DEBUG_REGION_HALF;
	if (boxMinX < 0) boxMinX = 0;
	if (boxMinY < 0) boxMinY = 0;
	if (boxMaxX > w - 1) boxMaxX = w - 1;
	if (boxMaxY > h - 1) boxMaxY = h - 1;

	std::cout << "Wheat-plan dump " << mapName << " : " << w << "x" << h
	          << ", team " << team << " start=(" << startX << "," << startY << ")"
	          << ", consumer seeds=" << seeds.size()
	          << ", region=(" << boxMinX << "," << boxMinY << ")-(" << boxMaxX << "," << boxMaxY << ")"
	          << " [fog bypassed]" << std::endl;
	std::cout << "  legend: ~=water #=blocked .=land c=food-source(unreached) o=open-margin"
	             " +=harvest-half X=forbidden S=seed " << team << "=start" << std::endl;

	for (int N = 0; N <= 2; N++)
	{
		Cortex::FoodSourceScanResult r = Cortex::scanFoodSourcesForbidden(
			map, teamMask, team, seeds,
			boxMinX, boxMinY, boxMaxX, boxMaxY,
			/*openMargin=*/N, /*ignoreFOW=*/true, /*wantDebug=*/true);

		std::cout << "=== team " << team << ", N=" << N << " ===  field=" << r.fieldTileCount
		          << " components=" << r.componentCount << " open=" << r.openCount
		          << " forbidden=" << r.forbiddenCount
		          << " add=" << r.addCount << " del=" << r.delCount << std::endl;

		for (int y = boxMinY; y <= boxMaxY; y++)
		{
			std::string row;
			for (int x = boxMinX; x <= boxMaxX; x++)
			{
				const int idx = static_cast<int>(map.coordToIndex(x, y));
				const Uint8 cls = r.classOf.empty() ? (Uint8)Cortex::WC_NONE : r.classOf[idx];
				char c;
				if (x == startX && y == startY)            c = (char)('0' + team);
				else if (seedBit[idx])                     c = 'S';
				else if (cls == Cortex::WC_OPEN_MARGIN)    c = 'o';
				else if (cls == Cortex::WC_FORBIDDEN)      c = 'X';
				else if (cls == Cortex::WC_CHECKER_OPEN)   c = '+';
				else if (map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food) > 0) c = 'c';
				else if (map.isWater(x, y))                c = '~';
				else if (!map.isFreeForGroundUnitNoForbidden(x, y, false)) c = '#';
				else                                       c = '.';
				row += c;
			}
			std::cout << row << std::endl;
		}
		std::cout << std::endl;
	}
	return 0;
}

namespace
{
void closeGameResources()
{
	delete globalContainer;
	globalContainer = nullptr;
}
} // namespace
static void dumpTeams(const Game& game)
{
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); t++)
	{
		Team* team = game.teams[t];
		int units = 0, buildings = 0, swarmCount = 0;
		std::string where;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
			if (team->myUnits[i] && !team->myUnits[i]->isDead)
			{
				units++;
				where += FormattableString(" u%0,%1").arg(team->myUnits[i]->posX).arg(team->myUnits[i]->posY);
			}
		for (int i = 0; i < Building::MAX_COUNT; i++)
			if (Building* b = team->myBuildings[i])
			{
				buildings++;
				if (b->type->semantics.production.enabledUnitMask)
				{
					swarmCount++;
					where += FormattableString(" (%0,%1)").arg(b->posX).arg(b->posY);
				}
			}
		std::cout << "  team " << t << " units=" << units << " buildings=" << buildings << " swarms=" << swarmCount << where << std::endl;
	}
}

// Headless check of a repeated map: -dump-tiled <map> <rx> <ry> <colonies> <swarms>
static int dumpTiled(const std::string& mapName, int rx, int ry, int colonies, int swarms)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-tiled: cannot open " << mapName << std::endl;
		delete stream;
		return 3;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok || !game.tileForPlay(rx, ry, colonies, swarms))
	{
		std::cerr << "dump-tiled: failed on " << mapName << std::endl;
		return 3;
	}
	std::cout << "Map " << mapName << " " << rx << "x" << ry << " : " << game.map.getW() << "x" << game.map.getH()
	          << ", teams=" << game.mapHeader.getNumberOfTeams() << std::endl;
	dumpTeams(game);
	return 0;
}

int runRenderSkin(const Cli::Request &request);
namespace {
class AssetStartupLoop : public GAGCore::ApplicationHost::Loop {
    std::shared_ptr<bool> cancelled;
    bool hidden = false;
public:
    explicit AssetStartupLoop(std::shared_ptr<bool> value) : cancelled(std::move(value)) {}
    bool frame(std::uint32_t, const std::vector<SDL_Event>& events) override {
        for (const auto &event : events) if (event.type == SDL_EVENT_QUIT) *cancelled = true;
        if (*cancelled) return false;
        auto *gfx = globalContainer->gfx;
        if (GAGCore::ApplicationHost::takeVisibilityChange(hidden)) gfx->resetRenderPacing();
        // The browser reports context loss as hidden. Leave prepared CPU data
        // queued until the host restores graphics and publishes visibility.
        if (hidden) return true;
        int width, height;
        if (GAGCore::ApplicationHost::takeViewportSize(width, height)) gfx->resizeViewport(width, height);
        if (globalContainer->finishAssetLoading()) return false;
#ifndef __EMSCRIPTEN__
        draw();
#endif
        return true;
    }
    void draw() override {
        if (!hidden && !*cancelled) globalContainer->drawAssetLoading();
    }
    std::uint32_t delay(std::uint32_t) override {
#ifdef __EMSCRIPTEN__
        return GAGCore::ApplicationHost::AnimationFrameDelay;
#else
        return 3;
#endif
    }
};
}

int Glob2::run(const Cli::Request &request)
{
	Online::MemoryStorage generatorStorage;
	MapGeneration::JavaScript::Library generatorLibrary(generatorStorage);
	const bool suppliedGenerators = request.has("--generator-package");
	for (const auto &package : request.all("--generator-package"))
		generatorLibrary.put(MapGeneration::JavaScript::Package::load(package)->canonical);
	if (suppliedGenerators)
		generatorLibrary.publish();
	auto dispatch = [&]()
	{
		const int skin = runRenderSkin(request);
		if (skin >= 0)
			return skin;
		if (request.command.rfind("map ", 0) == 0 && request.command != "map study")
			return runMapCommand(request);
		const int script = runScriptCommand(request);
		if (script >= 0)
			return script;
		return runHeadlessCommand(request);
	};
	// Machine payloads retain their schemas. Text views are presentation only.
	const bool inspection =
		request.command == "info catalog" || request.command == "info sim-version" ||
		request.command == "assets skin-info" || request.command == "assets compose-buildings";
	if (inspection && request.get("--format") == "text")
	{
		std::ostringstream payload;
		struct Restore
		{
			std::streambuf *previous;
			~Restore() { std::cout.rdbuf(previous); }
		} restore{std::cout.rdbuf(payload.rdbuf())};
		const int code = dispatch();
		std::cout.rdbuf(restore.previous);
		if (code == 0)
		{
			const auto data = nlohmann::json::parse(payload.str());
			for (const auto &[key, value] : data.items())
				std::cout << key << ": "
						  << (value.is_string() ? value.get<std::string>() : value.dump(2)) << '\n';
		}
		return code;
	}
	const int tool = dispatch();
	if (tool >= 0)
		return tool;
	globalContainer = new GlobalContainer("glob2", request.get("--building-catalog"));
	globalContainer->applyCommand(request);
	if (request.command == "info paths")
	{
		nlohmann::json paths = nlohmann::json::array();
		for (unsigned i = 0; i < globalContainer->fileManager->getDirCount(); ++i)
			paths.push_back(globalContainer->fileManager->getDir(i));
		if (request.get("--format") == "json")
			std::cout << paths.dump() << '\n';
		else
			for (size_t i = 0; i < paths.size(); ++i)
				std::cout << i << "\t" << paths[i].get<std::string>() << '\n';
		delete globalContainer;
		globalContainer = nullptr;
		return 0;
	}
	globalContainer->deferAssetLoading = !globalContainer->runNoX &&
										 !globalContainer->runTestGames &&
										 !globalContainer->runTestMapGeneration;
	globalContainer->load();
	if (!suppliedGenerators)
	{
		try
		{
			auto storage = Online::makeUserDirectoryStorage();
			MapGeneration::JavaScript::Library(*storage).publish();
		}
		catch (const std::exception &error)
		{
			fprintf(stderr, "Custom generators: %s\n", error.what());
		}
	}

	if (!globalContainer->recordingPath.empty() || !globalContainer->videoshotName.empty())
	{
		if (globalContainer->runNoX)
		{
			fprintf(stderr, "Video recording requires a rendered session\n");
			delete globalContainer;
			return 3;
		}
		auto path = globalContainer->recordingPath;
		if (path.empty())
		{
			const auto &name = globalContainer->videoshotName;
			if (name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)
			{
				fprintf(stderr, "--videoshot requires a bare recording name\n");
				delete globalContainer;
				return 3;
			}
			path = globalContainer->fileManager->getDir(0) + "/videoshots/" + name + ".mp4";
		}
		if (!GAGCore::Recording::recorder().start(path))
		{
			delete globalContainer;
			return 3;
		}
	}
	if (request.command.rfind("dev dump-", 0) == 0)
	{
		const auto &file = request.positionals.at(0);
		int code = 0;
		if (request.command == "dev dump-resources")
			code = dumpResources(file);
		else if (request.command == "dev dump-wheat")
			code = dumpWheatPlan(file, std::stoi(request.get("--team")));
		else
			code = dumpTiled(
				file, std::stoi(request.get("--repeat-x")), std::stoi(request.get("--repeat-y")),
				std::stoi(request.get("--colonies")), std::stoi(request.get("--swarms")));
		closeGameResources();
		return code == 0 ? 0 : 3;
	}

#ifndef __EMSCRIPTEN__
	if (!NET_Init())
	{
		fprintf(stderr, "Couldn't initialize net: %s\n", SDL_GetError());
		exit(3);
	}
	globalContainer->networkInitialized = true;
#endif

	if (globalContainer->runTestGames)
	{
		int ret = runTestGames();
		closeGameResources();
		return ret;
	}

	if (globalContainer->runTestMapGeneration)
	{
		runTestMapGeneration();
	}

	if (globalContainer->runNoX)
	{
		int ret = runNoX();
		closeGameResources();
		return ret;
	}

	auto cancelled = std::make_shared<bool>(false);
	auto launch = [cancelled]
	{
		if (*cancelled)
		{
			closeGameResources();
			GAGCore::ApplicationHost::exited(0);
			return;
		}
		GAGCore::ApplicationHost::run(std::make_unique<Application>(),
									  []
									  {
										  GAGCore::DrawableSurface::printFinishingText();
										  closeGameResources();
										  GAGCore::ApplicationHost::exited(0);
									  });
	};
	GAGCore::ApplicationHost::run(std::make_unique<AssetStartupLoop>(cancelled), launch);
	return HOSTED_RUN;

	return 0;
}

int main(int argc, char *argv[])
{
	auto finish = [](int code)
	{
		// Browser Module.start awaits this callback even for static commands/errors.
		std::cout.flush();
		std::cerr.flush();
		fflush(stdout);
		fflush(stderr);
		GAGCore::ApplicationHost::exited(code);
		return code;
	};
	Cli::Request request;
	try
	{
		request = Cli::parse(argc, argv);
		const int staticResult = Cli::runStatic(request);
		if (staticResult >= 0)
			return finish(staticResult);
		if (request.command == "info version")
		{
			const auto sdl = SDL_GetVersion();
			const nlohmann::json version = {
				{"cli_version", Cli::Version},
				{"version", PACKAGE_VERSION},
				{"compiled", std::string(__DATE__) + " " + __TIME__},
				{"sdl", std::to_string(SDL_VERSIONNUM_MAJOR(sdl)) + "." +
							std::to_string(SDL_VERSIONNUM_MINOR(sdl)) + "." +
							std::to_string(SDL_VERSIONNUM_MICRO(sdl))},
				{"save_version", VERSION_MINOR},
				{"minimum_save_version", MINIMUM_VERSION_MINOR},
				{"network_protocol", NET_PROTOCOL_VERSION}};
			if (request.get("--format") == "json")
				std::cout << version.dump() << '\n';
			else
				for (const auto &[key, value] : version.items())
					std::cout << key << ": "
							  << (value.is_string() ? value.get<std::string>() : value.dump())
							  << '\n';
			return finish(0);
		}
		if (request.command == "dev hive-worker")
			return finish(Hive::workerMain());
	}
	catch (const std::invalid_argument &error)
	{
		fprintf(stderr, "glob2: %s\n", error.what());
		return finish(2);
	}
	catch (const std::exception &error)
	{
		fprintf(stderr, "glob2: %s\n", error.what());
		return finish(3);
	}

	Hive::setExecutable(argv[0]);
#ifdef GLOB2_MOBILE
	try
	{
		initializeMobilePaths();
	}
	catch (const std::exception &error)
	{
		// Android does not expose native stderr in logcat. Keep early asset and
		// storage failures diagnosable even before the game logger is available.
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mobile startup: %s", error.what());
		fprintf(stderr, "Mobile startup: %s\n", error.what());
		return 3;
	}
#endif
	// Line-buffer stderr/stdout so abort() and assert failures don't swallow
	// the last log line. macOS block-buffers redirected stdio, and abort()
	// is not required to flush — without this, "fprintf(stderr, ...) ; abort()"
	// loses the message whenever stderr is a redirected file.
	setvbuf(stderr, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);

	// FileManager uses SDL_GetBasePath for macOS bundle assets. Never change
	// the caller's directory: replay, recording and tool paths are caller-relative.

	Glob2 glob2;
	int result;
	try
	{
		result = glob2.run(request);
	}
	catch (const std::exception &error)
	{
		fprintf(stderr, "Glob2 startup failed: %s\n", error.what());
		return finish(dynamic_cast<const std::invalid_argument *>(&error) ? 2 : 3);
	}
	if (result != Glob2::HOSTED_RUN)
		GAGCore::ApplicationHost::exited(result);
	return result == Glob2::HOSTED_RUN ? 0 : result;
}
