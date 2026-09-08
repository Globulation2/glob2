// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <ScreenStack.h>
#include "SinglePlayerFlow.h"

// Shared application state. Scheduling/event polling belong to platform hosts.
class Application : public GAGCore::ApplicationHost::Loop
{
public:
    Application();
    bool frame(std::uint32_t tick, const std::vector<SDL_Event>& events) override;
    std::uint32_t delay(std::uint32_t now) override;
private:
    GAGGUI::ScreenStack screens;
    SinglePlayerFlow singlePlayer;
    std::uint32_t lastFrame = 0;
    GAGGUI::Screen* minimumNotice = nullptr; // Owned by screens.
    void mainMenu();
    void choose(int choice);
};
