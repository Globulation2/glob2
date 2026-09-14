// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Glob2Screen.h"
#include <vector>
#include <memory>

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
	void onTimer(Uint32 tick) override;

private:
	std::vector<MainMenuButton*> buttons;
	std::unique_ptr<GAGCore::DrawableSurface> wordmark;
	int focusedButton = -1;
	int panelX, panelY, panelW, panelH;
	// A worker from the colony wandering the strip under the buttons.
	struct MenuGlob
	{
		double x = 0;
		int dir = 1;
		bool walking = true;
		Uint32 phase = 0, lastTick = 0, idleUntil = 0;
		unsigned seed = 0x9e3779b9u;
	} glob;
	// Sixteen walk frames (east, west) composited once with the sheet's faint
	// matte removed: it never showed on grass, but does on a pale membrane.
	std::vector<std::unique_ptr<GAGCore::DrawableSurface>> globFrames;
	void buildGlobFrames();
};
