/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <assert.h>
#include <sstream>

SettingsScreen::SettingsScreen()
{
	// language part
	addWidget(new Text(20, 60, ALIGN_LEFT, ALIGN_TOP, "standard", globalContainer->texts.getString("[language]")));
	languageList=new List(20, 90, 160, 160, ALIGN_LEFT, ALIGN_TOP, "standard");
	for (int i=0; i<globalContainer->texts.getNumberOfLanguage(); i++)
		languageList->addText(globalContainer->texts.getStringInLang("[language]", i));
	addWidget(languageList);

	// graphics part
	addWidget(new Text(345, 60, ALIGN_RIGHT, ALIGN_TOP, "standard", globalContainer->texts.getString("[display]")));
	modeList=new List(245, 90, 100, 160, ALIGN_RIGHT, ALIGN_TOP, "standard");
	globalContainer->gfx->beginVideoModeListing();
	int w, h;
	while(globalContainer->gfx->getNextVideoMode(&w, &h))
	{
		std::ostringstream ost;
		ost << w << "x" << h;
		modeList->addText(ost.str().c_str());
	}
	addWidget(modeList);

	fullscreen=new OnOffButton(200, 90, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings->screenFlags&DrawableSurface::FULLSCREEN, FULLSCREEN);
	addWidget(fullscreen);
	addWidget(new Text(180, 90, ALIGN_RIGHT, ALIGN_TOP, "standard", globalContainer->texts.getString("[fullscreen]")));

	hwaccel=new OnOffButton(200, 120, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings->screenFlags&DrawableSurface::HWACCELERATED, HWACCLEL);
	addWidget(hwaccel);
	addWidget(new Text(180, 120, ALIGN_RIGHT, ALIGN_TOP, "standard", globalContainer->texts.getString("[hwaccel]")));

	nodblbuff=new OnOffButton(200, 150, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings->screenFlags&DrawableSurface::NO_DOUBLEBUF, NODBLBUFF);
	addWidget(nodblbuff);
	addWidget(new Text(180, 150, ALIGN_RIGHT, ALIGN_TOP, "standard", globalContainer->texts.getString("[nodblbuff]")));

	lowquality=new OnOffButton(200, 180, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings->optionFlags&GlobalContainer::OPTION_LOW_SPEED_GFX, LOWQUALITY);
	addWidget(lowquality);
	addWidget(new Text(180, 180, ALIGN_RIGHT, ALIGN_TOP, "standard", globalContainer->texts.getString("[lowquality]")));

	// Username part
	userName=new TextInput(20, 80, 160, 25, ALIGN_LEFT, ALIGN_BOTTOM, "standard", globalContainer->getUsername(), true, 32);
	addWidget(userName);
	addWidget(new Text(20, 130, ALIGN_LEFT, ALIGN_BOTTOM, "standard", globalContainer->texts.getString("[username]")));

	// Screen entry/quit part
	ok=new TextButton( 60, 20, 200, 40, ALIGN_LEFT, ALIGN_BOTTOM, "", -1, -1, "menu", globalContainer->texts.getString("[ok]"), OK, 13);
	addWidget(ok);
	cancel=new TextButton(60, 20, 200, 40, ALIGN_RIGHT, ALIGN_BOTTOM, NULL, -1, -1, "menu", globalContainer->texts.getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);
	title=new Text(0, 18, ALIGN_FILL, ALIGN_TOP, "menu", globalContainer->texts.getString("[settings]"));
	addWidget(title);

	oldLanguage=globalContainer->texts.getLang();
	oldScreenW=globalContainer->settings->screenWidth;
	oldScreenH=globalContainer->settings->screenHeight;
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
		{
			globalContainer->setUserName(userName->getText());
			globalContainer->settings->optionFlags=lowquality->getState() ? GlobalContainer::OPTION_LOW_SPEED_GFX : 0;
			globalContainer->settings->screenFlags=DrawableSurface::DEFAULT;
			globalContainer->settings->screenFlags|=fullscreen->getState() ? DrawableSurface::FULLSCREEN : DrawableSurface::RESIZABLE;
			globalContainer->settings->screenFlags|=hwaccel->getState() ? DrawableSurface::HWACCELERATED : 0;
			globalContainer->settings->screenFlags|=nodblbuff->getState() ? DrawableSurface::NO_DOUBLEBUF : 0;
			endExecute(par1);
		}
		else if (par1==CANCEL)
		{
			globalContainer->texts.setLang(oldLanguage);
			globalContainer->settings->screenWidth=oldScreenW;
			globalContainer->settings->screenHeight=oldScreenH;
			endExecute(par1);
		}
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==languageList)
		{
			globalContainer->texts.setLang(par1);
			ok->setText(globalContainer->texts.getString("[ok]"));
			cancel->setText(globalContainer->texts.getString("[Cancel]"));
			title->setText(globalContainer->texts.getString("[settings]"));
		}
		else if (source==modeList)
		{
			int w, h;
			sscanf(modeList->getText(par1), "%dx%d", &w, &h);
			globalContainer->settings->screenWidth=w;
			globalContainer->settings->screenHeight=h;
		}
	}
}

int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
