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

HowNewMapScreen::HowNewMapScreen()
{
	addWidget(new TextButton( 20, 340, 280, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[new]"), NEW, 13));
	addWidget(new TextButton(340, 340, 280, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[load]"), LOAD));
	addWidget(new TextButton(340, 420, 280, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27));
}

void HowNewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==NEW)||(par1==LOAD)||(par1==CANCEL))
			endExecute(par1);
	}
}

void HowNewMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
}

NewMapScreen::NewMapScreen()
{
	firstPaint=true;

	//defaultTerrainTypeButton[0]=new OnOffButton(400, 110, 20, 20, true, 30);
	
	mapSizeX=new Number(20, 50, 100, 20, 20, globalContainer->menuFont);
	mapSizeX->add(64);
	mapSizeX->add(128);
	mapSizeX->add(256);
	mapSizeX->add(512);
	mapSizeX->setNth(descriptor.wDec-6);
	addWidget(mapSizeX);
	
	mapSizeY=new Number(20, 75, 100, 20, 20, globalContainer->menuFont);
	mapSizeY->add(64);
	mapSizeY->add(128);
	mapSizeY->add(256);
	mapSizeY->add(512);
	mapSizeY->setNth(descriptor.hDec-6);
	addWidget(mapSizeY);
	
	methodes=new List(20, 100, 280, 300, globalContainer->menuFont);
	methodes->addText(globalContainer->texts.getString("[uniform terrain]"));
	methodes->addText(globalContainer->texts.getString("[random terrain]"));
	methodes->addText(globalContainer->texts.getString("[islands terrain]"));
	methodes->setNth(0);
	addWidget(methodes);
	
	terrains=new List(320, 100, 280, 300, globalContainer->menuFont);
	terrains->addText(globalContainer->texts.getString("[water]"));
	terrains->addText(globalContainer->texts.getString("[sand]"));
	terrains->addText(globalContainer->texts.getString("[grass]"));
	terrains->setNth(0);
	addWidget(terrains);
	
	waterRatio=new Ratio(320, 120, 164, 18, 40, descriptor.waterRatio, globalContainer->menuFont);
	waterRatio->visible=false;
	addWidget(waterRatio);
	
	sandRatio=new Ratio(320, 140, 164, 18, 40, descriptor.sandRatio, globalContainer->menuFont);
	sandRatio->visible=false;
	addWidget(sandRatio);
	
	grassRatio=new Ratio(320, 160, 164, 18, 40, descriptor.grassRatio, globalContainer->menuFont);
	grassRatio->visible=false;
	addWidget(grassRatio);
	
	smooth=new Number(320, 180, 164, 18, 18, globalContainer->menuFont);
	smooth->add(1);
	smooth->add(2);
	smooth->add(3);
	smooth->add(4);
	smooth->add(5);
	smooth->add(6);
	smooth->add(7);
	smooth->add(8);
	smooth->setNth(descriptor.smooth-1);
	smooth->visible=false;
	addWidget(smooth);
	
	nbIslands=new Number(320, 100, 114, 18, 18, globalContainer->menuFont);
	nbIslands->add(1);
	nbIslands->add(2);
	nbIslands->add(3);
	nbIslands->add(4);
	nbIslands->add(5);
	nbIslands->add(6);
	nbIslands->add(7);
	nbIslands->add(8);
	nbIslands->setNth(descriptor.nbIslands-1);
	nbIslands->visible=false;
	addWidget(nbIslands);
	
	islandsSize=new Ratio(320, 120, 114, 18, 40, descriptor.islandsSize, globalContainer->menuFont);
	islandsSize->visible=false;
	addWidget(islandsSize);
	
	beach=new Number(320, 140, 114, 18, 18, globalContainer->menuFont);
	beach->add(0);
	beach->add(1);
	beach->add(2);
	beach->add(3);
	beach->add(4);
	beach->setNth(descriptor.beach);
	beach->visible=false;
	addWidget(beach);
	
	nbWorkers=new Number(320, 160, 114, 18, 18, globalContainer->menuFont);
	nbWorkers->add(1);
	nbWorkers->add(2);
	nbWorkers->add(3);
	nbWorkers->add(4);
	nbWorkers->add(5);
	nbWorkers->add(6);
	nbWorkers->add(7);
	nbWorkers->add(8);
	nbWorkers->setNth(descriptor.nbWorkers-1);
	nbWorkers->visible=false;
	addWidget(nbWorkers);
	
	
	addWidget(new TextButton( 20, 420, 280, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 13));
	addWidget(new TextButton(340, 420, 280, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27));
}

void NewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==OK)||(par1==CANCEL))
			endExecute(par1);
	}
	else if (action==NUMBER_ELEMENT_SELECTED)
	{
		descriptor.wDec=mapSizeX->getNth()+6;
		descriptor.hDec=mapSizeY->getNth()+6;
		
		//eRANDOM
		descriptor.smooth=smooth->getNth()+1;
		
		//eISLANDS
		descriptor.beach=beach->getNth();
		descriptor.nbIslands=nbIslands->getNth()+1;
		descriptor.nbWorkers=nbWorkers->getNth()+1;
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		//eUNIFORM
		if (source==terrains)
			descriptor.terrainType=(Map::TerrainType)terrains->getNth();
		
		//all
		if (source==methodes)
		{
			MapGenerationDescriptor::Methode old=descriptor.methode;
			descriptor.methode=(MapGenerationDescriptor::Methode)methodes->getNth();
			
			if (old!=descriptor.methode)
			{
				terrains->visible=(descriptor.methode==MapGenerationDescriptor::eUNIFORM);
				
				waterRatio->visible=(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				sandRatio->visible=(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				grassRatio->visible=(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				smooth->visible=(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				
				nbIslands->visible=(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				islandsSize->visible=(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				beach->visible=(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				nbWorkers->visible=(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				
				dispatchPaint(gfxCtx);
				addUpdateRect(320, 100, 320, 300);
			}
		}
	}
	else if (action==RATIO_CHANGED)
	{
		//eRANDOM
		descriptor.waterRatio=waterRatio->get();
		descriptor.sandRatio=sandRatio->get();
		descriptor.grassRatio=grassRatio->get();
		
		//eISLANDS
		descriptor.islandsSize=islandsSize->get();
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		
	}
}

void NewMapScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (firstPaint)
	{
		char *text= globalContainer->texts.getString("[create map]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);

		gfxCtx->drawString(140, 50, globalContainer->menuFont, globalContainer->texts.getString("[map size x]"));
		gfxCtx->drawString(140, 75, globalContainer->menuFont, globalContainer->texts.getString("[map size y]"));
		
		firstPaint=false;
	}
	
	if (descriptor.methode==MapGenerationDescriptor::eRANDOM)
	{
		gfxCtx->drawString(320, 100, globalContainer->menuFont, globalContainer->texts.getString("[ratios]"));
		gfxCtx->drawString(500, 120, globalContainer->menuFont, globalContainer->texts.getString("[water]"));
		gfxCtx->drawString(500, 140, globalContainer->menuFont, globalContainer->texts.getString("[sand]"));
		gfxCtx->drawString(500, 160, globalContainer->menuFont, globalContainer->texts.getString("[grass]"));
		gfxCtx->drawString(500, 180, globalContainer->menuFont, globalContainer->texts.getString("[smoothing]"));
	}
	
	if (descriptor.methode==MapGenerationDescriptor::eISLANDS)
	{
		gfxCtx->drawString(450, 100, globalContainer->menuFont, globalContainer->texts.getString("[islands number]"));
		gfxCtx->drawString(450, 120, globalContainer->menuFont, globalContainer->texts.getString("[islands size]"));
		gfxCtx->drawString(450, 140, globalContainer->menuFont, globalContainer->texts.getString("[beach size]"));
		gfxCtx->drawString(450, 160, globalContainer->menuFont, globalContainer->texts.getString("[workers]"));
	}
}
