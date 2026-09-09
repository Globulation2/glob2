// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include <Toolkit.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include "MessageScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "SettingsScreen.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "LANMenuScreen.h"
#include "YOGLoginScreen.h"
#include "YOGClient.h"
#include <GUIText.h>
#include <GUIButton.h>
#include "SoundMixer.h"

namespace {
// Keep graphics and the host alive until final writes have reached storage.
// The gameplay stack is destroyed first, so its destructor writes are included.
class ShutdownScreen : public Glob2Screen
{
    GAGGUI::Text* status;
    GAGGUI::TextButton* retry;
    GAGGUI::TextButton* leave;
    std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
    bool closing = false;
    void close() {
        persistence.reset();
        closing = true;
        retry->visible = leave->visible = false;
        status->setText(GAGCore::Toolkit::getStringTable()->getString("[game closed]"));
    }
    void failed() {
        persistence.reset();
        status->setText(GAGCore::Toolkit::getStringTable()->getString("[shutdown save failed]"));
        retry->visible = leave->visible = true;
    }
    void save() {
        retry->visible = leave->visible = false;
        status->setText(GAGCore::Toolkit::getStringTable()->getString("[saving to storage]"));
        try {
            if (GAGCore::ApplicationHost::storageRestoreFailed() || !globalContainer->settings.save()) {
                failed(); return;
            }
            persistence = GAGCore::ApplicationHost::persistStorage();
            if (!persistence) failed();
        } catch (const std::exception&) { failed(); }
    }
public:
    ShutdownScreen() {
        auto& strings = *GAGCore::Toolkit::getStringTable();
        status = new GAGGUI::Text(20, 230, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
            "standard", strings.getString("[saving to storage]"));
        retry = new GAGGUI::TextButton(20, 340, 280, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
            "menu", strings.getString("[retry save]"), 0);
        leave = new GAGGUI::TextButton(330, 340, 280, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
            "menu", strings.getString("[quit without saving]"), 1);
        addWidget(status); addWidget(retry); addWidget(leave);
        save();
    }
    void onAction(GAGGUI::Widget*, GAGGUI::Action action, int choice, int) override {
        if (persistence || closing || action != GAGGUI::BUTTON_RELEASED) return;
        if (choice == 0) save();
        else if (choice == 1) close();
    }
    void onTimer(Uint32) override {
        // Present the final message for one frame before releasing graphics.
        if (closing) { endExecute(0); return; }
        if (!persistence) return;
        const auto state = persistence->state();
        if (state == GAGCore::ApplicationHost::PersistenceState::Failed) failed();
        else if (state == GAGCore::ApplicationHost::PersistenceState::Succeeded) close();
    }
};

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

Application::Application() : screens(*globalContainer->gfx), shutdownScreens(*globalContainer->gfx), singlePlayer(screens)
{
    if (GAGCore::ApplicationHost::storageRestoreFailed()) {
        auto& strings = *GAGCore::Toolkit::getStringTable();
        screens.push(std::make_unique<MessageScreen>(strings.getString("[storage restore failed]"),
            std::vector<std::string>{strings.getString("[continue]")}),
            [this](GAGGUI::Screen&, int) { mainMenu(); });
    } else if (globalContainer->replaying) singlePlayer.replay(globalContainer->replayFileName);
    else {
        mainMenu();
        RecoveryStore store(*GAGCore::Toolkit::getFileManager());
        if (RecoveryStore::enabled() && store.pending()) {
            auto& strings = *GAGCore::Toolkit::getStringTable();
            screens.push(std::make_unique<MessageScreen>(strings.getString("[recovery available]"),
                std::vector<std::string>{strings.getString("[recover game]"), strings.getString("[discard recovery]"), strings.getString("[later]")}),
                [this](GAGGUI::Screen&, int choice) {
                    if (choice == 0) singlePlayer.recover();
                    else if (choice == 1) {
                        RecoveryStore store(*GAGCore::Toolkit::getFileManager());
                        if (!store.dismiss()) {
                            auto& strings = *GAGCore::Toolkit::getStringTable();
                            screens.push(std::make_unique<MessageScreen>(strings.getString("[recovery discard failed]"),
                                std::vector<std::string>{strings.getString("[ok]")}));
                        }
                    }
                });
        }
    }
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
    case MainMenuScreen::MULTIPLAYERS_LAN: screens.push(std::make_unique<LANMenuScreen>(screens)); break;
    case MainMenuScreen::MULTIPLAYERS_YOG:
        screens.push(std::make_unique<YOGLoginScreen>(screens, std::make_shared<YOGClient>())); break;
    case MainMenuScreen::QUIT: screens.stop(); break;
    }
}

bool Application::frame(std::uint32_t tick, const std::vector<SDL_Event>& events)
{
    lastFrame = tick;
    // Convert browser visibility into the same frame-boundary events as native
    // lifecycle callbacks. Hidden frames still drain lifecycle events so that
    // ScreenStack can freeze its clock and resume without catch-up.
    auto frameEvents = events;
    if (GAGCore::ApplicationHost::takeVisibilityChange(hidden)) {
        screens.suspendExecution();
        SDL_Event event{};
        event.type = hidden ? SDL_APP_WILLENTERBACKGROUND : SDL_APP_DIDENTERFOREGROUND;
        frameEvents.push_back(event);
    }
    for (const auto& event : frameEvents) {
        if (!globalContainer->mix) break;
        if (event.type == SDL_APP_WILLENTERBACKGROUND || event.type == SDL_APP_DIDENTERBACKGROUND)
            globalContainer->mix->setSuspended(true);
        else if (event.type == SDL_APP_DIDENTERFOREGROUND)
            globalContainer->mix->setSuspended(false);
    }
    int width, height;
    if (!hidden && GAGCore::ApplicationHost::takeViewportSize(width, height)) {
        const int oldWidth = globalContainer->gfx->getW(), oldHeight = globalContainer->gfx->getH();
        if (globalContainer->gfx->resizeViewport(width, height)) {
            screens.viewportResized(oldWidth, oldHeight, width, height);
            shutdownScreens.viewportResized(oldWidth, oldHeight, width, height);
            if (!quitting && (width < 800 || height < 600) && !minimumNotice) {
                auto notice = std::make_unique<MinimumViewportScreen>();
                minimumNotice = notice.get();
                screens.push(std::move(notice), [this](GAGGUI::Screen&, int) { minimumNotice = nullptr; });
            } else if (width >= 800 && height >= 600 && minimumNotice) {
                minimumNotice->endExecute(0);
            }
        }
    }
    if (quitting) {
        // Repeated window-close events must not bypass a pending write or its
        // explicit failure decision. Closing a browser tab remains abrupt.
        auto input = frameEvents;
        std::erase_if(input, [](const SDL_Event& event) { return event.type == SDL_QUIT; });
        shutdownScreens.frame(tick, input);
        return shutdownScreens.running();
    }
    screens.frame(tick, frameEvents);
    if (!screens.running()) {
        if (screens.result() == GAGGUI::Screen::QUIT_APPLICATION) {
            quitting = true;
            minimumNotice = nullptr;
            shutdownScreens.push(std::make_unique<ShutdownScreen>());
            return true;
        }
        mainMenu();
    }
    return true;
}

std::uint32_t Application::delay(std::uint32_t now)
{
    if (hidden) return 100;
    const auto elapsed = static_cast<std::uint32_t>(now - lastFrame);
    return (quitting ? shutdownScreens : screens).delay(now, elapsed < 40 ? 40 - elapsed : 0);
}
