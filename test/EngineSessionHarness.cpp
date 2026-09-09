// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "Unit.h"
#include "Building.h"
#include "GameSessionScreen.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "MapEdit.h"
#include "YOGLoginScreen.h"
#include "SettingsScreen.h"
#include "ChooseMapScreen.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include "Application.h"
#include "YOGClient.h"
#include "YOGClientEvent.h"
#include "GameLaunchMessages.h"
#include <GUITextArea.h>
#include <GUITabScreenWindow.h>
#include "FertilityCalculator.h"
#include "FertilityScreen.h"
#include "EditorLoadScreen.h"
#include "EditorGenerateScreen.h"
#include "GameLoadScreen.h"
#include "MapEditorScreen.h"
#include "MapGenerator.h"
#include "HeightMapGenerator.h"
#include "PerlinNoise.h"
#include <cmath>
#include <queue>
#include "Utilities.h"
#include "LegacyFertilityReference.h"
#include "GlobalContainer.h"
#include "ReplayWriter.h"
#include "native.h"
#include "code.h"
#include <SDL_net.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
GAGCore::CooperativeSlice fixedSlice()
{
    return GAGCore::CooperativeSlice([] { return GAGCore::CooperativeSlice::Time{}; },
                                   std::chrono::milliseconds(4), 8);
}
int main(int argc, char** argv)
{
    require(argc == 2, "A disposable profile is required");
    {
        struct TrackedValue : Value {
            bool& destroyed;
            TrackedValue(Heap* heap, bool& destroyed) : Value(heap, nullptr), destroyed(destroyed) {}
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
    {
        std::srand(17); const int expected = std::rand(); std::srand(17);
        PerlinNoise first(123), second(987);
        const float value = first.Noise(.125f, .75f);
        second.reseed(456);
        require(value == first.Noise(.125f, .75f), "Reseeding another noise instance changed existing noise");
        first.reseed(123);
        require(value == first.Noise(.125f, .75f), "Explicit noise seeds must repeat");
        require(std::rand() == expected, "Noise must not mutate libc RNG state");
        for (unsigned seed = 0; seed < 128; ++seed) {
            first.reseed(seed);
            require(std::isfinite(first.Noise(.25f, .5f, .75f)), "Seeded noise must remain finite");
        }
    }

    // A one-crater map repeats the exact first stamp location. Shared static
    // stamp caches used to skip that stamp in the second map instance.
    {
        std::vector<float> expected;
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            setSyncRandSeed(42);
            HeightMap heights(128, 128);
            if (!repeat) heights.makeCraters(1, 30, 24);
            else {
                auto task = heights.makeCratersTask(1, 30, 24);
                unsigned frames = 0;
                while (!task.advance()) {
                    require(++frames < 1000, "Height-map job exceeded fixture work budget");
                    PerlinNoise unrelated(frames); unrelated.Noise(.25f, .5f);
                }
                require(task.result() && frames > 20, "Height-map work must yield within its passes");
            }
            for (unsigned i = 0; i < 128 * 128; ++i) {
                require(std::isfinite(heights(i)) && heights(i) >= 0 && heights(i) <= 1,
                        "Height-map normalization must remain finite and bounded");
                if (!repeat) expected.push_back(heights(i));
                else require(heights(i) == expected[i], "Stamp state must belong to each height map");
            }
        }
        // Destroy nested jobs during stamp construction, filling, and noise work.
        // The next operation must be safe even after cancellation of partial work.
        for (unsigned stop : {1u, 5u, 20u, 40u}) {
            HeightMap partial(128, 128);
            {
                auto task = partial.makeIslandsTask(2, 24);
                for (unsigned frame = 0; frame < stop; ++frame)
                    require(!task.advance(), "Cancellation fixture finished before its checkpoint");
            }
            partial.makeSwamp(24);
            for (unsigned i = 0; i < 128 * 128; ++i)
                require(std::isfinite(partial(i)), "Cancelled height map could not be reused");
        }
    }

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    globalContainer = new GlobalContainer(argv[1]);
    globalContainer->settings.screenWidth = 800;
    globalContainer->settings.screenHeight = 600;
    globalContainer->settings.screenFlags = 0;
    globalContainer->settings.mute = true;
    globalContainer->settings.gameSpeed = 0;
    globalContainer->load();
    require(SDLNet_Init() == 0, "SDL networking init failed");
    {
        struct SelectionProbe : ChooseMapScreen {
            SelectionProbe() : ChooseMapScreen("maps", "map", false) {}
            void select(const std::string& filename) {
                for (auto* widget : widgets) if (auto* list = dynamic_cast<Glob2FileList*>(widget)) {
                    list->addText(list->fileToList(filename));
                    list->setSelection(list->getCount() - 1);
                    list->selectionChanged();
                    return;
                }
                require(false, "Map chooser has no file list");
            }
            bool hasPreview() {
                for (auto* widget : widgets) if (auto* preview = dynamic_cast<MapPreview*>(widget))
                    return preview->isThumbnailLoaded();
                throw std::runtime_error("Map chooser has no preview");
            }
        } chooser;
        chooser.beginExecution(globalContainer->gfx);
        for (const char* invalid : {"browser_missing_fixture.map", "browser_corrupt_fixture.map"}) {
            if (std::string(invalid).find("corrupt") != std::string::npos) {
                GAGCore::BinaryOutputStream output(GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(std::string("maps/") + invalid));
                output.write("bad", 3, "truncated header");
            }
            chooser.select("balanced.map");
            require(chooser.getSelectedType() == ChooseMapScreen::MAP && chooser.hasPreview(), "Valid map must be selectable");
            chooser.select(invalid);
            require(chooser.getSelectedType() == ChooseMapScreen::NONE && !chooser.hasPreview(), "Failed map read retained the previous selection or preview");
            SDL_Event enter{}; enter.type = SDL_KEYDOWN; enter.key.keysym.sym = SDLK_RETURN;
            chooser.handleExecutionEvent(enter);
            require(chooser.isExecutionRunning(), "Invalid map was accepted by Enter");
            chooser.drawExecution();
        }
        chooser.select("balanced.map");
        require(chooser.getSelectedType() == ChooseMapScreen::MAP, "Chooser did not recover after invalid files");
        chooser.endExecute(ChooseMapScreen::CANCEL); chooser.finishExecution();
        std::cout << "PASS map selection clears stale data and recovers from missing/corrupt files without a modal loop" << std::endl;
    }
    {
        SettingsScreen settings;
        settings.beginExecution(globalContainer->gfx);
        settings.onAction(nullptr, GAGGUI::BUTTON_RELEASED, SettingsScreen::OK, 0);
        require(settings.isExecutionRunning(), "Settings must poll persistence before closing");
        settings.onTimer(SDL_GetTicks());
        require(!settings.isExecutionRunning(), "Durable native settings should complete");
        settings.finishExecution();
        std::cout << "PASS settings close only after persistence completion" << std::endl;
    }
    {
        Application application;
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        require(application.frame(SDL_GetTicks(), {quit}), "Quit must begin final persistence before returning");
        require(application.frame(SDL_GetTicks(), {quit}), "Repeated close must not bypass final persistence");
        require(application.frame(SDL_GetTicks(), {}), "Shutdown must present its completion before releasing graphics");
        require(!application.frame(SDL_GetTicks(), {}), "Native shutdown must complete after persistence");
        std::cout << "PASS application quit waits for final persistence" << std::endl;
    }
    {
        struct LoginProbe : YOGLoginScreen {
            using YOGLoginScreen::YOGLoginScreen;
            std::string status() { return statusText->getText(); }
        };
        GAGGUI::ScreenStack screens(*globalContainer->gfx);
        LoginProbe login(screens, std::make_shared<YOGClient>());
        login.beginExecution(globalContainer->gfx);
        static_cast<YOGClientEventListener&>(login).handleYOGClientEvent(std::make_shared<YOGLoginRefusedEvent>(YOGClientVersionTooOld));
        require(login.status().find("same Glob2 release") != std::string::npos,
                "Protocol rejection must provide a translated actionable status");
        login.drawExecution();
        std::cout << "PASS protocol rejection provides an actionable translated status" << std::endl;
        login.endExecute(0); login.finishExecution();
    }

    {
        struct StartProbe : MultiplayerGame {
            using MultiplayerGame::MultiplayerGame;
            using MultiplayerGame::receiveMessage;
        };
        auto client = std::make_shared<YOGClient>();
        auto game = std::make_shared<StartProbe>(client);
        require(!game->takeStartRequest(), "A room cannot start before the server request");
        game->receiveMessage(std::make_shared<NetStartGame>());
        require(game->takeStartRequest() && !game->takeStartRequest(),
                "Network dispatch must defer launch and the host must consume it once");
        require(game->isWaitingForEngine(), "Router orders must remain queued after the host consumes launch");
        game->sessionEnded(false);
        require(!game->isWaitingForEngine(), "Cancelled initialization must release the router queue hold");
        Engine engine;
        require(!engine.initMultiplayerTask(game, client, -1).run(),
                "Multiplayer initialization must reject a missing local player");
        std::cout << "PASS deferred multiplayer launch and invalid local-player rejection" << std::endl;
    }

    {
        struct TabProbe : GAGGUI::TabScreenWindow {
            using TabScreenWindow::TabScreenWindow;
            using TabScreenWindow::endExecute;
        };
        GAGGUI::TabScreen tabs(true);
        auto first = std::make_unique<TabProbe>(&tabs, "First");
        TabProbe remaining(&tabs, "Remaining");
        tabs.beginExecution(globalContainer->gfx);
        const int firstID = first->getTabNumber();
        first->endExecute(7);
        tabs.onTimer(SDL_GetTicks());
        require(tabs.getReturnCode(firstID) == 7 && remaining.isActivated(),
                "Completing an owned tab must activate the remaining tab");
        first.reset();
        require(tabs.isExecutionRunning() && remaining.isActivated(),
                "Destroying a completed tab must preserve the surviving session");
        tabs.drawExecution();
        tabs.endExecute(0); tabs.finishExecution();
        std::cout << "PASS owned tab destruction preserves surviving tabs" << std::endl;
    }

    {
        auto& gfx = *globalContainer->gfx;
        SDL_Window* window = nullptr;
        // GlobalContainer can recreate its initial window while applying settings.
        // This isolated SDL2 fixture owns a single window; IDs need not start at one.
        for (Uint32 id = 1; id < 100 && !window; ++id) window = SDL_GetWindowFromID(id);
        require(window != nullptr, "No native test window");
        const auto windowID = SDL_GetWindowID(window);
        require(gfx.resizeViewport(1200, 800), "Software viewport resize failed");
        require(gfx.getW() == 1200 && gfx.getH() == 800, "Logical resolution did not follow viewport");
        require(SDL_GetWindowFromID(windowID) == window, "Resize replaced the SDL window");
        gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), GAGCore::Color(255, 0, 0));
        gfx.nextFrame();
        auto* presented = SDL_GetWindowSurface(window);
        require(presented && presented->pixels && presented->format->BytesPerPixel == 4, "No presented test surface");
        Uint8 red, green, blue;
        SDL_GetRGB(*static_cast<Uint32*>(presented->pixels), presented->format, &red, &green, &blue);
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
        view.viewportResized(800, 600, 1200, 800);
        require(((view.viewportX + (1200-160)/64) & view.game.map.wMask) == ((20 + (800-160)/64) & view.game.map.wMask), "Resize changed center tile horizontally");
        require(((view.viewportY + 800/64) & view.game.map.hMask) == ((30 + 600/64) & view.game.map.hMask), "Resize changed center tile vertically");
        require(view.game.checkSum() == checksum, "Viewport resize changed simulation state");
        Minimap minimap(false, 160, 800, 20, 10, 128, 128, Minimap::ShowFOW);
        minimap.setGame(view.game);
        minimap.resizeViewport(1200);
        require(minimap.insideMinimap(1100, 74) && !minimap.insideMinimap(700, 74), "Minimap hit area did not follow the viewport");
    }

    {
        using namespace GAGCore::ApplicationHost;
        struct ControlledPersistence : Persistence {
            PersistenceState current = PersistenceState::Pending;
            PersistenceState state() const override { return current; }
        };
        LoadSaveScreen dialog("games", "game", false, "Save", "test", glob2FilenameToName, glob2NameToFilename);
        auto operation = std::make_unique<ControlledPersistence>();
        auto* control = operation.get();
        dialog.beginPersistence(std::move(operation));
        dialog.onAction(nullptr, GAGGUI::BUTTON_RELEASED, LoadSaveScreen::CANCEL, 0);
        require(dialog.endValue == -1 && !dialog.pollPersistence(), "Pending save must not close or claim completion");
        control->current = PersistenceState::Failed;
        require(!dialog.pollPersistence() && dialog.endValue == -1, "Failed persistence must retain the dialog");
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
        MapGenerator generator;
        for (auto method : {MapGenerationDescriptor::eUNIFORM, MapGenerationDescriptor::eSWAMP,
                            MapGenerationDescriptor::eISLANDS, MapGenerationDescriptor::eCONCRETEISLANDS,
                            MapGenerationDescriptor::eRIVER, MapGenerationDescriptor::eCRATERLAKES,
                            MapGenerationDescriptor::eISLES, MapGenerationDescriptor::eOLDRANDOM,
                            MapGenerationDescriptor::eOLDISLANDS}) {
            Uint32 checksum = 0;
            std::string rng;
            for (int repeat = 0; repeat < 2; ++repeat) {
                MapGenerationDescriptor descriptor;
                descriptor.method = method;
                descriptor.nbTeams = 2;
                Game generated(nullptr);
                if (!repeat) require(generator.generateMap(generated, descriptor, 12345), "Seeded generation fixture failed");
                else {
                    auto job = generator.generateMapTask(generated, descriptor, 12345);
                    unsigned slices = 0;
                    while (!job.advance()) {
                        PerlinNoise interleaved(123 + slices); interleaved.Noise(.125f, .75f);
                        std::rand();
                        require(++slices < 10000, "Generation job did not finish");
                    }
                    require(slices > 1 && job.result(), "Generation must yield and succeed");
                }
                if (!repeat) { checksum = generated.checkSum(); rng = getSyncRandState(); }
                else require(checksum == generated.checkSum() && rng == getSyncRandState(),
                             "Seeded generation must repeat despite unrelated noise and libc RNG draws");
                PerlinNoise unrelated(999 + repeat); unrelated.Noise(.25f, .125f);
                for (int i = 0; i < 100; ++i) std::rand();
            }
        }
    }
    {
        const auto rng = getSyncRandState();
        for (unsigned frames : {1u, 2u}) {
            GAGGUI::ScreenStack screens(*globalContainer->gfx);
            screens.push(std::make_unique<EditorGenerateScreen>(MapGenerationDescriptor(), 12345, fixedSlice()));
            for (unsigned frame = 0; frame < frames; ++frame) screens.frame(frame, {});
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
            screens.frame(frames, {escape}); screens.frame(frames + 1, {});
            require(!screens.running() && screens.result() == 0 && getSyncRandState() == rng,
                    "Cancelled generation must release partial state and restore RNG");
        }
        // Concrete-island partitioning awaits distance floods, point searches,
        // and weighted area expansion. Cancelling deep in this nested chain
        // must destroy queues/vectors before the partial map and restore RNG.
        for (unsigned frames : {2u, 8u, 20u}) {
            MapGenerationDescriptor descriptor;
            descriptor.method = MapGenerationDescriptor::eCONCRETEISLANDS;
            descriptor.nbTeams = 2;
            GAGGUI::ScreenStack screens(*globalContainer->gfx);
            screens.push(std::make_unique<EditorGenerateScreen>(descriptor, 12345, fixedSlice()));
            for (unsigned frame = 0; frame < frames; ++frame) {
                require(screens.running(), "Partition cancellation fixture finished too early");
                screens.frame(frame, {});
            }
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
            screens.frame(frames, {escape}); screens.frame(frames + 1, {});
            require(!screens.running() && screens.result() == 0 && getSyncRandState() == rng,
                    "Cancelled partitioning must release its partial map and restore RNG");
        }
        GAGGUI::ScreenStack failed(*globalContainer->gfx);
        MapGenerationDescriptor invalid; invalid.wDec = -1;
        failed.push(std::make_unique<EditorGenerateScreen>(invalid, 12345, fixedSlice()));
        for (unsigned frame = 0; failed.running(); ++frame) {
            require(frame < 10, "Invalid generation descriptor did not fail promptly"); failed.frame(frame, {});
        }
        require(failed.result() == 2 && getSyncRandState() == rng, "Invalid generation must fail without changing RNG");
    }
    {
        Engine engine;
        require(engine.initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Replay save fixture failed");
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
        std::filesystem::create_directory(blocked);
        require(!writer.write(blocked.string()), "Replay save accepted a directory");
        require(writer.getBuffer()->getPosition() == position, "Failed replay save moved the recording cursor");
        require(writer.write(destination.string()) && read(destination) == bytes && !bytes.empty(),
            "Replay retry did not preserve the complete recording");
        globalContainer->replayWriter.reset();
        std::cout << "PASS atomic replay save, failed destination and retry" << std::endl;
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
                SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
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
            sentinel.type = SDL_USEREVENT;
            sentinel.user.code = 7821;
            require(SDL_PushEvent(&sentinel) == 1, "Cannot enqueue host event");
            running = engine.stepSession(now, {});
            SDL_Event retained{};
            require(SDL_PeepEvents(&retained, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) == 1 &&
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
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
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
        SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
        screens.frame(99, {escape}); screens.frame(132, {});
        require(screens.running() && original->game.checkSum() == checksum && getSyncRandState() == rng,
                "Failed or cancelled replacement must preserve the existing map and RNG");
        screens.frame(165, {escape});
        SDL_Event down{}; down.type = SDL_MOUSEBUTTONDOWN; down.button.button = SDL_BUTTON_LEFT;
        down.button.x = 400; down.button.y = 375;
        SDL_Event up = down; up.type = SDL_MOUSEBUTTONUP;
        screens.frame(198, {down, up}); screens.frame(231, {});
        require(original->needsQuitDecision(), "Replacement failure/cancellation must preserve unsaved edits");
        screens.stop(); screens.frame(264, {});
    }
    {
        MapEdit editor;
        require(editor.load("maps/balanced.map"), "Editor fixture load failed");
        const auto savedMap = std::filesystem::path(globalContainer->fileManager->getDir(0)) / "maps" / "Editor_atomic.map";
        require(editor.save(savedMap.string(), "Editor atomic"), "Editor atomic save failed");
        require(editor.game.mapHeader.getMapName() == "Editor atomic", "Saved editor name was not published");
        const auto invalidDestination = savedMap.parent_path() / "blocked.map";
        std::filesystem::create_directory(invalidDestination);
        require(!editor.save(invalidDestination.string(), "Must not publish"), "Editor accepted a directory as a save file");
        require(editor.game.mapHeader.getMapName() == "Editor atomic", "Failed save changed the editor name");
        require(editor.load(savedMap.string()), "Atomically saved map did not reload");
        std::cout << "PASS editor atomic save/reload and failed replacement retains live metadata" << std::endl;
        require(editor.load("maps/balanced.map"), "Restore the shared editor fixture after save tests");
        // Opening a script file dialog must return to the host without polling
        // input or suspending the C++ stack. Escape closes only that child.
        for (int action : {ScriptEditorScreen::LOAD, ScriptEditorScreen::SAVE}) {
            ScriptEditorScreen script(&editor.game);
            script.onAction(nullptr, GAGGUI::BUTTON_RELEASED, action, 0);
            script.dispatchTimer(0);
            script.dispatchPaint();
            script.drawFileDialog();
            SDL_Event escape{};
            escape.type = SDL_KEYDOWN;
            escape.key.keysym.sym = SDLK_ESCAPE;
            script.translateAndProcessEvent(&escape);
            require(script.endValue < 0, "Cancelling script file dialog must retain its parent");
            SDL_Event click{};
            click.type = SDL_MOUSEBUTTONDOWN;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.x = script.decX + 170;
            click.button.y = script.decY + 380;
            script.translateAndProcessEvent(&click);
            click.type = SDL_MOUSEBUTTONUP;
            script.translateAndProcessEvent(&click);
            require(script.endValue == ScriptEditorScreen::CANCEL, "Script editor must remain usable after child cancellation");
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
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
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
                    values.push_back(editor.game.map.getCase(x, y).fertility);
            values.push_back(editor.game.map.fertilityMaximum);
            return values;
        };
        const auto original = snapshot();
        {
            FertilityCalculator::Job cancelled(editor.game.map);
            require(!cancelled.advance(0), "Zero work must not finish a job");
            cancelled.advance(4096);
            bool rejected = false;
            try { cancelled.commit(); } catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Incomplete fertility must not be committed");
        }
        require(snapshot() == original, "Cancelled fertility changed the map");
        LegacyFertilityReference::compute(editor.game.map, {});
        const auto expected = snapshot();
        require(expected.back() > 0, "Fertility oracle fixture must exercise nonzero weights");
        for (const std::size_t budget : {1u, 7919u, 65536u}) {
            for (int x = 0; x < editor.game.map.getW(); ++x)
                for (int y = 0; y < editor.game.map.getH(); ++y)
                    editor.game.map.getCase(x, y).fertility = 42;
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
            require(snapshot() == expected, "Resumable fertility differs from original algorithm");
        }
        {
            const auto beforeCancel = snapshot();
            GAGGUI::ScreenStack screens(*globalContainer->gfx);
            screens.push(std::make_unique<FertilityScreen>(editor.game.map));
            SDL_Event escape{};
            escape.type = SDL_KEYDOWN;
            escape.key.keysym.sym = SDLK_ESCAPE;
            screens.frame(1000, {escape});
            screens.frame(1001, {});
            require(!screens.running() && screens.result() == 0, "Fertility screen must accept cancellation");
            require(snapshot() == beforeCancel, "Cancelling the progress screen changed the map");
        }
        editor.beginEditing();
        editor.mapHasBeenModified();
        SDL_Event open{};
        open.type = SDL_KEYDOWN;
        open.key.keysym.sym = SDLK_ESCAPE;
        open.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        SDL_Event down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = 400; down.button.y = 375;
        SDL_Event up = down; up.type = SDL_MOUSEBUTTONUP;
        require(editor.advanceEditing({open}, 1000), "Editor must accept menu input incrementally");
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
    delete globalContainer;
    SDLNet_Quit();
    std::cout << "PASS: engine sessions, editor decisions, fertility equivalence and cancellation\n";
}
