/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "SettingsScreen.h"
#include "GlobalContainer.h"

SettingsScreen::SettingsScreen()
{
	int decX=(globalContainer->gfx->getW()-400)>>1;
	languageList=new List(decX, 60, 400, 200, globalContainer->standardFont);
	for (int i=0; i<globalContainer->texts.getNumberOfLanguage(); i++)
		languageList->addText(globalContainer->texts.getStringInLang("[language]", i));
	addWidget(languageList);
	userName=new TextInput(decX, 280, 400, 30, globalContainer->standardFont, globalContainer->settings.userName, true);
	addWidget(userName);
	addWidget(new TextButton(decX, 330, 400, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), 0));
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_PRESSED)
	{
		strncpy(globalContainer->settings.userName, userName->text, BasePlayer::MAX_NAME_LENGTH);
		endExecute(par1);
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		globalContainer->texts.setLang(par1);
		endExecute(1);
	}
}

void SettingsScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (y<40)
	{
		char *text= globalContainer->texts.getString("[settings]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);
	}
}

int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
