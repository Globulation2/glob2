// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include "Application.h"
#include "ui/ThemeCatalog.h"
#include "FrontendTheme.h"
#include <Toolkit.h>
#include <AssetLoader.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "GameSessionScreen.h"
#include "MusicTrack.h"
#include "SoundMixer.h"
#include "MainMenuScreen.h"
#include "CustomGameScreen.h"
#include "MessageScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "SettingsScreen.h"
#include "RecordingFilesScreen.h"
#include "KeyboardManager.h"
#include "GameGUIKeyActions.h"
#include "CreditScreen.h"
#include "EditorMainMenu.h"
#include "LANMenuScreen.h"
#include "OnlineHubScreen.h"
#include "InviteLink.h"
#include "OnlineHandoff.h"
#include <FormatableString.h>
#include "ui/FrontendUI.h"
#include "OnlineServices.h"
#include "RelayTransport.h"
#include <algorithm>
#include <optional>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

namespace
{
// The recording hotkey works on every screen, so its single-key bindings from the
// game layout are matched here, before any screen sees the key.
bool isRecordingShortcut(const SDL_KeyboardEvent &key)
{
	static std::vector<KeyPress> bindings;
	static std::optional<unsigned> loaded;
	if (loaded != KeyboardManager::revision())
	{
		bindings.clear();
		KeyboardManager layout(GameGUIShortcuts);
		for (const auto &shortcut : layout.getKeyboardShortcuts())
			if (shortcut.getAction() == GameGUIKeyActions::ToggleRecording && shortcut.getKeyPressCount() == 1)
				bindings.push_back(KeyPress(shortcut.getKeyPress(0), true));
		loaded = KeyboardManager::revision();
	}
	const KeyPress pressed(key, true);
	return std::find(bindings.begin(), bindings.end(), pressed) != bindings.end();
}

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
			if (Online::lingeringRelayConnections() > 0 ||
				GAGCore::Recording::recorder().status().state == GAGCore::Recording::State::Finalizing)
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

namespace
{
// Themes are read once every data directory is known (after argument parsing).
std::unique_ptr<FrontendTheme> themedFrontend()
{
	Glob2UI::applyThemes(globalContainer->settings.menuTheme, globalContainer->settings.gameTheme);
	return std::make_unique<FrontendTheme>();
}
} // namespace

Application::Application()
	: frontend(themedFrontend()), screens(*globalContainer->gfx),
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
		screens.push(std::make_unique<SettingsScreen>(&screens));
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
    GAGCore::Toolkit::pollAssets();
    for (const auto &package : GAGCore::ApplicationHost::takeInstalledAssetPackages())
    {
        GAGCore::Toolkit::assets().invalidate();
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
		else if (package == "translations")
		{
			// Core had only each language's name and code; English stood in.
			auto *strings = GAGCore::Toolkit::getStringTable();
			if (strings->load("data/texts.list.txt"))
				strings->setLang(strings->getLangCode(globalContainer->settings.language));
			if (!inMatch)
			{
				const int width = globalContainer->gfx->getW(), height = globalContainer->gfx->getH();
				screens.viewportResized(width, height, width, height);
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
	if (GAGCore::Recording::takeFilesRequest()) screens.push(std::make_unique<RecordingFilesScreen>());
	// Invite links opened while running (macOS and iOS URL events) arrive as
	// dropped "files"; they become the pending join instead. SDL3 owns the
	// event's text, so nothing is freed here.
	std::vector<SDL_Event> withoutLinks;
	const std::vector<SDL_Event> *delivered = &incoming;
	if (std::any_of(incoming.begin(), incoming.end(),
					[](const SDL_Event &event) { return event.type == SDL_EVENT_DROP_FILE; }))
	{
		for (const auto &event : incoming)
		{
			if (event.type == SDL_EVENT_DROP_FILE && event.drop.data &&
				Online::acceptDroppedText(event.drop.data))
				continue;
			withoutLinks.push_back(event);
		}
		delivered = &withoutLinks;
	}
	const auto &events = *delivered;
	Online::pump();
	auto filtered = events;
	std::erase_if(filtered,
				  [](const SDL_Event &event)
				  {
					  if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
						  isRecordingShortcut(event.key) &&
						  (GAGCore::Recording::available() || GAGCore::Recording::recorder().active()))
					  {
						  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
							  GAGCore::Recording::toggle();
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
	installStagedAssets();
	screens.frame(tick, filtered);
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
	// An invite link (at launch or while running) opens the online hub from the
	// main menu; the hub consumes it.
	static std::uint32_t hubOpenedAt = 0;
	if (Online::pendingJoin() && dynamic_cast<MainMenuScreen *>(screens.top()) && tick - hubOpenedAt > 2000)
	{
		hubOpenedAt = tick;
		screens.push(std::make_unique<OnlineHubScreen>(screens));
	}
	// A running frontend accepts a new launch immediately. Matches and rooms keep
	// their current session until the player leaves it.
	auto *custom = dynamic_cast<CustomGameScreen *>(screens.top());
	const bool mapLaunchReady = dynamic_cast<MainMenuScreen *>(screens.top()) ||
		dynamic_cast<OnlineHubScreen *>(screens.top()) || (custom && !custom->roomMode());
	if (Online::pendingMapPlay() && mapLaunchReady)
	{
		if (Online::pendingMapPlay()->mode == Online::MapPlayRequest::Mode::Local)
		{
			auto request = Online::takePendingMapPlay();
			if (custom)
				custom->loadCatalogMap(*request);
			else
				singlePlayer.custom(request);
		}
		else if (!dynamic_cast<OnlineHubScreen *>(screens.top()))
			screens.push(std::make_unique<OnlineHubScreen>(screens));
	}

#endif
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
