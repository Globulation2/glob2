// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <EventQueue.h>
#include <InterfacePresentation.h>
#include <ScreenStack.h>
#include "FrontendTheme.h"
#include <memory>
class MapEdit;
class MapEditorScreen : public GAGGUI::Screen
{
  public:
	const char *recordingId() const override { return "map_editor"; }
	MapEditorScreen(GAGGUI::ScreenStack &screens, std::unique_ptr<MapEdit> editor);
	~MapEditorScreen() override;
	void updateExecution(Uint32 tick) override;
	void suspendExecution() override;
	void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
	void handleExecutionEvent(SDL_Event event) override;
	void drawExecution() override;
	Uint32 executionDelay(Uint32 now, Uint32) override;

    // Follows the editor's live presentation; ScreenStack re-reads it each frame.
    bool usesResponsiveViewport() const override;
    std::pair<int,int> minimumViewportSize() const override { return {800,600}; }
    void cancelExecutionInput() override { suspendExecution(); }
	bool interceptsQuit() const override;

  private:
	FrontendScope theme{false};
	GAGGUI::ScreenStack &screens;
	std::unique_ptr<MapEdit> editor;
	GAGCore::EventQueue input;
	bool started = false;
	Uint32 lastFrame = 0;
};
