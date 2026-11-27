// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include "ui/DiscordCommunity.h"
#include <memory>

class MainMenuScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "main_menu"; }
	enum
	{
		CAMPAIGN,
		TUTORIAL,
		LOAD_GAME,
		CUSTOM,
		PLAY_ONLINE,
		MULTIPLAYERS_LAN,
		GAME_SETUP,
		EDITOR,
		CREDITS,
		QUIT,
		REPORT_PROBLEM, // Report a Problem (docs/multiplayer/problem-reports.md)
	};
	MainMenuScreen();
	~MainMenuScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	// Sends queued problem reports while the main menu is up.
	void onTimer(Uint32 tick) override;

	void openCommunity() { community.open(); invalidate(); }
	// Whether the menu offers Quit (the button and Escape on the top level). A web
	// portal hosts the game in its own page, where quitting would only leave an
	// empty frame, so portal mode never offers it (src/app/PortalPolicy.h).
	static bool offersQuit();

  protected:
	void onEscape() override
	{
		if (community.visible)
		{
			community.visible = false;
			invalidate();
		}
		else if (more)
			showMore(false);
		else if (offersQuit())
			endExecute(QUIT);
	}
	bool panel() const override { return false; }

  private:
	Glob2UI::DiscordCommunity community;
	std::unique_ptr<GAGCore::DrawableSurface> wordmark;
	void loadWordmark(int width);
	void showMore(bool value);
	bool more = false;
	int wordmarkWidth = 0;
	unsigned wordmarkTheme = 0;
};
