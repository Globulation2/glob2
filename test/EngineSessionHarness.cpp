// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "Unit.h"
#include "Building.h"
#include "GameSessionScreen.h"
#include "MapEdit.h"
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
#include <SDL_net.h>
#include <iostream>
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
            require(task.result() && slices >= 8 && scheduled == expected,
                    "Scheduled gradient must yield and match the queue oracle");
            auto cancelled = seed;
            {
                auto partial = map.updateGlobalGradientTask(cancelled.data());
                require(!partial.advance() && !partial.advance(), "Gradient cancellation must precede completion");
            }
            // Monotonic relaxation can resume from an interrupted sweep.
            map.updateGlobalGradient(cancelled.data());
            require(cancelled == expected, "Interrupted gradient could not converge on restart");
        }
    }
    {
        // Adding a team to a private preparation game must be cancellable
        // after its header/Team exist but before all map arrays are allocated.
        for (unsigned extraSteps : {0u, 5u, 20u}) {
            Game partial(nullptr);
            MapGenerator generator;
            MapGenerationDescriptor descriptor;
            require(generator.generateMap(partial, descriptor, 12345), "Team fixture generation failed");
            auto task = partial.addTeamTask();
            while (std::string(task.stage()) != "[Building gradients]")
                require(!task.advance(), "Team task must yield during gradient construction");
            require(partial.mapHeader.getNumberOfTeams() == 2, "Team header must precede map preparation");
            for (unsigned step = 0; step < extraSteps; ++step)
                require(!task.advance(), "Team cancellation fixture finished too early");
            // task is destroyed before partial, releasing its nested frame.
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
        while (screens.running()) {
            screens.frame(1000 + frames * 40, {});
            require(++frames <= 2000, "Stack-driven loading/session failed to finish");
        }
        require(loadingFrames > 20 && frames == loadingFrames + 51 && screens.result() == GAGGUI::Screen::QUIT_APPLICATION,
                "Loading must yield before transferring the engine to the 50-tick session");
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
        {
            MapEdit partial;
            auto task = partial.loadTask("maps/balanced.map");
            while (std::string(task.stage()) != "[Building gradients]")
                require(!task.advance(), "Fixture must reach gradient allocation checkpoints");
            // Destruction at a partially built gradient array used to assert/leak.
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
