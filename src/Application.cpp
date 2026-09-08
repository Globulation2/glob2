// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "SettingsScreen.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "LANMenuScreen.h"
#include "YOGLoginScreen.h"
#include "YOGClient.h"

Application::Application() : screens(*globalContainer->gfx), singlePlayer(screens)
{
    if (globalContainer->replaying) singlePlayer.replay(globalContainer->replayFileName);
    else mainMenu();
}

void Application::mainMenu()
{
    // Rebuild translated labels after returning from settings.
    screens.push(std::make_unique<MainMenuScreen>(), [this](GAGGUI::Screen&, int choice) { choose(choice); });
}

void Application::choose(int choice)
{
    switch (choice) {
    case MainMenuScreen::CAMPAIGN:
        screens.push(std::make_unique<CampaignMainMenu>(screens)); break;
    case MainMenuScreen::TUTORIAL: {
        Campaign campaign;
        const bool saved = campaign.load("games/Tutorial_Campaign.txt");
        auto menu = std::make_unique<CampaignMenuScreen>(saved ? "games/Tutorial_Campaign.txt" : "campaigns/Tutorial_Campaign.txt", screens);
        if (!saved) menu->setNewCampaign();
        screens.push(std::move(menu));
        break;
    }
    case MainMenuScreen::CUSTOM: singlePlayer.custom(); break;
    case MainMenuScreen::LOAD_GAME: singlePlayer.load(); break;
    case MainMenuScreen::GAME_SETUP: screens.push(std::make_unique<SettingsScreen>()); break;
    case MainMenuScreen::CREDITS: screens.push(std::make_unique<CreditScreen>()); break;
    case MainMenuScreen::EDITOR: screens.push(std::make_unique<EditorMainMenu>(screens)); break;
    case MainMenuScreen::MULTIPLAYERS_LAN: screens.push(std::make_unique<LANMenuScreen>()); break;
    case MainMenuScreen::MULTIPLAYERS_YOG:
        screens.push(std::make_unique<YOGLoginScreen>(std::make_shared<YOGClient>())); break;
    case MainMenuScreen::QUIT: screens.stop(); break;
    }
}

bool Application::frame(std::uint32_t tick, const std::vector<SDL_Event>& events)
{
    lastFrame = tick;
    screens.frame(tick, events);
    if (!screens.running()) {
        if (screens.result() == GAGGUI::Screen::QUIT_APPLICATION) return false;
        mainMenu();
    }
    return true;
}

std::uint32_t Application::delay(std::uint32_t now)
{
    const auto elapsed = static_cast<std::uint32_t>(now - lastFrame);
    return screens.delay(now, elapsed < 40 ? 40 - elapsed : 0);
}
