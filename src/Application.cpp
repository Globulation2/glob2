// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include "FrontendTheme.h"
#include <Toolkit.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "GameSessionScreen.h"
#include "MusicTrack.h"
#include "SoundMixer.h"
#include "MainMenuScreen.h"
#include "OnlineMapsScreen.h"
#include "OnlineProfileScreen.h"
#include "QuickMatchScreen.h"
#include "MessageScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "SettingsScreen.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "LANMenuScreen.h"
#include "OnlineHubScreen.h"
#include "InviteLink.h"
#include "ui/FrontendUI.h"
#include "OnlineServices.h"
#include "RelayTransport.h"
#include <algorithm>
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
		// Present the final message for one frame before releasing graphics. A relay
		// connection still writing a Quit gets its few hundred milliseconds first
		// (bounded by Online::LINGER_MS).
		if (closing)
		{
			if (Online::lingeringRelayConnections() > 0)
				return;
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
	{
		mainMenu();
		openOnlineScreenForDevelopment();
	}
}

// GLOB2_ONLINE_SCREEN=quick-match|profile|maps opens that online screen over
// the main menu, for development and checks against an instance before the
// online hub links to it.
void Application::openOnlineScreenForDevelopment()
{
	const char *which = SDL_getenv("GLOB2_ONLINE_SCREEN");
	if (!which || !*which)
		return;
	const std::string name = which;
	if (name == "quick-match")
		screens.push(std::make_unique<QuickMatchScreen>(screens));
	else if (name == "profile")
		screens.push(std::make_unique<OnlineProfileScreen>(screens));
	else if (name == "maps")
		screens.push(std::make_unique<OnlineMapsScreen>(screens));
	else if (name == "my-maps")
		screens.push(std::make_unique<OnlineMapsScreen>(screens, OnlineMapsScreen::Tab::Mine));
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
	case MainMenuScreen::PLAY_ONLINE:
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
		screens.push(std::make_unique<OnlineHubScreen>(screens));
#endif
		break;
	case MainMenuScreen::QUIT:
		screens.stop();
		break;
	}
}

// The browser installs some data after the main menu is up (scons/web_assets.py);
// native hosts never report an installation.
void Application::installStagedAssets()
{
	const bool inMatch = dynamic_cast<GameSessionScreen *>(screens.top()) != nullptr;
	for (const auto &package : GAGCore::ApplicationHost::takeInstalledAssetPackages())
	{
		if (package == "game")
		{
			// The menu colony and the settings' building artwork use them at once.
			globalContainer->ensureGameGraphics();
		}
		else if (package == "menu-music")
		{
			if (globalContainer->loadMenuMusic() && !inMatch)
			{
				globalContainer->mix->setNextTrack(MusicTrack::Intro);
				globalContainer->mix->setNextTrack(MusicTrack::Menu);
			}
		}
		else if (package == "font-cjk")
		{
			// Same Latin glyphs; Chinese, Japanese and Korean text now has glyphs.
			GAGCore::Toolkit::reloadFonts();
			if (!inMatch)
			{
				const int width = globalContainer->gfx->getW(), height = globalContainer->gfx->getH();
				screens.viewportResized(width, height, width, height);
			}
		}
	}
}

bool Application::frame(std::uint32_t tick, const std::vector<SDL_Event> &incoming)
{
	lastFrame = tick;
	// Invite links opened while running (macOS and iOS URL events) arrive as
	// dropped "files"; they become the pending join instead.
	std::vector<SDL_Event> withoutLinks;
	const std::vector<SDL_Event> *delivered = &incoming;
	if (std::any_of(incoming.begin(), incoming.end(),
					[](const SDL_Event &event) { return event.type == SDL_DROPFILE; }))
	{
		for (const auto &event : incoming)
		{
			if (event.type == SDL_DROPFILE && event.drop.file &&
				Online::acceptDroppedText(event.drop.file))
			{
				SDL_free(event.drop.file);
				continue;
			}
			withoutLinks.push_back(event);
		}
		delivered = &withoutLinks;
	}
	const auto &events = *delivered;
	Online::pump();
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
		auto input = events;
		std::erase_if(input, [](const SDL_Event &event) { return event.type == SDL_QUIT; });
		shutdownScreens.frame(tick, input);
		return shutdownScreens.running();
	}
	installStagedAssets();
	screens.frame(tick, events);
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
	// An invite link (at launch or while running) opens the online hub from the
	// main menu; the hub consumes it.
	static std::uint32_t hubOpenedAt = 0;
	if (Online::pendingJoin() && dynamic_cast<MainMenuScreen *>(screens.top()) && tick - hubOpenedAt > 2000)
	{
		hubOpenedAt = tick;
		screens.push(std::make_unique<OnlineHubScreen>(screens));
	}
#endif
	if (!screens.running())
	{
		if (screens.result() == GAGGUI::Screen::QUIT_APPLICATION)
		{
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
