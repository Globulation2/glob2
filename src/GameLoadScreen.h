// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include <CooperativeTask.h>
#include <functional>
#include <memory>
#include <optional>
class Engine;
namespace GAGGUI { class Text; }
// Startup loading only: there must be no active engine/session.
class GameLoadScreen : public Glob2Screen
{
public:
    using Initializer = std::function<GAGCore::CooperativeTask(Engine&)>;
    explicit GameLoadScreen(Initializer initialize);
    ~GameLoadScreen() override;
    std::unique_ptr<Engine> takeEngine();
    void onTimer(Uint32) override;
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override;
    Uint32 executionDelay(Uint32, Uint32) override { return 1; }
private:
    std::string previousRng;
    std::unique_ptr<Engine> engine;
    std::optional<GAGCore::CooperativeTask> task;
    GAGGUI::Text* status;
    bool accepted = false;
};
