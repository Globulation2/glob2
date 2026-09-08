// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Glob2Screen.h"
#include <vector>

class MainMenuButton;

class MainMenuScreen:public Glob2Screen
{
public:
	enum
	{
		CAMPAIGN,
		TUTORIAL,
		LOAD_GAME,
		CUSTOM,
		MULTIPLAYERS_YOG,
		MULTIPLAYERS_LAN,
		GAME_SETUP,
		EDITOR,
		CREDITS,
		QUIT,
	};
	
public:
	MainMenuScreen();
	~MainMenuScreen() override;
	void onAction(Widget *source, Action action, int par1, int par2) override;
	static int menu(void);
	void paint(void) override;
	void onSDLEvent(SDL_Event *event) override;

private:
	std::vector<MainMenuButton*> buttons;
	int focusedButton = -1;
	int panelX, panelY, panelW, panelH;
};
