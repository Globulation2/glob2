// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include "gui/GameGUILoadSave.h"

class ReplayWriter;

// A host-driven presentation of the shared save dialog. The owning game session
// keeps the replay writer alive until this child and its results screen close.
class ReplaySaveScreen : public Glob2Screen
{
public:
    explicit ReplaySaveScreen(ReplayWriter& writer);
    ~ReplaySaveScreen() override;
    void beginExecution(GAGCore::DrawableSurface* surface) override;
    void updateExecution(Uint32 tick) override;
    void handleExecutionEvent(SDL_Event event) override;
    void drawExecution() override;
    void cancelExecutionInput() override;
    void suspendExecution() override { cancelExecutionInput(); }
    void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override {}
private:
    friend class GameGUITouchHarness;
    ReplayWriter& writer;
    LoadSaveScreen dialog;
    std::unique_ptr<PhoneForm> form;
};
