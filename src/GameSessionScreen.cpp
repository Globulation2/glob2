// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameSessionScreen.h"
#include "Engine.h"
#include "GameLoadScreen.h"
#include "MessageScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <stdexcept>

GameSessionScreen::GameSessionScreen(GAGGUI::ScreenStack& stack, std::unique_ptr<Engine> engine)
    : stack(stack), engine(std::move(engine))
{
    if (!this->engine) throw std::invalid_argument("A game screen requires an initialized engine");
}
GameSessionScreen::~GameSessionScreen() { if (started && engine) engine->restoreCursor(); }

void GameSessionScreen::updateExecution(Uint32 tick)
{
    if (!isExecutionRunning() || finished) return;
    if (!started) {
        clock = lastTick = tick;
        engine->prepareRun();
        engine->beginSession(clock);
        nextTick = clock;
        started = true;
    } else {
        if (!resetClock) clock += static_cast<Uint32>(tick - lastTick);
        lastTick = tick;
    }
    resetClock = false;
    if (clock < nextTick) return;
    const bool running = engine->stepSession(clock, input);
    input.clear();
    nextTick = clock + engine->sessionDelay(clock);
    if (!running) {
        if (auto request = engine->finishSessionForHost()) {
            const bool recoveryFailed = engine->recoveryCompletionFailed();
            engine->restoreCursor();
            started = false;
            finished = true;
            stack.push(std::make_unique<GameLoadScreen>(std::move(engine), [request = *request](Engine& next) {
                return request.replay ? next.loadReplayTask(request.filename) : next.initCustomTask(request.filename);
            }), [this](GAGGUI::Screen& loading, int result) {
                if (result == 1) {
                    engine = static_cast<GameLoadScreen&>(loading).takeEngine();
                    finished = false;
                    resetClock = false;
                    input.clear();
                } else {
                    if (globalContainer->mix) globalContainer->mix->setNextTrack(MusicTrack::Menu, true);
                    if (result == 2) {
                        auto& strings = *GAGCore::Toolkit::getStringTable();
                        stack.push(std::make_unique<MessageScreen>(strings.getString("[ERROR_CANT_LOAD_MAP]"),
                            std::vector<std::string>{strings.getString("[ok]")}),
                            [this](GAGGUI::Screen&, int choice) { endExecute(choice); });
                    } else endExecute(result);
                }
            });
            if (recoveryFailed) {
                auto& strings = *GAGCore::Toolkit::getStringTable();
                stack.push(std::make_unique<MessageScreen>(strings.getString("[recovery finish failed]"),
                    std::vector<std::string>{strings.getString("[ok]")}));
            }
            return;
        }
        finished = true;
        auto endScreen = engine->endRunScreen(stack);
        if (!endScreen) endExecute(QUIT_APPLICATION);
        else {
            stack.push(std::move(endScreen), [this](GAGGUI::Screen&, int result) { endExecute(result); });
            if (engine->recoveryCompletionFailed()) {
                auto& strings = *GAGCore::Toolkit::getStringTable();
                stack.push(std::make_unique<MessageScreen>(strings.getString("[recovery finish failed]"),
                    std::vector<std::string>{strings.getString("[ok]")}));
            }
        }
    }
}

void GameSessionScreen::handleExecutionEvent(SDL_Event event)
{
    // Engine/GameGUI translates native coordinates once, at consumption.
    if (isExecutionRunning() && !finished) input.push_back(event);
}

void GameSessionScreen::cancelExecutionInput()
{
    input.clear();
    if (engine) {
        engine->checkpointRecovery(true);
        engine->cancelSessionInput();
    }
    // Time spent under a child screen must not become simulation catch-up lag.
    resetClock = true;
}

void GameSessionScreen::drawExecution()
{
    // The engine owns presentation, including nextFrame; don't also present
    // through Screen::dispatchPaint.
    if (started && !finished && isExecutionRunning()) engine->drawSession();
}

Uint32 GameSessionScreen::executionDelay(Uint32 now, Uint32 fallback)
{
    if (!started || finished) return 0;
    return engine->sessionDelay(clock + static_cast<Uint32>(now - lastTick));
}

void GameSessionScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    if (engine) engine->viewportResized(oldWidth, oldHeight, width, height);
    input.clear();
    resetClock = true;
}

void GameSessionScreen::suspendExecution()
{
    cancelExecutionInput();
}
