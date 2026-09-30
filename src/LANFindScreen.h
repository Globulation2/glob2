// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "NetBroadcastListener.h"
#include "ui/FrontendUI.h"
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}

class LANFindScreen : public Glob2UI::Screen
{
  public:
	explicit LANFindScreen(GAGGUI::ScreenStack &screens);
	~LANFindScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	enum
	{
		CONNECT = 1,
		QUIT = 5
	};
	// Semantic entry points shared with harnesses.
	void setServer(const std::string &address);
	void connect();

  protected:
	void onEscape() override { endExecute(QUIT); }

  private:
	GAGGUI::ScreenStack &screens;
	std::string serverName = "localhost";
	std::string playerName;
	std::vector<std::string> games;
	int selectedGame = -1;
	NetBroadcastListener listener;
};
