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

#include "NewMapScreen.h"
#include <GUIText.h>
#include <GUINumber.h>
#include <GUIRatio.h>
#include <GUIButton.h>
#include <GUIList.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>

HowNewMapScreen::HowNewMapScreen()
{
	addWidget(new TextButton(150,  70, 340, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[new]"), NEW, 13));
	addWidget(new TextButton(150,  130, 340, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[load]"), LOAD));
	addWidget(new TextButton(150, 415, 340, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[editor]")));
}

void HowNewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==NEW)||(par1==LOAD)||(par1==CANCEL))
			endExecute(par1);
	}
}


NewMapScreen::NewMapScreen()
{
	//defaultTerrainTypeButton[0]=new OnOffButton(400, 110, 20, 20, true, 30);
	
	mapSizeX=new Number(20, 50, 100, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	mapSizeX->add(64);
	mapSizeX->add(128);
	mapSizeX->add(256);
	mapSizeX->add(512);
	mapSizeX->setNth(descriptor.wDec-6);
	addWidget(mapSizeX);
	
	mapSizeY=new Number(20, 75, 100, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	mapSizeY->add(64);
	mapSizeY->add(128);
	mapSizeY->add(256);
	mapSizeY->add(512);
	mapSizeY->setNth(descriptor.hDec-6);
	addWidget(mapSizeY);
	
	methodes=new List(20, 100, 280, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu");
	methodes->addText(Toolkit::getStringTable()->getString("[uniform terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[random terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[islands terrain]"));
	methodes->setNth(0);
	addWidget(methodes);
	
	// eUNIFORM

	terrains=new List(340, 100, 280, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu");
	terrains->addText(Toolkit::getStringTable()->getString("[water]"));
	terrains->addText(Toolkit::getStringTable()->getString("[sand]"));
	terrains->addText(Toolkit::getStringTable()->getString("[grass]"));
	terrains->setNth(descriptor.terrainType);
	addWidget(terrains);
	
	// not eUNIFORM
	
	nbTeams=new Number(310, 100, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	nbTeams->add(1);
	nbTeams->add(2);
	nbTeams->add(3);
	nbTeams->add(4);
	nbTeams->add(5);
	nbTeams->add(6);
	nbTeams->add(7);
	nbTeams->add(8);
	nbTeams->setNth(descriptor.nbTeams-1);
	nbTeams->visible=false;
	addWidget(nbTeams);
	
	nbWorkers=new Number(310, 120, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
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

	numberOfTeamText=new Text(430, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[number of teams]"));
	numberOfTeamText->visible=false;
	addWidget(numberOfTeamText);
	numberOfWorkerText=new Text (430, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[workers]"));
	numberOfWorkerText->visible=false;
	addWidget(numberOfWorkerText);
	
	// eRANDOM
	
	waterRatio=new Ratio(310, 160, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 40, descriptor.waterRatio, "menu");
	waterRatio->visible=false;
	addWidget(waterRatio);
	
	sandRatio=new Ratio(310, 180, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 40, descriptor.sandRatio, "menu");
	sandRatio->visible=false;
	addWidget(sandRatio);
	
	grassRatio=new Ratio(310, 200, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 40, descriptor.grassRatio, "menu");
	grassRatio->visible=false;
	addWidget(grassRatio);
	
	smooth=new Number(310, 220, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
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
	
	ratioText=new Text(310, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[ratios]"));
	ratioText->visible=false;
	addWidget(ratioText);
	waterText=new Text(480, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[water]"));
	waterText->visible=false;
	addWidget(waterText);
	sandText=new Text(480, 180, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[sand]"));
	sandText->visible=false;
	addWidget(sandText);
	grassText=new Text(480, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[grass]"));
	grassText->visible=false;
	addWidget(grassText);
	smoothingText=new Text(480, 220, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[smoothing]"));
	smoothingText->visible=false;
	addWidget(smoothingText);

	
	// eISLANDS

	islandsSize=new Ratio(310, 140, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 40, descriptor.islandsSize, "menu");
	islandsSize->visible=false;
	addWidget(islandsSize);
	
	beach=new Number(310, 160, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	beach->add(0);
	beach->add(1);
	beach->add(2);
	beach->add(3);
	beach->add(4);
	beach->setNth(descriptor.beach);
	beach->visible=false;
	addWidget(beach);
	
	islandSizeText=new Text(430, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[islands size]"));
	islandSizeText->visible=false;
	addWidget(islandSizeText);
	beachSizeText=new Text(430, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[beach size]"));
	beachSizeText->visible=false;
	addWidget(beachSizeText);
	
	
	// all
	
	addWidget(new TextButton( 20, 420, 280, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13));
	addWidget(new TextButton(340, 420, 280, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));

	const char *text= Toolkit::getStringTable()->getString("[create map]");
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", text));
	addWidget(new Text(130, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[map size x]")));
	addWidget(new Text(130, 75, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[map size y]")));
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
		
		// not eUNIFORM
		descriptor.nbTeams=nbTeams->getNth()+1;
		
		// eRANDOM
		descriptor.smooth=smooth->getNth()+1;
		
		// eISLANDS
		descriptor.beach=beach->getNth();
		descriptor.nbWorkers=nbWorkers->getNth()+1;
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		// eUNIFORM
		if (source==terrains)
			descriptor.terrainType=(TerrainType)terrains->getNth();
		
		// all
		if (source==methodes)
		{
			MapGenerationDescriptor::Methode old=descriptor.methode;
			descriptor.methode=(MapGenerationDescriptor::Methode)methodes->getNth();
			
			if (old!=descriptor.methode)
			{
				// eUNIFORM
				terrains->setVisible(descriptor.methode==MapGenerationDescriptor::eUNIFORM);
				
				// not eUNIFORM
				nbTeams->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				nbWorkers->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				numberOfTeamText->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				numberOfWorkerText->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				
				
				// eRANDOM
				waterRatio->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				sandRatio->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				grassRatio->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				smooth->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				ratioText->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				waterText->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				sandText->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				grassText->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				smoothingText->setVisible(descriptor.methode==MapGenerationDescriptor::eRANDOM);
				
				// eISLANDS
				islandsSize->setVisible(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				beach->setVisible(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				islandSizeText->setVisible(descriptor.methode==MapGenerationDescriptor::eISLANDS);
				beachSizeText->setVisible(descriptor.methode==MapGenerationDescriptor::eISLANDS);
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
