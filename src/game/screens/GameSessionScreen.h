// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MatchPlayReporter.h"
#include <GUIBase.h>
#include <EventQueue.h>
#include <InterfacePresentation.h>
#include <ScreenStack.h>
#include "FrontendTheme.h"
#include <memory>
#include <string>
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
    // Ends the session after a failure and shows message, then returns to the menu.
    void stopSession(const std::string &message);
    // Natural breaks for web portal ads (ApplicationHost::matchBreak). Returns
    // true while the match must wait for a break before it starts.
    bool awaitMatchBreak();
    void matchEnded();
    // Leaves the session with result, reporting the break to the host first.
    void leaveMatch(int result);
	// The colony stays loaded through the match: the results screen paints it
	// first, and reloading it there would hold back the results' first frame.
	FrontendScope theme{false, FrontendScope::Colony::Keep};
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
	// The host was told this match is starting (and may have run an ad), it is in
	// the match, and it was told the match ended. Relay matches report nothing:
	// a break must never delay a lockstep game.
	bool breakAsked = false, inMatch = false, endReported = false;
	// Pause and resume of a local match's play for the host (portal gameplay events).
	MatchPlayReporter play;
	// Once simulation ends, keep the save dialog alive through durable persistence.
	bool finishingSession = false;
#ifdef __EMSCRIPTEN__
	bool presentationDirty = true;
	Uint64 lastDraw = 0;
#endif
	Uint32 lastTick = 0;
	Uint64 clock = 0, nextTick = 0;
};
