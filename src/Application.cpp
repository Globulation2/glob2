// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include "Application.h"
#include "FrontendTheme.h"
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
#include "ui/FrontendUI.h"
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

namespace
{
// Keep graphics and the host alive until final writes have reached storage.
// The gameplay stack is destroyed first, so its destructor writes are included.
class ShutdownScreen : public Glob2UI::Screen
{
	std::string status;
	bool showActions = false;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	bool closing = false;
	void show(const char *key, bool actions)
	{
		status = Glob2UI::tr(key);
		showActions = actions;
		invalidate();
	}
	void close()
	{
		persistence.reset();
		closing = true;
		show("[game closed]", false);
	}
	void failed()
	{
		persistence.reset();
		show("[shutdown save failed]", true);
	}
	void save()
	{
		show("[saving to storage]", false);
		try
		{
			if (GAGCore::ApplicationHost::storageRestoreFailed() ||
				!globalContainer->settings.save())
			{
				failed();
				return;
			}
			persistence = GAGCore::ApplicationHost::persistStorage();
			if (!persistence)
				failed();
		}
		catch (const std::exception &)
		{
			failed();
		}
	}

  public:
	ShutdownScreen() { save(); }
	Glob2UI::Element build(const Glob2UI::Presentation &p) override
	{
		using namespace Glob2UI;
		std::vector<MenuAction> choices;
		if (showActions && !persistence && !closing)
		{
			choices.push_back({"retry", tr("[retry save]"), [this] { save(); }, true});
			choices.push_back({"leave", tr("[quit without saving]"), [this] { close(); }});
		}
		return page("", center(paragraph(status, {FontRole::Body, false, TextAlign::Center})),
					actions(std::move(choices), p), p, 480);
	}
	void onTimer(Uint32) override
	{
		// Present the final message for one frame before releasing graphics.
		if (closing)
		{
            if (GAGCore::Recording::recorder().status().state == GAGCore::Recording::State::Finalizing) return;
			endExecute(0);
			return;
		}
		if (!persistence)
			return;
		const auto state = persistence->state();
		if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
			failed();
		else if (state == GAGCore::ApplicationHost::PersistenceState::Succeeded)
			close();
	}
};

} // namespace

Application::Application()
	: frontend(std::make_unique<FrontendTheme>()), screens(*globalContainer->gfx),
	  shutdownScreens(*globalContainer->gfx), singlePlayer(screens)
{
	if (GAGCore::ApplicationHost::storageRestoreFailed())
	{
		auto &strings = *GAGCore::Toolkit::getStringTable();
		screens.push(std::make_unique<MessageScreen>(
						 strings.getString("[storage restore failed]"),
						 std::vector<std::string>{strings.getString("[continue]")}),
					 [this](GAGGUI::Screen &, int) { mainMenu(); });
	}
	else if (globalContainer->replaying)
		singlePlayer.replay(globalContainer->replayFileName);
	else
		mainMenu();
}

Application::~Application() = default;

void Application::mainMenu()
{
	// Rebuild translated labels after returning from settings.
	screens.push(std::make_unique<MainMenuScreen>(),
				 [this](GAGGUI::Screen &, int choice) { choose(choice); });
}

void Application::choose(int choice)
{
	switch (choice)
	{
	case MainMenuScreen::CAMPAIGN:
		screens.push(std::make_unique<CampaignMainMenu>(screens));
		break;
	case MainMenuScreen::TUTORIAL:
	{
		Campaign campaign;
		const bool saved = campaign.load("games/Tutorial_Campaign.txt");
		auto menu = std::make_unique<CampaignMenuScreen>(
			saved ? "games/Tutorial_Campaign.txt" : "campaigns/Tutorial_Campaign.txt", screens);
		if (!saved)
			menu->setNewCampaign();
		screens.push(std::move(menu));
		break;
	}
	case MainMenuScreen::CUSTOM:
		singlePlayer.custom();
		break;
	case MainMenuScreen::LOAD_GAME:
		singlePlayer.load();
		break;
	case MainMenuScreen::GAME_SETUP:
		screens.push(std::make_unique<SettingsScreen>());
		break;
	case MainMenuScreen::CREDITS:
		screens.push(std::make_unique<CreditScreen>());
		break;
	case MainMenuScreen::EDITOR:
		screens.push(std::make_unique<EditorMainMenu>(screens));
		break;
	case MainMenuScreen::MULTIPLAYERS_LAN:
		screens.push(std::make_unique<LANMenuScreen>(screens));
		break;
	case MainMenuScreen::MULTIPLAYERS_YOG:
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
		screens.push(std::make_unique<YOGLoginScreen>(screens, std::make_shared<YOGClient>()));
#endif
		break;
	case MainMenuScreen::QUIT:
		screens.stop();
		break;
	}
}

bool Application::frame(std::uint32_t tick, const std::vector<SDL_Event> &events)
{
	lastFrame = tick;
    auto filtered = events;
    std::erase_if(filtered, [](const SDL_Event& event) {
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && event.key.key == SDLK_R &&
            (event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT) && GAGCore::Recording::supported()) {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) GAGCore::Recording::toggle();
            return true;
        }
        return false;
    });
	if (GAGCore::ApplicationHost::takeVisibilityChange(hidden))
		screens.suspendExecution();
	if (hidden)
		return true;
	int width, height;
	if (GAGCore::ApplicationHost::takeViewportSize(width, height))
	{
		const int oldWidth = globalContainer->gfx->getW(), oldHeight = globalContainer->gfx->getH();
		if (globalContainer->gfx->resizeViewport(width, height))
		{
			width = globalContainer->gfx->getW();
			height = globalContainer->gfx->getH();
			screens.viewportResized(oldWidth, oldHeight, width, height);
			shutdownScreens.viewportResized(oldWidth, oldHeight, width, height);
		}
	}
	if (quitting)
	{
		// Repeated window-close events must not bypass a pending write or its
		// explicit failure decision. Closing a browser tab remains abrupt.
		auto input = filtered;
		std::erase_if(input, [](const SDL_Event &event) { return event.type == SDL_EVENT_QUIT; });
		shutdownScreens.frame(tick, input);
		return shutdownScreens.running();
	}
	screens.frame(tick, filtered);
	if (!screens.running())
	{
		if (screens.result() == GAGGUI::Screen::QUIT_APPLICATION)
		{
            GAGCore::Recording::recorder().stop();
			quitting = true;
			shutdownScreens.push(std::make_unique<ShutdownScreen>());
			return true;
		}
		mainMenu();
	}
	return true;
}

std::uint32_t Application::delay(std::uint32_t now)
{
	if (hidden)
		return 100;
	const auto elapsed = static_cast<std::uint32_t>(now - lastFrame);
	return (quitting ? shutdownScreens : screens).delay(now, elapsed < 40 ? 40 - elapsed : 0);
}
