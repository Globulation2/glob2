// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Order execution. Split out of Game.cpp; see Game.cpp for the rest of the
// Game class implementation.

#include <iostream>
#include <fstream>

#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <set>
#include <string>
#include <functional>
#include <algorithm>
#include <sstream>
#include <cmath>

#include <FileManager.h>
#include <GraphicContext.h>

#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "render/UnitSkin.h"
#include "Integrity.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"

#include "MapEdit.h"

#include "Brush.h"
#include "DynamicClouds.h"
#include "Bullet.h"
#include "TextStream.h"
#include "FertilityCalculatorDialog.h"

#include "ReplayWriter.h"

Building* Game::lookupBuilding(Uint16 gid) const
{
	int team=Building::GIDtoTeam(gid);
	int id=Building::GIDtoID(gid);
	return teams[team]->myBuildings[id];
}

void Game::executeOrder(std::shared_ptr<Order> order, int localPlayer)
{
	assert(order->sender>=0);
	assert(order->sender<Team::MAX_COUNT);
	assert(order->sender < gameHeader.getNumberOfPlayers());

	if (globalContainer->replayWriter && globalContainer->replayWriter->isValid())
	{
		globalContainer->replayWriter->pushOrder(order);
	}

	// Mirror the order into the AI-trainer dataset if requested via
	// GLOB2_DATASET_PATH. One record per executed order, tagged with
	// the firing tick (the live stepCounter is correct here because
	// executeOrder runs after Game::syncStep advances it).
	if (globalContainer->datasetWriter && globalContainer->datasetWriter->isValid())
	{
		globalContainer->datasetWriter->writeRecord((Uint32)stepCounter, *order, *this);
	}

	anyPlayerWaited=false;
	Team *team=players[order->sender]->team;
	assert(team);
	bool isPlayerAlive=team->isAlive;
	Uint8 orderType=order->getOrderType();
	switch (orderType)
	{
		case ORDER_CREATE:
			if (!isPlayerAlive) break;
			executeCreate(*std::static_pointer_cast<OrderCreate>(order), localPlayer);
			break;
		case ORDER_MODIFY_BUILDING:
			if (!isPlayerAlive) break;
			executeModifyBuilding(*std::static_pointer_cast<OrderModifyBuilding>(order), localPlayer);
			break;
		case ORDER_MODIFY_EXCHANGE:
			if (!isPlayerAlive) break;
			executeModifyExchange(*std::static_pointer_cast<OrderModifyExchange>(order), localPlayer);
			break;
		case ORDER_MODIFY_FLAG:
			if (!isPlayerAlive) break;
			executeModifyFlag(*std::static_pointer_cast<OrderModifyFlag>(order), localPlayer);
			break;
		case ORDER_MODIFY_CLEARING_FLAG:
			if (!isPlayerAlive) break;
			executeModifyClearingFlag(*std::static_pointer_cast<OrderModifyClearingFlag>(order), localPlayer);
			break;
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
			if (!isPlayerAlive) break;
			executeModifyMinLevelToFlag(*std::static_pointer_cast<OrderModifyMinLevelToFlag>(order), localPlayer);
			break;
		case ORDER_MOVE_FLAG:
			if (!isPlayerAlive) break;
			executeMoveFlag(*std::static_pointer_cast<OrderMoveFlag>(order), localPlayer);
			break;
		case ORDER_ALTERATE_FORBIDDEN:
			executeAlterateForbidden(*std::static_pointer_cast<OrderAlterateForbidden>(order), localPlayer);
			break;
		case ORDER_ALTERATE_GUARD_AREA:
			executeAlterateGuardArea(*std::static_pointer_cast<OrderAlterateGuardArea>(order), localPlayer);
			break;
		case ORDER_ALTERATE_CLEAR_AREA:
			executeAlterateClearArea(*std::static_pointer_cast<OrderAlterateClearArea>(order), localPlayer);
			break;
		case ORDER_MODIFY_SWARM:
			if (!isPlayerAlive) break;
			executeModifySwarm(*std::static_pointer_cast<OrderModifySwarm>(order), localPlayer);
			break;
		case ORDER_DELETE:
			executeDelete(*std::static_pointer_cast<OrderDelete>(order));
			break;
		case ORDER_CHANGE_PRIORITY:
			executeChangePriority(*std::static_pointer_cast<OrderChangePriority>(order));
			break;
		case ORDER_CANCEL_DELETE:
			executeCancelDelete(*std::static_pointer_cast<OrderCancelDelete>(order));
			break;
		case ORDER_CONSTRUCTION:
			if (!isPlayerAlive) break;
			executeConstruction(*std::static_pointer_cast<OrderConstruction>(order));
			break;
		case ORDER_CANCEL_CONSTRUCTION:
			if (!isPlayerAlive) break;
			// Historical: the cancel-construction case downcasts to OrderConstruction,
			// not OrderCancelConstruction. Preserve that — the two layouts overlap on
			// the fields read here, and changing it is a behavior change.
			executeCancelConstruction(*std::static_pointer_cast<OrderConstruction>(order));
			break;
		case ORDER_SET_ALLIANCE:
			executeSetAlliance(*std::static_pointer_cast<SetAllianceOrder>(order));
			break;
		case ORDER_PLAYER_QUIT_GAME:
			executePlayerQuitGame(*std::static_pointer_cast<PlayerQuitsGameOrder>(order));
			break;
	}
}

void Game::executeCreate(const OrderCreate& oc, int localPlayer)
{
	int posX=(oc.posX)&map.getMaskW();
	int posY=(oc.posY)&map.getMaskH();
	assert(oc.teamNumber==players[oc.sender]->team->teamNumber);
	BuildingType *bt=globalContainer->buildingsTypes.get(oc.typeNum);
	bool isVirtual=bt->isVirtual;
	int w=bt->width;
	int h=bt->height;
	if (!isVirtual && (teams[oc.teamNumber]->noMoreBuildingSitesCountdown>0))
		return;
	bool isRoom=checkRoomForBuilding(posX, posY, bt, oc.teamNumber);
	if (isVirtual || isRoom)
	{
		Building *b=addBuilding(posX, posY, oc.typeNum, oc.teamNumber, oc.unitWorking, oc.unitWorkingFuture);
		if (b)
		{
			if(isVirtual && oc.flagRadius>=0)
			{
				b->unitStayRange = oc.flagRadius;
				b->unitStayRangeLocal = oc.flagRadius;
			}
			b->owner->addToStaticAbilitiesLists(b);
			b->update();
		}
	}
	else if (!isVirtual && !isRoom && map.isHardSpaceForBuilding(posX, posY, w, h))
	{
		BuildProject buildProject;
		buildProject.posX = posX;
		buildProject.posY = posY;
		buildProject.teamNumber = oc.teamNumber;
		buildProject.typeNum = oc.typeNum;
		buildProject.unitWorking = oc.unitWorking;
		buildProject.unitWorkingFuture = oc.unitWorkingFuture;
		buildProjects.push_back(buildProject);
		Uint32 teamMask=Team::teamNumberToMask(oc.teamNumber);
		for (int y=posY; y<posY+h; y++)
			for (int x=posX; x<posX+w; x++)
			{
				size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
				map.cases[index].forbidden|=teamMask;
				if (oc.teamNumber == players[localPlayer]->teamNumber)
					map.localForbiddenMap.set(index, true);
			}
		map.updateForbiddenGradient(oc.teamNumber);
	}
}

void Game::executeModifyBuilding(const OrderModifyBuilding& omb, int localPlayer)
{
	Building *b=lookupBuilding(omb.gid);
	if ((b) && (b->buildingState==Building::ALIVE))
	{
		assert(omb.numberRequested <= MAX_BUILDING_WORKER_REQUEST);
		b->maxUnitWorking=omb.numberRequested;
		b->maxUnitWorkingPreferred=b->maxUnitWorking;
		if (omb.sender!=localPlayer)
			b->maxUnitWorkingLocal=b->maxUnitWorking;
		b->update();
	}
}

void Game::executeModifyExchange(const OrderModifyExchange& ome, int localPlayer)
{
	Building *b=lookupBuilding(ome.gid);
	if ((b) && (b->buildingState==Building::ALIVE))
	{
		b->receiveRessourceMask=ome.receiveRessourceMask;
		b->sendRessourceMask=ome.sendRessourceMask;
		if (ome.sender!=localPlayer)
		{
			b->receiveRessourceMaskLocal=b->receiveRessourceMask;
			b->sendRessourceMaskLocal=b->sendRessourceMask;
		}
		b->update();
	}
}

void Game::executeModifyFlag(const OrderModifyFlag& omf, int localPlayer)
{
	Building *b=lookupBuilding(omf.gid);
	if ((b) && (b->buildingState==Building::ALIVE) && (b->type->defaultUnitStayRange))
	{
		int oldRange=b->unitStayRange;
		int newRange=omf.range;
		b->unitStayRange=newRange;
		if (omf.sender!=localPlayer)
			b->unitStayRangeLocal=newRange;

		if (b->type->zonableForbidden)
		{
			if (newRange<oldRange)
			{
				b->owner->dirtyGlobalGradient();
				map.dirtyLocalGradient(b->posX-oldRange-GRADIENT_DIRTY_BORDER_TILES, b->posY-oldRange-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+oldRange*2, 2*GRADIENT_DIRTY_BORDER_TILES+oldRange*2, b->owner->teamNumber);
			}
		}
		else
		{
			b->resetPathfindGradients();
		}
	}
}

void Game::executeModifyClearingFlag(const OrderModifyClearingFlag& omcf, int localPlayer)
{
	Building *b=lookupBuilding(omcf.gid);
	if (b
		&& b->buildingState==Building::ALIVE
		&& b->type->defaultUnitStayRange
		&& b->type->zonable[WORKER])
	{
		memcpy(b->clearingRessources, omcf.clearingRessources, sizeof(bool)*BASIC_COUNT);
		if (omcf.sender!=localPlayer)
			memcpy(b->clearingRessourcesLocal, omcf.clearingRessources, sizeof(bool)*BASIC_COUNT);
	}
}

void Game::executeModifyMinLevelToFlag(const OrderModifyMinLevelToFlag& omwf, int localPlayer)
{
	Building *b=lookupBuilding(omwf.gid);
	if (b
		&& b->buildingState==Building::ALIVE
		&& b->type->defaultUnitStayRange
		&& (b->type->zonable[WARRIOR] || b->type->zonable[EXPLORER]))
	{
		b->minLevelToFlag = omwf.minLevelToFlag;
		// if it was another player, update local
		if (omwf.sender != localPlayer)
			b->minLevelToFlagLocal = b->minLevelToFlag;

		// flush all the actual units
		int maxUnitWorkingSaved = b->maxUnitWorking;
		b->maxUnitWorking = 0;
		b->update();
		b->maxUnitWorking = maxUnitWorkingSaved;
		b->update();
	}
}

void Game::executeMoveFlag(const OrderMoveFlag& omf, int localPlayer)
{
	bool drop=omf.drop;
	Building *b=lookupBuilding(omf.gid);
	if ((b) && (b->buildingState==Building::ALIVE) && (b->type->isVirtual))
	{
		if (drop && b->type->zonableForbidden)
		{
			int range=b->unitStayRange;
			map.dirtyLocalGradient(b->posX-range-GRADIENT_DIRTY_BORDER_TILES, b->posY-range-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, b->owner->teamNumber);
		}

		b->posX=omf.x;
		b->posY=omf.y;

		if (b->type->zonableForbidden)
		{
			if (drop)
				b->owner->dirtyGlobalGradient();
		}
		else
		{
			b->resetPathfindGradients();
		}

		if (omf.sender!=localPlayer || globalContainer->replaying)
		{
			b->posXLocal=b->posX;
			b->posYLocal=b->posY;
		}
	}
}

void Game::executeAlterateForbidden(const OrderAlterateForbidden& oaa, int localPlayer)
{
	if (oaa.type == BrushTool::MODE_ADD)
	{
		Uint32 teamMask = Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].forbidden |= teamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localForbiddenMap.set(index, true);
				}
				orderMaskIndex++;
			}
	}
	else if (oaa.type == BrushTool::MODE_DEL)
	{
		Uint32 notTeamMask = ~Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].forbidden &= notTeamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localForbiddenMap.set(index, false);
				}
				orderMaskIndex++;
			}

		// We remove, so we need to refresh the gradients, unfortunatly
		teams[oaa.teamNumber]->dirtyGlobalGradient();
		map.dirtyLocalGradient(oaa.centerX+oaa.minX-GRADIENT_DIRTY_BORDER_TILES, oaa.centerY+oaa.minY-GRADIENT_DIRTY_BORDER_TILES, oaa.maxX-oaa.minX+2*GRADIENT_DIRTY_BORDER_TILES, oaa.maxY-oaa.minY+2*GRADIENT_DIRTY_BORDER_TILES, oaa.teamNumber);
	}
	else
		assert(false);
	map.updateForbiddenGradient(oaa.teamNumber);
	map.updateGuardAreasGradient(oaa.teamNumber);
	map.updateClearAreasGradient(oaa.teamNumber);
}

void Game::executeAlterateGuardArea(const OrderAlterateGuardArea& oaa, int localPlayer)
{
	if (oaa.type == BrushTool::MODE_ADD)
	{
		Uint32 teamMask = Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].guardArea |= teamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localGuardAreaMap.set(index, true);
				}
				orderMaskIndex++;
			}
	}
	else if (oaa.type == BrushTool::MODE_DEL)
	{
		Uint32 notTeamMask = ~Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].guardArea &= notTeamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localGuardAreaMap.set(index, false);
				}
				orderMaskIndex++;
			}
	}
	else
		assert(false);
	map.updateGuardAreasGradient(oaa.teamNumber);
}

void Game::executeAlterateClearArea(const OrderAlterateClearArea& oaa, int localPlayer)
{
	if (oaa.type == BrushTool::MODE_ADD)
	{
		Uint32 teamMask = Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].clearArea |= teamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localClearAreaMap.set(index, true);
				}
				orderMaskIndex++;
			}
	}
	else if (oaa.type == BrushTool::MODE_DEL)
	{
		Uint32 notTeamMask = ~Team::teamNumberToMask(oaa.teamNumber);
		size_t orderMaskIndex = 0;
		for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; y++)
			for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; x++)
			{
				if (oaa.mask.get(orderMaskIndex))
				{
					size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].clearArea &= notTeamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.localClearAreaMap.set(index, false);
				}
				orderMaskIndex++;
			}
	}
	else
		assert(false);
	map.updateClearAreasGradient(oaa.teamNumber);
}

void Game::executeModifySwarm(const OrderModifySwarm& oms, int localPlayer)
{
	Building *b=lookupBuilding(oms.gid);
	if ((b) && (b->buildingState==Building::ALIVE) && (b->type->unitProductionTime))
	{
		for (int j=0; j<NB_UNIT_TYPE; j++)
		{
			b->ratio[j]=oms.ratio[j];
			if (oms.sender!=localPlayer)
				b->ratioLocal[j]=b->ratio[j];
		}
		b->update();
	}
}

void Game::executeDelete(const OrderDelete& od)
{
	Building *b=lookupBuilding(od.gid);
	if (b)
	{
		b->launchDelete();
		assert(b->type);
		if (b->type->zonableForbidden)
		{
			b->owner->dirtyGlobalGradient();
			int range=b->unitStayRange;
			map.dirtyLocalGradient(b->posX-range-GRADIENT_DIRTY_BORDER_TILES, b->posY-range-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, b->owner->teamNumber);
		}
	}
}

void Game::executeChangePriority(const OrderChangePriority& ocp)
{
	Building *b=lookupBuilding(ocp.gid);
	if (b)
	{
		b->priority = ocp.priority;
		b->updateCallLists();
	}
}

void Game::executeCancelDelete(const OrderCancelDelete& ocd)
{
	Building *b=lookupBuilding(ocd.gid);
	if (b)
	{
		b->cancelDelete();
	}
}

void Game::executeConstruction(const OrderConstruction& oc)
{
	Building *b=lookupBuilding(oc.gid);
	if (b)
	{
		b->launchConstruction(oc.unitWorking, oc.unitWorkingFuture);
	}
}

void Game::executeCancelConstruction(const OrderConstruction& oc)
{
	Building *b=lookupBuilding(oc.gid);
	if (b)
	{
		b->cancelConstruction(oc.unitWorking);
	}
}

void Game::executeSetAlliance(const SetAllianceOrder& sao)
{
	Uint32 team=sao.teamNumber;
	teams[team]->allies=sao.alliedMask;
	teams[team]->enemies=sao.enemyMask;
	teams[team]->sharedVisionExchange=sao.visionExchangeMask;
	teams[team]->sharedVisionFood=sao.visionFoodMask;
	teams[team]->sharedVisionOther=sao.visionOtherMask;
}

void Game::executePlayerQuitGame(const PlayerQuitsGameOrder& pqgo)
{
	bool found = false;
	for(int i=0; i<Team::MAX_COUNT; ++i)
	{
		if(i!=pqgo.player && players[i])
		{
			if(players[i]->teamNumber == players[pqgo.player]->teamNumber)
			{
				found = true;
			}
		}
	}
	if(! found)
	{
		teams[players[pqgo.player]->teamNumber]->isAlive = false;
	}

	players[pqgo.player]->makeItAI(AI::NONE);
	gameHeader.getBasePlayer(pqgo.player).makeItAI(AI::NONE);
}
