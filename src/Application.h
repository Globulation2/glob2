// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <ScreenStack.h>
#include "SinglePlayerFlow.h"
#include "OnlineServices.h"

class FrontendTheme;

// Shared application state. Scheduling/event polling belong to platform hosts.
class Application : public GAGCore::ApplicationHost::Loop
{
  public:
	Application();
	~Application() override;
	bool frame(std::uint32_t tick, const std::vector<SDL_Event> &events) override;
	std::uint32_t delay(std::uint32_t now) override;

  private:
	// The online services of this run (Online::services()). Declared first so they
	// outlive every screen: screens and their requests hold the platform client.
	Online::ServicesOwner online;
	std::unique_ptr<FrontendTheme> frontend;
	GAGGUI::ScreenStack screens;
	GAGGUI::ScreenStack shutdownScreens;
	SinglePlayerFlow singlePlayer;
	std::uint32_t lastFrame = 0;
	bool hidden = false;
	bool quitting = false;
	void mainMenu();
	void installStagedAssets();
	void choose(int choice);
};
