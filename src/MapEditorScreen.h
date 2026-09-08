// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <ScreenStack.h>
#include <memory>
class MapEdit;
class MapEditorScreen : public GAGGUI::Screen
{
public:
    MapEditorScreen(GAGGUI::ScreenStack& screens, std::unique_ptr<MapEdit> editor);
    ~MapEditorScreen() override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override {}
    void updateExecution(Uint32 tick) override;
    void handleExecutionEvent(SDL_Event event) override;
    void drawExecution() override;
    Uint32 executionDelay(Uint32 now, Uint32) override;
private:
    GAGGUI::ScreenStack& screens;
    std::unique_ptr<MapEdit> editor;
    std::vector<SDL_Event> input;
    bool started = false;
    Uint32 lastFrame = 0;
};
