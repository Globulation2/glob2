// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <ScreenStack.h>
#include "SinglePlayerFlow.h"
#include "mcp/Availability.h"
#include <BuildFeatures.h>
#if GLOB2_HAS_ONLINE
#include "OnlineServices.h"
#endif
#if GLOB2_DESKTOP_MCP
namespace MCP { class UIService; }
#endif

class FrontendTheme;
namespace ReleaseCheck { class Runner; }

// Shared application state. Scheduling/event polling belong to platform hosts.
class Application : public GAGCore::ApplicationHost::Loop
{
  public:
	Application();
	~Application() override;
	bool frame(std::uint32_t tick, const std::vector<SDL_Event> &events) override;
	std::uint32_t delay(std::uint32_t now) override;
    void draw() override;

  private:
	// The online services of this run (Online::services()). Declared first so they
	// outlive every screen: screens and their requests hold the platform client.
#if GLOB2_HAS_ONLINE
	Online::ServicesOwner online;
#endif
	std::unique_ptr<FrontendTheme> frontend;
#if GLOB2_DESKTOP_MCP
	std::unique_ptr<MCP::UIService> mcp;
#endif
	GAGGUI::ScreenStack screens;
	GAGGUI::ScreenStack shutdownScreens;
	SinglePlayerFlow singlePlayer;
	std::uint32_t lastFrame = 0;
	int pendingViewportWidth = 0, pendingViewportHeight = 0;
	bool hidden = false;
	// A web portal holds the game for an ad or its own pause (ApplicationHost::hostHeld).
	bool held = false;
	bool framed = false, drawn = false; // startup phase marks (GAGCore::StartupPhase)
	bool quitting = false;
	// Asks the first-run questions still unanswered, else opens the main menu;
	// true when it opened the main menu. A portal's first visit starts in the
	// introduction lesson instead (PortalPolicy::landsInFirstLesson).
	bool mainMenu();
	void installStagedAssets();
	void choose(int choice);
	void withAgeBand(std::function<void()> next);
	std::uint64_t ageRequest = 0; ///< the newest withAgeBand request
	std::unique_ptr<ReleaseCheck::Runner> releaseCheck; ///< GLOB2_RELEASE_CHECK runs (src/app/ReleaseCheck.h)
	bool crashesOffered = false; ///< the last run's crashes were looked at
	void offerCrashReports();

};
