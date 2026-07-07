// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <algorithm>
#include <iostream>
#include <optional>

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

// Interpret a click on a three-zone scrollbox widget. The widget is laid out
// as [<-arrow][===proportional drag track===][->arrow] inside a strip
// SCROLLBOX_BAR_WIDTH wide, with each arrow SCROLLBOX_ARROW_WIDTH wide.
//
// `lmx` is the click x-coordinate relative to the strip's left edge; the
// caller is responsible for having already y-band-gated and confirmed
// lmx is inside [0, SCROLLBOX_BAR_WIDTH). `current` is the value the
// widget currently shows, `max` its upper bound (inclusive; lower bound
// is 0).
//
// Returns the value the user just requested:
//   - left arrow click  → current - 1 (or nullopt if already at 0)
//   - drag-track click  → proportional value in [0, max)
//   - right arrow click → current + 1 (or nullopt if already at max)
// nullopt means "no order should be issued" — the click was an arrow press
// against an already-clamped value.
static std::optional<int> interpretScrollBoxClick(int lmx, int current, int max)
{
	if (lmx < SCROLLBOX_ARROW_WIDTH)
	{
		if (current > 0) return current - 1;
		return std::nullopt;
	}
	if (lmx < SCROLLBOX_BAR_WIDTH - SCROLLBOX_ARROW_WIDTH)
	{
		const int track = SCROLLBOX_BAR_WIDTH - 2 * SCROLLBOX_ARROW_WIDTH;
		return ((lmx - SCROLLBOX_ARROW_WIDTH) * max) / track;
	}
	if (current < max) return current + 1;
	return std::nullopt;
}

void GameGUI::handleMenuClickBuildingSelection(int mx, int my, int button)
{
	Building* selBuild=selectionBuilding();
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
			&& lmx < SCROLLBOX_BAR_WIDTH)
		{
			const int current = displayedMaxUnitWorking(*selBuild);
			if (auto nbReq = interpretScrollBoxClick(lmx, current, MAX_UNIT_WORKING))
			{
				pendingFor(selBuild->gid).pendingMaxUnitWorking = *nbReq;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, *nbReq)));
				defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, *nbReq);
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
				pendingFor(selBuild->gid).pendingPriority = -1;
			}
			else if(lmx>=(width) && lmx<(width+12))
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 0)));
				pendingFor(selBuild->gid).pendingPriority = 0;
			}
			else if(lmx>=(width*2) && lmx<=(width*2+12))
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 1)));
				pendingFor(selBuild->gid).pendingPriority = 1;
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
			&& (lmx < SCROLLBOX_BAR_WIDTH))
		{
			const int current = displayedUnitStayRange(*selBuild);
			if (auto nbReq = interpretScrollBoxClick(lmx, current, selBuild->type->maxUnitStayRange))
			{
				pendingFor(selBuild->gid).pendingUnitStayRange = *nbReq;
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, *nbReq)));
			}
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// flags specific options:
	if (((selBuild->owner->allies)&(1<<localTeamNo))
		&& lmx>10
		&& lmx<22)
	{

		// cleared ressources for clearing flags: one checkbox row per clearable
		// resource (stone is never cleared, so it has no row)
		if (buildingType->type == "clearingflag")
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<BASIC_COUNT; i++)
				if (i!=STONE)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						std::array<bool, BASIC_COUNT> next;
						for (int k=0; k<BASIC_COUNT; k++)
							next[k] = displayedClearingResource(*selBuild, k);
						next[i] = !next[i];
						pendingFor(selBuild->gid).pendingClearingRessources = next;
						bool wire[BASIC_COUNT];
						for (int k=0; k<BASIC_COUNT; k++) wire[k] = next[k];
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyClearingFlag(selBuild->gid, wire)));
					}

					ypos+=YOFFSET_TEXT_PARA;
				}
		}

		// minimum warrior level for war flags: one radio row per warrior level;
		// row i requests minLevelToFlag==i (drawn as level 1+i)
		if (buildingType->type == "warflag")
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<NB_UNIT_LEVELS; i++)
			{
				if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
				{
					pendingFor(selBuild->gid).pendingMinLevelToFlag = i;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, i)));
				}

				ypos+=YOFFSET_TEXT_PARA;
			}

		}

		// explorer requirement for exploration flags: one radio row per
		// EXPLORATION_FLAG_OPTION_* value (see GameGUIInternal.h — the flag
		// reuses minLevelToFlag as a which-explorers-may-answer choice)
		if (buildingType->type == "explorationflag")
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			for (int i=0; i<EXPLORATION_FLAG_OPTION_COUNT; i++)
			{
				if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
				{
					pendingFor(selBuild->gid).pendingMinLevelToFlag = i;
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, i)));
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
	// If revived: build the next masks in local Uint32 variables, stash them
	// as pending state on BuildingGuiState (add pendingReceiveRessourceMask /
	// pendingSendRessourceMask there), then emit the order. Same pattern as
	// pendingMaxUnitWorking / pendingPriority / pendingRatio.
	if (selBuild->type->canExchange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		int startY = ypos+YOFFSET_TEXT_PARA;
		int endY = startY+HAPPYNESS_COUNT*YOFFSET_TEXT_PARA;
		if ((my>startY) && (my<endY))
		{
			int r = (my-startY)/YOFFSET_TEXT_PARA;
			Uint32 nextRecv = selBuild->receiveRessourceMask;
			Uint32 nextSend = selBuild->sendRessourceMask;
			if ((lmx>92) && (lmx<104))
			{
				if (selBuild->receiveRessourceMask & (1<<r))
				{
					nextRecv &= ~(1<<r);
				}
				else
				{
					nextRecv |= (1<<r);
					nextSend &= ~(1<<r);
				}
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, nextRecv, nextSend)));
			}

			if ((lmx>110) && (lmx<122))
			{
				if (selBuild->sendRessourceMask & (1<<r))
				{
					nextSend &= ~(1<<r);
				}
				else
				{
					nextRecv &= ~(1<<r);
					nextSend |= (1<<r);
				}
				orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, nextRecv, nextSend)));
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
			if ((my>ypos+(i*20))&&(my<ypos+(i*20)+16)&&(lmx<SCROLLBOX_BAR_WIDTH))
			{
				const std::array<Sint32, NB_UNIT_TYPE> current = displayedRatio(*selBuild);
				if (auto nbReq = interpretScrollBoxClick(lmx, current[i], MAX_RATIO_RANGE))
				{
					std::array<Sint32, NB_UNIT_TYPE> next = current;
					next[i] = *nbReq;
					pendingFor(selBuild->gid).pendingRatio = next;
					// OrderModifySwarm wants a raw Sint32[NB_UNIT_TYPE]; copy out.
					Sint32 wire[NB_UNIT_TYPE];
					for (int k=0; k<NB_UNIT_TYPE; k++) wire[k] = next[k];
					orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, wire)));
				}
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
