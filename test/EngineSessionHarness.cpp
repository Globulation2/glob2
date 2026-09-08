// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameSessionScreen.h"
#include "MapEdit.h"
#include "FertilityCalculator.h"
#include "FertilityScreen.h"
#include "EditorLoadScreen.h"
#include "Utilities.h"
#include "LegacyFertilityReference.h"
#include "GlobalContainer.h"
#include <SDL_net.h>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    require(argc == 2, "A disposable profile is required");
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
        auto engine = std::make_unique<Engine>();
        require(engine->initCampaign("maps/balanced.map") == Engine::EE_NO_ERROR, "Stack fixture load failed");
        GAGGUI::ScreenStack screens(*globalContainer->gfx);
        screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)));
        unsigned frames = 0;
        while (screens.running()) {
            screens.frame(1000 + frames * 40, {});
            require(++frames <= 60, "Stack-driven session failed to finish");
        }
        require(frames == 51 && screens.result() == GAGGUI::Screen::QUIT_APPLICATION,
                "The stack must drive one session step per frame and defer its completion");
    }
    {
        MapEdit editor;
        require(editor.load("maps/balanced.map"), "Editor fixture load failed");
        const auto originalRng = getSyncRandState();
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
            screens.push(std::make_unique<EditorLoadScreen>("maps/balanced.map"));
            for (unsigned frame = 0; frame < frames; ++frame) screens.frame(frame, {});
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
            screens.frame(frames, {escape}); screens.frame(frames + 1, {});
            require(!screens.running() && screens.result() == 0, "Partial map loading must accept cancellation");
            require(getSyncRandState() == originalRng, "Cancelled load must restore RNG state");
        }
        {
            std::unique_ptr<MapEdit> loaded;
            GAGGUI::ScreenStack screens(*globalContainer->gfx);
            screens.push(std::make_unique<EditorLoadScreen>("maps/balanced.map"),
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
