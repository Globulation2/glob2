// SPDX-License-Identifier: GPL-3.0-or-later
#include <Environment.h>
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <utility>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iterator>
#include "Engine.h"
#include "GameGUITouch.h"
#include <GraphicContext.h>
#include "Unit.h"
#include "Building.h"
#include "GameSessionScreen.h"
#include "GameUtilities.h"
#include "MapEdit.h"
#include "SettingsScreen.h"
#include "ChooseMapScreen.h"
#include "GUIMapPreview.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include "Application.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "LoadSaveDialog.h"
#include "GameGUIDialog.h"
#include "FertilityCalculator.h"
#include "FertilityScreen.h"
#include "EditorLoadScreen.h"
#include "EditorGenerateScreen.h"
#include "GameLoadScreen.h"
#include "MapEditorScreen.h"
#include "MapGenerator.h"
#include <cmath>
#include <queue>
#include "Utilities.h"
#include "FertilityField.h"
#include <limits>
#include "GlobalContainer.h"
#include "CampaignEditor.h"
#include "ReplayWriter.h"
#include "online/ReplayAppearance.h"
#include "online/SkinDownloads.h"
#include "online/OnlineStorage.h"
#include "online/InstanceConfig.h"
#include "ReplayReader.h"
#include "Order.h"
#include "native.h"
#include "code.h"
#include <SDL3_net/SDL_net.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { GLOB2_REQUIRE(value, message); }
GAGCore::CooperativeSlice fixedSlice()
{
	return GAGCore::CooperativeSlice([] { return GAGCore::CooperativeSlice::Time{}; },
									 std::chrono::milliseconds(4), 8);
}
}

TEST_SUITE("EngineSession")
{
	TEST_CASE("momentum uses the SDL clock after session suspension [display][artifacts]")
	{
		glob2test::ScopedEnvironment desktopUI("GLOB2_MOBILE_UI", "0");
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = 800, .height = 600, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
		REQUIRE(NET_Init());
		struct NetworkScope { ~NetworkScope() { NET_Quit(); } } network;
		globalContainer->automaticEndingGame = false;
		for (bool threaded : {false, true})
		{
			INFO("threaded client path=" << threaded);
			Engine engine;
			REQUIRE(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR);
			// A resumed session's clock trails SDL wall time. Keep it at zero
			// while driving real timestamped touch events; no long sleep needed.
			engine.beginSession(0);
			engine.gui.gamePaused = true;
			if (threaded)
				REQUIRE(engine.startSimulationThread(0));
			auto frame = [&](const std::vector<SDL_Event> &events = {}) {
				REQUIRE(threaded ? engine.threadedClientFrame(0, events) : engine.stepSession(0, events));
			};
			auto finger = [&](Uint32 type, float x) {
				SDL_Event event{};
				event.type = type;
				event.tfinger.timestamp = SDL_GetTicksNS();
				event.tfinger.touchID = 7;
				event.tfinger.fingerID = 1;
				event.tfinger.x = x / 800;
				event.tfinger.y = 300.f / 600;
				frame({event});
			};
			for (int resume = 0; resume < 2; ++resume)
			{
				engine.suspendInput();
				engine.suspendSimulation();
				REQUIRE(!engine.gui.touch->scrollAnimating());
				engine.resumeSimulation(0);
				frame();
				finger(SDL_EVENT_FINGER_DOWN, 300);
				for (int move = 1; move <= 4; ++move)
				{
					SDL_Delay(16);
					finger(SDL_EVENT_FINGER_MOTION, 300 + 30 * move);
				}
				finger(SDL_EVENT_FINGER_UP, 420);
				const auto capture = [&](const char *phase) {
					engine.gui.drawAll(0);
					const std::string name = std::string("resume-") + (threaded ? "threaded-" : "serial-")
						+ std::to_string(resume) + "-" + phase + ".bmp";
					globalContainer->gfx->printScreen(name.c_str());
					globalContainer->gfx->nextFrame();
				};
				const double released = engine.gui.camera.originX;
				capture("released");
				SDL_Delay(20);
				frame();
				capture("coasting");
				const double coast = MapCamera::wrap(released - engine.gui.camera.originX,
					engine.gui.game.map.getW() * 32.0);
				std::cout << "Resume momentum: threaded=" << threaded << " cycle=" << resume
					<< " session_ms=0 SDL_ms=" << SDL_GetTicks() << " coast_pixels=" << coast << "\n";
				REQUIRE(coast > 1);
				REQUIRE(coast < engine.gui.game.map.getW() * 16.0);
			}
			// Teardown's GUI-only service has no session-time argument and must
			// park the producer even when there is no save left to finalize.
			const auto stoppedTick = engine.gui.game.stepCounter;
			REQUIRE_FALSE(engine.advancePendingSave({}));
			REQUIRE_FALSE(engine.simulationThreaded());
			REQUIRE(engine.gui.game.stepCounter == stoppedTick);
			engine.abortSession();
		}
	}
	TEST_CASE("serial sessions retain chat text across skipped GUI frames")
	{
		glob2test::ScopedEnvironment serialSession("GLOB2_SIM_THREAD", "0");
		glob2test::ScopedEnvironment desktopUI("GLOB2_MOBILE_UI", "0");
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = 800, .height = 600});
		REQUIRE(NET_Init());
		struct NetworkScope { ~NetworkScope() { NET_Quit(); } } network;
		globalContainer->settings.gameSpeed = Settings::GAME_SPEED_MAXIMUM;
		globalContainer->automaticEndingGame = false;
		Engine engine;
		REQUIRE(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR);
		engine.beginSession(1000);
		engine.gui.openChat();
		REQUIRE(engine.gui.typingInputScreen);
		REQUIRE(engine.stepSession(1000, {}));

		// The host may reuse SDL's text storage before the engine's next GUI
		// frame. Mutate a live buffer so shallow ownership fails deterministically.
		char text[] = "deferred chat";
		SDL_Event input{};
		input.type = SDL_EVENT_TEXT_INPUT;
		input.text.text = text;
		REQUIRE(engine.stepSession(1001, {input}));
		CHECK(engine.gui.typingInputScreen->getText().empty());
		std::fill(std::begin(text), std::end(text) - 1, 'x');

		const int interval = globalContainer->settings.getGameSpeedRenderInterval();
		REQUIRE(interval > 1);
		for (int frame = 1; frame < interval; ++frame)
			REQUIRE(engine.stepSession(1001 + frame, {}));
		CHECK(engine.gui.typingInputScreen->getText() == "deferred chat");
		engine.gui.closeChat();
		engine.gui.isRunning = false;
		CHECK_FALSE(engine.finishSession());
	}

	TEST_CASE("incremental sessions; editor decisions; fertility equivalence and cancellation [writes-preferences]")
	{
		glob2test::CapturedStdout trace;
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = 800, .height = 600});
		    // Session regressions use desktop menu coordinates and camera geometry.
		    // GameGUITouchHarness covers the touch presentation with the same engine.
		    GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", "0", 1);
			{
				struct TrackedValue : Value
				{
					bool &destroyed;
					TrackedValue(Heap *heap, bool &destroyed) : Value(heap, nullptr), destroyed(destroyed)
					{
					}
					~TrackedValue() override { destroyed = true; }
				};
				bool rootDestroyed = false, instructionDestroyed = false, garbageDestroyed = false;
		        {
		            Usl interpreter;
		            interpreter.setConstant("tracked", new TrackedValue(&interpreter.heap, rootDestroyed));
		            auto* number = new NativeValue<int>(&interpreter.heap, 42);
		            interpreter.setConstant("number", number);
		            new TrackedValue(&interpreter.heap, garbageDestroyed);
		            for (int round = 0; round < 5; ++round) {
		                interpreter.run(0);
		                require(!rootDestroyed && garbageDestroyed, "Script GC must retain roots and collect unreachable values on every pass");
		                require(dynamic_cast<NativeValue<int>*>(interpreter.getConstant("number"))->value == 42,
		                    "Repeated script GC lost a native constant");
		                Usl other;
		                auto* otherNumber = new NativeValue<int>(&other.heap, round);
		                other.setConstant("number", otherNumber);
		                other.collectGarbage();
		                require(number->prototype != otherNumber->prototype, "Native method tables must belong to their interpreter");
		            }
		            auto* prototype = new ScopePrototype(&interpreter.heap, interpreter.root->prototype);
		            auto* literal = new TrackedValue(&interpreter.heap, instructionDestroyed);
		            prototype->body.push_back(new ConstCode(literal));
		            prototype->body.push_back(new PopCode());
		            prototype->body.push_back(new ConstCode(literal));
		            interpreter.threads.emplace_back(&interpreter, new Scope(&interpreter.heap, prototype, interpreter.root.get()));
		            interpreter.run(1);
		            require(!instructionDestroyed, "Live thread stack was collected");
		            interpreter.run(1);
		            require(!instructionDestroyed, "Pending bytecode constant was collected after leaving the stack");
		            interpreter.run(1);
		            require(instructionDestroyed && !rootDestroyed, "Completed thread retained dead state or lost the root");
		        }
		        require(rootDestroyed, "Destroying an interpreter must release its retained heap");
		        std::cout << "PASS script GC roots, live frames, bytecode constants and interpreter isolation" << std::endl;
			}
		    globalContainer->settings.gameSpeed = 0;
		    require(NET_Init(), "SDL networking init failed");
		    {
		        // Both fixtures are listed at construction; one is then removed, the other truncated.
		        for (const char* fixture : {"browser-missing-fixture.map", "browser-corrupt-fixture.map"}) {
		            GAGCore::BinaryOutputStream output(GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(std::string("maps/") + fixture));
		            output.write("bad", 3, "truncated header");
		        }
		        ChooseMapScreen chooser("maps", "map", false);
		        GAGCore::Toolkit::getFileManager()->remove("maps/browser-missing-fixture.map");
		        chooser.beginExecution(globalContainer->gfx);
		        for (const char* invalid : {"browser-missing-fixture", "browser-corrupt-fixture"}) {
		            require(chooser.selectNamed("balanced") && chooser.getSelectedType() == ChooseMapScreen::MAP && chooser.hasPreview(), "Valid map must be selectable");
		            require(chooser.selectNamed(invalid), "Fixture map must be listed");
		            require(chooser.getSelectedType() == ChooseMapScreen::NONE && !chooser.hasPreview(), "Failed map read retained the previous selection or preview");
		            SDL_Event enter{}; enter.type = SDL_EVENT_KEY_DOWN; enter.key.key = SDLK_RETURN;
		            chooser.handleExecutionEvent(enter);
		            require(chooser.isExecutionRunning(), "Invalid map was accepted by Enter");
		            chooser.drawExecution();
		        }
		        require(chooser.selectNamed("balanced") && chooser.getSelectedType() == ChooseMapScreen::MAP, "Chooser did not recover after invalid files");
		        chooser.endExecute(ChooseMapScreen::CANCEL); chooser.finishExecution();
		        GAGCore::Toolkit::getFileManager()->remove("maps/browser-corrupt-fixture.map");
		        std::cout << "PASS map selection clears stale data and recovers from missing/corrupt files without a modal loop" << std::endl;
		    }
		    {
		        SettingsScreen settings;
		        settings.beginExecution(globalContainer->gfx);
		        settings.done();
		        require(settings.isExecutionRunning(), "Settings must poll persistence before closing");
		        settings.onTimer(SDL_GetTicks());
		        require(!settings.isExecutionRunning(), "Durable native settings should complete");
		        settings.finishExecution();
		        std::cout << "PASS settings close only after persistence completion" << std::endl;
		    }
		    {
		        Application application;
		        SDL_Event quit{};
		        quit.type = SDL_EVENT_QUIT;
		        require(application.frame(SDL_GetTicks(), {quit}), "Quit must begin final persistence before returning");
		        require(application.frame(SDL_GetTicks(), {quit}), "Repeated close must not bypass final persistence");
		        require(application.frame(SDL_GetTicks(), {}), "Shutdown must present its completion before releasing graphics");
		        require(!application.frame(SDL_GetTicks(), {}), "Native shutdown must complete after persistence");
		        std::cout << "PASS application quit waits for final persistence" << std::endl;
		    }
		    {
		        // The application owns its online services: created on first use while it
		        // runs, connecting, and released (connection closed) when it goes. Earlier
		        // cases may have created the harness-wide fallback services.
		        const bool fallback = Online::servicesCreated();
		        {
		            Application application;
		            require(!Online::servicesCreated(), "A new application must not create online services");
		            Online::Services& online = Online::services();
		            require(Online::servicesCreated() && &Online::services() == &online, "services() must return the application's");
		            online.client.start("https://127.0.0.1:9");
		            Online::quickMatch();
		            for (int i = 0; i < 20; ++i)
		                Online::pump();
		            SDL_Event quit{};
		            quit.type = SDL_EVENT_QUIT;
		            while (application.frame(SDL_GetTicks(), {quit}))
		                Online::pump();
		        }
		        require(Online::servicesCreated() == fallback, "The application must release its online services");
		        std::cout << "PASS the application owns and releases its online services" << std::endl;
		    }
		    {
		        auto& gfx = *globalContainer->gfx;
		        SDL_Window* window = nullptr;
		        // GlobalContainer can recreate its initial window while applying settings.
		        // This isolated SDL3 fixture owns a single window; IDs need not start at one.
		        for (Uint32 id = 1; id < 100 && !window; ++id) window = SDL_GetWindowFromID(id);
		        require(window != nullptr, "No native test window");
		        const auto windowID = SDL_GetWindowID(window);
		        require(gfx.resizeViewport(1200, 800), "Software viewport resize failed");
		        require(gfx.getW() == 1200 && gfx.getH() == 800, "Logical resolution did not follow viewport");
		        require(SDL_GetWindowFromID(windowID) == window, "Resize replaced the SDL window");
		        gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), GAGCore::Color(255, 0, 0));
		        gfx.printScreen("resize-check.bmp");
		        gfx.nextFrame();
		        // Mobile uses the portable renderer, not SDL_GetWindowSurface. Capture
		        // the actual presentation backend on both platforms.
		        auto* presented = SDL_LoadBMP((std::string(globalContainer->fileManager->getDir(0)) + "/resize-check.bmp").c_str());
		        require(presented && presented->pixels && SDL_BYTESPERPIXEL(presented->format) == 4, "No presented test surface");
		        Uint8 red, green, blue;
		        SDL_GetRGB(*static_cast<Uint32*>(presented->pixels), SDL_GetPixelFormatDetails(presented->format), SDL_GetSurfacePalette(presented), &red, &green, &blue);
		        SDL_DestroySurface(presented);
		        require(red == 255 && green == 0 && blue == 0, "Resized surface was not presented to the window");
		        require(!gfx.resizeViewport(0, 0) && gfx.getW() == 1200, "Zero viewport invalidated the render target");
		        require(gfx.resizeViewport(800, 600), "Could not restore test viewport");
		        GameGUI view;
		        auto map = Engine::loadMapHeader("maps/balanced.map");
		        GameHeader players;
		        players.setNumberOfPlayers(1);
		        players.getBasePlayer(0) = BasePlayer(0, "Viewport", 0, BasePlayer::P_LOCAL);
		        require(view.loadFromHeaders(map, players, true, true), "Viewport fixture failed to load");
		        view.viewportX = 20; view.viewportY = 30;
		        const auto checksum = view.game.checkSum();
		        require(gfx.resizeViewport(1200, 800), "Could not apply the gameplay viewport");
		        view.viewportResized(800, 600, 1200, 800);
		        require(((view.viewportX + (1200-160)/64) & view.game.map.wMask) == ((20 + (800-160)/64) & view.game.map.wMask), "Resize changed center tile horizontally");
		        require(((view.viewportY + 800/64) & view.game.map.hMask) == ((30 + 600/64) & view.game.map.hMask), "Resize changed center tile vertically");
		        require(view.game.checkSum() == checksum, "Viewport resize changed simulation state");
		        Minimap minimap(false, 160, 800, 20, 10, 128, 128, Minimap::ShowFOW);
		        minimap.setGame(view.game);
		        minimap.resizeViewport(1200);
		        require(minimap.insideMinimap(1100, 74) && !minimap.insideMinimap(700, 74), "Minimap hit area did not follow the viewport");
		        require(view.zoomMap(3, 300, 200), "Could not zoom the desktop camera");
		        const auto center = view.camera.screenToWorld(view.camera.offsetX + view.camera.width / 2,
		                                                      view.camera.offsetY + view.camera.height / 2);
		        const auto zoom = view.camera.zoom;
		        require(gfx.resizeViewport(800, 600), "Could not resize the zoomed gameplay viewport");
		        view.viewportResized(1200, 800, 800, 600);
		        const auto resizedCenter = view.camera.screenToWorld(view.camera.offsetX + view.camera.width / 2,
		                                                             view.camera.offsetY + view.camera.height / 2);
		        require(view.camera.zoom == zoom &&
		                std::abs(MapCamera::wrap(center.first, view.camera.mapWidth) - MapCamera::wrap(resizedCenter.first, view.camera.mapWidth)) < 1e-9 &&
		                std::abs(MapCamera::wrap(center.second, view.camera.mapHeight) - MapCamera::wrap(resizedCenter.second, view.camera.mapHeight)) < 1e-9,
		                "Zoomed resize changed the exact camera center or zoom");
		        require(view.game.checkSum() == checksum, "Zoomed resize changed simulation state");
		        require(gfx.resizeViewport(800, 600), "Could not restore the fixture viewport");
		    }

		    {
		        using namespace GAGCore::ApplicationHost;
		        struct ControlledPersistence : Persistence {
		            PersistenceState current = PersistenceState::Pending;
		            PersistenceState state() const override { return current; }
		        };
		        LoadSaveDialog dialog("games", "game", false, "Save", "test", glob2FilenameToName, glob2NameToFilename);
		        auto operation = std::make_unique<ControlledPersistence>();
		        auto* control = operation.get();
		        dialog.beginPersistence(std::move(operation));
		        dialog.cancelPresentedFile();
		        require(!dialog.finished() && !dialog.pollPersistence(), "Pending save must not close or claim completion");
		        control->current = PersistenceState::Failed;
		        require(!dialog.pollPersistence() && !dialog.finished(), "Failed persistence must retain the dialog");
		        operation = std::make_unique<ControlledPersistence>(); control = operation.get();
		        dialog.beginPersistence(std::move(operation));
		        control->current = PersistenceState::Succeeded;
		        require(dialog.pollPersistence(), "Successful persistence must complete the save dialog");
		    }
		    {
		        Map map;
		        map.setSize(7, 6);
		        const unsigned width = 128, height = 64;
		        for (unsigned fixture = 0; fixture < 4; ++fixture) {
		            std::vector<Uint8> seed(width * height, 1);
		            if (fixture) seed[0] = 255;
		            if (fixture >= 2) {
		                for (unsigned y = 0; y < height; ++y)
		                    for (unsigned x = 0; x < width; ++x)
		                        if (x % 16 == 7 && y % 13 != 0) seed[y * width + x] = 0;
		            }
		            if (fixture == 3) {
		                seed[32 * width + 64] = 200;
		                seed[16 * width + 32] = 254;
		            }
		            // Independent queue relaxation oracle: unlike the production
		            // forward/backward sweeps it propagates strongest values first.
		            auto expected = seed;
		            std::priority_queue<std::pair<int, unsigned>> pending;
		            for (unsigned i = 0; i < expected.size(); ++i)
		                if (expected[i] >= 3) pending.emplace(expected[i], i);
		            while (!pending.empty()) {
		                const auto [value, index] = pending.top(); pending.pop();
		                if (value != expected[index] || value < 3) continue;
		                for (int dy = -1; dy <= 1; ++dy)
		                    for (int dx = -1; dx <= 1; ++dx) {
		                        const unsigned x = (index % width + dx) & (width - 1);
		                        const unsigned y = (index / width + dy) & (height - 1);
		                        auto& neighbor = expected[y * width + x];
		                        if (neighbor && neighbor < value - 1) {
		                            neighbor = value - 1;
		                            pending.emplace(neighbor, y * width + x);
		                        }
		                    }
		            }
		            auto regular = seed;
		            map.updateGlobalGradient(regular.data());
		            require(regular == expected, "Synchronous gradient differs from queue oracle");
		            auto scheduled = seed;
		            auto task = map.updateGlobalGradientTask(scheduled.data());
		            unsigned slices = 0;
		            while (!task.advance()) require(++slices < 1000, "Gradient did not converge");
		            require(task.result() && (!fixture || slices >= 8) && scheduled == expected,
		                    "Scheduled gradient must yield and match the queue oracle");
		            auto cancelled = seed;
		            {
		                auto partial = map.updateGlobalGradientTask(cancelled.data());
		                if (fixture) require(!partial.advance() && !partial.advance(), "Gradient cancellation must precede completion");
		            }
		            // Monotonic relaxation can resume from an interrupted sweep.
		            map.updateGlobalGradient(cancelled.data());
		            require(cancelled == expected, "Interrupted gradient could not converge on restart");
		        }
		    }
		    {
		        const auto rng = getSyncRandState();
		        GAGGUI::ScreenStack cancelled(*globalContainer->gfx);
		        cancelled.push(std::make_unique<EditorGenerateScreen>(GenerationRequest(), 12345, fixedSlice()));
		        SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE;
		        cancelled.frame(0, {escape}); cancelled.frame(1, {});
		        require(!cancelled.running() && cancelled.result() == 0 && getSyncRandState() == rng,
		                "Cancelled editor generation must release its state and preserve RNG");
		        GAGGUI::ScreenStack failed(*globalContainer->gfx);
		        GenerationRequest invalid; invalid.wDec = -1;
		        failed.push(std::make_unique<EditorGenerateScreen>(invalid, 12345, fixedSlice()));
		        for (unsigned frame = 0; failed.running(); ++frame) {
		            require(frame < 10, "Invalid generation descriptor did not fail promptly"); failed.frame(frame, {});
		        }
		        require(failed.result() == 2 && getSyncRandState() == rng, "Invalid generation must fail without changing RNG");
		    }
		    {
		        Online::MemoryStorage skinStorage;
            Engine engine;
		        require(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Replay save fixture failed");
            engine.setColonySkins(std::make_unique<Online::SkinDownloads>(skinStorage,
                Online::OFFICIAL_INSTANCE_ORIGIN,"44444444-4444-4444-8444-444444444444",
                std::vector<Online::SkinDownloads::Ticket>{}));
            Online::InstanceConfig skinConfig(skinStorage);
		        auto& writer = *globalContainer->replayWriter;
		        const auto directory = std::filesystem::path(globalContainer->fileManager->getDir(0)) / "replays";
		        const auto destination = directory / "Atomic.replay";
		        const auto blocked = directory / "Blocked.replay";
		        const auto position = writer.getBuffer()->getPosition();
		        require(writer.write(destination.string()), "Initial replay save failed");
		        const auto read = [](const std::filesystem::path& path) {
		            std::ifstream input(path, std::ios::binary);
		            return std::string(std::istreambuf_iterator<char>(input), {});
		        };
		        const auto bytes = read(destination);
            const auto appearance=Online::readReplayAppearance(*globalContainer->fileManager,destination.string(),skinConfig);
            require(appearance && appearance->matchId=="44444444-4444-4444-8444-444444444444", "Online replay lost appearance companion");
		        std::filesystem::create_directory(blocked);
		        require(!writer.write(blocked.string()), "Replay save accepted a directory");
		        require(writer.getBuffer()->getPosition() == position, "Failed replay save moved the recording cursor");
		        require(writer.write(destination.string()) && read(destination) == bytes && !bytes.empty(),
		            "Replay retry did not preserve the complete recording");
            const auto observer = writer.getSaveObserver();
            writer.setSaveObserver({});
            require(writer.write(destination.string()) && read(destination) == bytes,
                "Classic overwrite changed recording bytes");
            require(!std::filesystem::exists(destination.string()+".appearance.json"),
                "Classic overwrite retained another match's appearance");
            writer.setSaveObserver(observer);
		        GameGUI replayGui;
		        {
		            // Closed before the rewrites below: Windows cannot replace a file
		            // that is still open for reading.
		            BinaryInputStream header(globalContainer->fileManager->openInputStreamBackend(destination.string()));
		            require(replayGui.load(&header), "Could not load replay header for the memory recording");
		        }
		        ReplayWriter memory;
		        memory.init("", replayGui);
		        for (ReplayWriter* recording : {&writer, &memory}) {
		            recording->setCheckSum(0xa1b2c3d4);
		            recording->pushOrder(std::make_shared<PauseGameOrder>(true));
		            for (int tick = 0; tick < 37; ++tick) recording->advanceStep();
		            require(recording->write(destination.string()), "Short replay save failed");
		            ReplayReader reader;
		            require(reader.loadReplay(destination.string()), "Saved short replay is malformed");
		            require(reader.getNumStepsTotal() == 37, "Replay save dropped its trailing idle ticks");
		        }
		        globalContainer->replayWriter.reset();
		        std::cout << "PASS atomic replay save, complete file/memory recordings and trailing ticks" << std::endl;
		    }
		    for (int outcome : {0, 1, 2}) {
		        auto previous = std::make_unique<Engine>();
		        require(previous->initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Reload ownership fixture initialization failed");
		        Engine* identity = previous.get();
		        // Mirror session finalization before reusing the initialized game.
		        globalContainer->replayWriter.reset();
		        const auto rng = getSyncRandState();
		        std::unique_ptr<Engine> accepted;
		        GAGGUI::ScreenStack reload(*globalContainer->gfx);
		        reload.push(std::make_unique<GameLoadScreen>(std::move(previous), [outcome](Engine& engine) {
		            return engine.initCampaignTask(outcome == 2 ? "maps/missing-reload-fixture.map" : "maps/balanced.map");
		        }, fixedSlice()), [&](GAGGUI::Screen& loading, int result) {
		            require(result == outcome, "Reload returned the wrong completion state");
		            if (result == 1) accepted = static_cast<GameLoadScreen&>(loading).takeEngine();
		        });
		        unsigned frames = 0;
		        while (reload.running()) {
		            std::vector<SDL_Event> events;
		            if (outcome == 0 && frames == 10) {
		                reload.suspendExecution();
		                reload.viewportResized(800,600,800,600);
		                SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE;
		                events.push_back(escape);
		            }
		            reload.frame(frames, events);
		            require(++frames < 2000, "Reused engine load did not complete");
		        }
		        if (outcome == 1) require(accepted.get() == identity && globalContainer->replayWriter && frames > 20,
		            "Successful reload must return the same engine and retain its new replay writer");
		        else require(!accepted && !globalContainer->replayWriter && !globalContainer->replayReader && getSyncRandState() == rng,
		            "Cancelled/failed reused-engine loading leaked replay state or changed RNG");
		    }
		    std::cout << "PASS reused-engine cooperative loading, cancellation and failure ownership" << std::endl;
		    globalContainer->automaticEndingGame = true;
		    globalContainer->automaticEndingSteps = 50;
		    globalContainer->automaticGameGlobalEndConditions = true;
		    for (bool delayed : {false, true}) {
		        Engine engine;
		        require(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Map load failed");
		        Uint64 now = 1000;
		        engine.beginSession(now);
		        bool rejected = false;
		        try { engine.beginSession(now); } catch (const std::logic_error&) { rejected = true; }
		        require(rejected, "Double session start must be rejected");
		        rejected = false;
		        try { engine.finishSession(); } catch (const std::logic_error&) { rejected = true; }
		        require(rejected, "Running session cannot be finalized");
		        int iterations = 0;
		        bool running;
		        do {
		            SDL_Event sentinel{};
		            sentinel.type = SDL_EVENT_USER;
		            sentinel.user.code = 7821;
		            require(SDL_PushEvent(&sentinel) == 1, "Cannot enqueue host event");
		            running = engine.stepSession(now, {});
		            SDL_Event retained{};
		            require(SDL_PeepEvents(&retained, 1, SDL_GETEVENT, SDL_EVENT_USER, SDL_EVENT_USER) == 1 &&
		                    retained.user.code == 7821, "Explicit session input must not consume the host queue");
		            engine.drawSession();
		            const Uint32 delay = engine.sessionDelay(now);
		            require(delay == engine.sessionDelay(now), "Delay queries must not advance the timing budget");
		            if (!delayed) require(delay == 40, "Regular callbacks must retain 25 Hz pacing");
		            now += delayed ? 1000 : delay;
		            require(++iterations <= 100, "Session failed to terminate");
		        } while (running);
		        require(iterations == 50, "Callback cadence changed simulation advancement");
		        require(!engine.stepSession(now), "Completed session must not advance");
		        require(!engine.finishSession(), "Finished fixture must not request another game");
		        rejected = false;
		        try { engine.stepSession(now); } catch (const std::logic_error&) { rejected = true; }
		        require(rejected, "A finalized session must reject advancement");
		    }
		    {
		        const auto rng = getSyncRandState();
		        for (bool replay : {false, true}) {
		            GAGGUI::ScreenStack cancelled(*globalContainer->gfx);
		            cancelled.push(std::make_unique<GameLoadScreen>([replay](Engine& engine) {
		                return replay ? engine.loadReplayTask("replays/last_game.replay") : engine.initCampaignTask("maps/balanced.map");
		            }, fixedSlice()));
		            for (unsigned frame = 0; frame < 20; ++frame) cancelled.frame(frame, {});
		            SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE;
		            cancelled.frame(20, {escape}); cancelled.frame(21, {});
		            require(!cancelled.running() && cancelled.result() == 0, "Game/replay load must accept cancellation");
		            require(getSyncRandState() == rng && !globalContainer->replaying && !globalContainer->replayReader &&
		                !globalContainer->replayWriter, "Cancelled startup must restore RNG and release replay state");
		        }
		        {
		            GAGGUI::ScreenStack failed(*globalContainer->gfx);
		            failed.push(std::make_unique<GameLoadScreen>([](Engine& engine) {
		                return engine.loadReplayTask("replays/missing-initialization-fixture.replay");
		            }, fixedSlice()));
		            unsigned attempts = 0;
		            while (failed.running()) { failed.frame(attempts++, {}); require(attempts < 10, "Failed load did not return"); }
		            require(failed.result() == 2 && getSyncRandState() == rng && !globalContainer->replaying,
		                "Failed startup must return an error and restore global state");
		        }
		        {
		        // Serial execution: exact frame counts against the scripted host clock.
		        glob2test::ScopedEnvironment serialSession("GLOB2_SIM_THREAD", "0");
		        GAGGUI::ScreenStack screens(*globalContainer->gfx);
		        unsigned frames = 0, loadingFrames = 0;
		        screens.push(std::make_unique<GameLoadScreen>([](Engine& engine) { return engine.initCampaignTask("maps/balanced.map"); }, fixedSlice()),
		            [&](GAGGUI::Screen& screen, int result) {
		                require(result == 1, "Scheduled game initialization failed");
		                loadingFrames = frames;
		                screens.push(std::make_unique<GameSessionScreen>(screens, static_cast<GameLoadScreen&>(screen).takeEngine()));
		            });
		        bool suspended = false;
		        while (screens.running()) {
		            if (loadingFrames && frames == loadingFrames + 10) {
		                screens.suspendExecution();
		                suspended = true;
		            }
		            screens.frame(1000 + frames * 40 + (suspended ? 60000 : 0), {});
		            require(++frames <= 2000, "Stack-driven loading/session failed to finish");
		        }
		        // Resumption keeps the pending 40ms tick deadline; hidden time is excluded.
		        require(loadingFrames > 20 && frames == loadingFrames + 52 && screens.result() == GAGGUI::Screen::QUIT_APPLICATION,
		                "Suspension must exclude hidden time and retain the pending tick deadline");
		        }
		        {
		            // Threaded execution (the default): the simulation thread paces on the
		            // host clock, so the session still ends, and the scripted 60 s spent
		            // suspended is not caught up.
		            GAGGUI::ScreenStack screens(*globalContainer->gfx);
		            unsigned frames = 0, loadingFrames = 0, resumedAt = 0;
		            screens.push(std::make_unique<GameLoadScreen>([](Engine& engine) { return engine.initCampaignTask("maps/balanced.map"); }, fixedSlice()),
		                [&](GAGGUI::Screen& screen, int result) {
		                    require(result == 1, "Scheduled game initialization failed");
		                    loadingFrames = frames;
		                    screens.push(std::make_unique<GameSessionScreen>(screens, static_cast<GameLoadScreen&>(screen).takeEngine()));
		                });
		            bool suspended = false;
		            while (screens.running()) {
		                if (loadingFrames && frames == loadingFrames + 10) {
		                    screens.suspendExecution();
		                    suspended = true;
		                    resumedAt = frames;
		                }
		                screens.frame(1000 + frames * 40 + (suspended ? 60000 : 0), {});
		                require(++frames <= 4000, "Threaded stack-driven session failed to finish");
		            }
		            require(loadingFrames > 20 && frames > resumedAt + 20 && screens.result() == GAGGUI::Screen::QUIT_APPLICATION,
		                    "Threaded session must end on the host clock without catching up hidden time");
		        }
		    }
		    for (bool cancel : {false, true}) {
		        auto editor = std::make_unique<MapEdit>();
		        require(editor->load("maps/balanced.map"), "Replacement fixture load failed");
		        editor->mapHasBeenModified();
		        MapEdit* original = editor.get();
		        const auto checksum = original->game.checkSum();
		        const auto rng = getSyncRandState();
		        GAGGUI::ScreenStack screens(*globalContainer->gfx);
		        screens.push(std::make_unique<MapEditorScreen>(screens, std::move(editor)));
		        screens.frame(0, {});
		        original->requestLoad(cancel ? "maps/balanced.map" : "maps/missing-replacement-fixture.map");
		        screens.frame(33, {}); screens.frame(66, {});
		        SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE;
		        screens.frame(99, {escape}); screens.frame(132, {});
		        require(screens.running() && original->game.checkSum() == checksum && getSyncRandState() == rng,
		                "Failed or cancelled replacement must preserve the existing map and RNG");
		        screens.frame(165, {escape});
		        require(original->hasDialog() && original->activeDialog(), "Escape must open the editor menu");
		        original->activeDialog()->draw(0);
		        const auto quit = original->activeDialog()->host().bounds("quit");
		        SDL_Event down{}; down.type = SDL_EVENT_MOUSE_BUTTON_DOWN; down.button.button = SDL_BUTTON_LEFT;
		        down.button.x = quit.x + quit.w / 2; down.button.y = quit.y + quit.h / 2;
		        SDL_Event up = down; up.type = SDL_EVENT_MOUSE_BUTTON_UP;
		        screens.frame(198, {down, up}); screens.frame(231, {});
		        require(original->needsQuitDecision(), "Replacement failure/cancellation must preserve unsaved edits");
		        screens.stop(); screens.frame(264, {});
		    }
		    {
		        MapEdit editor;
		        require(editor.load("maps/balanced.map"), "Editor fixture load failed");
		        const auto savedMap = std::filesystem::path(globalContainer->fileManager->getDir(0)) / "maps" / "Editor_atomic.map.gz";
		        require(editor.save(savedMap.string(), "Editor atomic"), "Editor atomic save failed");
		        require(editor.game.mapHeader.getMapName() == "Editor atomic", "Saved editor name was not published");
		        const auto invalidDestination = savedMap.parent_path() / "blocked.map.gz";
		        std::filesystem::create_directory(invalidDestination);
		        require(!editor.save(invalidDestination.string(), "Must not publish"), "Editor accepted a directory as a save file");
		        require(editor.game.mapHeader.getMapName() == "Editor atomic", "Failed save changed the editor name");
		        require(editor.load(savedMap.string()), "Atomically saved map did not reload");
		        std::cout << "PASS editor atomic save/reload and failed replacement retains live metadata" << std::endl;
		        require(editor.load("maps/balanced.map"), "Restore the shared editor fixture after save tests");
		        // Opening a script file dialog must return to the host without polling
		        // input or suspending the C++ stack. Escape closes only that child.
		        for (bool load : {true, false}) {
		            ScriptEditorScreen script(&editor.game);
		            script.attach(*globalContainer->gfx);
		            script.loadSave(load, "scripts", "sgsl");
		            require(script.fileDialog() != nullptr, "Script load/save must open its file dialog");
		            script.update(0);
		            script.draw(0);
		            script.fileDialog()->draw(0);
		            SDL_Event escape{};
		            escape.type = SDL_EVENT_KEY_DOWN;
		            escape.key.key = SDLK_ESCAPE;
		            script.fileDialog()->event(escape);
		            require(script.fileDialog()->finished(), "Escape must close the script file dialog");
		            script.finishFileDialog();
		            require(!script.fileDialog() && !script.finished(), "Cancelling script file dialog must retain its parent");
		            script.draw(0);
		            const auto cancel = script.host().bounds("cancel");
		            script.host().tapAt({cancel.x + cancel.w / 2, cancel.y + cancel.h / 2});
		            require(script.finished() && script.result() == ScriptEditorScreen::CANCEL, "Script editor must remain usable after child cancellation");
		        }
				// The script canvas edits the retained draft, not a flattened label:
				// composition stays provisional and tab changes keep every entry until OK.
				{
					ScriptEditorScreen script(&editor.game);
					script.attach(*globalContainer->gfx);
					script.draw(0);
					auto tapKey = [&](const std::string &key)
					{
						script.draw(0);
						const auto r = script.host().bounds(key);
						SDL_Event event{};
						event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
						event.button.button = SDL_BUTTON_LEFT;
						event.button.x = r.x + 8;
						event.button.y = r.y + 8;
						script.event(event);
						event.type = SDL_EVENT_MOUSE_BUTTON_UP;
						script.event(event);
					};
					tapKey("script");
					const auto before = script.scriptText();
					SDL_Event composition{};
					composition.type = SDL_EVENT_TEXT_EDITING;
					composition.edit.text = "provisional";
					script.event(composition);
					SDL_Event key{};
					key.type = SDL_EVENT_KEY_DOWN;
					key.key.key = SDLK_RETURN;
					script.event(key);
					require(script.scriptText() == before,
							"IME preedit or confirmation leaked into the script draft");
					SDL_Event text{};
					text.type = SDL_EVENT_TEXT_INPUT;
					text.text.text = "draft";
					script.event(text);
					require(script.scriptText().find("draft") != std::string::npos,
							"Script canvas did not accept committed text");
					script.event(key);
					require(script.scriptText().size() == before.size() + 6,
							"Script canvas lost multiline editing");
					script.showTab(ScriptEditorScreen::TAB_HINTS);
					tapKey("hint/0");
					text.text.text = "First line";
					script.event(text);
					script.showTab(ScriptEditorScreen::TAB_BRIEFING);
					require(script.hint(0).find("First line") != std::string::npos,
							"Hint entry did not preserve its text on tab change");
					const auto retainedCode = script.scriptText();
					const auto retainedHint = script.hint(0);
					script.showTab(ScriptEditorScreen::TAB_SCRIPT);
					script.draw(0);
					require(script.scriptText() == retainedCode && script.hint(0) == retainedHint,
							"Tab change modified an editing draft");
					key.key.key = SDLK_ESCAPE;
					script.event(key);
					require(script.finished() && script.result() == ScriptEditorScreen::CANCEL,
							"Script workspace cancellation failed");
					std::cout << "PASS script and hint entries, provisional IME, multiline drafts "
								 "and tab retention"
							  << std::endl;
				}
				{
					Campaign campaign;
					CampaignMapEntry entry("Test", "maps/balanced.map");
					entry.setDescription("First line\nSecond line");
					CampaignMapEntryEditor editor(campaign, entry);
					editor.beginExecution(globalContainer->gfx);
					editor.drawExecution();
					const auto bounds = editor.host().bounds("description");
					const int x = bounds.x, y = bounds.y, h = bounds.h;
					auto finger = [&](Uint32 type, int py) {
						SDL_Event event{}; event.type=type; event.tfinger.fingerID=1;
						event.tfinger.x=float(x+4)/globalContainer->gfx->getW();
						event.tfinger.y=float(py)/globalContainer->gfx->getH();
						editor.handleExecutionEvent(event);
					};
					finger(SDL_EVENT_FINGER_DOWN,y+5); finger(SDL_EVENT_FINGER_UP,y+5);
					require(editor.host().editing() == "description", "Campaign description tap did not open text input");
					SDL_Event composition{}; composition.type=SDL_EVENT_TEXT_EDITING;
					composition.edit.text = "provisional";
					editor.handleExecutionEvent(composition);
					require(editor.draftDescription()=="First line\nSecond line", "Campaign preedit changed the draft");
					SDL_Event text{}; text.type=SDL_EVENT_TEXT_INPUT;
					text.text.text = "New\n"; editor.handleExecutionEvent(text);
					require(editor.draftDescription()=="New\nFirst line\nSecond line", "Campaign touch cursor or multiline insertion failed");
					require(entry.getDescription()=="First line\nSecond line", "Campaign entry changed before confirmation");
					const auto draft=editor.draftDescription();
					finger(SDL_EVENT_FINGER_DOWN,y+h-5); finger(SDL_EVENT_FINGER_MOTION,y+5); finger(SDL_EVENT_FINGER_UP,y+5);
					require(editor.draftDescription()==draft, "Campaign scroll altered its draft");
					editor.endExecute(CampaignMapEntryEditor::CANCEL); editor.finishExecution();
					SDL_StopTextInput(SDL_GetKeyboardFocus());
				}
				const auto originalRng = getSyncRandState();
				for (const char* stage : {"[Loading units]", "[Loading buildings]", "[Resolving team links]"}) {
		            for (unsigned extraSteps : {1u, 3u}) {
		                {
		                    MapEdit partial;
		                    auto task = partial.loadTask("maps/balanced.map");
		                    unsigned steps = 0;
		                    while (std::string(task.stage()) != stage) {
		                        require(++steps < 5000 && !task.advance(), "Team parser did not reach its checkpoint");
		                    }
		                    for (unsigned step = 0; step < extraSteps; ++step)
		                        require(!task.advance(), "Team parser cancellation fixture completed too early");
		                    if (std::string(stage) == "[Resolving team links]") {
		                        auto& team = *partial.game.teams[0];
		                        bool hasUnit = false, hasBuilding = false;
		                        for (int i = 0; i < Unit::MAX_COUNT; ++i) hasUnit |= team.myUnits[i] != nullptr;
		                        for (int i = 0; i < Building::MAX_COUNT; ++i) hasBuilding |= team.myBuildings[i] != nullptr;
		                        require(hasUnit && hasBuilding, "Cancellation fixture must own real units and buildings");
		                    }
		                    // Destroy the suspended parser before the partially linked
		                    // team's units/buildings and their owning game.
		                }
		                setSyncRandState(originalRng);
		            }
		        }
		        setSyncRandState(originalRng);
		        for (unsigned frames : {1u, 4u, 20u}) {
		            GAGGUI::ScreenStack screens(*globalContainer->gfx);
		            screens.push(std::make_unique<EditorLoadScreen>("maps/balanced.map", fixedSlice()));
		            for (unsigned frame = 0; frame < frames; ++frame) screens.frame(frame, {});
		            SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE;
		            screens.frame(frames, {escape}); screens.frame(frames + 1, {});
		            require(!screens.running() && screens.result() == 0, "Partial map loading must accept cancellation");
		            require(getSyncRandState() == originalRng, "Cancelled load must restore RNG state");
		        }
		        {
		            std::unique_ptr<MapEdit> loaded;
		            GAGGUI::ScreenStack screens(*globalContainer->gfx);
		            screens.push(std::make_unique<EditorLoadScreen>("maps/balanced.map", fixedSlice()),
		                [&](GAGGUI::Screen& screen, int result) {
		                    require(result == 1, "Scheduled map load failed");
		                    loaded = static_cast<EditorLoadScreen&>(screen).takeEditor();
		                });
		            unsigned frame = 0;
		            while (screens.running()) { screens.frame(frame++, {}); require(frame < 2000, "Map load did not terminate"); }
		            require(frame > 20 && loaded->game.checkSum() == editor.game.checkSum(), "Scheduled and synchronous loads must agree");
		        }
		        auto snapshot = [&]() {
		            std::vector<Uint16> values;
		            for (int x = 0; x < editor.game.map.getW(); ++x)
		                for (int y = 0; y < editor.game.map.getH(); ++y)
		                    values.push_back(editor.game.map.getTile(x, y).fertility);
		            values.push_back(editor.game.map.fertilityMaximum);
		            return values;
		        };
		        const auto original = snapshot();
		        {
		            FertilityCalculator::Job cancelled(editor.game.map);
		            require(!cancelled.advance(0), "Zero work must not finish a job");
		            bool rejected = false;
		            try { cancelled.commit(); } catch (const std::logic_error&) { rejected = true; }
		            require(rejected, "Incomplete fertility must not be committed");
		        }
		        require(snapshot() == original, "Cancelled fertility changed the map");
		        // Compare the staged adapter with the current master's fertility field.
		        const auto field = Fertility::forMap(editor.game.map);
		        Uint16 maximum = 0;
		        for (int x = 0; x < editor.game.map.getW(); ++x)
		            for (int y = 0; y < editor.game.map.getH(); ++y) {
		                const auto value = static_cast<Uint16>(std::min(field.at(x,y),
		                    std::uint32_t(std::numeric_limits<Uint16>::max())));
		                editor.game.map.getTile(x,y).fertility = value;
		                maximum = std::max(maximum, value);
		            }
		        editor.game.map.fertilityMaximum = maximum;
		        const auto expected = snapshot();
		        require(expected.back() > 0, "Fertility oracle fixture must exercise nonzero weights");
		        for (const std::size_t budget : {1u, 7919u, 65536u}) {
		            for (int x = 0; x < editor.game.map.getW(); ++x)
		                for (int y = 0; y < editor.game.map.getH(); ++y)
		                    editor.game.map.getTile(x, y).fertility = 42;
		            editor.game.map.fertilityMaximum = 42;
		            const auto untouched = snapshot();
		            FertilityCalculator::Job job(editor.game.map);
		            float previous = 0;
		            while (!job.advance(budget)) {
		                require(job.progress() >= previous && job.progress() <= 1.f, "Progress must be monotonic");
		                previous = job.progress();
		            }
		            require(snapshot() == untouched, "Ready job published before commit");
		            job.commit(); job.commit();
		            require(snapshot() == expected, "Staged fertility differs from current field");
		        }
		        {
		            const auto beforeCancel = snapshot();
		            GAGGUI::ScreenStack screens(*globalContainer->gfx);
		            screens.push(std::make_unique<FertilityScreen>(editor.game.map));
		            SDL_Event escape{};
		            escape.type = SDL_EVENT_KEY_DOWN;
		            escape.key.key = SDLK_ESCAPE;
		            screens.frame(1000, {escape});
		            screens.frame(1001, {});
		            require(!screens.running() && screens.result() == 0, "Fertility screen must accept cancellation");
		            require(snapshot() == beforeCancel, "Cancelling the progress screen changed the map");
		        }
		        editor.beginEditing();
		        editor.mapHasBeenModified();
		        SDL_Event open{};
		        open.type = SDL_EVENT_KEY_DOWN;
		        open.key.key = SDLK_ESCAPE;
		        open.key.scancode = SDL_SCANCODE_ESCAPE;
		        require(editor.advanceEditing({open}, 1000), "Editor must accept menu input incrementally");
		        require(editor.hasDialog(), "Escape must open the editor menu");
		        editor.activeDialog()->draw(0);
		        const auto quitButton = editor.activeDialog()->host().bounds("quit");
		        SDL_Event down{};
		        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		        down.button.button = SDL_BUTTON_LEFT;
		        down.button.x = quitButton.x + quitButton.w / 2; down.button.y = quitButton.y + quitButton.h / 2;
		        SDL_Event up = down; up.type = SDL_EVENT_MOUSE_BUTTON_UP;
		        require(editor.advanceEditing({down, up}, 1033) && editor.needsQuitDecision(),
		                "Modified editor must request an explicit quit decision");
		        editor.drawEditing();
		        editor.resolveQuitDecision(2);
		        require(editor.advanceEditing({}, 1066) && !editor.needsQuitDecision(), "Cancel must keep editing");
		        editor.advanceEditing({open}, 1099);
		        editor.advanceEditing({down, up}, 1132);
		        require(editor.needsQuitDecision(), "Quit can be requested again");
		        editor.resolveQuitDecision(1);
		        require(!editor.advanceEditing({}, 1165) && editor.editingReturnCode() == 0,
		                "Discard must finish without advancing or drawing another editor frame");
		    }
		    NET_Quit();
		// The engine prints one checksum per completed session; all must agree,
		// including the session that ran on the simulation thread.
		const std::string output = trace.text();
		std::vector<std::string> checksums;
		for (size_t at = output.find("nox::gui.game.checkSum() = "); at != std::string::npos; at = output.find("nox::gui.game.checkSum() = ", at + 1))
			checksums.push_back(output.substr(at + 27, output.find('\n', at) - at - 27));
		REQUIRE_MESSAGE(checksums.size() == 4, "expected four session checksums, saw " << checksums.size());
		CHECK_MESSAGE(std::all_of(checksums.begin(), checksums.end(), [&](const std::string &c) { return c == checksums[0]; }),
			"session checksums differ: " << checksums[0] << " " << checksums[1] << " " << checksums[2] << " " << checksums[3]);
	}
}
