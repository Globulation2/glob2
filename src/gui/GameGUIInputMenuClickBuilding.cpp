// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <algorithm>
#include <iostream>

#include <SDL_keycode.h>

#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameGUIKeyActions.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "SoundMixer.h"
#include "Unit.h"
#include "VoiceRecorder.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::handleMenuClickBuildingSelection(int mx, int my, int button)
{
	Building* selBuild=selection.building;
	assert (selBuild);
	if (selBuild->owner->teamNumber!=localTeamNo)
		return;
	int ypos = YPOS_BASE_BUILDING +  YOFFSET_NAME + YOFFSET_ICON + YOFFSET_B_SEP;
	BuildingType *buildingType = selBuild->type;
	int lmx = mx - RIGHT_MENU_OFFSET; // local mx

	// working bar
	if (selBuild->type->maxUnitWorking)
	{
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& my>ypos+YOFFSET_TEXT_BAR
			&& my<ypos+YOFFSET_TEXT_BAR+16
			&& selBuild->buildingState==Building::ALIVE
			&& lmx < 128)
		{
			int nbReq;
			const int current = displayedMaxUnitWorking(*selBuild);
			if (lmx<18)
			{
				if(current>0)
				{
					nbReq = current - 1;
					pendingFor(selBuild->gid).pendingMaxUnitWorking = nbReq;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
			        defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
				}
			}
			else if (lmx<(128-18))
			{
				nbReq = ((lmx-18)*MAX_UNIT_WORKING)/(128-36);
				pendingFor(selBuild->gid).pendingMaxUnitWorking = nbReq;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
	        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
			}
			else
			{
				if(current<MAX_UNIT_WORKING)
				{
					nbReq = current + 1;
					pendingFor(selBuild->gid).pendingMaxUnitWorking = nbReq;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
		        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
				}
			}
		}
		ypos += YOFFSET_BAR + YOFFSET_B_SEP;
	}

	// priorities
	if(selBuild->type->maxUnitWorking)
	{
		ypos += YOFFSET_B_SEP;
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& my>ypos+16
			&& my<ypos+16+12
			&& selBuild->buildingState==Building::ALIVE)
		{
			int width = (128 - 8)/3;

			if(lmx>=0 && lmx<=12)
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, -1)));
				selBuild->priorityLocal = -1;
			}
			else if(lmx>=(width) && lmx<(width+12))
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 0)));
				selBuild->priorityLocal = 0;
			}
			else if(lmx>=(width*2) && lmx<=(width*2+12))
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 1)));
				selBuild->priorityLocal = 1;
			}
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// flag range bar
	if (buildingType->defaultUnitStayRange)
	{
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& (my>ypos+YOFFSET_TEXT_BAR)
			&& (my<ypos+YOFFSET_TEXT_BAR+16)
			&& (lmx < 128))
		{
			int nbReq;
			const int current = displayedUnitStayRange(*selBuild);
			if (lmx<18)
			{
				if(current>0)
				{
					nbReq = current - 1;
					pendingFor(selBuild->gid).pendingUnitStayRange = nbReq;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
				}
			}
			else if (lmx<RIGHT_MENU_WIDTH-18)
			{
				nbReq = ((lmx-18)*(unsigned)selBuild->type->maxUnitStayRange)/(128-36);
				pendingFor(selBuild->gid).pendingUnitStayRange = nbReq;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
			}
			else
			{
				// TODO : check in orderQueue to avoid useless orders.
				if (current < selBuild->type->maxUnitStayRange)
				{
					nbReq = current + 1;
					pendingFor(selBuild->gid).pendingUnitStayRange = nbReq;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
				}
			}
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// flags specific options:
	if (((selBuild->owner->allies)&(1<<localTeamNo))
		&& lmx>10
		&& lmx<22)
	{

		// cleared ressources for clearing flags:
		if (buildingType->type == "clearingflag")
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<BASIC_COUNT; i++)
				if (i!=STONE)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->clearingRessourcesLocal[i]=!selBuild->clearingRessourcesLocal[i];
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyClearingFlag(selBuild->gid, selBuild->clearingRessourcesLocal)));
					}

					ypos+=YOFFSET_TEXT_PARA;
				}
		}

		if (buildingType->type == "warflag")
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<4; i++)
			{
				if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
				{
					selBuild->minLevelToFlagLocal=i;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
				}

				ypos+=YOFFSET_TEXT_PARA;
			}

		}

		if (buildingType->type == "explorationflag")
		{
			// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
			// must be able to do to be accepted at this flag
			// 0 == any explorer
			// 1 == must be able to attack ground
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<2; i++)
			{
				if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
				{
					selBuild->minLevelToFlagLocal=i;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
				}

				ypos+=YOFFSET_TEXT_PARA;
			}
		}
	}

	if (buildingType->armor)
		ypos+=YOFFSET_TEXT_LINE;
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
		ypos += YOFFSET_TOWER;
	ypos += YOFFSET_B_SEP;

	//Exchannge building
	//Exchanging as a feature is broken
	/*
	if (selBuild->type->canExchange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		int startY = ypos+YOFFSET_TEXT_PARA;
		int endY = startY+HAPPYNESS_COUNT*YOFFSET_TEXT_PARA;
		if ((my>startY) && (my<endY))
		{
			int r = (my-startY)/YOFFSET_TEXT_PARA;
			if ((lmx>92) && (lmx<104))
			{
				if (selBuild->receiveRessourceMask & (1<<r))
				{
					selBuild->receiveRessourceMaskLocal &= ~(1<<r);
				}
				else
				{
					selBuild->receiveRessourceMaskLocal |= (1<<r);
					selBuild->sendRessourceMaskLocal &= ~(1<<r);
				}
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
			}

			if ((lmx>110) && (lmx<122))
			{
				if (selBuild->sendRessourceMask & (1<<r))
				{
					selBuild->sendRessourceMaskLocal &= ~(1<<r);
				}
				else
				{
					selBuild->receiveRessourceMaskLocal &= ~(1<<r);
					selBuild->sendRessourceMaskLocal |= (1<<r);
				}
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
			}
		}
	}
	*/
	// ressources in
	for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
	{
		if (buildingType->maxRessource[i])
		{
			ypos += 11;
		}
	}
	if (buildingType->maxBullets)
	{
		ypos += 11;
	}
	ypos+=5;

	if (selBuild->type->unitProductionTime)
	{
		ypos+=15;
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			if ((my>ypos+(i*20))&&(my<ypos+(i*20)+16)&&(lmx<128))
			{
				if (lmx<18)
				{
					if (selBuild->ratioLocal[i]>0)
					{
						selBuild->ratioLocal[i]--;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
					}
				}
				else if (lmx<(128-18))
				{
					selBuild->ratioLocal[i]=((lmx-18)*MAX_RATIO_RANGE)/(128-36);
					orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
				}
				else
				{
					if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
					{
						selBuild->ratioLocal[i]++;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
					}
				}
				//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
			}
		}
	}

	if ((my>globalContainer->gfx->getH()-48) && (my<globalContainer->gfx->getH()-32))
	{
		if (selBuild->constructionResultState==Building::REPAIR)
		{
			int typeNum = selBuild->typeNum; //determines type of updated building
			int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
		}
		else if (selBuild->constructionResultState==Building::UPGRADE)
		{
			int typeNum = selBuild->typeNum; //determines type of updated building
			int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
		}
		else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
		{
			repairAndUpgradeBuilding(selBuild, true, true);
		}
	}

	if ((my>globalContainer->gfx->getH()-24) && (my<globalContainer->gfx->getH()-8))
	{
		if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
		{
			orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(selBuild->gid)));
		}
		else if (selBuild->buildingState==Building::ALIVE)
		{
			orderQueue.push_back(shared_ptr<Order>(new OrderDelete(selBuild->gid)));
		}
	}
}
