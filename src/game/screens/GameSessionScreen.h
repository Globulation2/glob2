// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <EventQueue.h>
#include <InterfacePresentation.h>
#include <ScreenStack.h>
#include "FrontendTheme.h"
#include <memory>
#include <vector>
class Engine;

// Retains the initialized engine through gameplay and the end-game screen.
// In-game load/replay requests transfer the finalized engine to a loader child.
class GameSessionScreen : public GAGGUI::Screen
{
  public:
	const char *recordingId() const override { return "game_session"; }
	GameSessionScreen(GAGGUI::ScreenStack &stack, std::unique_ptr<Engine> engine);
	~GameSessionScreen() override;
	void updateExecution(Uint32 tick) override;
	void suspendExecution() override;
	void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
	void handleExecutionEvent(SDL_Event event) override;
    bool defersExecutionInput() const override { return true; }
    bool hasPendingExecutionInput() const override;
    void beginAdditionalInput(std::shared_ptr<GAGCore::InputDispatchReceipt>) override;
    void endAdditionalInput(std::size_t enqueued) override;
	void drawExecution() override;
	std::vector<GAGGUI::ui::Host *> inspectionHosts() override;
	Uint32 executionDelay(Uint32 now, Uint32 fallback) override;
	ExecutionWait executionWait() const override;

    bool supportsCompactViewport() const override { return true; }
    bool usesResponsiveViewport() const override { return GAGCore::phonePresentationRequested(); }
    std::pair<int,int> minimumViewportSize() const override { return {800,600}; }
    void cancelExecutionInput() override { suspendExecution(); }
    // The running engine, for harnesses that watch the game and its turn session.
    Engine *runningEngine() const { return engine.get(); }

  private:
    void updateExecutionImpl(Uint32 tick);
	FrontendScope theme{false};
	GAGGUI::ScreenStack &stack;
	std::unique_ptr<Engine> engine;
	GAGCore::EventQueue input;
    struct BufferedInputDispatch {
        std::shared_ptr<GAGCore::InputDispatchReceipt> receipt;
        std::size_t first = 0, count = 0;
    };
    std::vector<BufferedInputDispatch> inputReceipts;
    void pruneCancelledScreenInput();
    void cancelScreenInputReceipt();
    void transferInputReceipt();
	bool started = false, finished = false, resetClock = false;
	// Once simulation ends, keep the save dialog alive through durable persistence.
	bool finishingSession = false;
#ifdef __EMSCRIPTEN__
	bool presentationDirty = true;
	Uint64 lastDraw = 0;
#endif
	Uint32 lastTick = 0;
	Uint64 clock = 0, nextTick = 0;
};
