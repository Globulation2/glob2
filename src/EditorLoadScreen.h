// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include <CooperativeTask.h>
#include <optional>
#include <memory>
#include <functional>
class MapEdit;
namespace GAGGUI { class Text; }
class EditorLoadScreen : public Glob2Screen
{
public:
    explicit EditorLoadScreen(const std::string& filename);
    ~EditorLoadScreen() override;
    std::unique_ptr<MapEdit> takeEditor();
    void onTimer(Uint32) override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override;
    Uint32 executionDelay(Uint32, Uint32) override { return 1; }
protected:
    using Initializer = std::function<GAGCore::CooperativeTask(MapEdit&)>;
    EditorLoadScreen(Initializer initialize, const char* caption);
private:
    std::string previousRng;
    std::unique_ptr<MapEdit> editor;
    std::optional<GAGCore::CooperativeTask> task;
    GAGGUI::Text* status;
    bool accepted = false;
};
