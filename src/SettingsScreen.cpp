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

SettingsScreen::SettingsScreen()
{
	//int decX=(globalContainer->gfx->getW()-400)>>1;
	languageList=new List(120, 60, 400, 200, globalContainer->standardFont);
	for (int i=0; i<globalContainer->texts.getNumberOfLanguage(); i++)
		languageList->addText(globalContainer->texts.getStringInLang("[language]", i));
	addWidget(languageList);
	userName=new TextInput(120, 280, 400, 30, globalContainer->standardFont, globalContainer->userName, true, 32);
	addWidget(userName);
	
	ok    =new TextButton( 60, 330, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13);
	cancel=new TextButton(380, 330, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27);
	addWidget(ok);
	addWidget(cancel);
	
	oldLanguage=globalContainer->texts.getLang();
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
			globalContainer->setUserName(userName->text);
		else if (par1==CANCEL)
			globalContainer->texts.setLang(oldLanguage);
		else
			assert(false);
		endExecute(par1);
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		globalContainer->texts.setLang(par1);
		ok->setText(globalContainer->texts.getString("[ok]"));
		cancel->setText(globalContainer->texts.getString("[cancel]"));
	}
}

void SettingsScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (y<40)
	{
		char *text=globalContainer->texts.getString("[settings]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);
	}
	addUpdateRect(x, y, w, h);
}

int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
