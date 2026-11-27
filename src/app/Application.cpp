// SPDX-License-Identifier: GPL-3.0-or-later
#include "hive/LocalCommander.h"
#include "hive/GameStats.h"
#include "EndGameScreen.h"
#include <GameplayRecording.h>
#include "Application.h"
#include <StartupPhase.h>
#include "mcp/UIService.h"
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
#include "HandPreferenceScreen.h"
#include "AudioPreferenceScreen.h"
#include "AgeQuestionScreen.h"
#include "AgeGate.h"
#include "PortalPolicy.h"
#include "tutorial/TutorialModel.h"
#include "IntroductionOfferScreen.h"
#include "CustomGameScreen.h"
#include "MessageScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "SettingsScreen.h"
#include "RecordingFilesScreen.h"
#include "ui/RecordingNotice.h"
#include "KeyboardManager.h"
#include "GameGUIKeyActions.h"
#include "CreditScreen.h"
#include "ProblemReportScreen.h"
#include "CrashCapture.h"
#include "CompileProfile.h"
#include "DeviceAdaptationSession.h"
#include <iostream>
#include "EditorEntry.h"

#include "LANMenuScreen.h"
#include "OnlineHubScreen.h"
#include "InviteLink.h"
#include "OnlineHandoff.h"
#include <FormatableString.h>
#include "ui/FrontendUI.h"
#include "OnlineServices.h"
#include "RelayTransport.h"
#include <algorithm>
#include <cstdlib>
#include <optional>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>

namespace
{
// First-screen frames drawn but not shown after loading (Application::Application).
constexpr unsigned MenuSettleFrames = 30;
}
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
#if GLOB2_HAS_ONLINE
			if (Online::lingeringRelayConnections() > 0)
				return;
#endif
			if (GAGCore::Recording::recorder().status().state == GAGCore::Recording::State::Finalizing)
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
	// A web portal's restrictions apply before the first screen can open a link.
	PortalPolicy::current();
#if GLOB2_DESKTOP_MCP
    mcp = std::make_unique<MCP::UIService>(*globalContainer->gfx);
	mcp->domainHandler = [this](const std::string &tool, const MCP::Json &args)
	{
		MCP::Result result;
		// After a match its results screen still holds the finished game: game_stats
		// reads every team there, as the screen charts them.
		if (tool == "game_stats")
			if (const auto *results = dynamic_cast<const EndGameScreen *>(screens.top()))
			{
				result.data = Hive::finishedMatchStats(*results, args);
				return result;
			}
		result.data = Hive::localCall(tool, args);
		return result;
	};
	mcp->inspectionHosts = [this] {
		return screens.top() ? screens.top()->inspectionHosts() : std::vector<GAGGUI::ui::Host *>{};
	};
	mcp->inspectionScreen = [this] {
		return screens.top() ? std::string(screens.top()->recordingId())
							 : std::string("unavailable");
	};
	// game_* outside a match names the screen the player is on.
	Hive::setScreenSource([this] { return screens.top() ? std::string(screens.top()->recordingId()) : std::string(); });
	mcp->configure(globalContainer->settings.mcpActive(), globalContainer->settings.mcpActivePort());
	// Enabling MCP makes this client assisted, whichever tools an agent then uses:
	// it quick-matches only against other players with MCP enabled.
	Online::setAssistedSource([] { return globalContainer->settings.mcpActive(); });
#endif
	// The first screen's first frames after loading (the main menu and the
	// colony behind it, or a first-run question in front of it) are drawn but
	// not shown, so every renderer streams terrain, artwork and atlases and
	// settles before the player sees anything; the last loading-screen frame
	// stays up meanwhile (about half a second), as the game's introduction does.
	if (globalContainer->gfx && !globalContainer->runNoX)
		globalContainer->gfx->holdPresentation(MenuSettleFrames);
	if (GAGCore::ApplicationHost::storageRestoreFailed())
	{
		auto &strings = *GAGCore::Toolkit::getStringTable();
		screens.push(std::make_unique<MessageScreen>(
						 strings.getString("[storage restore failed]"),
						 std::vector<std::string>{strings.getString("[continue]")}),
					 [this](GAGGUI::Screen &, int) { mainMenu(); });
	}
	else if (std::getenv("GLOB2_GENERATOR_STUDIO"))
		singlePlayer.generatorStudio();
	else if (std::getenv("GLOB2_STUDIO_PLAYTEST"))
		singlePlayer.studio();
	else if (globalContainer->replaying)
		singlePlayer.replay(globalContainer->replayFileName);
	else
		mainMenu();
}

Application::~Application()
{
#if GLOB2_DESKTOP_MCP
	// The source reads this application's settings; harnesses build several.
	Online::setAssistedSource({});
#endif
}

bool Application::mainMenu()
{
	if (PortalPolicy::landsInFirstLesson(globalContainer->settings.introductionOffered,
										 GAGCore::ApplicationHost::storageRestoreFailed()))
	{
		// Portals want new players in gameplay at once. Like accepting the
		// first-run offer, this marks the introduction offered; a failed save only
		// means the next visit starts in the lesson again, so it never blocks.
		globalContainer->settings.introductionOffered = true;
		try
		{
			if (globalContainer->settings.save())
				GAGCore::ApplicationHost::persistStorage();
		}
		catch (const std::exception &)
		{
		}
		// Nothing lies beneath the lesson: leaving it asks the questions it
		// skipped, then opens the main menu (and the lesson list when chosen).
		singlePlayer.introduction([this](int navigation)
		{
			if (mainMenu() && navigation == Tutorial::Lessons)
				singlePlayer.lessons();
		});
		return false;
	}
#ifdef GLOB2_MOBILE
	const bool mobileAudio = true;
#else
	const bool mobileAudio = Glob2UI::touchPresentation();
#endif
	if (mobileAudio && !globalContainer->settings.audioPreferenceChosen)
	{
		screens.push(std::make_unique<AudioPreferenceScreen>(),
			[this](GAGGUI::Screen &, int) { mainMenu(); });
		return false;
	}
	if (HandPreferenceScreen::needed())
	{
		screens.push(std::make_unique<HandPreferenceScreen>(&screens),
			[this](GAGGUI::Screen &, int) { mainMenu(); });
		return false;
	}
    if (!globalContainer->settings.introductionOffered && !GAGCore::ApplicationHost::storageRestoreFailed())
    {
        screens.push(std::make_unique<IntroductionOfferScreen>(),
            [this](GAGGUI::Screen &,int result) { mainMenu(); if (result==1) singlePlayer.introduction(); });
        return false;
    }
	// Rebuild translated labels after returning from settings.
	screens.push(std::make_unique<MainMenuScreen>(),
				 [this](GAGGUI::Screen &, int choice) { choose(choice); });
	return true;
}

void Application::choose(int choice)
{
	switch (choice)
	{
	case MainMenuScreen::CAMPAIGN:
		screens.push(std::make_unique<CampaignMainMenu>(screens));
		break;
    case MainMenuScreen::TUTORIAL:
        singlePlayer.lessons();
        break;
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
	case MainMenuScreen::REPORT_PROBLEM:
		screens.push(std::make_unique<ProblemReportScreen>());
		break;
	case MainMenuScreen::EDITOR:
		// A compile_profile=core build has no editor; the button then only explains why.
		if (!EditorEntry::open(screens))
			std::cerr << CompileProfile::missing("The map editor") << std::endl;
		break;
	case MainMenuScreen::MULTIPLAYERS_LAN:
#if GLOB2_HAS_LAN
		withAgeBand([this] { screens.push(std::make_unique<LANMenuScreen>(screens)); });
#endif
		break;
	case MainMenuScreen::PLAY_ONLINE:
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE) && GLOB2_HAS_ONLINE
		if (PortalPolicy::onlineSignIn())
			withAgeBand([this] { screens.push(std::make_unique<OnlineHubScreen>(screens)); });
#endif
		break;
	case MainMenuScreen::QUIT:
		if (MainMenuScreen::offersQuit())
			screens.stop();
		break;
	}
}

// Before playing with others: while the age band is unknown, ask the platform's
// age signal (Declared Age Range on Apple builds), then the one-time question.
// Every answer, "Not now" included, continues to `next`: LAN and online play
// stay available, only their chat depends on the band (src/app/AgeGate.h).
void Application::withAgeBand(std::function<void()> next)
{
	if (!AgeGate::shouldAsk(AgeGate::local()))
	{
		next();
		return;
	}
	// A slow platform answer (Play Age Signals is a network call) comes back to the
	// main menu, which is up again meanwhile: only the newest request continues, and
	// only while the menu is still what the player sees.
	const std::uint64_t request = ++ageRequest;
	AgeGate::requestPlatformSignal([this, request, next = std::move(next)](AgeGate::Signal signal) {
		if (request != ageRequest)
			return;
		if (auto *top = screens.top(); top && !dynamic_cast<MainMenuScreen *>(top))
			return;
		const AgeGate::Local current = AgeGate::local();
		const AgeGate::Local updated = AgeGate::applySignal(current, signal, AgeGate::platformSource());
		if (updated.band != current.band)
			AgeGate::store(updated);
		if (!AgeGate::shouldAsk(updated))
		{
			next();
			return;
		}
		screens.push(std::make_unique<AgeQuestionScreen>(), [next](GAGGUI::Screen &, int) { next(); });
	});
}

// Crash reports are opt-in (docs/multiplayer/crash-reports.md#consent): a
// player who may send is asked, unless they chose Always send; players under
// 13, or who never said their age, are never asked and keep the crash for Save
// to file.
void Application::offerCrashReports()
{
	const auto userDir = ProblemReport::userDirectory();
	if (CrashCapture::pending(userDir).empty())
		return;
	const auto consent = globalContainer->settings.alwaysSendCrashReports ? CrashCapture::Consent::Always
																		   : CrashCapture::Consent::Ask;
	switch (CrashCapture::decide(ProblemReport::currentSendPolicy(), consent))
	{
	case CrashCapture::Action::KeepLocal:
		return;
	case CrashCapture::Action::Send:
		CrashCapture::queueAll(userDir);
		return;
	case CrashCapture::Action::Ask:
		break;
	}
	auto &strings = *GAGCore::Toolkit::getStringTable();
	auto prompt = std::make_unique<MessageScreen>(
		strings.getString("[crash prompt title]"), strings.getString("[crash prompt message]"),
		std::vector<std::string>{strings.getString("[crash prompt send]"), strings.getString("[crash prompt always]"),
								 strings.getString("[crash prompt dont send]")});
	screens.push(std::move(prompt),
				 [userDir](GAGGUI::Screen &, int choice)
				 {
					 if (choice == 1)
					 {
						 globalContainer->settings.alwaysSendCrashReports = true;
						 if (!globalContainer->settings.save())
							 std::cerr << "Could not save the crash report choice." << std::endl;
					 }
					 if (choice == 0 || choice == 1)
						 CrashCapture::queueAll(userDir);
					 else if (choice == 2)
						 CrashCapture::discardAll(userDir);
					 // Quitting with the question open leaves them for next time.
				 });
}

// The browser installs some data after the main menu is up (buildsys/web_assets.py);
// native hosts never report an installation.
void Application::installStagedAssets()
{
    // Packages remain in the host queue until the replacement surface exists;
    // translation/font relayout and artwork preparation need its dimensions.
    if (!globalContainer->gfx->getSDLSurface()) return;
	const bool inMatch = dynamic_cast<GameSessionScreen *>(screens.top()) != nullptr;
    GAGCore::Toolkit::pollAssets();
    for (const auto &package : GAGCore::ApplicationHost::takeInstalledAssetPackages())
    {
        GAGCore::Toolkit::assets().invalidate();
        if (package == "hd" || package.starts_with("hd-terrain-"))
        {
            GAGCore::Sprite::refreshHighResolutionSprite(
                package == "hd" ? std::string{} : "data/gfx/" + package.substr(3));
        }
		else if (package == "game")
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
	if (!framed)
	{
		framed = true;
		GAGCore::StartupPhase::mark("application:first-frame");
	}
	lastFrame = tick;
#if GLOB2_HAS_RECORDING
	if (GAGCore::Recording::takeFilesRequest()) screens.push(std::make_unique<RecordingFilesScreen>());
#endif
	Glob2UI::deliverFinishedRecordings();
	// Invite links opened while running (macOS and iOS URL events) arrive as
	// dropped "files"; they become the pending join instead. SDL3 owns the
	// event's text, so nothing is freed here.
	std::vector<SDL_Event> withoutLinks;
	const std::vector<SDL_Event> *delivered = &incoming;
#if GLOB2_HAS_ONLINE
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
#endif
	const auto &events = *delivered;
#if GLOB2_HAS_ONLINE
	Online::pump();
#endif
	AgeGate::pollPlatformSignal();
	// Memory signals and the VRAM probe, also while hidden: a trim in the
	// background is when the system judges the app's size.
	DeviceAdaptation::serviceFrame(events);
	auto filtered = events;
	if (GAGCore::ApplicationHost::takeVisibilityChange(hidden))
	{
		globalContainer->gfx->resetRenderPacing();
		screens.suspendExecution();
	}
	// A web portal holds the game while an ad plays between matches, or while its
	// SDK pauses it (ApplicationHost::hostHeld): like a hidden page, no screen
	// frame runs, so the simulation and input stop until the portal releases it.
	if (const bool nowHeld = GAGCore::ApplicationHost::hostHeld(); nowHeld != held)
	{
		held = nowHeld;
		globalContainer->gfx->resetRenderPacing();
		screens.suspendExecution();
	}
#if GLOB2_DESKTOP_MCP
	if (hidden)
		screens.pollInputReceipts();
	mcp->configure(!quitting && globalContainer->settings.mcpActive(),
				   globalContainer->settings.mcpActivePort());
	const auto mcpAvailable = [this]
	{
		auto *window = globalContainer->gfx->getWindow();
		return !hidden && !quitting && !GAGCore::ApplicationHost::nativeDialogOpen() && window &&
			   !(SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
	};
	// Merge human/remote ownership before ordinary dispatch. Request admission
	// and cleanup delivery wait until the screen stack settles human actions.
	mcp->pump(filtered, mcpAvailable(), false);
#endif
	const auto recordingShortcut = [](const SDL_Event &event)
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
	};
	std::erase_if(filtered, recordingShortcut);
	if (hidden || held)
	{
#if GLOB2_DESKTOP_MCP
		// Visibility suspension already cancels buffered consumer receipts.
		// Clear hidden release state without a screen update or simulation tick;
		// never acknowledge a previously pending buffered request here.
		std::vector<SDL_Event> releases;
		mcp->drainCleanup(releases);
		if (!releases.empty())
		{
			const auto complete = mcp->dispatchCompletion();
			if (auto *screen = screens.top())
			{
				for (const auto &event : releases)
					screen->handleExecutionEvent(event);
				if (screen->defersExecutionInput())
					screen->cancelExecutionInput();
			}
			complete(releases.size(), releases.size());
		}
		else if (!mcp->awaitingInputDispatch())
		{
			// A release with no held inputs has no event range to acknowledge.
			mcp->dispatchCompletion()(0, 0);
		}
#endif
		return true;
	}
	int width, height;
	if (GAGCore::ApplicationHost::takeViewportSize(width, height))
	{
		pendingViewportWidth = width;
		pendingViewportHeight = height;
	}
	// A surface handover can temporarily retire the SDL window. Keep the
	// latest browser request until the replacement can accept it.
	if (pendingViewportWidth > 0 && pendingViewportHeight > 0 && globalContainer->gfx->getSDLSurface())
	{
		const int oldWidth = globalContainer->gfx->getW(), oldHeight = globalContainer->gfx->getH();
		if (globalContainer->gfx->resizeViewport(pendingViewportWidth, pendingViewportHeight))
		{
			pendingViewportWidth = pendingViewportHeight = 0;
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
#ifdef __EMSCRIPTEN__
        shutdownScreens.frame(tick, input, false);
#else
        shutdownScreens.frame(tick, input);
#endif
		return shutdownScreens.running();
	}
	installStagedAssets();
#ifdef __EMSCRIPTEN__
    screens.frame(tick, filtered, false);
#else
#if GLOB2_DESKTOP_MCP
	GAGGUI::ScreenStack::AdditionalInput remote;
	remote.prepare = [&](std::vector<SDL_Event> &extra) { mcp->pump(extra, mcpAvailable()); };
    remote.completion = [service = mcp.get()] { return service->dispatchCompletion(); };
	remote.prepareCleanup = [this](std::vector<SDL_Event> &extra) { mcp->drainCleanup(extra); };
        remote.cancellation = [this] { return mcp->dispatchCancellation(); };
        remote.intercept = recordingShortcut;
	screens.frame(tick, filtered, true, remote);
#else
	screens.frame(tick, filtered);
#endif
#endif
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE) && GLOB2_HAS_ONLINE
	// An invite link (at launch or while running) opens the online hub from the
	// main menu; the hub consumes it.
	static std::uint32_t hubOpenedAt = 0;
	// A portal without online sign-in has no hub: invite and online map links
	// are dropped, and a local map link still plays.
	if (!PortalPolicy::onlineSignIn())
	{
		Online::takePendingJoin();
		if (Online::pendingMapPlay() && Online::pendingMapPlay()->mode != Online::MapPlayRequest::Mode::Local)
			Online::takePendingMapPlay();
	}
	if (Online::pendingJoin() && dynamic_cast<MainMenuScreen *>(screens.top()) &&
		tick - hubOpenedAt > 2000)
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
	// Crashes of earlier runs, once the main menu is up; MetricKit may deliver
	// one later in the run.
	if (CrashCapture::built() && screens.running() && dynamic_cast<MainMenuScreen *>(screens.top()) &&
		(!crashesOffered || CrashCapture::takeArrival()))
	{
		crashesOffered = true;
		offerCrashReports();
	}
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

void Application::draw()
{
    if (!hidden && !held) (quitting ? shutdownScreens : screens).draw();
    if (!drawn) { drawn = true; GAGCore::StartupPhase::mark("application:drawn"); }
}

std::uint32_t Application::delay(std::uint32_t now)
{
	if (hidden || held)
		return 100;
	const auto elapsed = static_cast<std::uint32_t>(now - lastFrame);
	return (quitting ? shutdownScreens : screens).delay(now, elapsed < 40 ? 40 - elapsed : 0);
}
