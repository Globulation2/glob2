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
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Unit.h"
#include "UnitSkin.h"
#include "Utilities.h"
#include "GameGUI.h"

#include "MapEdit.h"

#include "Brush.h"
#include "DynamicClouds.h"

#include "TextStream.h"
#include "FertilityCalculatorDialog.h"

#include "NetMessage.h"

#define BULLET_IMGID 0

#define MIN_MAX_PRESIGE 500
#define TEAM_MAX_PRESTIGE 150

Game::Game(GameGUI *gui, MapEdit* edit):
	mapscript(gui)
{
	logFile = globalContainer->logFileManager->getFile("Game.log");
	
	isRecordingReplay = false;
	
	init(gui, edit);
}

Game::~Game()
{
	int sum=0;
	for (int i=0; i<Team::MAX_COUNT; i++)
		sum+=ticksGameSum[i];
	if (sum)
	{
		fprintf(logFile, "(sync)stepCounter=%d\n", stepCounter);
		fprintf(logFile, "execution time of Game::step: sum=%d\n", sum);
		for (int i=0; i<Team::MAX_COUNT; i++)
			fprintf(logFile, "ticksGameSum[%2d]=%8d, (%f %%)\n", i, ticksGameSum[i], (float)ticksGameSum[i]*100./(float)sum);
		fprintf(logFile, "\n");
		for (int i=0; i<Team::MAX_COUNT; i++)
		{
			fprintf(logFile, "ticksGameSum[%2d]=", i);
			for (int j=0; j<(int)(0.5+(float)ticksGameSum[i]*100./(float)sum); j++)
				fprintf(logFile, "*");
			fprintf(logFile, "\n");
		}
	}

	overlayAlphas.resize(0);

	clearGame();

	if (isRecordingReplay)
	{
		delete replay;
	}
}

void Game::init(GameGUI *gui, MapEdit* edit)
{
	this->gui=gui;
	this->edit=edit;
	buildProjects.clear();

	mapHeader.reset();
	gameHeader.reset();

	teams=new Team*[Team::MAX_COUNT];
	players=new Player*[Team::MAX_COUNT];
	for (int i=0; i<Team::MAX_COUNT; i++)
	{
		teams[i]=NULL;
		players[i]=NULL;
	}
	clearGame();

	mouseX=0;
	mouseY=0;

	stepCounter=0;
	prestigeToReach=0;
	
	ticksGameSum=new int[Team::MAX_COUNT];
	for (int i=0; i<Team::MAX_COUNT; i++)
		ticksGameSum[i]=0;

	anyPlayerWaitedTimeFor = 0;
	maskAwayPlayer = 0;
}


/** Reset player and team lists, game end stuff and selection stuff. */
void Game::clearGame()
{
	// Delete existing teams and players
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (teams[i])
		{
			delete teams[i];
			teams[i]=NULL;
		}
	}
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if (players[i])
		{
			delete players[i];
			players[i]=NULL;
		}
	}

	///Clears prestige
	totalPrestige=0;
	totalPrestigeReached=false;
	isGameEnded=false;

	// Clears selections
	mouseUnit = NULL;
	selectedUnit = NULL;
	selectedBuilding = NULL;

	hilightBuildingType=0;
	hilightUnitType=0;
}



void Game::setMapHeader(const MapHeader& newMapHeader)
{
	mapHeader = newMapHeader;

	// set the base team, for now the number is corect but we should check that further
	for (int i=0; i<newMapHeader.getNumberOfTeams(); i++)
		teams[i]->setBaseTeam(&newMapHeader.getBaseTeam(i));
}



void Game::setGameHeader(const GameHeader& newGameHeader, bool saveAI)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->playersMask=0;
		teams[i]->numberOfPlayer=0;
	}

	for (int i=0; i<newGameHeader.getNumberOfPlayers(); i++)
	{
		//Don't change AI's
		if(!saveAI || gameHeader.getBasePlayer(i).type < BasePlayer::P_AI)
		{
			delete players[i];
			players[i]=new Player();
			players[i]->setBasePlayer(&newGameHeader.getBasePlayer(i), teams);
		}
		teams[players[i]->teamNumber]->numberOfPlayer+=1;
		teams[players[i]->teamNumber]->playersMask|=(1<<i);
	}

	setSyncRandSeed(newGameHeader.getRandomSeed());

	if(newGameHeader.isMapDiscovered())
		map.setMapDiscovered();

	gameHeader = newGameHeader;
	anyPlayerWaited=false;
}

void Game::executeOrder(boost::shared_ptr<Order> order, int localPlayer)
{
	assert(order->sender>=0);
	assert(order->sender<Team::MAX_COUNT);
	assert(order->sender < gameHeader.getNumberOfPlayers());

	if (isRecordingReplay && order->getOrderType() != ORDER_VOICE_DATA) // TODO: optionally save VOIP
	{
		assert(replay);

		// Steps since last order

		replay->writeUint32(replayStepCounter, "replayStepCounter" );
		replayStepCounter=0;

		// Write actual order to replay

		NetSendOrder* msg = new NetSendOrder(order);
		msg->encodeData(replay);
		delete msg;
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
			boost::shared_ptr<OrderCreate> oc=boost::static_pointer_cast<OrderCreate>(order);
			if (!isPlayerAlive)
				break;

			int posX=(oc->posX)&map.getMaskW();
			int posY=(oc->posY)&map.getMaskH();;
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
					fprintf(logFile, "ORDER_CREATE (%d, %d, %d)", posX, posY, bt->shortTypeNum);
					b->owner->addToStaticAbilitiesLists(b);
					b->update();
				}
			}
			else if (!isVirtual && !isRoom && map.isHardSpaceForBuilding(posX, posY, w, h))
			{
				BuildProject buildProject;
				buildProject.posX = posX;
				buildProject.posY = posY;
				fprintf(logFile, "new BuildProject (%d, %d)", posX, posY);
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
			boost::shared_ptr<OrderModifyBuilding> omb=boost::static_pointer_cast<OrderModifyBuilding>(order);
			Uint16 gid=omb->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				fprintf(logFile, "ORDER_MODIFY_BUILDING");
				assert(omb->numberRequested <= 20);
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
			boost::shared_ptr<OrderModifyExchange> ome=boost::static_pointer_cast<OrderModifyExchange>(order);
			Uint16 gid=ome->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				fprintf(logFile, "ORDER_MODIFY_EXCHANGE");
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
			boost::shared_ptr<OrderModifyFlag> omf=boost::static_pointer_cast<OrderModifyFlag>(order);
			Uint16 gid=omf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->defaultUnitStayRange))
			{
				fprintf(logFile, "ORDER_MODIFY_FLAG");
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
						map.dirtyLocalGradient(b->posX-oldRange-16, b->posY-oldRange-16, 32+oldRange*2, 32+oldRange*2, team);
					}
				}
				else
				{
					for (int i=0; i<2; i++)
					{
						b->dirtyLocalGradient[i]=true;
						b->locked[i]=false;
						if (b->globalGradient[i])
						{
							delete b->globalGradient[i];
							b->globalGradient[i]=NULL;
						}
						if (b->localRessources[i])
						{
							delete b->localRessources[i];
							b->localRessources[i]=NULL;
						}
					}
				}
			}
		}
		break;
		case ORDER_MODIFY_CLEARING_FLAG:
		{
			if (!isPlayerAlive)
				break;
			boost::shared_ptr<OrderModifyClearingFlag> omcf=boost::static_pointer_cast<OrderModifyClearingFlag>(order);
			Uint16 gid=omcf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b
				&& b->buildingState==Building::ALIVE
				&& b->type->defaultUnitStayRange
				&& b->type->zonable[WORKER])
			{
				fprintf(logFile, "ORDER_MODIFY_CLEARING_FLAG");
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
			boost::shared_ptr<OrderModifyMinLevelToFlag> omwf=boost::static_pointer_cast<OrderModifyMinLevelToFlag>(order);
			int team=Building::GIDtoTeam(omwf->gid);
			int id=Building::GIDtoID(omwf->gid);
			Building *b=teams[team]->myBuildings[id];
			if (b
				&& b->buildingState==Building::ALIVE
				&& b->type->defaultUnitStayRange
				&& (b->type->zonable[WARRIOR] || b->type->zonable[EXPLORER]))
			{
				fprintf(logFile, "ORDER_MODIFY_MIN_LEVEL_TO_FLAG");
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
			boost::shared_ptr<OrderMoveFlag> omf=boost::static_pointer_cast<OrderMoveFlag> (order);
			Uint16 gid=omf->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			bool drop=omf->drop;
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->isVirtual))
			{
				fprintf(logFile, "ORDER_MOVE_FLAG");
				if (drop && b->type->zonableForbidden)
				{
					int range=b->unitStayRange;
					map.dirtyLocalGradient(b->posX-range-16, b->posY-range-16, 32+range*2, 32+range*2, team);
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
					for (int i=0; i<2; i++)
					{
						b->dirtyLocalGradient[i]=true;
						b->locked[i]=false;
						if (b->globalGradient[i])
						{
							delete[] b->globalGradient[i];
							b->globalGradient[i]=NULL;
						}
						if (b->localRessources[i])
						{
							delete b->localRessources[i];
							b->localRessources[i]=NULL;
						}
					}
				}

				if (order->sender!=localPlayer)
				{
					b->posXLocal=b->posX;
					b->posYLocal=b->posY;
				}
			}
		}
		break;
		case ORDER_ALTERATE_FORBIDDEN:
		{
			fprintf(logFile, "ORDER_ALTERATE_FORBIDDEN");
			boost::shared_ptr<OrderAlterateForbidden> oaa = boost::static_pointer_cast<OrderAlterateForbidden>(order);
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
				map.dirtyLocalGradient(oaa->centerX+oaa->minX-16, oaa->centerY+oaa->minY-16, oaa->maxX-oaa->minX+32, oaa->maxY-oaa->minY+32, oaa->teamNumber);
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
			fprintf(logFile, "ORDER_ALTERATE_GUARD_AREA");
			boost::shared_ptr<OrderAlterateGuardArea> oaa = boost::static_pointer_cast<OrderAlterateGuardArea>(order);
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
			fprintf(logFile, "ORDER_ALTERATE_CLEAR_AREA");
			boost::shared_ptr<OrderAlterateClearArea> oaa = boost::static_pointer_cast<OrderAlterateClearArea>(order);
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
			boost::shared_ptr<OrderModifySwarm> oms=boost::static_pointer_cast<OrderModifySwarm>(order);
			Uint16 gid=oms->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE) && (b->type->unitProductionTime))
			{
				fprintf(logFile, "ORDER_MODIFY_SWARM");
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
			Uint16 gid=boost::static_pointer_cast<OrderDelete>(order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_DELETE");
				b->launchDelete();
				assert(b->type);
				if (b->type->zonableForbidden)
				{
					teams[team]->dirtyGlobalGradient();
					int range=b->unitStayRange;
					map.dirtyLocalGradient(b->posX-range-16, b->posY-range-16, 32+range*2, 32+range*2, team);
				}
			}
		}
		break;
		case ORDER_CHANGE_PRIORITY:
		{
			Uint16 gid=boost::static_pointer_cast<OrderChangePriority>(order)->gid;
			Sint32 priority=boost::static_pointer_cast<OrderChangePriority>(order)->priority;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CHANGE_PRIORITY");
				b->priority = priority;
				b->updateCallLists();
			}
		}
		break;
		case ORDER_CANCEL_DELETE:
		{
			Uint16 gid=boost::static_pointer_cast<OrderCancelDelete>(order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CANCEL_DELETE");
				b->cancelDelete();
			}
		}
		break;
		case ORDER_CONSTRUCTION:
		{
			if (!isPlayerAlive)
				break;
			boost::shared_ptr<OrderConstruction> oc = boost::static_pointer_cast<OrderConstruction>(order);
			Uint16 gid = oc->gid;

			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CONSTRUCTION");
				b->launchConstruction(oc->unitWorking, oc->unitWorkingFuture);
			}
		}
		break;
		case ORDER_CANCEL_CONSTRUCTION:
		{
			if (!isPlayerAlive)
				break;
			boost::shared_ptr<OrderConstruction> oc = boost::static_pointer_cast<OrderConstruction>(order);
			Uint16 gid=oc->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CANCEL_CONSTRUCTION");
				b->cancelConstruction(oc->unitWorking);
			}
		}
		break;
		case ORDER_SET_ALLIANCE:
		{
			boost::shared_ptr<SetAllianceOrder> sao=boost::static_pointer_cast<SetAllianceOrder>(order);
			Uint32 team=sao->teamNumber;
			teams[team]->allies=sao->alliedMask;
			teams[team]->enemies=sao->enemyMask;
			teams[team]->sharedVisionExchange=sao->visionExchangeMask;
			teams[team]->sharedVisionFood=sao->visionFoodMask;
			teams[team]->sharedVisionOther=sao->visionOtherMask;
			fprintf(logFile, "ORDER_SET_ALLIANCE");
		}
		break;
		case ORDER_PLAYER_QUIT_GAME:
		{
			boost::shared_ptr<PlayerQuitsGameOrder> pqgo=boost::static_pointer_cast<PlayerQuitsGameOrder>(order);

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
			fprintf(logFile, "ORDER_PLAYER_QUIT_GAME");
		}
		break;
	}
}



void Game::setAlliances(void)
{
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		int allyTeam = gameHeader.getAllyTeamNumber(i);
		teams[i]->allies = 0;
		teams[i]->enemies = 0;
		for(int j=0; j<mapHeader.getNumberOfTeams(); ++j)
		{
			int otherAllyTeam = gameHeader.getAllyTeamNumber(j);
			if(allyTeam == otherAllyTeam)
			{
				teams[i]->allies |= teams[j]->me;
				teams[i]->sharedVisionOther |= teams[j]->me;
			}
			else
			{
				teams[i]->enemies |= teams[j]->me;
			}
		}
	}
}

bool Game::load(GAGCore::InputStream *stream)
{
	assert(stream);

	// Initialize the replay
	isRecordingReplay = !globalContainer->replaying; // TODO: provide an option for this
	if (isRecordingReplay)
	{
		replayStepCounter = 0;
		replay = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("replays/last_game.replay"));
	}

	stream->readEnterSection("Game");

	///Clears any previous game
	clearGame();
	mapHeader.reset();
	gameHeader.reset();

	// We load the map header
	MapHeader tempMapHeader;
	if (verbose)
		printf("Loading map header\n");
	if (!tempMapHeader.load(stream))
	{
		fprintf(logFile, "Game::load::tempMapHeader.load\n");
		stream->readLeaveSection();
		return false;
	}
	mapHeader=tempMapHeader;
	Sint32 versionMinor=mapHeader.getVersionMinor();


	// We load the game header
	GameHeader tempGameHeader;
	if (verbose)
		printf("Loading game header\n");
	if (!tempGameHeader.load(stream, versionMinor))
	{
		fprintf(logFile, "Game::load::tempMapHeader.load\n");
		stream->readLeaveSection();
		return false;
	}
	gameHeader=tempGameHeader;

	// Test the beginning signature. Signatures are basic corruption tests.
	// Since Game loads many other structures, it has many of them.
	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature,"GaBe", 4)!=0)
	{
		fprintf(logFile, "Signature missmatch at Game::load begin\n");
		stream->readLeaveSection();
		return false;
	}

	///Load the step counter
	stepCounter = stream->readUint32("stepCounter");

	if(versionMinor < 64)
	{
		///Load random seeds, these are no longer used
		stream->readUint32("SyncRandSeedA");
		stream->readUint32("SyncRandSeedB");
		stream->readUint32("SyncRandSeedC");

		stream->read(signature, 4, "signatureAfterSyncRand");
		if (memcmp(signature,"GaSy", 4)!=0)
		{
			fprintf(logFile, "Signature missmatch after Game::load sync rand\n");
			stream->readLeaveSection();
			return false;
		}
	}
	else
	{
		stream->read(signature, 4, "signatureBeforeTeams");
		if (memcmp(signature,"GaBt", 4)!=0)
		{
			fprintf(logFile, "Signature missmatch before Game::load teams \n");
			stream->readLeaveSection();
			return false;
		}
	}

	///Load teams
	stream->readEnterSection("teams");
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		stream->readEnterSection(i);
		teams[i]=new Team(stream, this, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureAfterTeams");
	if (memcmp(signature,"GaTe", 4)!=0)
	{
		fprintf(logFile, "Signature missmatch after Game::load teams\n");
		stream->readLeaveSection();
		return false;
	}

	// Load the map. Team has to be saved and loaded first.
	if(!map.load(stream, mapHeader, this))
	{
		fprintf(logFile, "Signature missmatch in map\n");
		stream->readLeaveSection();
		return false;
	}

	stream->read(signature, 4, "signatureAfterMap");
	if (memcmp(signature,"GaMa", 4)!=0)
	{
		fprintf(logFile, "Signature missmatch after map\n");
		stream->readLeaveSection();
		return false;
	}

	// Load the players. Both Map and Team must be loaded first.
	stream->readEnterSection("players");
	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		stream->readEnterSection(i);
		players[i]=new Player(stream, teams, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureAfterPlayers");
	if (memcmp(signature,"GaPl", 4)!=0)
	{
		fprintf(logFile, "Signature missmatch after players\n");
		stream->readLeaveSection();
		return false;
	}

	// We have to finish Team's loading
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		teams[i]->update();
	}

	// Check integrity of loaded game
	integrity();

	// Now load the old map script
	if (!script.load(stream, this))
	{
		stream->readLeaveSection();
		return false;
	}

	if(versionMinor >= 80)
	{
		// This is the new map script system
		mapscript.decodeData(stream, mapHeader.getVersionMinor());
	}

	///Load the campaign text for the game.
	if(versionMinor < 75)
		stream->readText("campaignText");

	// default prestige calculation
	prestigeToReach = std::max(MIN_MAX_PRESIGE, mapHeader.getNumberOfTeams()*TEAM_MAX_PRESTIGE);

	if(mapHeader.getVersionMinor() >= 75)
	{
		objectives.decodeData(stream, mapHeader.getVersionMinor());
	}

	if(mapHeader.getVersionMinor() >= 76)
	{
		missionBriefing = stream->readText("briefing");
		gameHints.decodeData(stream, mapHeader.getVersionMinor());
	}

	stream->readLeaveSection();

	///versions less than 63 did not have fertility computed with the map, but computed it live.
	///compute it now
	if(mapHeader.getVersionMinor() < 63)
	{
	    if(globalContainer->runNoX)
	    {
    	    std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> > incoming;
    	    boost::recursive_mutex incomingMutex;
    	    FertilityCalculatorThread calculator(map, incoming, incomingMutex);
    	    calculator();
	    }
	    else
	    {
    		FertilityCalculatorDialog dialog(globalContainer->gfx, map);
	    	dialog.execute();
	    }
	}

	return true;
}

void Game::integrity(void)
{
	///Check teams integrity
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		teams[i]->integrity();

	///Check that all ID do point to existing objects
	for (int y=0; y<map.getH(); y++)
		for (int x=0; x<map.getW(); x++)
		{
			Case& c = map.getCase(x, y);
			if (c.building != NOGBID)
			{
				int tid = Building::GIDtoTeam(c.building);
				assert(teams[tid]);
				assert(teams[tid]->myBuildings[Building::GIDtoID(c.building)]);
			}
			if (c.groundUnit != NOGUID)
			{
				int tid = Unit::GIDtoTeam(c.groundUnit);
				assert(teams[tid]);
				assert(teams[tid]->myUnits[Unit::GIDtoID(c.groundUnit)]);
			}
			if (c.airUnit != NOGUID)
			{
				int tid = Unit::GIDtoTeam(c.airUnit);
				assert(teams[tid]);
				assert(teams[tid]->myUnits[Unit::GIDtoID(c.airUnit)]);
			}
		}
}

void Game::save(GAGCore::OutputStream *stream, bool fileIsAMap, const std::string& name)
{
	assert(stream);
	stream->writeEnterSection("Game");
	if(dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
	{
		dynamic_cast<GAGCore::BinaryOutputStream*>(stream)->enableSHA1();
	}

	///Save the two headers, record the position in the file because mapHeader will
	///will need to be overwritten with the mapOffset known
	Uint32 mapHeaderOffset = stream->getPosition();
	mapHeader.setMapName(name);
	mapHeader.setIsSavedGame(!fileIsAMap);
	mapHeader.resetGameSHA1();

	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		mapHeader.getBaseTeam(i)=*teams[i];
		mapHeader.getBaseTeam(i).disableRecursiveDestruction=true;
	}

	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		gameHeader.getBasePlayer(i)=*players[i];
		gameHeader.getBasePlayer(i).disableRecursiveDestruction=true;
	}

	mapHeader.save(stream);
	gameHeader.save(stream);

	///Save basic informations
	stream->write("GaBe", 4, "signatureStart");
	stream->writeUint32(stepCounter, "stepCounter");
	stream->write("GaBt", 4, "signatureBeforeTeams");

	///Save teams
	stream->writeEnterSection("teams");
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		stream->writeEnterSection(i);
		teams[i]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->write("GaTe", 4, "signatureAfterTeams");


	///Save the map offset to the header, before we save the map
	///Then, save the map
	mapHeader.setMapOffset(stream->getPosition());
	map.save(stream);
	stream->write("GaMa", 4, "signatureAfterMap");

	///Save the players
	stream->writeEnterSection("players");
	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		stream->writeEnterSection(i);
		players[i]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->write("GaPl", 4, "signatureAfterPlayers");

	// Save the old map script state
	script.save(stream, this);

	// This is the new map script system
	mapscript.encodeData(stream);

	///Save game objectives
	objectives.encodeData(stream);
	stream->writeText(missionBriefing, "missionBriefing");
	gameHints.encodeData(stream);

	Uint8 sha1[20];
	for(int i=0; i<20; ++i)
		sha1[i]=0;
	if(dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
	{
		dynamic_cast<GAGCore::BinaryOutputStream*>(stream)->finishSHA1(sha1);
	}
	mapHeader.setGameSHA1(sha1);

	///Overwrite the MapHeader. This is done after the map
	///offset has been set.
	if (stream->canSeek())
	{
		Uint32 position = stream->getPosition();
		stream->seekFromStart(mapHeaderOffset);
		mapHeader.save(stream);
		stream->seekFromStart(position);
	}

	stream->writeLeaveSection();
}

void Game::buildProjectSyncStep(Sint32 localTeam)
{
	for (std::list<BuildProject>::iterator bpi=buildProjects.begin(); bpi!=buildProjects.end();)
	{
		int posX=bpi->posX&map.getMaskW();
		int posY=bpi->posY&map.getMaskH();
		int teamNumber=bpi->teamNumber;
		Sint32 typeNum=(bpi->typeNum);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
		int w=bt->width;
		int h=bt->height;
		if (!map.isHardSpaceForBuilding(posX, posY, w, h))
		{
			fprintf(logFile, "BuildProject failure (%d, %d)\n", posX, posY);
			Uint32 notTeamMask=~Team::teamNumberToMask(teamNumber);
			for (int y=posY; y<posY+h; y++)
				for (int x=posX; x<posX+w; x++)
				{
					size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].forbidden&=notTeamMask;
					// Update local map
					if (teamNumber == localTeam)
						map.localForbiddenMap.set(index, false);
				}
			map.updateForbiddenGradient(teamNumber);
			std::list<BuildProject>::iterator to_erase=bpi;
			bpi++;
			buildProjects.erase(to_erase);
			continue;
		}
		else if (checkRoomForBuilding(posX, posY, bt, teamNumber))
		{
			Building *b=addBuilding(posX, posY, typeNum, teamNumber, bpi->unitWorking, bpi->unitWorkingFuture);
			if (b)
			{
				Uint32 notTeamMask=~Team::teamNumberToMask(teamNumber);
				for (int y=posY; y<posY+h; y++)
					for (int x=posX; x<posX+w; x++)
					{
						size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
						// Update real map
						map.cases[index].forbidden&=notTeamMask;
						// Update local map
						if (teamNumber == localTeam)
							map.localForbiddenMap.set(index, false);
					}
				map.updateForbiddenGradient(teamNumber);
				b->owner->addToStaticAbilitiesLists(b);
				b->update();
				fprintf(logFile, "BuildProject success (%d, %d)\n", posX, posY);
				std::list<BuildProject>::iterator to_erase=bpi;
				bpi++;
				buildProjects.erase(to_erase);
				continue;
			}
		}
		bpi++;
	}
}

void Game::wonSyncStep(void)
{
	//TODO: sideeffects? 
	//std::list<boost::shared_ptr<WinningCondition> >& conditions = 
	gameHeader.getWinningConditions();

	bool areAllDecided=true;
	//We do this twice, because some win conditions depend on other win conditions
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->checkWinConditions();
	}
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->checkWinConditions();
		if(teams[i]->winCondition == WCUnknown)
			areAllDecided=false;
	}
	isGameEnded = areAllDecided;

}

void Game::scriptSyncStep()
{
	// do a script step
	script.syncStep(gui);
	mapscript.syncStep(gui);
}



void Game::prestigeSyncStep()
{
	totalPrestige=0;
	totalPrestigeReached=false;
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		totalPrestige += teams[i]->prestige;
	}
	if(totalPrestige >= prestigeToReach)
	{
		totalPrestigeReached=true;
	}
}



void Game::syncStep(Sint32 localTeam)
{
	if (isRecordingReplay) replayStepCounter++;

	if (!anyPlayerWaited)
	{
		Sint32 startTick=SDL_GetTicks();

		for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
			teams[i]->syncStep();

		map.syncStep(stepCounter);

		syncRand();

		if ((stepCounter&31)==16)
		{
			map.switchFogOfWar();
			for (int t=0; t<mapHeader.getNumberOfTeams(); t++)
				for (int i=0; i<Building::MAX_COUNT; i++)
				{
					Building *b=teams[t]->myBuildings[i];
					if (b)
					{
						assert(b->owner==teams[t]);
						assert(b->type);
					}
					if ((b)&&(!b->type->isBuildingSite || (b->type->level>0))&&(!b->type->isVirtual))
					{
						b->setMapDiscovered();
					}
				}
		}

		if ((stepCounter&15)==1)
			buildProjectSyncStep(localTeam);

		if ((stepCounter&31)==0)
		{
			prestigeSyncStep();
			scriptSyncStep();
			wonSyncStep();
		}

		Sint32 endTick=SDL_GetTicks();
		ticksGameSum[stepCounter&31]+=endTick-startTick;
		stepCounter++;
		anyPlayerWaitedTimeFor+=1;
	}
}

void Game::dirtyWarFlagGradient(void)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		teams[i]->dirtyWarFlagGradient();
}

// Script interface

int Game::isTeamAlive(int team)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams())
	)
		return teams[team]->isAlive;
	else
		return false;
}

int Game::unitsCount(int team, int type)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < NB_UNIT_TYPE)
	)
		return teams[team]->stats.getLatestStat()->numberUnitPerType[type];
	else
		return 0;
}

int Game::unitsUpgradesCount(int team, int type, int ability, int level)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < NB_UNIT_TYPE) &&
		(ability >= 0) && (ability < NB_ABILITY) &&
		(level >= 0) && (level < NB_UNIT_LEVELS)
	)
		return teams[team]->stats.getLatestStat()->upgradeStatePerType[type][ability][level];
	else
		return 0;
}

int Game::buildingsCount(int team, int type, int level)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < IntBuildingType::NB_BUILDING) &&
		(level >= 0) && (level < 6)
	)
		return teams[team]->stats.getLatestStat()->numberBuildingPerTypePerLevel[type][level];
	else
		return 0;
}


void Game::addTeam(int pos)
{
	assert(mapHeader.getNumberOfTeams()<Team::MAX_COUNT);
	if(pos==-1)
		pos=mapHeader.getNumberOfTeams();
	teams[pos]=new Team(this);
	teams[pos]->teamNumber=mapHeader.getNumberOfTeams();
	teams[pos]->race.load();
	teams[pos]->setCorrectMasks();

	pos=mapHeader.getNumberOfTeams();
	pos+=1;
	mapHeader.setNumberOfTeams(pos);
	for (int i=0; i<pos; i++)
		teams[i]->setCorrectColor( ((float)i*360.0f) /(float)pos );

	prestigeToReach = std::max(MIN_MAX_PRESIGE, pos*TEAM_MAX_PRESTIGE);

	map.addTeam();

	script.addTeam();
}

void Game::removeTeam(int pos)
{
	if(pos==-1)
	{
		pos=mapHeader.getNumberOfTeams();
		pos-=1;
		mapHeader.setNumberOfTeams(pos);
	}
	if (mapHeader.getNumberOfTeams()>0)
	{
		Team *team=teams[pos];

		team->clearMap();

		delete team;
		assert (mapHeader.getNumberOfTeams()!=0);
		for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
			teams[i]->setCorrectColor(((float)i*360.0f)/(float)mapHeader.getNumberOfTeams());

		map.removeTeam();
		script.removeTeam(pos);
		teams[pos]=NULL;
	}
}

void Game::clearingUncontrolledTeams(void)
{
	for (int ti=0; ti<mapHeader.getNumberOfTeams(); ti++)
	{
		Team *team=teams[ti];
		if (team->playersMask==0)
		{
			fprintf(logFile, "clearing team %d\n", ti);
			team->clearMap();
			team->clearLists();
			team->clearMem();
		}
	}
}

void Game::regenerateDiscoveryMap(void)
{
	map.unsetMapDiscovered();
	for (int t=0; t<mapHeader.getNumberOfTeams(); t++)
	{
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=teams[t]->myUnits[i];
			if (u)
			{
				map.setMapDiscovered(u->posX-1, u->posY-1, 3, 3, teams[t]->sharedVisionOther);
			}
		}
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=teams[t]->myBuildings[i];
			if (b)
			{
				b->setMapDiscovered();
			}
		}
	}
}

Unit *Game::addUnit(int x, int y, int team, Sint32 typeNum, int level, int delta, int dx, int dy)
{
	assert(team<mapHeader.getNumberOfTeams());

	UnitType *ut=teams[team]->race.getUnitType(typeNum, level);

	x = (x + map.getW()) % map.getW();
	y = (y + map.getH()) % map.getH();

	bool fly=ut->performance[FLY];
	bool free;
	if (fly)
		free=map.isFreeForAirUnit(x, y);
	else
		free=map.isFreeForGroundUnit(x, y, ut->performance[SWIM], Team::teamNumberToMask(team));
	if (!free)
		return NULL;

	int id=-1;
	for (int i=0; i<Unit::MAX_COUNT; i++)//we search for a free place for a unit.
		if (teams[team]->myUnits[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==-1)
		return NULL;

	//ok, now we can safely deposite an unit.
	int gid=Unit::GIDfrom(id, team);
	if (fly)
		map.setAirUnit(x, y, gid);
	else
		map.setGroundUnit(x, y, gid);

	teams[team]->myUnits[id]= new Unit(x, y, gid, typeNum, teams[team], level);
	teams[team]->myUnits[id]->dx=dx;
	teams[team]->myUnits[id]->dy=dy;
	teams[team]->myUnits[id]->directionFromDxDy();
	teams[team]->myUnits[id]->delta=delta;
	teams[team]->myUnits[id]->selectPreferedMovement();
	return teams[team]->myUnits[id];
}

Building *Game::addBuilding(int x, int y, int typeNum, int teamNumber, Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	Team *team=teams[teamNumber];
	assert(team);

	int id=-1;
	for (int i=0; i<Building::MAX_COUNT; i++)//we search for a free place for a building.
		if (team->myBuildings[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==-1)
	{
		//TODO:Building limit reached!
		return NULL;
	}

	//ok, now we can safely deposite an building.
	int gid=Building::GIDfrom(id, teamNumber);

	int w=globalContainer->buildingsTypes.get(typeNum)->width;
	int h=globalContainer->buildingsTypes.get(typeNum)->height;

	Building *b=new Building(x&map.getMaskW(), y&map.getMaskH(), gid, typeNum, team, &globalContainer->buildingsTypes, unitWorking, unitWorkingFuture);

	if (b->type->canExchange)
		team->canExchange.push_front(b);
	if (b->type->isVirtual)
		team->virtualBuildings.push_front(b);
	else
		map.setBuilding(x, y, w, h, gid);
	team->myBuildings[id]=b;
	return b;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, unsigned flags)
{
	bool found=false;
	if (flags & DEL_GROUND_UNIT)
	{
		Uint16 gauid=map.getAirUnit(x, y);
		if (gauid!=NOGUID)
		{
			int id=Unit::GIDtoID(gauid);
			int team=Unit::GIDtoTeam(gauid);
			map.setAirUnit(x, y, NOGUID);
			delete (teams[team]->myUnits[id]);
			teams[team]->myUnits[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_AIR_UNIT)
	{
		Uint16 gguid=map.getGroundUnit(x, y);
		if (gguid!=NOGUID)
		{
			int id=Unit::GIDtoID(gguid);
			int team=Unit::GIDtoTeam(gguid);
			map.setGroundUnit(x, y, NOGUID);
			delete (teams[team]->myUnits[id]);
			teams[team]->myUnits[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_BUILDING)
	{
		Uint16 gbid=map.getBuilding(x, y);
		if (gbid!=NOGBID)
		{
			int id=Building::GIDtoID(gbid);
			int team=Building::GIDtoTeam(gbid);
			Building *b=teams[team]->myBuildings[id];
			if (!b->type->isVirtual)
				map.setBuilding(b->posX, b->posY, b->type->width, b->type->height, NOGBID);
			delete b;
			teams[team]->myBuildings[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_FLAG)
	{
		for (int ti=0; ti<mapHeader.getNumberOfTeams(); ti++)
			for (std::list<Building *>::iterator bi=teams[ti]->virtualBuildings.begin(); bi!=teams[ti]->virtualBuildings.end(); ++bi)
				if ((*bi)->posX==x && (*bi)->posY==y)
				{
					teams[ti]->virtualBuildings.erase(bi);
					teams[ti]->myBuildings[Building::GIDtoID((*bi)->gid)]=NULL;;
					delete *bi;
					found=true;
					break;
				}
	}
	return found;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, int size, unsigned flags)
{
	int sts = size>>1;
	int stp = (~size)&1;
	bool somethingInRect = false;

	for (int scx=(x-sts); scx<=(x+sts-stp); scx++)
		for (int scy=(y-sts); scy<=(y+sts-stp); scy++)
			if (removeUnitAndBuildingAndFlags((scx&(map.getMaskW())), (scy&(map.getMaskH())), flags))
				somethingInRect = true;

	return somethingInRect;
}

bool Game::checkRoomForBuilding(int mousePosX, int mousePosY, const BuildingType *bt, int *buildingPosX, int *buildingPosY, int teamNumber, bool checkFow)
{
	int x=mousePosX+bt->decLeft;
	int y=mousePosY+bt->decTop;

	*buildingPosX=x;
	*buildingPosY=y;

	return checkRoomForBuilding(x, y, bt, teamNumber, checkFow);
}

bool Game::checkRoomForBuilding(int x, int y, const BuildingType *bt, int teamNumber, bool checkFow)
{
	Team *team=teams[teamNumber];
	assert(team);

	int w=bt->width;
	int h=bt->height;

	bool isRoom=true;
	if (bt->isVirtual)
	{
		if (teamNumber<0)
			return true;

		for (std::list<Building *>::iterator vb=team->virtualBuildings.begin(); vb!=team->virtualBuildings.end(); ++vb)
		{
			Building *b=*vb;
			if ((b->posX==(x&map.getMaskW())) && (b->posY==(y&map.getMaskH())))
				return false;
		}
		return true;
	}
	else
		isRoom=map.isFreeForBuilding(x, y, w, h);

	if (!checkFow)
		return isRoom;

	if (isRoom)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if (map.isMapDiscovered(dx, dy, team->me))
					return true;
		return false;
	}
	else
		return false;
}

bool Game::checkHardRoomForBuilding(int coordX, int coordY, const BuildingType *bt, int *mapX, int *mapY)
{
	int x=coordX+bt->decLeft;
	int y=coordY+bt->decTop;

	*mapX=x;
	*mapY=y;

	return checkHardRoomForBuilding(x, y, bt);
}

bool Game::checkHardRoomForBuilding(int x, int y, const BuildingType *bt)
{
	int w=bt->width;
	int h=bt->height;
	assert(!bt->isVirtual); // This method is not for flags!
	return map.isHardSpaceForBuilding(x, y, w, h);
}



Unit* Game::getUnit(int guid)
{
	if(guid == NOGUID)
		return NULL;
	return teams[Unit::GIDtoTeam(guid)]->myUnits[Unit::GIDtoID(guid)];
}



void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth)
{
	assert(maxLength>=0);
	assert(maxLength<65536);
	assert(actLength<=maxLength);

	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
		/*globalContainer->gfx->drawHorzLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawHorzLine(x, y+barWidth+1, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawVertLine(x+i*3, y+1, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, maxLength*3+1, barWidth+2, 0, 0, 0);

		if (orientation==LEFT_TO_RIGHT)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
		}
	}
	else if ((orientation==BOTTOM_TO_TOP) || (orientation==TOP_TO_BOTTOM))
	{
		/*globalContainer->gfx->drawVertLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawVertLine(x+barWidth+1, y, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawHorzLine(x+1, y+i*3, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, barWidth+2, maxLength*3+1, 0, 0, 0);

		if (orientation==TOP_TO_BOTTOM)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
	}
	else
		assert(false);
}

void Game::drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int screenW, int screenH, int localTeam, Uint32 drawOptions)
{
	int id=Unit::GIDtoID(gid);
	int team=Unit::GIDtoTeam(gid);
	Unit *unit=teams[team]->myUnits[id];
	assert(unit);
	if (!unit)
	{
		globalContainer->gfx->drawRect((x<<5)+1, (y<<5)+1, 30, 30, 255, 255, 0);
		return;
	}
	int dx=unit->dx;
	int dy=unit->dy;

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
		if ((!map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))&&(!map.isFOWDiscovered(x+viewportX-dx, y+viewportY-dy, visibleTeams)))
			return;

	int imgid;
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=unit->skin->startImage[unit->action];
	int px, py;
	map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
	int deltaLeft=255-unit->delta;
	if (unit->action<BUILD)
	{
		px-=(unit->dx*deltaLeft)>>3;
		py-=(unit->dy*deltaLeft)>>3;
	}
	else
	{
		// TODO : if looks ugly, do something intelligent here
	}

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	// draw unit
	Sprite *unitSprite = unit->skin->sprite;
	unitSprite->setBaseColor(teams[team]->color);
	int decX = (unitSprite->getW(imgid)-32)>>1;
	int decY = (unitSprite->getH(imgid)-32)>>1;
	globalContainer->gfx->drawSprite(px-decX, py-decY, unitSprite, imgid);

	// draw selection
	if (unit==selectedUnit)
	{
		globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 255);
		if (unit->owner->teamNumber == localTeam)
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 190);
		else if ((teams[localTeam]->allies) & (unit->owner->me))
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 255, 196, 0);
		else
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 190, 0, 0);
	}

	// draw xp animation
	if (unit->levelUpAnimation)
	{
		std::ostringstream oss;
		oss << unit->experienceLevel;
		globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 242, 131, 14));
		globalContainer->gfx->drawString(px + 16 - (globalContainer->standardFont->getStringWidth(oss.str().c_str()) >> 1), py - 16 - 2 *( LEVEL_UP_ANIMATION_FRAME_COUNT - unit->levelUpAnimation), globalContainer->standardFont, oss.str(), 0, (255*unit->levelUpAnimation) / LEVEL_UP_ANIMATION_FRAME_COUNT);
		globalContainer->standardFont->popStyle();
	}

	// draw magic animation
	if (unit->magicActionAnimation)
	{
		if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		{
			globalContainer->gfx->drawSprite(px+16-(globalContainer->magiceffect->getW(0)>>1), py+16-(globalContainer->magiceffect->getH(0)>>1), globalContainer->magiceffect, 0);
		}
		else
		{
			unsigned alpha = (unit->magicActionAnimation * 255) / MAGIC_ACTION_ANIMATION_FRAME_COUNT;
			if (globalContainer->gfx->canDrawStretchedSprite())
			{
				int stretchW = ((MAGIC_ACTION_ANIMATION_FRAME_COUNT - unit->magicActionAnimation) * globalContainer->magiceffect->getW(0)) / (MAGIC_ACTION_ANIMATION_FRAME_COUNT * 2);
				int stretchH = ((MAGIC_ACTION_ANIMATION_FRAME_COUNT - unit->magicActionAnimation) * globalContainer->magiceffect->getH(0)) / (MAGIC_ACTION_ANIMATION_FRAME_COUNT * 2);
				globalContainer->gfx->drawSprite(px+16-stretchW, py+16-stretchH, stretchW*2, stretchH*2, globalContainer->magiceffect, 0, alpha);
			}
			else
			{
				globalContainer->gfx->drawSprite(px+16-(globalContainer->magiceffect->getW(0)>>1), py+16-(globalContainer->magiceffect->getH(0)>>1), globalContainer->magiceffect, 0, alpha);
			}
		}
	}

	if ((px<mouseX)&&((px+32)>mouseX)&&(py<mouseY)&&((py+32)>mouseY)&&(((drawOptions & DRAW_WHOLE_MAP) != 0) ||(map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))||(Unit::GIDtoTeam(gid)==localTeam)))
		mouseUnit=unit;

	if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0 )
	{
		drawPointBar(px+1, py+25, LEFT_TO_RIGHT, 10, (unit->hungry*10)/Unit::HUNGRY_MAX, 80, 179, 223);

		float hpRatio=(float)unit->hp/(float)unit->performance[HP];
		if (hpRatio>0.6)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 78, 187, 78);
		else if (hpRatio>0.3)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 255, 0);
		else
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 0, 0);

		if ((unit->performance[HARVEST]) && (unit->caryedRessource>=0))
			globalContainer->gfx->drawSprite(px+24, py, globalContainer->ressourceMini, unit->caryedRessource);
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << unit->owner->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = px+((32-accessW)>>1);
		int accessY = py+((32-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}
	if(hilightUnitType & (1<<unit->typeNum))
	{
		globalContainer->gfx->drawSprite(px, py-decY-32, globalContainer->gamegui, 36);
	}
}

struct BuildingPosComp
{
	bool operator () (Building * const & a, Building * const & b)
	{
		if (a->posY != b->posY)
			return a->posY < b->posY;
		else
			return a->posX < b->posX;
	}
};

inline void Game::drawMapWater(int sw, int sh, int viewportX, int viewportY, int time)
{
	int waterStartX = -(((viewportX<<5)+time/2) % 512);
	int waterStartY = -((viewportY<<5) % 512);
	for (int y=waterStartY; y<sh; y += 512)
		for (int x=waterStartX; x<sw; x += 512)
			globalContainer->gfx->drawSprite(x, y, globalContainer->terrainWater, 0);
}

inline void Game::drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	// we draw the terrains, eventually with debug rects:
	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
					map.isMapPartiallyDiscovered(
							x+viewportX-1,
							y+viewportY-1,
							x+viewportX+1,
							y+viewportY+1,
							visibleTeams) ||
				((drawOptions & DRAW_WHOLE_MAP) != 0))
			{
				// draw terrain
				int id=map.getTerrain(x+viewportX, y+viewportY);
				Sprite *sprite;
				if (id<272)
				{
					sprite=globalContainer->terrain;
				}
				else
				{
					assert(false); // Now there shouldn't be any more ressources on "terrain".
					sprite=globalContainer->ressources;
					id-=272;
				}
				if ((id < 256) || (id >= 256+16))
					globalContainer->gfx->drawSprite(x<<5, y<<5, sprite, id);
			}
}

inline void Game::drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
				map.isMapPartiallyDiscovered(
						x+viewportX-1,
						y+viewportY-1,
						x+viewportX+1,
						y+viewportY+1,
						visibleTeams) ||
				((drawOptions & DRAW_WHOLE_MAP) != 0))
			{
				Ressource r=map.getRessource(x+viewportX, y+viewportY);
				if (r.type!=NO_RES_TYPE)
				{
					Sprite *sprite=globalContainer->ressources;
					int type=r.type;
					int amount=r.amount;
					int variety=r.variety;
					const RessourceType *rt=globalContainer->ressourcesTypes.get(type);
					int imgid=rt->gfxId+(variety*rt->sizesCount)+amount;
					if (!rt->eternal)
						imgid--;
					int dx=(sprite->getW(imgid)-32)>>1;
					int dy=(sprite->getH(imgid)-32)>>1;
					assert(type>=0);
					assert(type<(int)globalContainer->ressourcesTypes.size());
					assert(amount>=0);
					assert(amount<=rt->sizesCount);
					assert(variety>=0);
					assert(variety<rt->varietiesCount);
					globalContainer->gfx->drawSprite((x<<5)-dx, (y<<5)-dy, sprite, imgid);
				}
			}
}

inline void Game::drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	//Reset the mouse unit to NULL, as this time arround there may not be a unit
	//under the mouse pointer
	mouseUnit=NULL;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getGroundUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions);
		}
}

inline void Game::drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if (false)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
			{
				//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((AICastor *)players[1]->ai->aiImplementation)->wheatCareMap[0][(x+viewportX)+(y+viewportY)*map.w]);
				//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((AICastor *)players[1]->ai->aiImplementation)->notGrassMap[(x+viewportX)+(y+viewportY)*map.w]);
//				globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.guardAreasGradient[0][1][(x+viewportX)+(y+viewportY)*map.w]);
//				globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((Nicowar::AINicowar*)players[3]->ai->aiImplementation)->getGradientManager().getGradient(Nicowar::Gradient::VillageCenter, Nicowar::Gradient::Resource).getHeight(x+viewportX, y+viewportY));
				//((AICastor *)players[0].ai->aiImplementation)->wheatCareMap
			}
				//if (map.getForbidden(x+viewportX, y+viewportY))
				//{
					//if (!map.isFreeForGroundUnit(x+viewportX, y+viewportY, 1, 1))
					//	globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 16, 32);
					//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 16, 32);
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(1, 5, 0, x+viewportX, y+viewportY));
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(0, STONE, 1, x+viewportX, y+viewportY));
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.forbiddenGradient[0][0][(x+viewportX)+(y+viewportY)*map.w]);
					//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, ((x+viewportX)&(map.getMaskW())));
					//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, ((y+viewportY)&(map.getMaskH())));
				//}

	// We draw debug area:
	if (false)
	{
		assert(teams[0]);
		Building *b=selectedBuilding;
		if (b)
			for (int y=top-1; y<=bot; y++)
				for (int x=left-1; x<=right; x++)
				{
					//if (map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY)<16)
					{
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.getGradient(0, 6, 1, x+viewportX, y+viewportY));
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY));
						//int lx=(x+viewportX-b->posX+15+32)&31;
						//int ly=(y+viewportY-b->posY+15+32)&31;
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[1][lx+ly*32]);
						if(b->globalGradient[1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->globalGradient[1][(x+viewportX) + (y+viewportY)*map.w]);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, lx);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+10, globalContainer->littleFont, ly);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX-b->posX+16);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY-b->posY+16);
					}
				}
	}

	// We draw debug area:
	if (false)
		if (selectedUnit && selectedUnit->verbose)
		{
			//assert(teams[0]);
			Building *b=selectedUnit->attachedBuilding;
			//b=teams[0]->myBuildings[21];
			//if (teams[0]->virtualBuildings.size())
			//	b=*teams[0]->virtualBuildings.begin();
			if (b && b->localRessources[1])
				for (int y=top-1; y<=bot; y++)
					for (int x=left-1; x<=right; x++)
						if (map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY)<16)
						{
							int lx=(x+viewportX-b->posX+15)&31;
							int ly=(y+viewportY-b->posY+15)&31;
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localRessources[1][lx+ly*32]);
						}
		}

	// We draw debug area:
	//if (selectedUnit && selectedUnit->verbose)
	if (selectedBuilding && selectedBuilding->verbose)
	{
		//Building *b=NULL;
		Building *b=selectedBuilding;
		//Building *b=selectedUnit->attachedBuilding;

		//assert(teams[0]);
		//Building *b=teams[0]->myBuildings[0];
		//if (teams[0]->virtualBuildings.size())
		//	b=*teams[0]->virtualBuildings.begin();

		int w=map.getW();
		if (b)
			for (int y=top-1; y<=bot; y++)
				for (int x=left-1; x<=right; x++)
				{
					if (b->verbose==1 || b->verbose==2)
					{
						if (b->globalGradient[b->verbose&1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont,
								b->globalGradient[b->verbose&1][((x+viewportX)&(map.getMaskW()))+((y+viewportY)&(map.getMaskH()))*w]);
					}
					else if ((b->verbose==3 || b->verbose==4) && map.isInLocalGradient(x+viewportX, y+viewportY, b->posX, b->posY))
					{
						int lx=(x+viewportX-b->posX+15)&31;
						int ly=(y+viewportY-b->posY+15)&31;
						if (!b->dirtyLocalGradient[b->verbose&1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[b->verbose&1][lx+ly*32]);
					}

					globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 192, 192, 192));
					globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, (x+viewportX+map.getW())&(map.getMaskW()));
					globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, (y+viewportY+map.getH())&(map.getMaskH()));
					globalContainer->littleFont->popStyle();
				}

	}
}

inline void Game::drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Building *building = teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
	assert(building);
	BuildingType *type=building->type;
	Team *team=building->owner;

	int imgid;
	if (type->crossConnectMultiImage)
	{
		int add = 0;
		Uint16 b;
		// Up
		b = map.getBuilding(building->posXLocal, building->posYLocal-1);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<3);
		// Bottom
		b = map.getBuilding(building->posXLocal, building->posYLocal+building->type->height);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<2);
		// Left
		b = map.getBuilding(building->posXLocal-1, building->posYLocal);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<1);
		// Right
		b = map.getBuilding(building->posXLocal+building->type->width, building->posYLocal);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<0);
		imgid = type->gameSpriteImage + add;
	}
	else
	{
		// FIXME : why building->hp is > type->hpMax ?
		int hp = std::min(building->hp, type->hpMax);
		int damageImgShift = type->gameSpriteCount - ((hp * type->gameSpriteCount) / (type->hpMax+1)) - 1;
		assert(damageImgShift >= 0);
		imgid = type->gameSpriteImage + damageImgShift;
	}
//	int x, y;
	int dx, dy;

//	map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

	// select buildings and set the team colors
	Sprite *buildingSprite = type->gameSpritePtr;
	dx = (type->width<<5)-buildingSprite->getW(imgid);
	dy = (type->height<<5)-buildingSprite->getH(imgid);
	buildingSprite->setBaseColor(team->color);

	// draw building
	globalContainer->gfx->drawSprite(x+dx, y+dy, buildingSprite, imgid);

	if ((drawOptions & DRAW_BUILDING_RECT) != 0)
	{
		int batW=(type->width )<<5;
		int batH=(type->height)<<5;
		int typeNum=building->typeNum;
		globalContainer->gfx->drawRect(x, y, batW, batH, 255, 255, 255, 127);

		BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
		int lastTypeNum=typeNum;
		int max=0;
		while(lastbt->nextLevel>=0)
		{
			lastTypeNum=lastbt->nextLevel;
			lastbt=globalContainer->buildingsTypes.get(lastTypeNum);
			if (max++>200)
			{
				printf("GameGUI: Error: nextLevelTypeNum architecture is broken.\n");
				assert(false);
				break;
			}
		}
		int exBatX=x+((lastbt->decLeft-type->decLeft)<<5);
		int exBatY=y+((lastbt->decTop-type->decTop)<<5);
		int exBatW=(lastbt->width)<<5;
		int exBatH=(lastbt->height)<<5;

		globalContainer->gfx->drawRect(exBatX, exBatY, exBatW, exBatH, 255, 255, 255, 127);
	}

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) && (building->owner->sharedVisionOther & visibleTeams))
	{
		//int unitDecx=(building->type->width*16)-((3*building->maxUnitInside)>>1);
		// TODO : find better color for this
		// health
		if (type->hpMax)
		{
			int maxWidth, actWidth, addDec;
			float hpRatio=(float)building->hp/(float)type->hpMax;
			if (type->width==1)
			{
				maxWidth=8;
				actWidth=1+(int)(7.0f*hpRatio);
				addDec=2;
			}
			else
			{
				maxWidth=16;
				actWidth=1+(int)(15.0f*hpRatio);
				addDec=7;
			}
			int decy=(type->height*32);
			int healDecx=(type->width-(maxWidth>>3))*16+addDec;

			if (building->hp!=type->hpMax || !building->type->crossConnectMultiImage)
			{
				if (hpRatio>0.6)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 78, 187, 78);
				else if (hpRatio>0.3)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 255, 0);
				else
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 0, 0);
			}
		}

		// units
		if (building->maxUnitInside>0)
			drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
		if (building->maxUnitWorking>0)
			drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 0, 255, 255, 255, 255, 64, 0);

		// food (for inns)
		if ((type->canFeedUnit) || (type->unitProductionTime))
		{
			// compute bar size, prevent oversize
			int bDiv=1;
			assert(type->height!=0);
			while ( ((type->maxRessource[CORN]*3+1)/bDiv)>((type->height*32)-10))
				bDiv++;
			drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxRessource[CORN]/bDiv, building->ressources[CORN]/bDiv, 255, 255, 120, 1+bDiv);
		}

		// bullets (for defence towers)
		if (type->maxBullets)
		{
			// compute bar size, prevent oversize
			int bDiv=1;
			assert(type->height!=0);
			while ( ((type->maxBullets*3+1)/bDiv)>((type->height*32)-10))
				bDiv++;
			drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxBullets/bDiv, building->bullets/bDiv, 200, 200, 200, 1+bDiv);
		}
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << building->owner->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = x+(((type->width<<5)-accessW)>>1);
		int accessY = y+(((type->height<<5)-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}

	if(hilightBuildingType & (1<<building->shortTypeNum))
	{
		globalContainer->gfx->drawSprite(x + buildingSprite->getW(imgid)/2 - 16, y-36, globalContainer->gamegui, 36);
	}
}


inline void Game::drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	std::set<Building*> drawnBuildings;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getBuilding(x+viewportX, y+viewportY);
			if (gid!=NOGBID) // Then this is a building
			{
				//globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 128, 0);
				//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 128, 0);

				int id = Building::GIDtoID(gid);
				int team = Building::GIDtoTeam(gid);

				Building *building=teams[team]->myBuildings[id];
				if(drawnBuildings.find(building)==drawnBuildings.end())
				{
					assert(building); // if this fails, and unwanted garbage-UID is on the ground.
					if (((drawOptions & DRAW_WHOLE_MAP) != 0)
						|| Building::GIDtoTeam(gid)==localTeam
						|| (building->seenByMask & visibleTeams)
						|| map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))
					{
						int px,py;					
						map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &px, &py, viewportX, viewportY);
					 	drawMapBuilding(px, py, gid, viewportX, viewportY, localTeam, drawOptions);
						drawnBuildings.insert(building);
					}
				}
			}
		}
	if(visibleBuildings)
		*visibleBuildings = drawnBuildings;
}
/**
 * Draws the visible (viewport) part of the given map
 */
inline void Game::drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	static int areaAnimationTick = 0;
	
	typedef bool (Map::*MapIsFP)(int, int);
	MapIsFP mapIs;
	
	if ((drawOptions & DRAW_AREA) != 0 && (!globalContainer->replaying || globalContainer->replayShowActions))
	{
		mapIs=&Map::isForbiddenLocal; drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, mapIs, areaAnimationTick, ForbiddenArea);
		mapIs=&Map::isGuardAreaLocal; drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, mapIs, areaAnimationTick, GuardArea);
		mapIs=&Map::isClearAreaLocal; drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, mapIs, areaAnimationTick, ClearingArea);
		for (int y=top; y<bot; y++)
			for (int x=left; x<right; x++)
			{
				if((drawOptions & DRAW_NO_RESSOURCE_GROWTH_AREAS) != 0)
				{
					if(!map.canRessourcesGrow(x+viewportX, y+viewportY))
					{
						globalContainer->gfx->drawLine((x<<5), 8+(y<<5), 32+(x<<5), 8+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 16+(y<<5), 32+(x<<5), 16+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 24+(y<<5), 32+(x<<5), 24+(y<<5), 128, 64, 0);
//						globalContainer->gfx->drawLine((x<<5), 32+(y<<5), 32+(x<<5), 32+(y<<5), 128, 64, 0);

						if (map.canRessourcesGrow(x+viewportX, y+viewportY-1))
							globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (map.canRessourcesGrow(x+viewportX, y+viewportY+1))
							globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, 255, 128, 0);

						if (map.canRessourcesGrow(x+viewportX-1, y+viewportY))
							globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (map.canRessourcesGrow(x+viewportX+1, y+viewportY))
							globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, 255, 128, 0);
						}
				}
			}
		areaAnimationTick++;
	}
}
/**
 * Draws the visible (viewport) part of the given map
 */
inline void Game::drawMapArea(int left, int top, int right, int bot, int sw,
		int sh, int viewportX, int viewportY, int localTeam,
		Uint32 drawOptions, Map * map, bool (Map::*mapIs)(int, int), int areaAnimationTick,
		AreaType areaType)
{
	Sprite* sprite;
	GAGCore::Color c;
	switch (areaType)
	{
		case ClearingArea: sprite = globalContainer->areaClearing; c = GAGCore::Color(255,255,0); break;
		case ForbiddenArea: sprite = globalContainer->areaForbidden; c = GAGCore::Color(255,0,0); break;
		case GuardArea: sprite = globalContainer->areaGuard; c = GAGCore::Color(0,0,255); break;
		default: assert(false);
	}
	for (int y=top; y<bot; y++)
	{
		for (int x=left; x<right; x++)
		{
			if ((map->*mapIs)(x+viewportX, y+viewportY))
			{
				int randId = (x+viewportX) * 7919 + (y+viewportY) * 17;
				int frame = ((randId + areaAnimationTick) % (sprite->getFrameCount() * 2)) / 2;
				globalContainer->gfx->drawSprite((x<<5), (y<<5), sprite, frame);

				if (!(map->*mapIs)(x+viewportX, y+viewportY-1))
					globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, c);
				if (!(map->*mapIs)(x+viewportX, y+viewportY+1))
					globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, c);

				if (!(map->*mapIs)(x+viewportX-1, y+viewportY))
					globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, c);
				if (!(map->*mapIs)(x+viewportX+1, y+viewportY))
					globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, c);
			}
		}
	}
}

inline void Game::drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getAirUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions);
		}
}

inline void Game::drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY)
{
	for (int y=top; y<bot; y++)
		for (int x=left; x<right; x++)
		{
			std::stringstream str;
			for(int n=0; n<9; ++n)
			{
				if(map.isPointSet(n, x+viewportX, y+viewportY))
				{
					str.str("");
					str<<n+1;
					globalContainer->gfx->drawString((x<<5)+(n%3)*10, (y<<5)+(n/3)*10, globalContainer->littleFont, str.str());

					globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, 64, 255, 255);
					globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, 64, 255, 255);

					globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, 64, 255, 255);
					globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, 64, 255, 255);
				}
			}
		}
}

inline void Game::drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	// Let's paint the bullets and explosions
	// TODO : optimise : test only possible sectors to show bullets.

	Sprite *bulletSprite = globalContainer->bullet;
	// FIXME : have team in bullets to have the correct color

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	int mapPixW=(map.getW())<<5;
	int mapPixH=(map.getH())<<5;

	for (int i=0; i<(map.getSectorW()*map.getSectorH()); i++)
	{
		Sector *s=map.getSector(i);
		// bullets
		for (std::list<Bullet *>::iterator it=s->bullets.begin();it!=s->bullets.end();it++)
		{
			int x=(*it)->px-(viewportX<<5);
			int y=(*it)->py-(viewportY<<5);
			int balisticShift = 0;

			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;
			if ((*it)->ticksInitial)
			{
				float x = static_cast<float>((*it)->ticksLeft);
				float T = static_cast<float>((*it)->ticksInitial);
				float speedX = static_cast<float>((*it)->speedX);
				float speedY = static_cast<float>((*it)->speedX);
				float K = static_cast<float>(sqrt(speedX * speedX + speedY * speedY));
				balisticShift = static_cast<int>(K * ((-1.0f * x * x) / T + x));
			}

			//printf("px=(%d, %d) vp=(%d, %d)\n", (*it)->px, (*it)->py, viewportX, viewportY);
			if ( (x<=sw) && (y<=sh) )
			{
				globalContainer->gfx->drawSprite(x, y-balisticShift, bulletSprite, BULLET_IMGID);
				globalContainer->gfx->drawSprite(x+(balisticShift>>1), y, bulletSprite, BULLET_IMGID+1);
			}
		}
		// explosions
		for (std::list<BulletExplosion *>::iterator it=s->explosions.begin();it!=s->explosions.end();it++)
		{
			if (map.isFOWDiscovered((*it)->x, (*it)->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable((*it)->x, (*it)->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->bulletExplosion->getFrameCount() - (*it)->ticksLeft - 1;
				int decX = globalContainer->bulletExplosion->getW(frame)>>1;
				int decY = globalContainer->bulletExplosion->getH(frame)>>1;
				globalContainer->gfx->drawSprite(x+16-decX, y+16-decY, globalContainer->bulletExplosion, frame);
			}
		}
		// death animations
		for (std::list<UnitDeathAnimation *>::iterator it=s->deathAnimations.begin();it!=s->deathAnimations.end();++it)
		{
			if (map.isFOWDiscovered((*it)->x, (*it)->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable((*it)->x, (*it)->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->deathAnimation->getFrameCount() - (*it)->ticksLeft - 1;
				int decX = globalContainer->deathAnimation->getW(frame)>>1;
				int decY = globalContainer->deathAnimation->getH(frame)>>1;
				Team *team = (*it)->team;

				globalContainer->deathAnimation->setBaseColor(team->color);
				globalContainer->gfx->drawSprite(x+16-decX, y+16-decY-frame, globalContainer->deathAnimation, frame);
			}
		}
	}
}

inline void Game::drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
	{
		// we have decrease on because we do unalign lookup
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
			{
				unsigned i0, i1, i2, i3;

				/*if ( (!map.isMapDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me)))
				{
					globalContainer->gfx->drawFilledRect(x<<5, y<<5, 32, 32, 10, 10, 10);
				}
				else if ( (!map.isFOW(x+viewportX, y+viewportY, teams[localTeam]->me)))
				{
					globalContainer->gfx->drawSprite(x<<5, y<<5, globalContainer->terrainShader, 0);
				}*/

				Uint32 visibleTeams = teams[localTeam]->me;
				if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

				// first draw black
				i0=!map.isMapDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
				i1=!map.isMapDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
				i2=!map.isMapDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
				i3=!map.isMapDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
				unsigned blackValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);
				if (blackValue==15)
					globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0);
				else if (blackValue)
					globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainBlack, blackValue);

				// then if it isn't full black, draw shade
				if (blackValue!=15)
				{
					i0=!map.isFOWDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
					i1=!map.isFOWDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
					i2=!map.isFOWDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
					i3=!map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
					unsigned shadeValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);

					if (shadeValue==15)
						globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0, 127);
					else if (shadeValue)
						globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainShader, shadeValue);
				}
			}
	}
}

inline void Game::drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if(drawOptions & DRAW_OVERLAY)
	{
		OverlayArea* overlays;
		if(gui)
			overlays=&gui->overlay;
		else if(edit)
			overlays=&edit->overlay;
		int overlayMax=overlays->getMaximum();
		Color overlayColor;
		if(overlays->getOverlayType() == OverlayArea::Starving)
			overlayColor=Color(192, 0, 0);
		if(overlays->getOverlayType() == OverlayArea::Damage)
			overlayColor=Color(192, 0, 0);
		if(overlays->getOverlayType() == OverlayArea::Defence)
			overlayColor=Color(0, 0, 192);
		if(overlays->getOverlayType() == OverlayArea::Fertility)
			overlayColor=Color(0, 192, 128);
		///Both width and height have +2 to cover half-squares arround the edge of the viewport
		int width = (right - left) + 2;
		int height = (bot - top) + 2;

		overlayAlphas.resize(width * height);
		for (int y=0; y<height; y++)
		{
			for (int x=0; x<width; x++)
			{
				Uint32 visibleTeams = teams[localTeam]->me;
				if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

				int rx=(x+viewportX-1+map.getW())%map.getW();
				int ry=(y+viewportY-1+map.getH())%map.getH();
				if(!edit && !map.isMapDiscovered(rx, ry, visibleTeams))
					continue;
				if(overlays->getValue(rx, ry))
				{
					const int value_c=overlays->getValue(rx, ry);
					const int alpha_c=int(float(200)/float(overlayMax) * float(value_c));
					overlayAlphas[width * y + x] = alpha_c;
				}
			}
		}

		///This is to correct OpenGL's blending not beeing offset correctly to line up with the map tiles
		if(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
			globalContainer->gfx->drawAlphaMap(overlayAlphas, width, height, -16, -16, 32, 32, overlayColor);
		else
			globalContainer->gfx->drawAlphaMap(overlayAlphas, width, height, -32, -32, 32, 32, overlayColor);
	}
}



inline void Game::drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if ((drawOptions & DRAW_PATH_LINE) != 0)
	{
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit *unit=teams[localTeam]->myUnits[i];
			if (unit)
			{
				drawUnitPathLine(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, unit);
			}
		}
	}
	if(selectedUnit != NULL)
	{
		drawUnitPathLine(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, selectedUnit);
	}
}



inline void Game::drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Unit* unit)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if(unit->owner->sharedVisionOther & visibleTeams)
	{
		if (unit->validTarget)
		{
			if(isOnScreen(left,top,right,bot,viewportX,viewportY,unit->posX,unit->posY) || isOnScreen(left,top,right,bot,viewportX,viewportY,unit->targetX,unit->targetY))
			{
				int px, py;
				map.mapCaseToDisplayableVector(unit->posX, unit->posY, &px, &py, viewportX, viewportY, sw, sh);
				int deltaLeft=255-unit->delta;
				if (unit->action<BUILD)
				{
					px-=(unit->dx*deltaLeft)>>3;
					py-=(unit->dy*deltaLeft)>>3;
				}


				int lsx, lsy, ldx, ldy;
				map.mapCaseToDisplayableVector(unit->targetX, unit->targetY, &ldx, &ldy, viewportX, viewportY, sw, sh);
				lsx=px+16;
				lsy=py+16;
				if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
					globalContainer->gfx->drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250);
				else
					globalContainer->gfx->drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250, 128);
			}
		}
	}
}



inline void Game::drawUnitOffScreen(int sx, int sy, int sw, int sh, int viewportX, int viewportY, Unit* unit, Uint32 drawOptions)
{
	// Get the direction to the unit
	int px, py;
	map.mapCaseToDisplayableVector(unit->posX, unit->posY, &px, &py, viewportX, viewportY, sw, sh);
	int deltaLeft=255-unit->delta;
	if (unit->action<BUILD)
	{
		px-=(unit->dx*deltaLeft)>>3;
		py-=(unit->dy*deltaLeft)>>3;
	}

	// To get the center of the unit
	px+=16;
	py+=16;

	// Place the internal box dimensions
	int i_sx = sx + 20;
	int i_sy = sy + 20;
	int i_sw = sw - 40;
	int i_sh = sh - 40;

	// The units draw position releative to the center of the internal square
	int rel_cx = px - i_sx - i_sw/2;
	int rel_cy = py - i_sy - i_sh/2;
	if(rel_cx == 0)
		rel_cx = 1;
	if(rel_cy == 0)
		rel_cy = 1;

	//globalContainer->gfx->drawLine(sx + sw/2, sy + sh/2, px, py, Color::white);

	// Decide which edge of the screen the box is on, and compute its center cordinates
	int bx = 0;
	int by = 0;
	float slope = float(rel_cy) / float(rel_cx);
	float angle = atan2f(float(rel_cy), float(rel_cx));
	float screen=float(i_sh) / float(i_sw);
	if(rel_cx > 0 && std::abs(slope) <= std::abs(screen))
	{
		bx = i_sx + i_sw;
		by = i_sy + (i_sh/2) + int(slope * float(i_sw/2));
	}
	else if(rel_cx < 0 && std::abs(slope) <= std::abs(screen))
	{
		bx = i_sx;
		by = i_sy + (i_sh/2) - int(slope * float(i_sw/2));
	}
	else if(rel_cy > 0 && std::abs(slope) >= std::abs(screen))
	{
		bx = i_sx + (i_sw/2) + int(float(i_sh/2) / slope);
		by = i_sy + i_sh;
	}
	else if(rel_cy < 0 && std::abs(slope) >= std::abs(screen))
	{
		bx = i_sx + (i_sw/2) - int(float(i_sh/2) / slope);
		by = i_sy;
	}

	bx -= 20;
	by -= 20;

	// draw unit's image
	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);

	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(unit->owner->color);
	int decX = (32-unitSprite->getW(imgid))>>1;
	int decY = (32-unitSprite->getH(imgid))>>1;

	// Draw the code
	//globalContainer->gfx->drawFilledRect(bx, by, 40, 40, 0,0,0,128);
	//globalContainer->gfx->drawCircle(bx+20, by+20, 20, Color::white);
	Color transpWhite = Color(255, 255, 255, 192);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*5,
		by+20+sinf(angle)*5,
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		Color::white);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		bx+20+cosf(angle-M_PI/6)*10,
		by+20+sinf(angle-M_PI/6)*10,
		Color::white);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		bx+20+cosf(angle+M_PI/6)*10,
		by+20+sinf(angle+M_PI/6)*10,
		Color::white);
	globalContainer->gfx->drawSprite(bx+decX+4, by+decY+4, unitSprite, imgid, 160);
}


float Game::interpolateValues(float a, float b, float x)
{
	float ft = 3.141592653f * x;
	float f = (1.0f - std::cos(ft)) * 0.5f;
	return  a*(1.0-f) + b*f;
}



inline bool Game::isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y)
{

	left += viewportX;
	right += viewportX;
	top += viewportY;
	bot += viewportY;

	if((x >= left-1 && x <= right) || (x+map.getW() >= left-1 && x+map.getW() <= right))
	{
		if((y >= top-1 && y <= bot) || (y+map.getH() >= top-1 && y+map.getH() <= bot))
		{
			return true;
		}
	}
	return false;
}



void Game::drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings)
{
	static int time = 0;
	static DynamicClouds ds(&globalContainer->settings);
	int left=(sx>>5);
	int top=(sy>>5);
	int right=((sx+sw+31)>>5);
	int bot=((sy+sh+31)>>5);

	time++;
	drawMapWater(sw, sh, viewportX, viewportY, time);
	drawMapTerrain(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapRessources(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapGroundUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapDebugAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapGroundBuildings(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, visibleBuildings);
	drawMapAirUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	if((drawOptions & DRAW_SCRIPT_AREAS) != 0)
		drawMapScriptAreas(left, top, right, bot, viewportX, viewportY);
	drawMapBulletsExplosionsDeathAnimations(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);

	// compute and draw cloud shadow if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		ds.compute(viewportX, viewportY, sw, sh, time);
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::SHADOW);
	}

	drawMapFogOfWar(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapOverlayMaps(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawUnitPathLines(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);

	// draw cloud overlay if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::CLOUD);

	// Draw units that are off the screen for the selected building

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if(selectedBuilding != NULL && (selectedBuilding->owner->sharedVisionOther & visibleTeams))
	{
		for(std::list<Unit*>::iterator i = selectedBuilding->unitsWorking.begin(); i!=selectedBuilding->unitsWorking.end(); ++i)
		{
			Unit* unit = *i;
			if(!isOnScreen(left, top, right, bot, viewportX, viewportY, unit->posX, unit->posY))
			{
				drawUnitOffScreen(0, 16, sw - 160, sh-16, viewportX, viewportY, unit, drawOptions);
			}
		}
	}

	// we look on the whole map for buildings
	// TODO : increase speed, do not count on graphic clipping
	if (!globalContainer->replaying || globalContainer->replayShowActions)
	{
		for (std::list<Building *>::iterator virtualIt=teams[localTeam]->virtualBuildings.begin();
			virtualIt!=teams[localTeam]->virtualBuildings.end(); ++virtualIt)
		{
			Building *building=*virtualIt;
			BuildingType *type=building->type;

			int team = building->owner->teamNumber;

			int imgid = type->gameSpriteImage;

			int x, y;
			map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

			// all flags are hued:
			Sprite *buildingSprite = type->gameSpritePtr;
			buildingSprite->setBaseColor(teams[team]->color);
			globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

			// flag circle:
			if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) || (building==selectedBuilding))
				globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

			// FIXME : ugly copy past
			if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0)
			{
				int decy=(type->height*32);
				int healDecx=(type->width-2)*16+1;
				//int unitDecx=(building->type->width*16)-((3*building->maxUnitInside)>>1);

				// TODO : find better color for this
				// health
				if (type->hpMax)
				{
					float hpRatio=(float)building->hp/(float)type->hpMax;
					if (hpRatio>0.6)
						drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 78, 187, 78);
					else if (hpRatio>0.3)
						drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 255, 255, 0);
					else
						drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 255, 0, 0);
				}

				// units

				if (building->maxUnitInside>0)
					drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
				if (building->maxUnitWorking>0)
					drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 255, 255, 255);

				// food
				if ((type->canFeedUnit) || (type->unitProductionTime))
				{
					// compute bar size, prevent oversize
					int bDiv=1;
					assert(type->height!=0);
					while ( ((type->maxRessource[CORN]*3+1)/bDiv)>((type->height*32)-10))
						bDiv++;
					drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxRessource[CORN]/bDiv, building->ressources[CORN]/bDiv, 255, 255, 120, 1+bDiv);
				}
			}
		}
	}

	if (false)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				for (int pi=0; pi<gameHeader.getNumberOfPlayers(); pi++)
					if (players[pi] && players[pi]->ai && players[pi]->ai->implementitionID==AI::CASTOR)
					{
						AICastor *ai=(AICastor *)players[pi]->ai->aiImplementation;
						//Uint8 *gradient=ai->wheatCareMap[1];
						Uint8 *gradient=ai->hydratationMap;
						//Uint8 *gradient=ai->enemyWarriorsMap;
						//Uint8 *gradient=map.forbiddenGradient[1][0];
						//Uint8 *gradient=map.ressourcesGradient[0][CORN][0];

						assert(gradient);
						size_t addr=((x+viewportX)&map.wMask)+map.w*((y+viewportY)&map.hMask);
						Uint8 value=gradient[addr];
						if (value)
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, value);

						/*Uint8 *gradient2=ai->wheatCareMap[1];
						assert(gradient2);
						Uint8 value2=gradient2[addr];
						if (value2)
							globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, value2);*/
						break;
					}
}



void Game::setWaitingOnMask(Uint32 mask)
{
	Uint32 oldMask = maskAwayPlayer;
	maskAwayPlayer = mask;

	if(mask != 0)
	{
		if(oldMask == 0)
			anyPlayerWaitedTimeFor = 0;
		anyPlayerWaited = true;
	}
	else
	{
		anyPlayerWaited = false;
	}
}



void Game::dumpAllData(const std::string& file)
{
	OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(file));
	if (stream->isEndOfStream())
	{
		std::cerr << "Can't dump full game memory to file "<< file << std::endl;
	}
	else
	{
		std::cerr << "Dumped full game memory to file "<< file << std::endl;
		save(stream, false, file);
	}
	delete stream;
}



Uint32 Game::checkSum(std::vector<Uint32> *checkSumsVector, std::vector<Uint32> *checkSumsVectorForBuildings, std::vector<Uint32> *checkSumsVectorForUnits, bool heavy)
{
	Uint32 cs=0;

	Uint32 headerCs=mapHeader.checkSum();
	cs^=headerCs;
	if (checkSumsVector)
		checkSumsVector->push_back(headerCs);// [0]

	cs=(cs<<31)|(cs>>1);

	Uint32 teamsCs=0;
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		teamsCs^=teams[i]->checkSum(checkSumsVector, checkSumsVectorForBuildings, checkSumsVectorForUnits);
		teamsCs=(teamsCs<<31)|(teamsCs>>1);
		cs=(cs<<31)|(cs>>1);
	}
	cs^=teamsCs;
	if (checkSumsVector)
		checkSumsVector->push_back(teamsCs);// [1+t*20]

	cs=(cs<<31)|(cs>>1);

	Uint32 playersCs=0;
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		playersCs^=players[i]->checkSum(checkSumsVector);
		playersCs=(playersCs<<31)|(playersCs>>1);
		cs=(cs<<31)|(cs>>1);
	}
	cs^=playersCs;
	if (checkSumsVector)
		checkSumsVector->push_back(playersCs);// [2+t*20+p*2]

	cs=(cs<<31)|(cs>>1);

	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if (players[i]->type==BasePlayer::P_IP)
		{
			heavy=true;
			break;
		}
	}
	Uint32 mapCs=map.checkSum(heavy);
	cs^=mapCs;
	if (checkSumsVector)
		checkSumsVector->push_back(mapCs);// [3+t*20+p*2]

	cs=(cs<<31)|(cs>>1);

	Uint32 scriptCs=script.checkSum();
	cs^=scriptCs;
	if (checkSumsVector)
		checkSumsVector->push_back(scriptCs);// [4+t*20+p*2]

	return cs;
}

Team *Game::getTeamWithMostPrestige(void)
{
	int maxPrestige=0;
	Team *maxPrestigeTeam=NULL;

	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		Team *t=teams[i];
		if (t->prestige > maxPrestige)
		{
			maxPrestigeTeam=t;
			maxPrestige=t->prestige;
		}
	}
	return maxPrestigeTeam;
}

bool Game::isPrestigeWinCondition(void)
{
	std::list<boost::shared_ptr<WinningCondition> >& conditions = gameHeader.getWinningConditions();
	for(std::list<boost::shared_ptr<WinningCondition> >::iterator i = conditions.begin(); i!=conditions.end(); ++i)
	{
		if((*i)->getType() == WCPrestige)
			return true;
	}
	return false;
}

OutputStream *Game::getReplayStream()
{
	return replay;
}