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
    const char* recordingId() const override { return "game_session"; }
	GameSessionScreen(GAGGUI::ScreenStack &stack, std::unique_ptr<Engine> engine);
	~GameSessionScreen() override;
	void updateExecution(Uint32 tick) override;
	void suspendExecution() override;
	void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
	void handleExecutionEvent(SDL_Event event) override;
	void drawExecution() override;
	Uint32 executionDelay(Uint32 now, Uint32 fallback) override;

    bool supportsCompactViewport() const override { return true; }
    bool usesResponsiveViewport() const override { return GAGCore::phonePresentationRequested(); }
    std::pair<int,int> minimumViewportSize() const override { return {800,600}; }
    void cancelExecutionInput() override { suspendExecution(); }

  private:
    void updateExecutionImpl(Uint32 tick);
	FrontendScope theme{false};
	GAGGUI::ScreenStack &stack;
	std::unique_ptr<Engine> engine;
	GAGCore::EventQueue input;
	bool started = false, finished = false, resetClock = false;
	// Once simulation ends, keep the save dialog alive through durable persistence.
	bool finishingSession = false;
	Uint32 lastTick = 0;
	//! Host tick at which the last threaded frame started (frame-rate cap).
	Uint32 frameStarted = 0;
	Uint64 clock = 0, nextTick = 0;
};
