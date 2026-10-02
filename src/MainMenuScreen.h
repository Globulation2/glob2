// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include <memory>

class MainMenuScreen : public Glob2UI::Screen
{
  public:
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
	};
	MainMenuScreen();
	~MainMenuScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override
	{
		if (more)
			showMore(false);
		else
			endExecute(QUIT);
	}
	bool panel() const override { return false; }

  private:
	std::unique_ptr<GAGCore::DrawableSurface> wordmark;
	void loadWordmark(int width);
	void showMore(bool value);
	bool more = false;
	int wordmarkWidth = 0;
};
