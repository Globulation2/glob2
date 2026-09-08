// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <ScreenStack.h>
#include <memory>
#include <vector>
class Engine;

// Retains the initialized engine through gameplay and the end-game screen.
// Loading remains a separate migration concern.
class GameSessionScreen : public GAGGUI::Screen
{
public:
    GameSessionScreen(GAGGUI::ScreenStack& stack, std::unique_ptr<Engine> engine);
    ~GameSessionScreen() override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override {}
    void updateExecution(Uint32 tick) override;
    void suspendExecution() override;
    void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
    void handleExecutionEvent(SDL_Event event) override;
    void cancelExecutionInput() override;
    void drawExecution() override;
    Uint32 executionDelay(Uint32 now, Uint32 fallback) override;
private:
    GAGGUI::ScreenStack& stack;
    std::unique_ptr<Engine> engine;
    std::vector<SDL_Event> input;
    bool started = false, finished = false, resetClock = false;
    Uint32 lastTick = 0;
    Uint64 clock = 0, nextTick = 0;
};
