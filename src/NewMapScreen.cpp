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

#include "NewMapScreen.h"
#include "GlobalContainer.h"

NewMapScreen::NewMapScreen()
{
	int i;
	firstPaint=true;
	sizeX=7;
	sizeY=7;
	defaultTerrainType=Map::WATER;

	sizeXButton[0]=new OnOffButton(400, 60, 20, 20, false, 10);
	sizeXButton[1]=new OnOffButton(400, 85, 20, 20, true, 11);
	sizeXButton[2]=new OnOffButton(400, 110, 20, 20, false, 12);
	sizeXButton[3]=new OnOffButton(400, 135, 20, 20, false, 13);
	for (i=0; i<4; i++)
		addWidget(sizeXButton[i]);

	sizeYButton[0]=new OnOffButton(400, 185, 20, 20, false, 20);
	sizeYButton[1]=new OnOffButton(400, 210, 20, 20, true, 21);
	sizeYButton[2]=new OnOffButton(400, 235, 20, 20, false, 22);
	sizeYButton[3]=new OnOffButton(400, 260, 20, 20, false, 23);
	for (i=0; i<4; i++)
		addWidget(sizeYButton[i]);

	defaultTerrainTypeButton[0]=new OnOffButton(400, 310, 20, 20, true, 30);
	defaultTerrainTypeButton[1]=new OnOffButton(400, 335, 20, 20, false, 31);
	defaultTerrainTypeButton[2]=new OnOffButton(400, 360, 20, 20, false, 32);
	for (i=0; i<3; i++)
		addWidget(defaultTerrainTypeButton[i]);

	addWidget(new TextButton(150, 415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), 0));
}

void NewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
	{
		int i, id;
		if (par1==0)
		{
			endExecute(0);
		}
		else if ((par1>=10) && (par1<20))
		{
			id=par1-10;
			for (i=0; i<4; i++)
				if (i==id)
					sizeXButton[i]->setState(true);
				else
					sizeXButton[i]->setState(false);
			sizeX=id+6;
		}
		else if ((par1>=20) && (par1<30))
		{
			id=par1-20;
			for (i=0; i<4; i++)
				if (i==id)
					sizeYButton[i]->setState(true);
				else
					sizeYButton[i]->setState(false);
			sizeY=id+6;
		}
		else if ((par1>=30) && (par1<40))
		{
			id=par1-30;
			for (i=0; i<3; i++)
				if (i==id)
					defaultTerrainTypeButton[i]->setState(true);
				else
					defaultTerrainTypeButton[i]->setState(false);
			defaultTerrainType=(Map::TerrainType)id;
		}
	}
}

void NewMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (firstPaint)
	{
		char *text= globalContainer->texts.getString("[create map]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);

		gfxCtx->drawString(20, 90, globalContainer->menuFont, globalContainer->texts.getString("[map size x]"));
		gfxCtx->drawString(20, 215, globalContainer->menuFont, globalContainer->texts.getString("[map size y]"));
		gfxCtx->drawString(20, 330, globalContainer->menuFont, globalContainer->texts.getString("[default terrain]"));

		gfxCtx->drawString(440, 60, globalContainer->menuFont, "64");
		gfxCtx->drawString(440, 85, globalContainer->menuFont, "128");
		gfxCtx->drawString(440, 110, globalContainer->menuFont, "256");
		gfxCtx->drawString(440, 135, globalContainer->menuFont, "512");

		gfxCtx->drawString(440, 185, globalContainer->menuFont, "64");
		gfxCtx->drawString(440, 210, globalContainer->menuFont, "128");
		gfxCtx->drawString(440, 235, globalContainer->menuFont, "256");
		gfxCtx->drawString(440, 260, globalContainer->menuFont, "512");

		gfxCtx->drawString(440, 310, globalContainer->menuFont, globalContainer->texts.getString("[water]"));
		gfxCtx->drawString(440, 335, globalContainer->menuFont, globalContainer->texts.getString("[sand]"));
		gfxCtx->drawString(440, 360, globalContainer->menuFont, globalContainer->texts.getString("[grass]"));
		firstPaint=false;
	}
}
