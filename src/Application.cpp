// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include <Toolkit.h>
#include <StringTable.h>
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

namespace {
class MinimumViewportScreen : public GAGGUI::Screen
{
public:
    void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override {}
    void paint() override {
        GAGGUI::Screen::paint();
        auto* font = GAGCore::Toolkit::getFont("standard");
        const auto text = GAGCore::Toolkit::getStringTable()->getString("[browser window too small]");
        getSurface()->drawString(std::max(8, (getW() - font->getStringWidth(text)) / 2),
                                std::max(8, getH()/2 - 10), font, text);
    }
};
}

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
    if (GAGCore::ApplicationHost::takeVisibilityChange(hidden)) screens.suspendExecution();
    if (hidden) return true;
    int width, height;
    if (GAGCore::ApplicationHost::takeViewportSize(width, height)) {
        const int oldWidth = globalContainer->gfx->getW(), oldHeight = globalContainer->gfx->getH();
        if (globalContainer->gfx->resizeViewport(width, height)) {
            screens.viewportResized(oldWidth, oldHeight, width, height);
            if ((width < 800 || height < 600) && !minimumNotice) {
                auto notice = std::make_unique<MinimumViewportScreen>();
                minimumNotice = notice.get();
                screens.push(std::move(notice), [this](GAGGUI::Screen&, int) { minimumNotice = nullptr; });
            } else if (width >= 800 && height >= 600 && minimumNotice) {
                minimumNotice->endExecute(0);
            }
        }
    }
    screens.frame(tick, events);
    if (!screens.running()) {
        if (screens.result() == GAGGUI::Screen::QUIT_APPLICATION) return false;
        mainMenu();
    }
    return true;
}

std::uint32_t Application::delay(std::uint32_t now)
{
    if (hidden) return 100;
    const auto elapsed = static_cast<std::uint32_t>(now - lastFrame);
    return screens.delay(now, elapsed < 40 ? 40 - elapsed : 0);
}
