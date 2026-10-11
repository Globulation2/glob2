// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "Engine.h"
#include "Application.h"
#include "FrontendTheme.h"
#include "MenuColony.h"
#include "MainMenuScreen.h"
#include "EndGameScreen.h"
#include "ui/RecordingControls.h"
#include <GameplayRecording.h>
#include <RendererSelection.h>
#include <RenderDevice.h>
#include <BinaryStream.h>
#include <FileManager.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <array>
#include <sstream>

namespace
{
class ObservedFrontend final : public FrontendTheme
{
public:
    struct Turn {Uint32 before,after;bool visible;};
    std::vector<Turn> turns;
    void onFrame() override
    {
        const auto before=colony->tick();
        auto *window=SDL_GetWindowFromID(globalContainer->gfx->windowID());
        const bool visible=window && !(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED);
        FrontendTheme::onFrame();turns.push_back({before,colony->tick(),visible});
    }
};
void requireRequestedRenderer(GAGCore::GraphicContext &context)
{
    if (const char *requested = SDL_getenv("GLOB2_RENDERER"))
    {
        const auto backend = GAGCore::parseRendererBackend(requested);
        REQUIRE(backend.has_value());
        INFO("requested renderer: " << requested);
        REQUIRE(context.backendSwitchStatus().active == *backend);
    }
}
void show(GAGGUI::Screen &screen, int milliseconds)
{
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
	do
	{
		screen.updateExecution(Uint32(SDL_GetTicks()));
		screen.drawExecution();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	} while (std::chrono::steady_clock::now() < end);
}
std::string read(const std::filesystem::path &path)
{
	std::ifstream input(path);
	return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace
TEST_SUITE("GameplayRecording.Integration")
{
	TEST_CASE("production presentation records menus and gameplay without changing per-tick state "
			  "[display][artifacts][recording]")
	{
		if (!GAGCore::Recording::supported()) return;
		const bool gpu = SDL_getenv("GLOB2_TEST_RECORD_GPU") != nullptr;
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .loadStrings = true,
			 .width = 800,
			 .height = 600,
			 .screenFlags = gpu ? GAGCore::GraphicContext::USEGPU : 0u});
        requireRequestedRenderer(*globals->gfx);
		globals->settings.autosaveGames = false;
		globals->settings.decorativeAnimations = false;
		// Application owns this theme throughout menus and gameplay. Keep that
		// lifetime here so capture includes the real animated colony backdrop.
		ObservedFrontend frontend;
		const auto root = glob2test::artifactDir();
		const auto initial = root / "recording-initial.game";
		const auto video =
			root / ((gpu ? std::string("production-gpu-") : std::string("production-software-")) +
					std::to_string(SDL_GetPerformanceCounter()) + ".mp4");
		{
			Engine engine;
			REQUIRE(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR);
			GAGCore::BinaryOutputStream output(
				globals->fileManager->openOutputStreamBackend(initial.string()));
			engine.gui.save(&output, "Recording initial state");
			output.flush();
		}
		std::vector<Uint32> reference;
		std::vector<double> baselineTimes;
		auto run = [&](bool recording)
		{
			auto &recorder = GAGCore::Recording::recorder();
			recorder.options.chapterTicks = 20;
			if (recording)
			{
				REQUIRE(recorder.start(video.string()));
				MainMenuScreen menu;
				menu.beginExecution(globals->gfx);
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				// Recorder initialization and menu-scene loading are independent workers.
				while ((recorder.status().state == GAGCore::Recording::State::Starting ||
						!frontend.colony->ready()) && std::chrono::steady_clock::now() < deadline)
					show(menu, 20);
				REQUIRE(recorder.status().state == GAGCore::Recording::State::Recording);
				REQUIRE(frontend.colony->ready());
				const auto colonyTick = frontend.colony->tick();
                const auto firstTurn=frontend.turns.size();
				show(menu, 200);
                INFO("frontend frames before=",firstTurn," after=",frontend.turns.size(),
                    " styleOwned=",GAGGUI::Style::style==&frontend," allowed=",FrontendTheme::allowed);
                std::ostringstream turns;
                for(std::size_t i=0;i<frontend.turns.size();++i) {
                    const auto &turn=frontend.turns[i];
                    turns<<"turn="<<i<<" before="<<turn.before<<" after="<<turn.after<<" visible="<<turn.visible<<';';
                }
                INFO(turns.str());
				CHECK(frontend.colony->tick() > colonyTick);
				// Recording is controlled from Settings, the in-game menu and its hotkey.
				REQUIRE(menu.host().find("recording/toggle") == nullptr);
				globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() +
										  "/recording-menu.bmp");
				show(menu, 20);
				menu.endExecute(0);
				menu.finishExecution();
			}
			Engine engine;
			REQUIRE(engine.initCustom(initial.string()) == Engine::EE_NO_ERROR);
			globals->automaticEndingGame = true;
			globals->automaticEndingSteps = 60;
			globals->automaticGameGlobalEndConditions = true;
			Uint64 clock = 1000;
			engine.beginSession(clock);
			std::vector<Uint32> checksums;
			std::vector<double> frameTimes;
			bool running;
			do
			{
				const auto begin = std::chrono::steady_clock::now();
				running = engine.stepSession(clock, {});
				engine.drawSession();
				frameTimes.push_back(std::chrono::duration<double, std::milli>(
										 std::chrono::steady_clock::now() - begin)
										 .count());
				checksums.push_back(engine.gui.game.checkSum());
				if (recording && engine.gui.game.stepCounter == 20)
				{
					engine.gui.openMainMenu();
					for (int i = 0; i < 15; ++i)
					{
						engine.drawSession();
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
					}
					engine.gui.closeDialog();
				}
				clock += 40;
				if (recording)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
			} while (running);
			REQUIRE(checksums.size() == 60);
			REQUIRE_FALSE(engine.finishSession());
			if (recording)
			{
				globals->automaticEndingGame = false;
				auto results = engine.endRunScreen();
				REQUIRE(results);
				results->beginExecution(globals->gfx);
				show(*results, 250);
				globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() +
										  "/recording-results.bmp");
				show(*results, 20);
				results->endExecute(0);
				results->finishExecution();
				recorder.stop();
				recorder.shutdown();
				INFO(recorder.status().error);
				REQUIRE(recorder.status().state == GAGCore::Recording::State::Complete);
				const auto manifest = read(video.string() + ".json");
				CHECK(manifest.find("\"screen\":\"main_menu\"") != std::string::npos);
				CHECK(manifest.find("\"phase\":\"gameplay\"") != std::string::npos);
				CHECK(manifest.find("\"dialog\":\"in_game_main\"") != std::string::npos);
				CHECK(manifest.find("\"phase\":\"results\"") != std::string::npos);
				std::ofstream evidence(root / "per-tick-checksums.csv");
				evidence << "tick,baseline,recording\n";
				std::ofstream timing(root / "frame-times.csv");
				timing << "tick,baseline_ms,recording_ms\n";
				for (std::size_t i = 0; i < checksums.size(); ++i)
				{
					evidence << i + 1 << ',' << reference[i] << ',' << checksums[i] << '\n';
					timing << i + 1 << ',' << baselineTimes[i] << ',' << frameTimes[i] << '\n';
				}
			}
			if (!recording)
				baselineTimes = std::move(frameTimes);
			return checksums;
		};
		reference = run(false);
		CHECK(run(true) == reference);
	}

    TEST_CASE("shutdown drains a completed asynchronous screenshot before file services retire "
              "[display][artifacts]")
    {
        for (const auto backend : {GAGCore::RendererBackend::ProgrammableGL, GAGCore::RendererBackend::SDLGPU})
        {
            const std::string name(GAGCore::rendererBackendName(backend));
            glob2test::ScopedEnvironment requested("GLOB2_RENDERER", name.c_str());
            const std::string filename = std::string("shutdown-") + name + ".bmp";
            const auto screenshot = glob2test::profileDir() / filename;
            std::filesystem::remove(screenshot);
            {
                glob2test::HeadlessGlobals globals({.display = true, .width = 64, .height = 48});
                if (globals->gfx->backendSwitchStatus().active != backend)
                {
                    MESSAGE("Screenshot shutdown backend unavailable: ", GAGCore::rendererBackendName(backend));
                    continue;
                }
                globals->gfx->drawFilledRect(0, 0, 64, 48, GAGCore::Color(190, 20, 30));
                globals->gfx->printScreen(filename);
                globals->gfx->nextFrame();
                auto *device = globals->gfx->renderDevice();
                REQUIRE(device);
                // An independent synchronous copy orders completion after the
                // screenshot copy without polling GC's screenshot result queue.
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> barrier(
                    device->captureSynchronously({}), SDL_DestroySurface);
                REQUIRE(barrier);
                CHECK_FALSE(std::filesystem::exists(screenshot));
            }
            REQUIRE(std::filesystem::exists(screenshot));
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> saved(SDL_LoadBMP(screenshot.string().c_str()), SDL_DestroySurface);
            REQUIRE(saved);
            CHECK(saved->w == 64);
            CHECK(saved->h == 48);
        }
    }

	TEST_CASE("global recording shortcut toggles and shutdown awaits finalization [display][artifacts][writes-preferences]")
	{
		if (!GAGCore::Recording::supported())
			return;
		glob2test::HeadlessGlobals globals(
			{.display = true, .loadStrings = true, .width = 800, .height = 600});
        requireRequestedRenderer(*globals->gfx);
		auto &recorder = GAGCore::Recording::recorder();
		Application application;
		REQUIRE(GAGCore::Recording::available());
		SDL_Event shortcut{};
		shortcut.type = SDL_EVENT_KEY_DOWN;
		shortcut.key.key = SDLK_R;
		shortcut.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
		REQUIRE(application.frame(SDL_GetTicks(), {shortcut}));
		REQUIRE(recorder.active());
		auto deadline = SDL_GetTicks() + 5000;
		while (recorder.status().state == GAGCore::Recording::State::Starting &&
			   SDL_GetTicks() < deadline)
		{
			REQUIRE(application.frame(SDL_GetTicks(), {}));
			SDL_Delay(10);
		}
		REQUIRE(recorder.status().state == GAGCore::Recording::State::Recording);
		for (int i = 0; i < 20; ++i)
		{
			REQUIRE(application.frame(SDL_GetTicks(), {}));
			SDL_Delay(10);
		}
		// Key repeat must not stop the current recording.
		shortcut.key.repeat = 1;
		REQUIRE(application.frame(SDL_GetTicks(), {shortcut}));
		REQUIRE(recorder.active());
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		application.frame(SDL_GetTicks(), {quit});
		CHECK_FALSE(recorder.active());
		deadline = SDL_GetTicks() + 10000;
		bool running = true;
		while (running && SDL_GetTicks() < deadline)
		{
			running = application.frame(SDL_GetTicks(), {});
			SDL_Delay(10);
		}
		CHECK_FALSE(running);
		INFO(recorder.status().error);
		REQUIRE(recorder.status().state == GAGCore::Recording::State::Complete);
		const auto original = std::filesystem::path(recorder.status().path);
		for (const auto suffix : {"", ".json", ".events.jsonl"})
			std::filesystem::copy_file(original.string() + suffix,
									   glob2test::artifactDir() /
										   (original.filename().string() + suffix));
	}
	TEST_CASE("threaded scene recording preserves baseline per-tick checksums "
			  "[display][artifacts][recording]")
	{
		if (!GAGCore::Recording::supported())
			return;
		glob2test::ScopedEnvironment threaded("GLOB2_SIM_THREAD", "1");
		glob2test::ScopedEnvironment sidecars("GLOB2_CHECKSUM_SIDECAR", "1");
		glob2test::HeadlessGlobals globals(
			{.display = true, .loadStrings = true, .width = 800, .height = 600});
        requireRequestedRenderer(*globals->gfx);
		globals->settings.autosaveGames = false;
		const auto root = glob2test::artifactDir();
		const auto initial = root / "threaded-initial.game";
		{
			Engine engine;
			REQUIRE(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR);
			GAGCore::BinaryOutputStream output(
				globals->fileManager->openOutputStreamBackend(initial.string()));
			engine.gui.save(&output, "Threaded recording initial state");
			output.flush();
		}
		std::array<std::string, 2> traces;
		for (int capture = 0; capture < 2; ++capture)
		{
			const auto replay =
				root / (capture ? "threaded-recording.replay" : "threaded-baseline.replay");
			glob2test::ScopedEnvironment replayPath("GLOB2_REPLAY_PATH", replay.string().c_str());
			Engine engine;
			REQUIRE(engine.initCustom(initial.string()) == Engine::EE_NO_ERROR);
			globals->automaticEndingGame = true;
			globals->automaticEndingSteps = 60;
			globals->automaticGameGlobalEndConditions = true;
			auto &recorder = GAGCore::Recording::recorder();
			const auto video =
				root / ("threaded-" + std::to_string(SDL_GetPerformanceCounter()) + ".mp4");
			if (capture)
			{
				recorder.options.chapterTicks = 20;
				REQUIRE(recorder.start(video.string()));
			}
			engine.beginSession(SDL_GetTicks());
			REQUIRE(engine.startSimulationThread(SDL_GetTicks()));
			const auto deadline = SDL_GetTicks() + 15000;
			bool running = true;
			while (running && SDL_GetTicks() < deadline)
			{
				running = engine.threadedClientFrame(SDL_GetTicks(), {});
				engine.drawSession();
				SDL_Delay(10);
			}
			REQUIRE_FALSE(running);
			engine.stopSimulationThread();
			REQUIRE(engine.gui.game.stepCounter == 60);
			REQUIRE_FALSE(engine.finishSession());
			traces[capture] = read(replay.string() + ".checksums");
			REQUIRE_FALSE(traces[capture].empty());
			if (capture)
			{
				recorder.shutdown();
				INFO(recorder.status().error);
				REQUIRE(recorder.status().state == GAGCore::Recording::State::Complete);
				const auto manifest = read(video.string() + ".json");
				CHECK(manifest.find("\"era_start\":20") != std::string::npos);
				CHECK(manifest.find("\"era_start\":40") != std::string::npos);
			}
		}
		CHECK(traces[0] == traces[1]);
	}
}
