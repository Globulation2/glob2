// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "UnitDisplayNames.h"

void GameGUI::drawBuildingInfos(void)
{
	Building* selBuild = selection.building;
	assert(selBuild);
	BuildingType *buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	Uint8 r, g, b;
	unsigned unitInsideBarYDec = 0;

	// draw "building" of "player"
	std::string title;
	std::string key ="[" + buildingType->type + "]";
	title += Toolkit::getStringTable()->getString(key.c_str());
	{
		title += " (";
		title += displayPlayerName(*selBuild->owner);
		title += ")";
	}

	if (localTeam->teamNumber == selBuild->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selBuild->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	// building text
	title = "";
	if ((buildingType->nextLevel>=0) ||  (buildingType->prevLevel>=0))
	{
		const std::string textT = Toolkit::getStringTable()->getString("[level]");
		title += FormatableString("%0 %1").arg(textT).arg(buildingType->level+1);
	}
	if (buildingType->isBuildingSite)
	{
		title += " (";
		title += Toolkit::getStringTable()->getString("[building site]");
		title += ")";
	}
	if (buildingType->prestige)
	{
		title += " - ";
		title += Toolkit::getStringTable()->getString("[Prestige]");
	}
	titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 200));
	globalContainer->gfx->drawString(titlePos, ypos+YOFFSET_TEXT_PARA-1, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;


	// building icon
	Sprite *miniSprite;
	int imgid;
	if (buildingType->miniSpriteImage >= 0)
	{
		miniSprite = buildingType->miniSpritePtr;
		imgid = buildingType->miniSpriteImage;
	}
	else
	{
		miniSprite = buildingType->gameSpritePtr;
		imgid = buildingType->gameSpriteImage;
	}
	int dx = (56-miniSprite->getW(imgid))>>1;
	int dy = (46-miniSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	miniSprite->setBaseColor(selBuild->owner->color);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+dx, ypos+4+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);
	globalContainer->gfx->finishDrawingSprite(miniSprite, 255);

	// draw HP
	if (buildingType->hpMax)
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
		globalContainer->littleFont->popStyle();

		if (selBuild->hp <= buildingType->hpMax/5)
			{ r=255; g=0; b=0; }
		else
			{ r=0; g=255; b=0; }

		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->hp).arg(buildingType->hpMax).c_str());
		globalContainer->littleFont->popStyle();
	}

	// inside
	if (buildingType->maxUnitInside && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
		globalContainer->littleFont->popStyle();
		if (selBuild->buildingState==Building::ALIVE)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->unitsInside.size()).arg(buildingType->maxUnitInside).c_str());
		}
		else
		{
			if (selBuild->unitsInside.size()>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0%1").arg(Toolkit::getStringTable()->getString("[Still (i)]")).arg(selBuild->unitsInside.size()).c_str());
			}
			else if (selBuild->unitsInside.size()==1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[Still one]") );
			}
		}
	}

	// it is a unit ranged attractor (aka flag)
	if (buildingType->defaultUnitStayRange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		// get flag stat — feed the displayed (optimistic) position and range
		// so the count tracks the cursor during a flag drag or scroll-resize.
		int goingTo, onSpot;
		computeFlagStatDisplayed(*selBuild,
			displayedPosX(*selBuild), displayedPosY(*selBuild),
			displayedUnitStayRange(*selBuild),
			&goingTo, &onSpot);
		// display flag stat
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0").arg(Toolkit::getStringTable()->getString("[In way]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(goingTo).c_str());
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
		globalContainer->littleFont, FormatableString(Toolkit::getStringTable()->getString("[On the spot]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-+RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(onSpot).c_str());
	}

	ypos += YOFFSET_ICON+YOFFSET_B_SEP;

	// working bar
	if (buildingType->maxUnitWorking)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if (selBuild->buildingState==Building::ALIVE)
			{
				// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
				const int maxUnitsWorking = (globalContainer->replaying?selBuild->maxUnitWorking:displayedMaxUnitWorking(*selBuild));

				std::string working = Toolkit::getStringTable()->getString("[working]");
				const int len = globalContainer->littleFont->getStringWidth(working)+4;
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, working);
				globalContainer->littleFont->popStyle();
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0/%1").arg((int)selBuild->unitsWorking.size()).arg(maxUnitsWorking).c_str());
				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, maxUnitsWorking, maxUnitsWorking, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
			}
			else
			{
				if (selBuild->unitsWorking.size()>1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0%1%2").arg(Toolkit::getStringTable()->getString("[still (w)]")).arg(selBuild->unitsWorking.size()).arg(Toolkit::getStringTable()->getString("[units working]")).c_str());
				}
				else if (selBuild->unitsWorking.size()==1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
						Toolkit::getStringTable()->getString("[still one unit working]") );
				}
			}
		}
		if(hilights.find(HilightUnitsAssignedBar) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, ypos+6, 38));
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// priority buttons
	if(buildingType->maxUnitWorking)
	{
		if((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if(selBuild->buildingState==Building::ALIVE)
			{
				// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
				const int priority = (globalContainer->replaying?selBuild->priority:selBuild->priorityLocal);

				ypos += YOFFSET_B_SEP;

				int width = 128/3;
				std::string prioritystr = Toolkit::getStringTable()->getString("[priority]");
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, prioritystr);

				std::string lowstr = Toolkit::getStringTable()->getString("[low priority]");
				std::string medstr = Toolkit::getStringTable()->getString("[medium priority]");
				std::string highstr = Toolkit::getStringTable()->getString("[high priority]");

				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+12+4, (priority==-1));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14, ypos+12+2, globalContainer->littleFont, lowstr);

				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width, ypos+12+4, (priority==0));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width, ypos+12+2, globalContainer->littleFont, medstr);

				drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width*2, ypos+12+4, (priority==1));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width*2, ypos+12+2, globalContainer->littleFont, highstr);

				ypos += YOFFSET_BAR+YOFFSET_B_SEP;
			}
		}
	}

	// flag range bar
	if (buildingType->defaultUnitStayRange)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
			const int unitStayRange = (globalContainer->replaying?selBuild->unitStayRange:displayedUnitStayRange(*selBuild));

			std::string range = Toolkit::getStringTable()->getString("[range]");
			const int len = globalContainer->littleFont->getStringWidth(range)+4;
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, range);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0").arg(selBuild->unitStayRange).c_str());
			drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, selBuild->unitStayRange, unitStayRange, 0, selBuild->type->maxUnitStayRange);
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// flag control of team and allies (clearing/war/exploration)
	drawBuildingFlagControls(selBuild, buildingType, ypos);

	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);

	// other infos
	if (buildingType->armor)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[armor]")).arg(buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+1, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[damage]")).arg(buildingType->shootDamage).c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+12, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[range]")).arg(buildingType->shootingRange).c_str());
		ypos += YOFFSET_TOWER;
	}

	// There is unit inside, show time to leave
	drawBuildingTimeToLeaveBar(selBuild, buildingType, ypos, unitInsideBarYDec);

	ypos += YOFFSET_B_SEP;

	// exchange building
	if (buildingType->canExchange && ((selBuild->owner->sharedVisionExchange)&(1<<localTeamNo)))
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
		globalContainer->littleFont->popStyle();
		//globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
		ypos += YOFFSET_TEXT_PARA;
		for (unsigned i=0; i<HAPPYNESS_COUNT; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1/%2)").arg(getRessourceName(i+HAPPYNESS_BASE)).arg(selBuild->ressources[i+HAPPYNESS_BASE]).arg(buildingType->maxRessource[i+HAPPYNESS_BASE]).c_str());

			/*
			int inId, outId;
			if (selBuild->receiveRessourceMaskLocal & (1<<i))
				inId = 20;
			else
				inId = 19;
			if (selBuild->sendRessourceMaskLocal & (1<<i))
				outId = 20;
			else
				outId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36, ypos+2, globalContainer->gamegui, inId);
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-18, ypos+2, globalContainer->gamegui, outId);
			*/

			ypos += YOFFSET_TEXT_PARA;
		}
	}

	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// ressorces for every building except exchange building
		if (!buildingType->canExchange)
		{

			// ressources in
			for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
			{
				if (buildingType->maxRessource[i])
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(getRessourceName(i)).arg(selBuild->ressources[i]).arg(buildingType->maxRessource[i]).c_str());
					ypos += 11;
				}
			}
			if (buildingType->maxBullets)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
				ypos += 11;
			}
			ypos+=5;
		}
		//Unit production ratios and unit production
		if (buildingType->unitProductionTime) // swarm
		{
			int left=(selBuild->productionTimeout*128)/buildingType->unitProductionTime;
			int elapsed=128-left;
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, elapsed, 7, 100, 100, 255);
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+elapsed, ypos, left, 7, 128, 128, 128);

			ypos+=15;
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
				const int ratio = (globalContainer->replaying?selBuild->ratio[i]:selBuild->ratioLocal[i]);

				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selBuild->ratio[i], ratio, 0, MAX_RATIO_RANGE);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+24, ypos, globalContainer->littleFont, getUnitName(i));

				if(i==1 && hilights.find(HilightRatioBar) != hilights.end())
				{
					arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET-36, ypos-8, 38));
				}

				ypos+=20;
			}

		}

		// data on whether or not the building is recieving units
		bool otherFailure=true;
		for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
		{
			int n = selBuild->unitsFailingRequirements[j];
			if(j!=0 && n>0)
				otherFailure=true;
		}
		if(otherFailure)
		{
			for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
			{
				int n = selBuild->unitsFailingRequirements[j];
				if(n>0 && (int)selBuild->unitsWorking.size() < selBuild->desiredMaxUnitWorking)
				{
					std::string s;
					if(j == Building::UnitNotAvailable)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units not available]")).arg(n);
					if(j == Building::UnitTooLowLevel)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too low level]")).arg(n);
					else if(j == Building::UnitCantAccessBuilding)
					{
						if (buildingType->isVirtual)
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access flag]")).arg(n);
						else
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access building]")).arg(n);
					}
					else if(j == Building::UnitTooFarFromBuilding)
					{
						if (buildingType->isVirtual)
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from flag]")).arg(n);
						else
							s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from building]")).arg(n);
					}
					else if(j == Building::UnitCantAccessResource)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access resource]")).arg(n);
					else if(j == Building::UnitCantAccessFruit)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from resource]")).arg(n);
					else if(j == Building::UnitTooFarFromResource)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access fruit]")).arg(n);
					else if(j == Building::UnitTooFarFromFruit)
						s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from fruit]")).arg(n);
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+10, ypos, globalContainer->littleFont, s.c_str());
					ypos+=11;
				}
			}
		}

		// repair and upgrade
		if(selBuild->owner == localTeam)
		{
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				if (buildingType->isBuildingSite)
					assert(buildingType->nextLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel repair]");
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				assert(buildingType->nextLevel!=-1);
				if (buildingType->isBuildingSite)
					assert(buildingType->prevLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel upgrade]");
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
			{
				if (selBuild->hp<buildingType->hpMax)
				{
					// repair
					if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && localTeam->maxBuildLevel()>=buildingType->level)
					{
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[repair]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
							{
								globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
								int ressources[BASIC_COUNT];
								selBuild->getRessourceCountToRepair(ressources);
								drawCosts(ressources, globalContainer->littleFont);
								globalContainer->littleFont->popStyle();
							}
					}
				}
				else if (buildingType->nextLevel!=-1)
				{
					// upgrade
					if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
					{
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[upgrade]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
							{
								globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
								drawBuildingUpgradePreview(selBuild, buildingType, unitInsideBarYDec);
								globalContainer->littleFont->popStyle();
							}
					}
				}
			}

			// building destruction
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[cancel destroy]");
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[destroy]");
			}
		}
	}
}
