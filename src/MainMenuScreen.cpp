/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "MainMenuScreen.h"
#include "GlobalContainer.h"
#include "Version.h"
#include "YOGConsts.h"
#include "NetConsts.h"

MainMenuScreen::MainMenuScreen()
{
	addWidget(new TextButton( 20,  20, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[campagn]"), CAMPAIN));
	
	addWidget(new TextButton( 340,  20, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[tutorial]"), TUTORIAL));
	
	addWidget(new TextButton( 20, 100, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(340, 100, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[custom game]"), CUSTOM));
	
	addWidget(new TextButton( 20, 180, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[yog]"), MULTIPLAYERS_YOG));
	addWidget(new TextButton(340, 180, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[lan]"), MULTIPLAYERS_LAN));
	
	addWidget(new TextButton( 20, 340, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[settings]"), GAME_SETUP));
	addWidget(new TextButton(340, 340, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[editor]"), EDITOR));

	addWidget(new TextButton(20, 420, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[credits]"), CREDITS));
	addWidget(new TextButton(340, 420, 280,  40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[quit]"), QUIT, 27));
	
	addWidget(new Text(3, 0, ALIGN_RIGHT, ALIGN_BOTTOM, "standard", GAG::nsprintf("V %d.%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, NET_PROTOCOL_VERSION, YOG_PROTOCOL_VERSION).c_str()));

	globalContainer->gfx->setClipRect();

	background=globalContainer->gfx->createDrawableSurface("data/gfx/IntroMN.png");
}

MainMenuScreen::~MainMenuScreen()
{
	delete background;
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
