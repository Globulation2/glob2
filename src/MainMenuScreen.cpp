// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MainMenuScreen.h"
#include "GlobalContainer.h"
#include "Version.h"
#include "YOGConsts.h"
#include "NetConsts.h"
#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIImage.h>
#include <GUIProgressBar.h>
using namespace GAGGUI;
#include <Toolkit.h>
#include <StringTable.h>
#include <SupportFunctions.h>
using namespace GAGCore;

// version related stuff
#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif
#ifndef PACKAGE_VERSION
	#define PACKAGE_VERSION "System Specific - not using autoconf"
#endif

MainMenuScreen::MainMenuScreen()
{
	addWidget(new TextButton( 10, 100, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[campaign]"), CAMPAIGN));
	
	addWidget(new TextButton( 330, 100, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[tutorial]"), TUTORIAL));
	
	addWidget(new TextButton( 10, 180, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(330, 180, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[custom game]"), CUSTOM));
	
	addWidget(new TextButton( 10, 260, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[yog]"), MULTIPLAYERS_YOG));
	addWidget(new TextButton(330, 260, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[lan]"), MULTIPLAYERS_LAN));
	
	addWidget(new TextButton( 10, 340, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[settings]"), GAME_SETUP));
	
	addWidget(new TextButton(330, 340, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[editor]"), EDITOR));

	addWidget(new TextButton(10, 420, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[credits]"), CREDITS));
	addWidget(new TextButton(330, 420, 300,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[quit]"), QUIT, 27));
	
	addWidget(new Text(3, 0, ALIGN_RIGHT, ALIGN_BOTTOM, "standard", FormatableString("V %0.%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR).arg(NET_PROTOCOL_VERSION).c_str()));
	
	addWidget(new Text(3, 0, ALIGN_LEFT, ALIGN_BOTTOM, "standard", PACKAGE_VERSION));
	
	Image *title = new Image(0, 18, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->title.get());
	addWidget(title);
}

MainMenuScreen::~MainMenuScreen()
{
	
}

void MainMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endExecute(par1);
}

int MainMenuScreen::menu(void)
{
	return MainMenuScreen().execute(globalContainer->gfx, 40);
}
