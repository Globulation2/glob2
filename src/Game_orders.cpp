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
		{
			std::shared_ptr<OrderCreate> oc=std::static_pointer_cast<OrderCreate>(order);
			if (!isPlayerAlive)
				break;

			int posX=(oc->posX)&map.getMaskW();
			int posY=(oc->posY)&map.getMaskH();
			assert(oc->teamNumber==team->teamNumber);
			BuildingType *bt=globalContainer->buildingsTypes.get(oc->typeNum);
			bool isVirtual=bt->isVirtual;
			int w=bt->width;
			int h=bt->height;
			if (!isVirtual && (team->noMoreBuildingSitesCountdown>0))
				break;
			bool isRoom=checkRoomForBuilding(posX, posY, bt, oc->teamNumber);
			if (isVirtual || isRoom)
			{
				Building *b=addBuilding(posX, posY, oc->typeNum, oc->teamNumber, oc->unitWorking, oc->unitWorkingFuture);
				if (b)
				{
					if(isVirtual && oc->flagRadius>=0)
					{
						b->unitStayRange = oc->flagRadius;
						b->unitStayRangeLocal = oc->flagRadius;
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
				buildProject.teamNumber = oc->teamNumber;
				buildProject.typeNum = oc->typeNum;
				buildProject.unitWorking = oc->unitWorking;
				buildProject.unitWorkingFuture = oc->unitWorkingFuture;
				buildProjects.push_back(buildProject);
				Uint32 teamMask=Team::teamNumberToMask(oc->teamNumber);
				for (int y=posY; y<posY+h; y++)
					for (int x=posX; x<posX+w; x++)
					{
						size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
						map.cases[index].forbidden|=teamMask;
						if (oc->teamNumber == players[localPlayer]->teamNumber)
							map.localForbiddenMap.set(index, true);
					}
				map.updateForbiddenGradient(oc->teamNumber);
			}
		}
		break;
		case ORDER_MODIFY_BUILDING:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifyBuilding> omb=std::static_pointer_cast<OrderModifyBuilding>(order);
			Uint16 gid=omb->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				assert(omb->numberRequested <= MAX_BUILDING_WORKER_REQUEST);
				b->maxUnitWorking=omb->numberRequested;
				b->maxUnitWorkingPreferred=b->maxUnitWorking;
				if (order->sender!=localPlayer)
					b->maxUnitWorkingLocal=b->maxUnitWorking;
				b->update();
			}
		}
		break;
		case ORDER_MODIFY_EXCHANGE:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifyExchange> ome=std::static_pointer_cast<OrderModifyExchange>(order);
			Uint16 gid=ome->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				b->receiveRessourceMask=ome->receiveRessourceMask;
				b->sendRessourceMask=ome->sendRessourceMask;
				if (order->sender!=localPlayer)
				{
					b->receiveRessourceMaskLocal=b->receiveRessourceMask;
					b->sendRessourceMaskLocal=b->sendRessourceMask;
				}
				b->update();
			}
		}
		break;
		case ORDER_MODIFY_FLAG:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifyFlag> omf=std::static_pointer_cast<OrderModifyFlag>(order);
			Uint16 gid=omf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->defaultUnitStayRange))
			{
				int oldRange=b->unitStayRange;
				int newRange=omf->range;
				b->unitStayRange=newRange;
				if (order->sender!=localPlayer)
					b->unitStayRangeLocal=newRange;

				if (b->type->zonableForbidden)
				{
					if (newRange<oldRange)
					{
						teams[team]->dirtyGlobalGradient();
						map.dirtyLocalGradient(b->posX-oldRange-GRADIENT_DIRTY_BORDER_TILES, b->posY-oldRange-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+oldRange*2, 2*GRADIENT_DIRTY_BORDER_TILES+oldRange*2, team);
					}
				}
				else
				{
					b->resetPathfindGradients();
				}
			}
		}
		break;
		case ORDER_MODIFY_CLEARING_FLAG:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifyClearingFlag> omcf=std::static_pointer_cast<OrderModifyClearingFlag>(order);
			Uint16 gid=omcf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b
				&& b->buildingState==Building::ALIVE
				&& b->type->defaultUnitStayRange
				&& b->type->zonable[WORKER])
			{
				memcpy(b->clearingRessources, omcf->clearingRessources, sizeof(bool)*BASIC_COUNT);
				if (order->sender!=localPlayer)
					memcpy(b->clearingRessourcesLocal, omcf->clearingRessources, sizeof(bool)*BASIC_COUNT);
			}
		}
		break;
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifyMinLevelToFlag> omwf=std::static_pointer_cast<OrderModifyMinLevelToFlag>(order);
			int team=Building::GIDtoTeam(omwf->gid);
			int id=Building::GIDtoID(omwf->gid);
			Building *b=teams[team]->myBuildings[id];
			if (b
				&& b->buildingState==Building::ALIVE
				&& b->type->defaultUnitStayRange
				&& (b->type->zonable[WARRIOR] || b->type->zonable[EXPLORER]))
			{
				b->minLevelToFlag = omwf->minLevelToFlag;
				// if it was another player, update local
				if (order->sender != localPlayer)
					b->minLevelToFlagLocal = b->minLevelToFlag;

				// flush all the actual units
				int maxUnitWorkingSaved = b->maxUnitWorking;
				b->maxUnitWorking = 0;
				b->update();
				b->maxUnitWorking = maxUnitWorkingSaved;
				b->update();
			}
		}
		break;
		case ORDER_MOVE_FLAG:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderMoveFlag> omf=std::static_pointer_cast<OrderMoveFlag> (order);
			Uint16 gid=omf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			bool drop=omf->drop;
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->isVirtual))
			{
				if (drop && b->type->zonableForbidden)
				{
					int range=b->unitStayRange;
					map.dirtyLocalGradient(b->posX-range-GRADIENT_DIRTY_BORDER_TILES, b->posY-range-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, team);
				}

				b->posX=omf->x;
				b->posY=omf->y;

				if (b->type->zonableForbidden)
				{
					if (drop)
						teams[team]->dirtyGlobalGradient();
				}
				else
				{
					b->resetPathfindGradients();
				}

				if (order->sender!=localPlayer || globalContainer->replaying)
				{
					b->posXLocal=b->posX;
					b->posYLocal=b->posY;
				}
			}
		}
		break;
		case ORDER_ALTERATE_FORBIDDEN:
		{
			std::shared_ptr<OrderAlterateForbidden> oaa = std::static_pointer_cast<OrderAlterateForbidden>(order);
			if (oaa->type == BrushTool::MODE_ADD)
			{
				Uint32 teamMask = Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].forbidden |= teamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localForbiddenMap.set(index, true);
						}
						orderMaskIndex++;
					}
			}
			else if (oaa->type == BrushTool::MODE_DEL)
			{
				Uint32 notTeamMask = ~Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].forbidden &= notTeamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localForbiddenMap.set(index, false);
						}
						orderMaskIndex++;
					}

				// We remove, so we need to refresh the gradients, unfortunatly
				teams[oaa->teamNumber]->dirtyGlobalGradient();
				map.dirtyLocalGradient(oaa->centerX+oaa->minX-GRADIENT_DIRTY_BORDER_TILES, oaa->centerY+oaa->minY-GRADIENT_DIRTY_BORDER_TILES, oaa->maxX-oaa->minX+2*GRADIENT_DIRTY_BORDER_TILES, oaa->maxY-oaa->minY+2*GRADIENT_DIRTY_BORDER_TILES, oaa->teamNumber);
			}
			else
				assert(false);
			map.updateForbiddenGradient(oaa->teamNumber);
			map.updateGuardAreasGradient(oaa->teamNumber);
			map.updateClearAreasGradient(oaa->teamNumber);
		}
		break;
		case ORDER_ALTERATE_GUARD_AREA:
		{
			std::shared_ptr<OrderAlterateGuardArea> oaa = std::static_pointer_cast<OrderAlterateGuardArea>(order);
			if (oaa->type == BrushTool::MODE_ADD)
			{
				Uint32 teamMask = Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].guardArea |= teamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localGuardAreaMap.set(index, true);
						}
						orderMaskIndex++;
					}
			}
			else if (oaa->type == BrushTool::MODE_DEL)
			{
				Uint32 notTeamMask = ~Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].guardArea &= notTeamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localGuardAreaMap.set(index, false);
						}
						orderMaskIndex++;
					}
			}
			else
				assert(false);
			map.updateGuardAreasGradient(oaa->teamNumber);
		}
		break;
		case ORDER_ALTERATE_CLEAR_AREA:
		{
			std::shared_ptr<OrderAlterateClearArea> oaa = std::static_pointer_cast<OrderAlterateClearArea>(order);
			if (oaa->type == BrushTool::MODE_ADD)
			{
				Uint32 teamMask = Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].clearArea |= teamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localClearAreaMap.set(index, true);
						}
						orderMaskIndex++;
					}
			}
			else if (oaa->type == BrushTool::MODE_DEL)
			{
				Uint32 notTeamMask = ~Team::teamNumberToMask(oaa->teamNumber);
				size_t orderMaskIndex = 0;
				for (int y=oaa->centerY+oaa->minY; y<oaa->centerY+oaa->maxY; y++)
					for (int x=oaa->centerX+oaa->minX; x<oaa->centerX+oaa->maxX; x++)
					{
						if (oaa->mask.get(orderMaskIndex))
						{
							size_t index = (x&map.wMask)+(((y&map.hMask)<<map.wDec));
							// Update real map
							map.cases[index].clearArea &= notTeamMask;
							// Update local map
							if (oaa->teamNumber == players[localPlayer]->teamNumber)
								map.localClearAreaMap.set(index, false);
						}
						orderMaskIndex++;
					}
			}
			else
				assert(false);
			map.updateClearAreasGradient(oaa->teamNumber);
		}
		break;
		case ORDER_MODIFY_SWARM:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderModifySwarm> oms=std::static_pointer_cast<OrderModifySwarm>(order);
			Uint16 gid=oms->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->unitProductionTime))
			{
				for (int j=0; j<NB_UNIT_TYPE; j++)
				{
					b->ratio[j]=oms->ratio[j];
					if (order->sender!=localPlayer)
						b->ratioLocal[j]=b->ratio[j];
				}
				b->update();
			}
		}
		break;
		case ORDER_DELETE:
		{
			Uint16 gid=std::static_pointer_cast<OrderDelete>(order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				b->launchDelete();
				assert(b->type);
				if (b->type->zonableForbidden)
				{
					teams[team]->dirtyGlobalGradient();
					int range=b->unitStayRange;
					map.dirtyLocalGradient(b->posX-range-GRADIENT_DIRTY_BORDER_TILES, b->posY-range-GRADIENT_DIRTY_BORDER_TILES, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, 2*GRADIENT_DIRTY_BORDER_TILES+range*2, team);
				}
			}
		}
		break;
		case ORDER_CHANGE_PRIORITY:
		{
			Uint16 gid=std::static_pointer_cast<OrderChangePriority>(order)->gid;
			Sint32 priority=std::static_pointer_cast<OrderChangePriority>(order)->priority;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				b->priority = priority;
				b->updateCallLists();
			}
		}
		break;
		case ORDER_CANCEL_DELETE:
		{
			Uint16 gid=std::static_pointer_cast<OrderCancelDelete>(order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				b->cancelDelete();
			}
		}
		break;
		case ORDER_CONSTRUCTION:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderConstruction> oc = std::static_pointer_cast<OrderConstruction>(order);
			Uint16 gid = oc->gid;

			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				b->launchConstruction(oc->unitWorking, oc->unitWorkingFuture);
			}
		}
		break;
		case ORDER_CANCEL_CONSTRUCTION:
		{
			if (!isPlayerAlive)
				break;
			std::shared_ptr<OrderConstruction> oc = std::static_pointer_cast<OrderConstruction>(order);
			Uint16 gid=oc->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				b->cancelConstruction(oc->unitWorking);
			}
		}
		break;
		case ORDER_SET_ALLIANCE:
		{
			std::shared_ptr<SetAllianceOrder> sao=std::static_pointer_cast<SetAllianceOrder>(order);
			Uint32 team=sao->teamNumber;
			teams[team]->allies=sao->alliedMask;
			teams[team]->enemies=sao->enemyMask;
			teams[team]->sharedVisionExchange=sao->visionExchangeMask;
			teams[team]->sharedVisionFood=sao->visionFoodMask;
			teams[team]->sharedVisionOther=sao->visionOtherMask;
		}
		break;
		case ORDER_PLAYER_QUIT_GAME:
		{
			std::shared_ptr<PlayerQuitsGameOrder> pqgo=std::static_pointer_cast<PlayerQuitsGameOrder>(order);

			bool found = false;
			for(int i=0; i<Team::MAX_COUNT; ++i)
			{
				if(i!=pqgo->player && players[i])
				{
					if(players[i]->teamNumber == players[pqgo->player]->teamNumber)
					{
						found = true;
					}
				}
			}
			if(! found)
			{
				teams[players[pqgo->player]->teamNumber]->isAlive = false;
			}

			players[pqgo->player]->makeItAI(AI::NONE);
			gameHeader.getBasePlayer(pqgo->player).makeItAI(AI::NONE);
		}
		break;
	}
}
