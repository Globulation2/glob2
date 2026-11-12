#include <SDL3/SDL_main.h>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
// Native builds generate the revision stamp; other toolchains report "unknown".
#if __has_include(<glob2/BuildStamp.h>)
#include <glob2/BuildStamp.h>
#endif
#ifndef GLOB2_BUILD_REVISION
#define GLOB2_BUILD_REVISION "unknown"
#endif
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
#include <stdexcept>
#include <string>
#ifdef GLOB2_MOBILE
#include "platforms/mobile/MobilePaths.h"
#include <exception>
#include <iostream>
#include <vector>
#include <SDL3/SDL_log.h>
#endif
#include "Glob2.h"
#include "ParallelFor.h"
#include "ComputeThreads.h"
#include "CommandLine.h"
#include "Version.h"
#include "InviteLink.h"
#include "GlobalContainer.h"


#include "CampaignMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CreditScreen.h"
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
#include "AIRegistry.h"
#include "CompileProfile.h"

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
		for (const Unit* u : team->liveUnits.entries())
			if (!u->isDead)
			{
				units++;
				where += FormattableString(" u%0,%1").arg(u->posX).arg(u->posY);
			}
		for (const Building* b : team->liveBuildings.entries())
			{
				buildings++;
				if (!b->type->semantics.production.enabledUnits.empty())
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
    explicit AssetStartupLoop(std::shared_ptr<bool> value) : cancelled(std::move(value)) {
#ifdef __EMSCRIPTEN__
        // The splash is static apart from progress. Leave CPU time for asset
        // preparation when browser graphics are backed by software rendering.
        const int requested = globalContainer->settings.targetRenderFps;
        globalContainer->gfx->setTargetRenderFps(requested > 0 ? std::min(requested, 5) : 5);
#endif
    }
#ifdef __EMSCRIPTEN__
    ~AssetStartupLoop() override {
        globalContainer->gfx->setTargetRenderFps(globalContainer->settings.targetRenderFps);
        globalContainer->gfx->resetRenderPacing();
    }
#endif
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
        if (globalContainer->finishAssetLoading()) {
#ifndef __EMSCRIPTEN__
            // Leave a full bar up while the first menu frame is built.
            draw();
#endif
            return false;
        }
#ifndef __EMSCRIPTEN__
        draw();
#endif
        return true;
    }
    void draw() override {
        if (!hidden && !*cancelled) globalContainer->drawAssetLoading();
    }
    std::uint32_t delay(std::uint32_t) override {
        // Asset work must advance even when display callbacks are infrequent.
        // The host schedules drawing separately on animation callbacks.
#ifdef __EMSCRIPTEN__
        if (hidden) return 100; // Match the application's background idle cadence.
#endif
        return 3;
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
		// Map tools fork/join on every core unless a runner of several processes
		// narrows each one (--compute-threads 1 keeps a process on one core).
		if (request.command.rfind("map ", 0) == 0 && request.has("--compute-threads"))
			ParallelFor::setLimit(parseComputeThreadCount(request.get("--compute-threads")));
		if (request.command == "map study-worker")
		{
			// Study runners pay process start and setup once per worker instead of
			// once per map. Each line is a map study's own arguments, run as that
			// command would be; the done line lets the runner frame its results.
			for (std::string line; std::getline(std::cin, line);)
			{
				if (line.empty())
					continue;
				int code = 3;
				try
				{
					const auto job = Cli::parse(nlohmann::json::parse(line).get<std::vector<std::string>>());
					if (job.command != "map study" || job.has("--generator-package"))
						throw std::invalid_argument("study-worker runs map study lines without generator packages");
					ParallelFor::setLimit(job.has("--compute-threads")
						? parseComputeThreadCount(job.get("--compute-threads"))
						: request.has("--compute-threads") ? parseComputeThreadCount(request.get("--compute-threads")) : 0);
					code = runHeadlessCommand(job);
				}
				catch (const std::invalid_argument &error)
				{
					std::cerr << "glob2: " << error.what() << std::endl;
					code = 2;
				}
				catch (const std::exception &error)
				{
					std::cerr << "glob2: " << error.what() << std::endl;
					code = 3;
				}
				std::cout << std::flush;
				std::fflush(stdout);
				std::cout << "glob2-study-worker done " << code << std::endl;
			}
			return 0;
		}
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
	// Saved-game repeats enter Engine::run directly; complete graphics assets
	// before the rendered repeat rather than bypassing the interactive startup host.
	globalContainer->deferAssetLoading = request.command != "game repeat" && !globalContainer->runNoX &&
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
			path = globalContainer->fileManager->getDir(0) + "/videoshots/" + name + GAGCore::Recording::Extension;
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
			code = AIRegistry::dumpWheatPlan(file, std::stoi(request.get("--team")));
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

	if (request.command == "game repeat")
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
				{"revision", GLOB2_BUILD_REVISION},
				{"sdl", std::to_string(SDL_VERSIONNUM_MAJOR(sdl)) + "." +
							std::to_string(SDL_VERSIONNUM_MINOR(sdl)) + "." +
							std::to_string(SDL_VERSIONNUM_MICRO(sdl))},
				{"save_version", VERSION_MINOR},
				{"minimum_save_version", MINIMUM_VERSION_MINOR},
				{"network_protocol", NET_PROTOCOL_VERSION},
				{"compile_profile", CompileProfile::name()}};
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
