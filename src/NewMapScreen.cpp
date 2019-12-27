/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
#include <assert.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
using namespace GAGCore;
#include <GUIText.h>
#include <GUINumber.h>
#include <GUIRatio.h>
#include <GUIButton.h>
#include <GUIList.h>
using namespace GAGGUI;


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

	logRepeatAreaTimes=new Number(310, 75, 114, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	logRepeatAreaTimes->add(1);
	logRepeatAreaTimes->add(2);
	logRepeatAreaTimes->add(4);
	logRepeatAreaTimes->add(8);
	logRepeatAreaTimes->add(16);
	logRepeatAreaTimes->add(32);
	logRepeatAreaTimes->visible=false;
	addWidget(logRepeatAreaTimes);

	methodes=new List(20, 100, 280, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu");
	methodes->addText(Toolkit::getStringTable()->getString("[uniform terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[swamp terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[river terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[islands terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[crater lakes terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[concrete islands terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[isles terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[old random terrain]"));
	methodes->addText(Toolkit::getStringTable()->getString("[old islands terrain]"));
	methodes->setSelectionIndex(0);
	addWidget(methodes);

	// eUNIFORM

	terrains=new List(340, 100, 280, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu");
	terrains->addText(Toolkit::getStringTable()->getString("[water]"));
	terrains->addText(Toolkit::getStringTable()->getString("[sand]"));
	terrains->addText(Toolkit::getStringTable()->getString("[grass]"));
	terrains->setSelectionIndex(descriptor.terrainType);
	addWidget(terrains);

	// not eUNIFORM"", -1, -1,

	nbTeams=new Number(310, 100, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	nbTeams->add(1);
	nbTeams->add(2);
	nbTeams->add(3);
	nbTeams->add(4);
	nbTeams->add(5);
	nbTeams->add(6);
	nbTeams->add(7);
	nbTeams->add(8);
	nbTeams->add(9);
	nbTeams->add(10);
	nbTeams->add(11);
	nbTeams->add(12);
	/*
	nbTeams->add(13);
	nbTeams->add(14);
	nbTeams->add(15);
	nbTeams->add(16);
	*/
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

	areaTimesText=new Text(430, 75, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[repeat area]"));
	areaTimesText->visible=false;
	addWidget(areaTimesText);

	numberOfWorkerText=new Text (430, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[workers]"));
	numberOfWorkerText->visible=false;
	addWidget(numberOfWorkerText);

	waterRatio=new Ratio(310, 160, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.waterRatio, "menu");
	waterRatio->set(50);
	waterRatio->visible=false;
	addWidget(waterRatio);

	sandRatio=new Ratio(310, 180, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.sandRatio, "menu");
	sandRatio->set(0);
	sandRatio->visible=false;
	addWidget(sandRatio);

	grassRatio=new Ratio(310, 200, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.grassRatio, "menu");
	grassRatio->set(50);
	grassRatio->visible=false;
	addWidget(grassRatio);

	desertRatio=new Ratio(310, 220, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.desertRatio, "menu");
	desertRatio->set(0);
	desertRatio->visible=false;
	addWidget(desertRatio);

	algaeRatio=new Ratio(310, 240, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.algaeRatio, "menu");
	algaeRatio->set(50);
	algaeRatio->visible=false;
	addWidget(algaeRatio);

	wheatRatio=new Ratio(310, 260, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.wheatRatio, "menu");
	wheatRatio->set(50);
	wheatRatio->visible=false;
	addWidget(wheatRatio);

	woodRatio=new Ratio(310, 280, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.woodRatio, "menu");
	woodRatio->set(50);
	woodRatio->visible=false;
	addWidget(woodRatio);

	stoneRatio=new Ratio(310, 300, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.stoneRatio, "menu");
	stoneRatio->set(50);
	stoneRatio->visible=false;
	addWidget(stoneRatio);

	fruitRatio=new Ratio(310, 320, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.fruitRatio, "menu");
	fruitRatio->set(4);
	fruitRatio->visible=false;
	addWidget(fruitRatio);

	riverDiameter=new Ratio(310, 340, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.riverDiameter, "menu");
	riverDiameter->set(50);
	riverDiameter->visible=false;
	addWidget(riverDiameter);

	craterDensity=new Ratio(310, 340, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 64, descriptor.craterDensity, "menu");
	craterDensity->set(50);
	craterDensity->visible=false;
	addWidget(craterDensity);

	extraIslands=new Number(310, 340, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	extraIslands->add(0);
	extraIslands->add(1);
	extraIslands->add(2);
	extraIslands->add(3);
	extraIslands->add(4);
	extraIslands->add(5);
	extraIslands->add(6);
	extraIslands->add(7);
	extraIslands->add(8);
	extraIslands->setNth(descriptor.extraIslands+1);
	extraIslands->visible=false;
	addWidget(extraIslands);

	smooth=new Number(310, 360, 164, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
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
	desertText=new Text(480, 220, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[desert]"));
	desertText->visible=false;
	addWidget(desertText);
	algaeText=new Text(480, 240, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[algae]"));
	algaeText->visible=false;
	addWidget(algaeText);
	wheatText=new Text(480, 260, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Wheat]"));
	wheatText->visible=false;
	addWidget(wheatText);
	woodText=new Text(480, 280, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[wood]"));
	woodText->visible=false;
	addWidget(woodText);
	stoneText=new Text(480, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[stone]"));
	stoneText->visible=false;
	addWidget(stoneText);
	fruitText=new Text(480, 320, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[fruit]"));
	fruitText->visible=false;
	addWidget(fruitText);
	riverDiameterText=new Text(480, 340, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[river diameter]"));
	riverDiameterText->visible=false;
	addWidget(riverDiameterText);
	craterDensityText=new Text(480, 340, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[crater density]"));
	craterDensityText->visible=false;
	addWidget(craterDensityText);
	extraIslandsText=new Text(480, 340, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[extra islands]"));
	extraIslandsText->visible=false;
	addWidget(extraIslandsText);
	smoothingText=new Text(480, 360, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[smoothing]"));
	smoothingText->visible=false;
	addWidget(smoothingText);
	// eOLDISLANDS

	oldIslandSize=new Ratio(310, 140, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 40, descriptor.oldIslandSize, "menu");
	oldIslandSize->visible=false;
	addWidget(oldIslandSize);

	oldBeach=new Number(310, 160, 114, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	oldBeach->add(0);
	oldBeach->add(1);
	oldBeach->add(2);
	oldBeach->add(3);
	oldBeach->add(4);
	oldBeach->setNth(descriptor.oldBeach);
	oldBeach->visible=false;
	addWidget(oldBeach);

	oldIslandSizeText=new Text(430, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[islands size]"));
	oldIslandSizeText->visible=false;
	addWidget(oldIslandSizeText);
	oldBeachSizeText=new Text(430, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[beach size]"));
	oldBeachSizeText->visible=false;
	addWidget(oldBeachSizeText);

	// all

	addWidget(new TextButton(10, 420, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13));
	addWidget(new TextButton(330, 420, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));

	const std::string text= Toolkit::getStringTable()->getString("[create map]");
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

		// eOLDISLANDS
		descriptor.oldBeach=oldBeach->getNth();
		descriptor.nbWorkers=nbWorkers->getNth()+1;

		descriptor.logRepeatAreaTimes=logRepeatAreaTimes->getNth();
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		// eUNIFORM
		if (source==terrains)
			descriptor.terrainType=(TerrainType)terrains->getSelectionIndex();

		// all
		if (source==methodes)
		{
			MapGenerationDescriptor::Methode old=descriptor.methode;
			descriptor.methode=(MapGenerationDescriptor::Methode)methodes->getSelectionIndex();

			if (old!=descriptor.methode)
			{
				terrains->visible=false;
				ratioText->visible=false;
				waterRatio->visible=false;
				waterText->visible=false;
				sandRatio->visible=false;
				sandText->visible=false;
				grassRatio->visible=false;
				grassText->visible=false;
				desertRatio->visible=false;
				desertText->visible=false;
				smooth->visible=false;
				smoothingText->visible=false;
				wheatRatio->visible=false;
				wheatText->visible=false;
				woodRatio->visible=false;
				woodText->visible=false;
				stoneRatio->visible=false;
				stoneText->visible=false;
				algaeRatio->visible=false;
				algaeText->visible=false;
				riverDiameter->visible=false;
				riverDiameterText->visible=false;
				extraIslands->visible=false;
				extraIslandsText->visible=false;
				craterDensity->visible=false;
				craterDensityText->visible=false;
				riverDiameter->visible=false;
				riverDiameterText->visible=false;
				oldBeach->visible=false;
				oldBeachSizeText->visible=false;
				oldIslandSize->visible=false;
				oldIslandSizeText->visible=false;
				riverDiameter->visible=false;
				riverDiameterText->visible=false;
				fruitRatio->visible=false;
				fruitText->visible=false;
				logRepeatAreaTimes->visible=false;
				areaTimesText->visible=false;

				// not eUNIFORM
				nbTeams->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				nbWorkers->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				numberOfTeamText->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);
				numberOfWorkerText->setVisible(descriptor.methode!=MapGenerationDescriptor::eUNIFORM);

				switch (descriptor.methode)
				{
					case MapGenerationDescriptor::eUNIFORM:
						terrains->visible=true;
						break;
					case MapGenerationDescriptor::eSWAMP:
						ratioText->visible=
						waterRatio->visible=waterText->visible=
						grassRatio->visible=grassText->visible=
						smooth->visible=smoothingText->visible=
						wheatRatio->visible=wheatText->visible=
						woodRatio->visible=woodText->visible=
						stoneRatio->visible=stoneText->visible=
						algaeRatio->visible=algaeText->visible=
						fruitRatio->visible=fruitText->visible=
						logRepeatAreaTimes->visible=areaTimesText->visible=
						true;
						break;
					case  MapGenerationDescriptor::eRIVER:
						ratioText->visible=
						waterRatio->visible=waterText->visible=
						sandRatio->visible=sandText->visible=
						grassRatio->visible=grassText->visible=
						desertRatio->visible=desertText->visible=
						smooth->visible=smoothingText->visible=
						wheatRatio->visible=wheatText->visible=
						woodRatio->visible=woodText->visible=
						stoneRatio->visible=stoneText->visible=
						algaeRatio->visible=algaeText->visible=
						riverDiameter->visible=riverDiameterText->visible=
						fruitRatio->visible=fruitText->visible=
						logRepeatAreaTimes->visible=areaTimesText->visible=
						true;
						break;
					case  MapGenerationDescriptor::eISLANDS:
						ratioText->visible=
						waterRatio->visible=waterText->visible=
						sandRatio->visible=sandText->visible=
						grassRatio->visible=grassText->visible=
						desertRatio->visible=desertText->visible=
						smooth->visible=smoothingText->visible=
						wheatRatio->visible=wheatText->visible=
						woodRatio->visible=woodText->visible=
						stoneRatio->visible=stoneText->visible=
						algaeRatio->visible=algaeText->visible=
						extraIslands->visible=extraIslandsText->visible=
						fruitRatio->visible=fruitText->visible=
						logRepeatAreaTimes->visible=areaTimesText->visible=
						true;
						break;
					case  MapGenerationDescriptor::eCRATERLAKES:
						ratioText->visible=
						waterRatio->visible=waterText->visible=
						sandRatio->visible=sandText->visible=
						grassRatio->visible=grassText->visible=
						desertRatio->visible=desertText->visible=
						smooth->visible=smoothingText->visible=
						wheatRatio->visible=wheatText->visible=
						woodRatio->visible=woodText->visible=
						stoneRatio->visible=stoneText->visible=
						algaeRatio->visible=algaeText->visible=
						craterDensity->visible=craterDensityText->visible=
						fruitRatio->visible=fruitText->visible=
						logRepeatAreaTimes->visible=areaTimesText->visible=
						true;
						break;
					case  MapGenerationDescriptor::eCONCRETEISLANDS:
						break;
					case  MapGenerationDescriptor::eISLES:
						break;
					case  MapGenerationDescriptor::eOLDRANDOM:
						ratioText->visible=
						waterRatio->visible=waterText->visible=
						sandRatio->visible=sandText->visible=
						grassRatio->visible=grassText->visible=
						smooth->visible=smoothingText->visible=
						wheatRatio->visible=wheatText->visible=
						woodRatio->visible=woodText->visible=
						stoneRatio->visible=stoneText->visible=
						algaeRatio->visible=algaeText->visible=
						true;
						break;
					case  MapGenerationDescriptor::eOLDISLANDS:
						oldBeach->visible=oldBeachSizeText->visible=
						oldIslandSize->visible=oldIslandSizeText->visible=
						true;
						break;
					default: assert(false);break;
				}
			}
		}
	}
	else if (action==RATIO_CHANGED)
	{
		descriptor.waterRatio=waterRatio->get();
		descriptor.sandRatio=sandRatio->get();
		descriptor.grassRatio=grassRatio->get();
		descriptor.desertRatio=desertRatio->get();
		descriptor.wheatRatio=wheatRatio->get();
		descriptor.woodRatio=woodRatio->get();
		descriptor.algaeRatio=algaeRatio->get();
		descriptor.stoneRatio=stoneRatio->get();
		descriptor.fruitRatio=fruitRatio->get();
		descriptor.riverDiameter=riverDiameter->get();
		descriptor.craterDensity=craterDensity->get();
		descriptor.extraIslands=extraIslands->get();
		//eISLANDS
		descriptor.oldIslandSize=oldIslandSize->get();
	}
}
