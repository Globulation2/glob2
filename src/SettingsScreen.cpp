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
	languageList=new List(20, 60, 200, 200, ALIGN_LEFT, ALIGN_LEFT, "standard");
	for (int i=0; i<globalContainer->texts.getNumberOfLanguage(); i++)
		languageList->addText(globalContainer->texts.getStringInLang("[language]", i));
	addWidget(languageList);
	userName=new TextInput(120, 280, 400, 30, ALIGN_LEFT, ALIGN_LEFT, "standard", globalContainer->getUsername(), true, 32);
	addWidget(userName);

	modeList=new List(20, 60, 200, 200, ALIGN_RIGHT, ALIGN_LEFT, "standard");
	globalContainer->gfx->beginVideoModeListing();
	int w, h;
	while(globalContainer->gfx->getNextVideoMode(&w, &h))
	{
		std::ostringstream ost;
		ost << w << "x" << h;
		modeList->addText(ost.str().c_str());
	}
	addWidget(modeList);

	ok=new TextButton( 60, 330, 200, 40, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(380, 330, 200, 40, ALIGN_LEFT, ALIGN_LEFT, NULL, -1, -1, "menu", globalContainer->texts.getString("[Cancel]"), CANCEL, 27);
	title=new Text(0, 18, ALIGN_LEFT, ALIGN_LEFT, "menu", globalContainer->texts.getString("[settings]"), 640);
	
	addWidget(ok);
	addWidget(cancel);
	addWidget(title);

	oldLanguage=globalContainer->texts.getLang();
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
			globalContainer->setUserName(userName->getText());
		else if (par1==CANCEL)
			globalContainer->texts.setLang(oldLanguage);
		else
			assert(false);
		endExecute(par1);
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
