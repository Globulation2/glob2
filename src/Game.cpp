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

#include "AICastor.h"

#include <assert.h>
#include <string.h>

#include <set>
#include <string>
#include <functional>
#include <algorithm>

#include <FileManager.h>
#include <GraphicContext.h>

#include "BuildingType.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"

#define BULLET_IMGID 35

#define MIN_MAX_PRESIGE 500
#define TEAM_MAX_PRESTIGE 230

Game::Game(GameGUI *gui)
{
	logFile = globalContainer->logFileManager->getFile("Game.log");
	init(gui);
}

Game::~Game()
{
	int sum=0;
	for (int i=0; i<32; i++)
		sum+=ticksGameSum[i];
	if (sum)
	{
		fprintf(logFile, "execution time of Game::step: sum=%d\n", sum);
		for (int i=0; i<32; i++)
			fprintf(logFile, "ticksGameSum[%2d]=%8d, (%f %%)\n", i, ticksGameSum[i], (float)ticksGameSum[i]*100./(float)sum);
		fprintf(logFile, "\n");
		for (int i=0; i<32; i++)
		{
			fprintf(logFile, "ticksGameSum[%2d]=", i);
			for (int j=0; j<(int)(0.5+(float)ticksGameSum[i]*100./(float)sum); j++)
				fprintf(logFile, "*");
			fprintf(logFile, "\n");
		}
	}
	
	// delete existing teams and players
	for (int i=0; i<session.numberOfTeam; i++)
		if (teams[i])
		{
			delete teams[i];
			teams[i]=NULL;
		}
	for (int i=0; i<session.numberOfPlayer; i++)
		if (players[i])
		{
			delete players[i];
			players[i]=NULL;
		}

	if (minimap)
		delete minimap;
	minimap=NULL;
}

void Game::init(GameGUI *gui)
{
	this->gui=gui;
	
	// init minimap
	minimap=globalContainer->gfx->createDrawableSurface();
	minimap->setRes(100, 100);
	minimap->drawFilledRect(0, 0, 100, 100, 0, 0, 0);

	session.numberOfTeam=0;
	session.numberOfPlayer=0;
	
	for (int i=0; i<32; i++)
	{
		teams[i]=NULL;
		players[i]=NULL;
	}
	
	setSyncRandSeed();
	fprintf(logFile, "setSyncRandSeed(%d, %d, %d)\n", getSyncRandSeedA(), getSyncRandSeedB(), getSyncRandSeedC());
	
	mouseX=0;
	mouseY=0;
	mouseUnit=NULL;
	selectedUnit=NULL;
	selectedBuilding=NULL;

	stepCounter=0;
	totalPrestige=0;
	prestigeToReach=0;
	totalPrestigeReached=false;
	isGameEnded=false;
	
	for (int i=0; i<32; i++)
		ticksGameSum[i]=0;
}

void Game::setBase(const SessionInfo *initial)
{
	// This function reset some team info and overwrite the players

	assert(initial);
	assert(initial->numberOfTeam==session.numberOfTeam);
	// TODO, we should be able to play with less team than planed on the map
	// for instance, play at 2 on a 4 player map, and we will have to check the following code !!!

	// the GUI asserts that we have not more team that planed on the map

	// set the base team, for now the number is corect but we should check that further
	for (int i=0; i<session.numberOfTeam; i++)
		teams[i]->setBaseTeam(&(initial->team[i]), session.fileIsAMap);

	// set the base players
	for (int i=0; i<session.numberOfPlayer; i++)
		delete players[i];

	session.numberOfPlayer=initial->numberOfPlayer;

	for (int i=0; i<initial->numberOfPlayer; i++)
	{
		players[i]=new Player();
		players[i]->setBasePlayer(&(initial->players[i]), teams);
	}

	session.gameTPF=initial->gameTPF;
	session.gameLatency=initial->gameLatency;

	anyPlayerWaited=false;
}

void Game::executeOrder(Order *order, int localPlayer)
{
	anyPlayerWaited=false;
	assert(order->sender>=0);
	assert(order->sender<32);
	assert(order->sender<session.numberOfPlayer);
	Team *team=players[order->sender]->team;
	assert(team);
	bool isPlayerAlive=team->isAlive;
	Uint8 orderType=order->getOrderType();
	if (orderType!=ORDER_WAITING_FOR_PLAYER)
	{
		anyPlayerWaitedTimeFor=0;
		fprintf(logFile, "[%d] %d (", order->ustep, order->getOrderType());
	}
	switch (orderType)
	{
		case ORDER_CREATE:
		{
			if (!isPlayerAlive)
				break;

			int posX=((OrderCreate *)order)->posX;
			int posY=((OrderCreate *)order)->posY;
			int teamNumber=((OrderCreate *)order)->team;
			assert(teamNumber==team->teamNumber);
			int typeNumber=((OrderCreate *)order)->typeNumber;
			bool isVirtual=globalContainer->buildingsTypes.get(typeNumber)->isVirtual;
			if (!isVirtual && (team->noMoreBuildingSitesCountdown>0))
				break;
			if (isVirtual || checkRoomForBuilding(posX, posY, typeNumber, teamNumber))
			{
				if(posX<0)
					posX+=map.getW();
				if(posY<0)
					posY+=map.getH();
				posX&=map.getMaskW();
				posY&=map.getMaskH();

				Building *b=addBuilding(posX, posY, typeNumber, teamNumber);
				assert(b);
				if (b)
				{
					fprintf(logFile, "ORDER_CREATE");
					BuildingType *type=b->type;
					Team *owner=b->owner;
					if (type->unitProductionTime)
						owner->swarms.push_back(b);
					if (type->shootingRange)
						owner->turrets.push_back(b);
					if (type->canExchange)
						owner->canExchange.push_back(b);
					if (type->isVirtual)
						owner->virtualBuildings.push_back(b);
					if (type->zonable[WORKER])
						owner->clearingFlags.push_back(b);
					if (type->zonableForbidden)
					{
						owner->zonableForbidden.push_back(b);
						map.setForbiddenArea(posX, posY, b->unitStayRange, team->me);
					}
					b->update();
				}
			}
		}
		break;
		case ORDER_MODIFY_BUILDING:
		{
			if (!isPlayerAlive)
				break;
			OrderModifyBuilding *omb=(OrderModifyBuilding *)order;
			Uint16 gid=omb->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				fprintf(logFile, "ORDER_MODIFY_BUILDING");
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
			OrderModifyExchange *ome=(OrderModifyExchange *)order;
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
			OrderModifyFlag *omf=(OrderModifyFlag *)order;
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
					teams[team]->computeForbiddenArea();
					if (newRange<oldRange)
					{
						teams[team]->dirtyGlobalGradient();
						map.dirtyLocalGradient(b->posX-oldRange-16, b->posY-oldRange-16, 32+oldRange*2, 32+oldRange*2, team);
					}
				}
				else
				{
					if (b->type->zonable[WORKER])
					{
						b->updateClearingFlag(0);
						b->updateClearingFlag(1);
					}
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
			OrderModifyClearingFlag *omcf=(OrderModifyClearingFlag *)order;
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
				b->updateClearingFlag(0);
				b->updateClearingFlag(1);
			}
		}
		break;
		case ORDER_MOVE_FLAG:
		{
			if (!isPlayerAlive)
				break;
			OrderMoveFlag *omf=(OrderMoveFlag *)order;
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
					{
						teams[team]->computeForbiddenArea();
						teams[team]->dirtyGlobalGradient();
					}
				}
				else
				{
					if (drop && b->type->zonable[WORKER])
					{
						b->updateClearingFlag(0);
						b->updateClearingFlag(1);
					}
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
		case ORDER_MODIFY_SWARM:
		{
			if (!isPlayerAlive)
				break;
			OrderModifySwarm *oms=(OrderModifySwarm *)order;
			Uint16 gid=oms->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Building *b=teams[team]->myBuildings[id];
			assert(b);
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
			Uint16 gid=((OrderDelete *)order)->gid;
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
					teams[team]->computeForbiddenArea();
					teams[team]->dirtyGlobalGradient();
					int range=b->unitStayRange;
					map.dirtyLocalGradient(b->posX-range-16, b->posY-range-16, 32+range*2, 32+range*2, team);
				}
			}
		}
		break;
		case ORDER_CANCEL_DELETE:
		{
			Uint16 gid=((OrderCancelDelete *)order)->gid;
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
			Uint16 gid=((OrderConstruction *)order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CONSTRUCTION");
				b->launchConstruction();
			}
		}
		break;
		case ORDER_CANCEL_CONSTRUCTION:
		{
			if (!isPlayerAlive)
				break;
			Uint16 gid=((OrderCancelConstruction *)order)->gid;
			int team=Building::GIDtoTeam(gid);
			int id=Building::GIDtoID(gid);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
			{
				fprintf(logFile, "ORDER_CANCEL_CONSTRUCTION");
				b->cancelConstruction();
			}
		}
		break;
		case ORDER_SET_ALLIANCE:
		{
			SetAllianceOrder *sao=((SetAllianceOrder *)order);
			Uint32 team=sao->teamNumber;
			teams[team]->allies=sao->alliedMask;
			teams[team]->enemies=sao->enemyMask;
			teams[team]->sharedVisionExchange=sao->visionExchangeMask;
			teams[team]->sharedVisionFood=sao->visionFoodMask;
			teams[team]->sharedVisionOther=sao->visionOtherMask;
			setAIAlliance();
			fprintf(logFile, "ORDER_SET_ALLIANCE");
		}
		break;
		case ORDER_WAITING_FOR_PLAYER:
		{
			anyPlayerWaited=true;
			maskAwayPlayer=((WaitingForPlayerOrder *)order)->maskAwayPlayer;
			//fprintf(logFile, "ORDER_WAITING_FOR_PLAYER");
		}
		break;
		case ORDER_PLAYER_QUIT_GAME:
		{
			//PlayerQuitsGameOrder *pqgo=(PlayerQuitsGameOrder *)order;
			//netGame have to handle this
			// players[pqgo->player]->type=Player::P_LOST_B;
			fprintf(logFile, "ORDER_PLAYER_QUIT_GAME");
		}
		break;
	}
	if (orderType!=ORDER_WAITING_FOR_PLAYER)
		fprintf(logFile, ")\n");
}

bool Game::isHumanAllAllied(void)
{
	Uint32 nonAIMask=0;
	
	// AIMask now have the mask of everything which isn't AI
	for (int i=0; i<session.numberOfTeam; i++)
	{
		nonAIMask |= ((teams[i]->type != BaseTeam::T_AI) ? 1 : 0) << i;
		//printf("team %d is AI is %d\n", i, teams[i]->type == BaseTeam::T_AI);
	}

	// if there is any non-AI player with which we aren't allied, return false
	// or if there is any player allied to AI
	for (int i=0; i<session.numberOfTeam; i++)
		if (teams[i]->type != BaseTeam::T_AI)
		{
			if (teams[i]->allies != nonAIMask)
				return false;
		}
	
	return true;
}

void Game::setAIAlliance(void)
{
	if (isHumanAllAllied())
	{
		printf("Game : AIs are now allied vs human\n");
		
		// all human are allied, ally AI
		Uint32 aiMask = 0;
		
		// find all AI
		for (int i=0; i<session.numberOfTeam; i++)
			if (teams[i]->type == BaseTeam::T_AI)
				aiMask |= (1<<i);
		
		printf("AI mask : %x\n", aiMask);
				
		// ally them together
		for (int i=0; i<session.numberOfTeam; i++)
			if (teams[i]->type == BaseTeam::T_AI)
			{
				teams[i]->allies = aiMask;
				teams[i]->enemies = ~teams[i]->allies;
			}
	}
	else
	{
		printf("Game : AIs are now in ffa mode\n");
		
		// free for all on AI side
		for (int i=0; i<session.numberOfTeam; i++)
		{
			if (teams[i]->type == BaseTeam::T_AI)
			{
				teams[i]->allies = teams[i]->me;
				teams[i]->enemies = ~teams[i]->allies;
			}
		}
	}
}

bool Game::load(SDL_RWops *stream)
{
	assert(stream);
	
	// delete existing teams
	for (int i=0; i<session.numberOfTeam; ++i)
		if (teams[i])
		{
			delete teams[i];
			teams[i]=NULL;
		}
	session.numberOfTeam=0;
	for (int i=0; i<session.numberOfPlayer; ++i)
		if (players[i])
		{
			delete players[i];
			players[i]=NULL;
		}
	session.numberOfPlayer=0;

	// clear prestige
	totalPrestige=0;
	totalPrestigeReached=false;
	isGameEnded=false;

	// We load the file's header:
	SessionInfo tempSessionInfo;
	printf("Loading map header\n");
	if (!tempSessionInfo.load(stream))
	{
		fprintf(logFile, "Game::load::tempSessionInfo.load\n");
		return false;
	}

	if (tempSessionInfo.mapGenerationDescriptor && tempSessionInfo.fileIsAMap)
	{
		tempSessionInfo.mapGenerationDescriptor->synchronizeNow();
		if (!generateMap(*tempSessionInfo.mapGenerationDescriptor))
		{
			fprintf(logFile, "Game::load::generateMap\n");
			return false;
		}
	}
	else
	{
		session=(SessionGame)tempSessionInfo;

		SDL_RWseek(stream, tempSessionInfo.gameOffset, SEEK_SET);

		char signature[4];
		SDL_RWread(stream, signature, 4, 1);
		if (session.versionMinor>=31)
		{
			if (memcmp(signature,"GaBe", 4)!=0)
			{
				fprintf(logFile, "Signature missmatch at begin\n");
				return false;
			}
		}
		else if (memcmp(signature, "GAMb",4)!=0)
		{
			fprintf(logFile, "Signature missmatch at begin\n");
			return false;
		}
		
		if (session.versionMinor>=31)
			stepCounter=SDL_ReadBE32(stream);
		setSyncRandSeedA(SDL_ReadBE32(stream));
		setSyncRandSeedB(SDL_ReadBE32(stream));
		setSyncRandSeedC(SDL_ReadBE32(stream));

		SDL_RWread(stream, signature, 4, 1);
		
		if (session.versionMinor>=31)
		{
			if (memcmp(signature,"GaSy", 4)!=0)
			{
				fprintf(logFile, "Signature missmatch after sync rand\n");
				return false;
			}
		}
		else if (memcmp(signature, "GAMm", 4)!=0)
		{
			fprintf(logFile, "Signature missmatch after sync rand\n");
			return false;
		}

		// we load teams
		SDL_RWseek(stream, tempSessionInfo.teamsOffset, SEEK_SET);
		for (int i=0; i<session.numberOfTeam; ++i)
			teams[i]=new Team(stream, this, session.versionMinor);
		if (session.versionMinor>=31)
		{
			SDL_RWread(stream, signature, 4, 1);
			if (memcmp(signature,"GaTe", 4)!=0)
			{
				fprintf(logFile, "Signature missmatch after teams\n");
				return false;
			}
		}
		
		// we have to load team before map
		SDL_RWseek(stream, tempSessionInfo.mapOffset, SEEK_SET);
		if(!map.load(stream, &session, this))
		{
			fprintf(logFile, "Signature missmatch in map\n");
			return false;
		}
		SDL_RWread(stream, signature, 4, 1);
		if (session.versionMinor>=31)
		{
			if (memcmp(signature,"GaMa", 4)!=0)
			{
				fprintf(logFile, "Signature missmatch after map\n");
				return false;
			}
		}
		else if (memcmp(signature,"GAMe", 4)!=0)
		{
			fprintf(logFile, "Signature missmatch after map\n");
			return false;
		}

		// we have to load map and team before players
		SDL_RWseek(stream, tempSessionInfo.playersOffset, SEEK_SET);
		for (int i=0; i<session.numberOfPlayer; ++i)
			players[i]=new Player(stream, teams, session.versionMinor);
			
		if (session.versionMinor>=31)
		{
			SDL_RWread(stream, signature, 4, 1);
			if (memcmp(signature,"GaPl", 4)!=0)
			{
				fprintf(logFile, "Signature missmatch after players\n");
				return false;
			}
		}
		else
			stepCounter=SDL_ReadBE32(stream);

		// we have to finish Team's loading:
		for (int i=0; i<session.numberOfTeam; i++)
			teams[i]->update();
		
		for (int i=0; i<session.numberOfTeam; i++)
			teams[i]->integrity();
		
		// then script
		ErrorReport er;
		SDL_RWseek(stream, tempSessionInfo.mapScriptOffset , SEEK_SET);
		script.load(stream);
		er=script.compileScript(this);
		
		if (er.type!=ErrorReport::ET_OK)
		{
			if (er.type==ErrorReport::ET_NO_SUCH_FILE)
			{
				printf("SGSL : Can't find script file testscript.txt\n");
			}
			else
			{
				printf("SGSL : %s at line %d on col %d\n", er.getErrorString(), er.line+1, er.col);
				return false;
			}
		}
	}

	// Compute new max prestige
	prestigeToReach = std::max(MIN_MAX_PRESIGE, session.numberOfTeam*TEAM_MAX_PRESTIGE);

	return true;
}

void Game::save(SDL_RWops *stream, bool fileIsAMap, const char* name)
{
	assert(stream);

	// first we save a session info
	SessionInfo tempSessionInfo(session);

	// A typical use case: You have loaded a map, and you want to save your game.
	// In this case, the file is no more a map, but a game.
	tempSessionInfo.fileIsAMap=(Sint32)fileIsAMap;
	tempSessionInfo.setMapName(name);

	for (int i=0; i<session.numberOfTeam; ++i)
	{
		tempSessionInfo.team[i]=*teams[i];
		tempSessionInfo.team[i].disableRecursiveDestruction=true;
	}

	for (int i=0; i<session.numberOfPlayer; ++i)
	{
		tempSessionInfo.players[i]=*players[i];
		tempSessionInfo.players[i].disableRecursiveDestruction=true;
	}
	
	tempSessionInfo.save(stream);
	
	if (session.mapGenerationDescriptor && session.fileIsAMap)
	{
		// In this case, the map is fully determinated by the mapGenerationDescriptor.
		//printf("giga compression system activated.\n");
	}
	else
	{
		SAVE_OFFSET(stream, 16);
		SDL_RWwrite(stream, "GaBe", 4, 1);

		SDL_WriteBE32(stream, stepCounter);
		SDL_WriteBE32(stream, getSyncRandSeedA());
		SDL_WriteBE32(stream, getSyncRandSeedB());
		SDL_WriteBE32(stream, getSyncRandSeedC());
		SDL_RWwrite(stream, "GaSy", 4, 1);

		SAVE_OFFSET(stream, 20);
		for (int i=0; i<session.numberOfTeam; ++i)
			teams[i]->save(stream);
		SDL_RWwrite(stream, "GaTe", 4, 1);

		SAVE_OFFSET(stream, 28);
		map.save(stream);
		SDL_RWwrite(stream, "GaMa", 4, 1);
		
		SAVE_OFFSET(stream, 24);
		for (int i=0; i<session.numberOfPlayer; ++i)
			players[i]->save(stream);
		SDL_RWwrite(stream, "GaPl", 4, 1);

		SAVE_OFFSET(stream, 32);
		script.save(stream);
	}

}

void Game::wonSyncStep(void)
{
	totalPrestige=0;
	isGameEnded=false;
	
	for (int i=0; i<session.numberOfTeam; i++)
	{
		bool isOtherAlive=false;
		for (int j=0; j<session.numberOfTeam; j++)
		{
			if ((j!=i) && (!( ((teams[i]->me) & (teams[j]->allies)) /*&& ((teams[j]->me) & (teams[i]->allies))*/ )) && (teams[j]->isAlive))
				isOtherAlive=true;
		}
		teams[i]->hasWon|=!isOtherAlive;
		isGameEnded|=teams[i]->hasWon;
		totalPrestige+=teams[i]->prestige;
	}
	if (totalPrestige >= prestigeToReach)
	{
		totalPrestigeReached=true;
		isGameEnded=true;
	}
}

void Game::scriptSyncStep()
{
	// do a script step
	script.syncStep(gui);

	// alter win/loose conditions
	for (int i=0; i<session.numberOfTeam; i++)
	{
		if (teams[i]->isAlive)
		{
			if (script.hasTeamWon(i))
				teams[i]->hasWon=true;
			if (script.hasTeamLost(i))
				teams[i]->isAlive=false;
		}
	}
}

void Game::syncStep(Sint32 localTeam)
{
	if (!anyPlayerWaited)
	{
		Sint32 startTick=SDL_GetTicks();
		for (int i=0; i<session.numberOfTeam; i++)
			teams[i]->syncStep();
		
		map.syncStep(stepCounter);
		
		syncRand();
		
		if ((stepCounter&31)==16)
		{
			map.switchFogOfWar();
			for (int t=0; t<session.numberOfTeam; t++)
				for (int i=0; i<1024; i++)
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
		
		renderMiniMap(localTeam, false, stepCounter%25, 25);

		if ((stepCounter&31)==0)
		{
			scriptSyncStep();
			wonSyncStep();
		}

		Sint32 endTick=SDL_GetTicks();
		ticksGameSum[stepCounter&31]+=endTick-startTick;
		stepCounter++;
	}
}

void Game::addTeam(void)
{
	if (session.numberOfTeam<32)
	{
		teams[session.numberOfTeam]=new Team(this);
		teams[session.numberOfTeam]->teamNumber=session.numberOfTeam;
		teams[session.numberOfTeam]->race.create(Race::USE_DEFAULT);
		teams[session.numberOfTeam]->setCorrectMasks();

		session.numberOfTeam++;
		for (int i=0; i<session.numberOfTeam; i++)
			teams[i]->setCorrectColor( ((float)i*360.0f) /(float)session.numberOfTeam );
		
		prestigeToReach = std::max(MIN_MAX_PRESIGE, session.numberOfTeam*TEAM_MAX_PRESTIGE);
		
		map.addTeam();
	}
	else
		assert(false);
}

void Game::removeTeam(void)
{
	if (session.numberOfTeam>0)
	{
		Team *team=teams[--session.numberOfTeam];

		team->clearMap();

		delete team;

		assert (session.numberOfTeam!=0);
		for (int i=0; i<session.numberOfTeam; ++i)
			teams[i]->setCorrectColor( ((float)i*360.0f) /(float)session.numberOfTeam );

		map.removeTeam();
	}
}

void Game::cleanUncontrolledTeams(void)
{
	for (int t=0; t<session.numberOfTeam; t++)
	{
		Team *team = teams[t];
		if (team->playersMask==0)
		{
			team->clearMap();
			team->clearLists();
			team->clearMem();
		}
	}
}

void Game::regenerateDiscoveryMap(void)
{
	map.unsetMapDiscovered();
	for (int t=0; t<session.numberOfTeam; t++)
	{
		for (int i=0; i<1024; i++)
		{
			Unit *u=teams[t]->myUnits[i];
			if (u)
			{
				map.setMapDiscovered(u->posX-1, u->posY-1, 3, 3, teams[t]->sharedVisionOther);
			}
		}
		for (int i=0; i<1024; i++)
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
	assert(team<session.numberOfTeam);

	UnitType *ut=teams[team]->race.getUnitType(typeNum, level);

	bool fly=ut->performance[FLY];
	bool free;
	if (fly)
		free=map.isFreeForAirUnit(x, y);
	else
		free=map.isFreeForGroundUnit(x, y, ut->performance[SWIM], Team::teamNumberToMask(team));
	if (!free)
		return NULL;

	int id=-1;
	for (int i=0; i<1024; i++)//we search for a free place for a unit.
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

Building *Game::addBuilding(int x, int y, int typeNum, int teamNumber)
{
	Team *team=teams[teamNumber];
	assert(team);

	int id=-1;
	for (int i=0; i<1024; i++)//we search for a free place for a building.
		if (team->myBuildings[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==-1)
		return NULL;

	//ok, now we can safely deposite an building.
	int gid=Building::GIDfrom(id, teamNumber);

	int w=globalContainer->buildingsTypes.get(typeNum)->width;
	int h=globalContainer->buildingsTypes.get(typeNum)->height;

	Building *b=new Building(x&map.getMaskW(), y&map.getMaskH(), gid, typeNum, team, &globalContainer->buildingsTypes);

	if (b->type->canExchange)
		team->canExchange.push_front(b);
	if (b->type->isVirtual)
		team->virtualBuildings.push_front(b);
	else
		map.setBuilding(x, y, w, h, gid);
	team->myBuildings[id]=b;
	return b;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, SDL_Rect* r, unsigned flags)
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
			r->x=x;
			r->y=y;
			r->w=1;
			r->h=1;
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
			r->x=x;
			r->y=y;
			r->w=1;
			r->h=1;
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
			r->x=b->posX;
			r->y=b->posY;
			r->w=b->type->width;
			r->h=b->type->height;
			if (!b->type->isVirtual)
				map.setBuilding(r->x, r->y, r->w, r->h, NOGBID);
			delete b;
			teams[team]->myBuildings[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_FLAG)
	{
		for (int ti=0; ti<session.numberOfTeam; ti++)
			for (std::list<Building *>::iterator bi=teams[ti]->virtualBuildings.begin(); bi!=teams[ti]->virtualBuildings.end(); ++bi)
				if ((*bi)->posX==x && (*bi)->posY==y)
				{
					teams[ti]->virtualBuildings.erase(bi);
					teams[ti]->myBuildings[Building::GIDtoID((*bi)->gid)]=NULL;;
					delete *bi;
					r->x=x;
					r->y=y;
					r->w=1;
					r->h=1;
					found=true;
					break;
				}
	}
	return found;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, int size, SDL_Rect* r, unsigned flags)
{
	int sts=size>>1;
	int stp=(~size)&1;
	SDL_Rect rl;
	r->x=x;
	r->y=y;
	r->w=0;
	r->h=0;
	bool somethingInRect=false;
	
	for (int scx=(x-sts); scx<=(x+sts-stp); scx++)
		for (int scy=(y-sts); scy<=(y+sts-stp); scy++)
			if (removeUnitAndBuildingAndFlags((scx&(map.getMaskW())), (scy&(map.getMaskH())), &rl, flags))
				if (somethingInRect)
					Utilities::rectExtendRect(&rl, r);
				else
				{
					*r=rl;
					somethingInRect=true;
				}
	
	return somethingInRect;
}

bool Game::checkRoomForBuilding(int mousePosX, int mousePosY, int typeNum, int *buildingPosX, int *buildingPosY, int teamNumber, bool checkFow)
{
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int x=mousePosX+bt->decLeft;
	int y=mousePosY+bt->decTop;

	*buildingPosX=x;
	*buildingPosY=y;

	return checkRoomForBuilding(x, y, typeNum, teamNumber, checkFow);
}

bool Game::checkRoomForBuilding(int x, int y, int typeNum, int teamNumber, bool checkFow)
{
	Team *team=teams[teamNumber];
	assert(team);
	
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
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

bool Game::checkHardRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY)
{
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int x=coordX+bt->decLeft;
	int y=coordY+bt->decTop;

	*mapX=x;
	*mapY=y;

	return checkHardRoomForBuilding(x, y, typeNum);
}

bool Game::checkHardRoomForBuilding(int x, int y, int typeNum)
{
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int w=bt->width;
	int h=bt->height;
	assert(!bt->isVirtual); // This method is not for flags!
	return map.isHardSpaceForBuilding(x, y, w, h);
}

void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth)
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
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-actLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
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
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-actLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
	}
	else
		assert(false);
}

void Game::drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int localTeam, bool drawHealthFoodBar, bool drawPathLines, const bool useMapDiscovered)
{
	int id=Unit::GIDtoID(gid);
	int team=Unit::GIDtoTeam(gid);
	Unit *unit=teams[team]->myUnits[id];
	assert(unit);
	if (!unit)
	{
		//printf("Error (%d, %d)\n", x, y);
		globalContainer->gfx->drawRect((x<<5)+1, (y<<5)+1, 30, 30, 255, 255, 0);
		return;
	}
	int dx=unit->dx;
	int dy=unit->dy;

	if (!useMapDiscovered)
		if ((!map.isFOWDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me))&&(!map.isFOWDiscovered(x+viewportX-dx, y+viewportY-dy, teams[localTeam]->me)))
			return;

	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];
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

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(teams[team]->colorR, teams[team]->colorG, teams[team]->colorB);

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);
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

	if ((px<mouseX)&&((px+32)>mouseX)&&(py<mouseY)&&((py+32)>mouseY)&&((useMapDiscovered)||(map.isFOWDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me))||(Unit::GIDtoTeam(gid)==localTeam)))
		mouseUnit=unit;

	if (drawHealthFoodBar)
	{
		drawPointBar(px+1, py+25, LEFT_TO_RIGHT, 10, (unit->hungry*10)/Unit::HUNGRY_MAX, 80, 179, 223);
		float hpRatio=(float)unit->hp/(float)unit->performance[HP];
		if (hpRatio>0.6)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 78, 187, 78);
		else if (hpRatio>0.3)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 255, 0);
		else
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 0, 0);
	}
	if ((drawPathLines) && (unit->owner->sharedVisionOther & teams[localTeam]->me))
		if (unit->displacement==Unit::DIS_GOING_TO_FLAG
			|| unit->displacement==Unit::DIS_GOING_TO_RESSOURCE
			|| unit->displacement==Unit::DIS_GOING_TO_BUILDING)
		{
			int lsx, lsy, ldx, ldy;
			lsx=px+16;
			lsy=py+16;
			map.mapCaseToDisplayable(unit->targetX, unit->targetY, &ldx, &ldy, viewportX, viewportY);
			globalContainer->gfx->drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250);
		}
}

void Game::drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int localTeam, bool drawHealthFoodBar, bool drawPathLines, bool drawBuildingRects, const bool useMapDiscovered)
{
	int id;
	int left=(sx>>5);
	int top=(sy>>5);
	int right=((sx+sw+31)>>5);
	int bot=((sy+sh+31)>>5);
	std::set<Building *> buildingList;
	std::list<BuildingType *> localySeenBuildings;

	// we draw the terrains, eventually with debug rects:
	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
				(map.isMapDiscovered(x+viewportX-1, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX-1, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX-1, y+viewportY+1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY+1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY+1,  teams[localTeam]->me)) ||
				(useMapDiscovered))
			{
				// draw terrain
				id=map.getTerrain(x+viewportX, y+viewportY);
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

				globalContainer->gfx->drawSprite(x<<5, y<<5, sprite, id);

			}

	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
				(map.isMapDiscovered(x+viewportX-1, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY-1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX-1, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX-1, y+viewportY+1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX, y+viewportY+1,  teams[localTeam]->me)) ||
				(map.isMapDiscovered(x+viewportX+1, y+viewportY+1,  teams[localTeam]->me)) ||
				(useMapDiscovered))
			{
				// draw ressource
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

	//for (int y=top; y<=bot; y++)
	//	for (int x=left; x<=right; x++)
	//		if (map.getBuilding(x+viewportX, y+viewportY)!=NOGBID)
	//			globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 0, 0);
	
	//for (int y=top; y<=bot; y++)
	//	for (int x=left; x<=right; x++)
	//		if (!map.isFreeForGroundUnit(x+viewportX, y+viewportY, 0, 0))
	//			globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 0, 0);
	
	// We draw ground units:
	mouseUnit=NULL;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getGroundUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, localTeam, drawHealthFoodBar, drawPathLines, useMapDiscovered);
		}
	
	if (false)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				if (players[1] && players[1]->ai)
				{
					AICastor *ai=(AICastor *)players[1]->ai->aiImplementation;
					Uint8 *gradient=ai->buildingNeighbourMap;
					assert(gradient);
					size_t addr=((x+viewportX)&map.wMask)+map.w*((y+viewportY)&map.hMask);
					
					globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, gradient[addr]);
				}
	
	// We draw debug area:
	if (false)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				//if (map.getForbidden(x+viewportX, y+viewportY))
				{
					//if (!map.isFreeForGroundUnit(x+viewportX, y+viewportY, 1, 1))
					//	globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 16, 32);
					//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 16, 32);
					globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(1, 5, 0, x+viewportX, y+viewportY));
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(0, CORN, 1, x+viewportX, y+viewportY));
					
					globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, ((x+viewportX)&(map.getMaskW())));
					globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, ((y+viewportY)&(map.getMaskH())));
				}

	// We draw debug area:
	if (false)
	{
		assert(teams[0]);
		Building *b=teams[0]->myBuildings[5];
		if (b)
			for (int y=top-1; y<=bot; y++)
				for (int x=left-1; x<=right; x++)
					if (map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY)<16)
					{
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.getGradient(0, 6, 1, x+viewportX, y+viewportY));
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY));
						int lx=(x+viewportX-b->posX+15+32)&31;
						int ly=(y+viewportY-b->posY+15+32)&31;
						globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[1][lx+ly*32]);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, lx);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+10, globalContainer->littleFont, ly);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX-b->posX+16);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY-b->posY+16);
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
					if (b->verbose==1)
					{
						if (b->globalGradient[1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont,
								b->globalGradient[1][((x+viewportX)&(map.getMaskW()))+((y+viewportY)&(map.getMaskH()))*w]);
					}
					else if (map.isInLocalGradient(x+viewportX, y+viewportY, b->posX, b->posY))
					{
						int lx=(x+viewportX-b->posX+15)&31;
						int ly=(y+viewportY-b->posY+15)&31;
						if (!b->dirtyLocalGradient[1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[1][lx+ly*32]);
					}

					globalContainer->littleFont->pushColor(192, 192, 192);
					globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, (x+viewportX+map.getW())&(map.getMaskW()));
					globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, (y+viewportY+map.getH())&(map.getMaskH()));
					globalContainer->littleFont->popColor();
				}

	}

	// We draw ground buildings:
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
				assert(building); // if this fails, and unwanted garbage-UID is on the ground.
				if (useMapDiscovered
					|| Building::GIDtoTeam(gid)==localTeam
					|| (building->seenByMask & teams[localTeam]->me)
					|| map.isFOWDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me))
					buildingList.insert(building); //TODO: we may make it faster by pushing a Building* in the buildingList instead of a uid.
			}
		}
	
	for (std::set <Building *>::iterator it=buildingList.begin(); it!=buildingList.end(); ++it)
	{
		Building *building = *it;
		assert(building);
		BuildingType *type=building->type;
		Team *team=building->owner;

		int imgid=type->startImage;
		int x, y;
		int dx, dy;
		map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

		// select buildings and set the team colors
		Sprite *buildingSprite=globalContainer->buildings;
		dx = (type->width<<5)-buildingSprite->getW(imgid);
		dy = (type->height<<5)-buildingSprite->getH(imgid);
		buildingSprite->setBaseColor(team->colorR, team->colorG, team->colorB);

		// draw building
		globalContainer->gfx->drawSprite(x+dx, y+dy, buildingSprite, imgid);

		if (!type->hueImage)
		{
			// Here we draw the sprite with a flag:
			// Then we draw a hued flag of the team.
			int flagImgid=type->flagImage;
			//int w=building->type->width;
			int h=type->height;

			//We draw the flag at left bottom corner on the building
			int fw=buildingSprite->getW(flagImgid);
			//int fh=flagSprite->getH();

			globalContainer->gfx->drawSprite(x/*+(w<<5)-fh*/, y+(h<<5)-fw, buildingSprite, flagImgid);

			//We add a hued color over the flag
			//globalContainer->gfx->drawSprite(x/*+(w<<5)-fh*/, y+(h<<5)-fw, buildingSprite, flagImgid+1);
		}

		if (drawBuildingRects)
		{
			int batW=(type->width )<<5;
			int batH=(type->height)<<5;
			int typeNum=building->typeNum;
			globalContainer->gfx->drawRect(x, y, batW, batH, 255, 255, 255, 127);

			BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
			int lastTypeNum=typeNum;
			int max=0;
			while(lastbt->nextLevelTypeNum>=0)
			{
				lastTypeNum=lastbt->nextLevelTypeNum;
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

		if (drawHealthFoodBar && (building->owner->sharedVisionOther & teams[localTeam]->me))
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

				if (hpRatio>0.6)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 78, 187, 78);
				else if (hpRatio>0.3)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 255, 0);
				else
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 0, 0);
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
	
	
	// We draw air units:
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getAirUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, localTeam, drawHealthFoodBar, drawPathLines, useMapDiscovered);
		}

	// Let's paint the bullets:
	// TODO : optimise : test only possible sectors to show bullets.

	Sprite *bulletSprite=globalContainer->buildings;
	// FIXME : have team in bullets to have the correct color

	int mapPixW=(map.getW())<<5;
	int mapPixH=(map.getH())<<5;

	for (int i=0; i<(map.getSectorW()*map.getSectorH()); i++)
	{
		Sector *s=map.getSector(i);
		for (std::list<Bullet *>::iterator it=s->bullets.begin();it!=s->bullets.end();it++)
		{
			int x=(*it)->px-(viewportX<<5);
			int y=(*it)->py-(viewportY<<5);

			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;

			//printf("px=(%d, %d) vp=(%d, %d)\n", (*it)->px, (*it)->py, viewportX, viewportY);
			if ( (x<=sw) && (y<=sh) )
				globalContainer->gfx->drawSprite(x, y, bulletSprite, BULLET_IMGID);
		}
	}
	
	// draw black & shading

	if (!useMapDiscovered)
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

				// first draw black
				i0=!map.isMapDiscovered(x+viewportX+1, y+viewportY+1, teams[localTeam]->me) ? 1 : 0;
				i1=!map.isMapDiscovered(x+viewportX, y+viewportY+1, teams[localTeam]->me) ? 1 : 0;
				i2=!map.isMapDiscovered(x+viewportX+1, y+viewportY, teams[localTeam]->me) ? 1 : 0;
				i3=!map.isMapDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me) ? 1 : 0;
				unsigned blackValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);
				if (blackValue==15)
					globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0);
				else if (blackValue)
					globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainBlack, blackValue);

				// then if it isn't full black, draw shade
				if (blackValue!=15)
				{
					i0=!map.isFOWDiscovered(x+viewportX+1, y+viewportY+1, teams[localTeam]->me) ? 1 : 0;
					i1=!map.isFOWDiscovered(x+viewportX, y+viewportY+1, teams[localTeam]->me) ? 1 : 0;
					i2=!map.isFOWDiscovered(x+viewportX+1, y+viewportY, teams[localTeam]->me) ? 1 : 0;
					i3=!map.isFOWDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me) ? 1 : 0;
					unsigned shadeValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);
					
					if (shadeValue==15)
						globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0, 127);
					else if (shadeValue)
						globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainShader, shadeValue);
				}
			}
	}

	// we look on the whole map for buildings
	// TODO : increase speed, do not count on graphic clipping
	for (std::list<Building *>::iterator virtualIt=teams[localTeam]->virtualBuildings.begin();
		virtualIt!=teams[localTeam]->virtualBuildings.end(); ++virtualIt)
	{
		Building *building=*virtualIt;
		BuildingType *type=building->type;

		int team=building->owner->teamNumber;

		int imgid=type->startImage;

		int x, y;
		map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

		// all flags are hued:
		Sprite *buildingSprite=globalContainer->buildings;
		buildingSprite->setBaseColor(teams[team]->colorR, teams[team]->colorG, teams[team]->colorB);
		globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

		// flag circle:
		if (drawHealthFoodBar || (building==selectedBuilding))
			globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

		// FIXME : ugly copy past
		if (drawHealthFoodBar)
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

void Game::drawMiniMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int localTeam)
{
	// draw the prerendered minimap, decide if we use low speed graphics or nor
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
	{
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 0, 128, 14, 0, 0, 0);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 114, 128, 14, 0, 0, 0);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 14, 14, 100, 0, 0, 0);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-14, 14, 14, 100, 0, 0, 0);
	}
	else
	{
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 0, 128, 14, 0, 0, 40, 180);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 114, 128, 14, 0, 0, 40, 180);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 14, 14, 100, 0, 0, 40, 180);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-14, 14, 14, 100, 0, 0, 40, 180);
	}

	globalContainer->gfx->drawRect(globalContainer->gfx->getW()-115, 13, 102, 102, 200, 200, 200);
	assert(minimap);
	globalContainer->gfx->drawSurface(globalContainer->gfx->getW()-114, 14, minimap);


	// get data for minimap
	int rx, ry, rw, rh, n;
	int mMax;
	int szX, szY;
	int decX, decY;
	GameUtilities::globalCoordToLocalView(this, localTeam, viewportX, viewportY, &rx, &ry);
	Utilities::computeMinimapData(100, map.getW(), map.getH(), &mMax, &szX, &szY, &decX, &decY);

	// draw screen lines
	rx=(rx*100)/mMax;
	ry=(ry*100)/mMax;
	rw=((globalContainer->gfx->getW()-128)*100)/(32*mMax);
	rh=(globalContainer->gfx->getH()*100)/(32*mMax);

	for (n=0; n<rw+1; n++)
	{
		globalContainer->gfx->drawPixel(globalContainer->gfx->getW()-114+((rx+n)%szX)+decX, 14+(ry%szY)+decY, 255, 255, 255);
		globalContainer->gfx->drawPixel(globalContainer->gfx->getW()-114+((rx+n)%szX)+decX, 14+((ry+rh)%szY)+decY, 255, 255, 255);
	}
	for (n=0; n<rh+1; n++)
	{
		globalContainer->gfx->drawPixel(globalContainer->gfx->getW()-114+(rx%szX)+decX, 14+((ry+n)%szY)+decY, 255, 255, 255);
		globalContainer->gfx->drawPixel(globalContainer->gfx->getW()-114+((rx+rw)%szX)+decX, 14+((ry+n)%szY)+decY, 255, 255, 255);
	}

	// draw flags
	if (localTeam>=0)
		for (std::list<Building *>::iterator virtualIt=teams[localTeam]->virtualBuildings.begin();
				virtualIt!=teams[localTeam]->virtualBuildings.end(); ++virtualIt)
		{
			int fx, fy;
			GameUtilities::globalCoordToLocalView(this, localTeam, (*virtualIt)->posXLocal, (*virtualIt)->posYLocal, &fx, &fy);
			fx = ((fx*100)/mMax);
			fy = ((fy*100)/mMax);

			globalContainer->gfx->drawPixel(globalContainer->gfx->getW()-114+fx+decX, 14+fy+decY, 210, 255, 210);
		}
}

void Game::renderMiniMap(int localTeam, const bool useMapDiscovered, int step, int stepCount)
{
	float dMx, dMy;
	int dx, dy;
	float minidx, minidy;
	int r, g, b;
	int nCount;
	int UnitOrBuildingIndex = -1;
	assert(localTeam>=0);
	assert(localTeam<32);

	int terrainColor[3][3] = {
		{ 0, 40, 120 }, // Water
		{ 170, 170, 0 }, // Sand
		{ 0, 90, 0 }, // Grass
	};

	int buildingsUnitsColor[6][3] = {
		{ 10, 240, 20 }, // self
		{ 220, 200, 20 }, // ally
		{ 220, 25, 30 }, // enemy
		{ (10*3)/5, (240*3)/5, (20*3)/5 }, // self FOW
		{ (220*3)/5, (200*3)/5, (20*3)/5 }, // ally FOW
		{ (220*3)/5, (25*3)/5, (30*3)/5 }, // enemy FOW
	};

	int pcol[3+MAX_RESSOURCES];
	int pcolIndex, pcolAddValue;
	int teamId;

	int decSPX, decSPY;

	// get data
	int mMax;
	int szX, szY;
	int decX, decY;
	Utilities::computeMinimapData(100, map.getW(), map.getH(), &mMax, &szX, &szY, &decX, &decY);

	int stepLength = szY/stepCount;
	int stepStart = step * stepLength;

	dMx=(float)mMax/100.0f;
	dMy=(float)mMax/100.0f;

	decSPX=teams[localTeam]->startPosX-map.getW()/2;
	decSPY=teams[localTeam]->startPosY-map.getH()/2;

	for (dy=stepStart; dy<stepStart+stepLength; dy++)
	{
		for (dx=0; dx<szX; dx++)
		{
			memset(pcol, 0, sizeof(pcol));
			nCount=0;
			
			// compute
			for (minidx=(dMx*dx)+decSPX; minidx<=(dMx*(dx+1))+decSPX; minidx++)
			{
				for (minidy=(dMy*dy)+decSPY; minidy<=(dMy*(dy+1))+decSPY; minidy++)
				{
					Uint16 gid;
					bool seenUnderFOW = false;

					gid=map.getAirUnit((Sint16)minidx, (Sint16)minidy);
					if (gid==NOGUID)
						gid=map.getGroundUnit((Sint16)minidx, (Sint16)minidy);
					if (gid==NOGUID)
					{
						gid=map.getBuilding((Sint16)minidx, (Sint16)minidy);
						if (gid!=NOGUID)
						{
							if (teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)]->seenByMask & teams[localTeam]->me)
							{
								seenUnderFOW = true;
							}
						}
					}
					if (gid!=NOGUID)
					{
						teamId=gid/1024;
						if (useMapDiscovered || map.isFOWDiscovered((int)minidx, (int)minidy, teams[localTeam]->me))
						{
							if (teamId==localTeam)
								UnitOrBuildingIndex = 0;
							else if ((teams[localTeam]->allies) & (teams[teamId]->me))
								UnitOrBuildingIndex = 1;
							else
								UnitOrBuildingIndex = 2;
							goto unitOrBuildingFound;
						}
						else if (seenUnderFOW)
						{
							if (teamId==localTeam)
								UnitOrBuildingIndex = 3;
							else if ((teams[localTeam]->allies) & (teams[teamId]->me))
								UnitOrBuildingIndex = 4;
							else
								UnitOrBuildingIndex = 5;
							goto unitOrBuildingFound;
						}
					}
					
					if (useMapDiscovered || map.isMapDiscovered((int)minidx, (int)minidy, teams[localTeam]->me))
					{
						// get color to add
						Ressource r=map.getRessource((int)minidx, (int)minidy);
						if (r.type!=NO_RES_TYPE)
						{
							pcolIndex=r.type + 3;
						}
						else
						{
							pcolIndex=map.getUMTerrain((int)minidx,(int)minidy);
						}
						
						// get weight to add
						if (useMapDiscovered || map.isFOWDiscovered((int)minidx, (int)minidy, teams[localTeam]->me))
							pcolAddValue=5;
						else
							pcolAddValue=3;

						pcol[pcolIndex]+=pcolAddValue;
					}

					nCount++;
				}
			}

			// Yes I know, this is *ugly*, but this piece of code *needs* speedup
			unitOrBuildingFound:

			if (UnitOrBuildingIndex >= 0)
			{
				r = buildingsUnitsColor[UnitOrBuildingIndex][0];
				g = buildingsUnitsColor[UnitOrBuildingIndex][1];
				b = buildingsUnitsColor[UnitOrBuildingIndex][2];
				UnitOrBuildingIndex = -1;
			}
			else
			{
				nCount*=5;

				int lr, lg, lb;
				lr = lg = lb = 0;
				for (int i=0; i<3; i++)
				{
					lr += pcol[i]*terrainColor[i][0];
					lg += pcol[i]*terrainColor[i][1];
					lb += pcol[i]*terrainColor[i][2];
				}
				for (int i=0; i<MAX_RESSOURCES; i++)
				{
					RessourceType *rt = globalContainer->ressourcesTypes.get(i);
					lr += pcol[i+3]*(rt->minimapR);
					lg += pcol[i+3]*(rt->minimapG);
					lb += pcol[i+3]*(rt->minimapB);
				}

				r = lr/nCount;
				g = lg/nCount;
				b = lb/nCount;
			}
			minimap->drawPixel(dx+decX, dy+decY, r, g, b);
		}
	}

	if (stepStart+stepLength<szY)
	{
		minimap->drawHorzLine(decX, decY+stepStart+stepLength, szX, 100, 100, 100);
	}

	/*// overdraw flags
	if (localTeam>=0)
		for (std::list<Building *>::iterator virtualIt=teams[localTeam]->virtualBuildings.begin();
				virtualIt!=teams[localTeam]->virtualBuildings.end(); ++virtualIt)
		{
			int fx, fy;
			fx=(*virtualIt)->posXLocal-decSPX+map.getW();
			fx&=map.getMaskW();
			fy=(*virtualIt)->posYLocal-decSPY+map.getH();
			fy&=map.getMaskH();
			r=210;
			g=255;
			b=210;
			fx = ((fx*szX)/mMax);
			fy = ((fy*szY)/mMax);

			if ((fy>=stepStart) && (fy<stepStart+stepLength))
				minimap->drawPixel(fx+decX, fy+decY, r, g, b);
		}*/
}

Uint32 Game::checkSum(std::list<Uint32> *checkSumsList, std::list<Uint32> *checkSumsListForBuildings, std::list<Uint32> *checkSumsListForUnits)
{
	Uint32 cs=0;

	cs^=session.checkSum();
	if (checkSumsList)
		checkSumsList->push_back(cs);// [0]

	cs=(cs<<31)|(cs>>1);
	for (int i=0; i<session.numberOfTeam; i++)
	{
		cs^=teams[i]->checkSum(checkSumsList, checkSumsListForBuildings, checkSumsListForUnits);
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [1+t*20]
	
	cs=(cs<<31)|(cs>>1);
	for (int i=0; i<session.numberOfPlayer; i++)
	{
		cs^=players[i]->checkSum(checkSumsList);
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [2+t*20+p*2]
	
	cs=(cs<<31)|(cs>>1);
	cs^=map.checkSum(false);
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [3+t*20+p*2]

	cs^=getSyncRandSeedA();
	cs^=getSyncRandSeedB();
	cs^=getSyncRandSeedC();
	if (checkSumsList)
		checkSumsList->push_back(cs);// [4+t*20+p*2]

	cs^=script.checkSum();
	if (checkSumsList)
		checkSumsList->push_back(cs);// [5+t*20+p*2]
	
	return cs;
}

Team *Game::getTeamWithMostPrestige(void)
{
	int maxPrestige=0;
	Team *maxPrestigeTeam=NULL;
	
	for (int i=0; i<session.numberOfTeam; i++)
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

const char *glob2FilenameToName(const char *filename)
{
	SessionInfo tempSession;
	SDL_RWops *stream=globalContainer->fileManager->open(filename, "rb");
	if (stream)
	{
		if (tempSession.load(stream))
		{
			SDL_RWclose(stream);
			return Utilities::strdup(tempSession.getMapName());
		}
		else
		{
			SDL_RWclose(stream);
		}
	}
	return NULL;
}

template<typename It, typename T>
class contains: std::unary_function<T, bool>
{
public:
	contains(const It from, const It to) : from(from), to(to) {}
	bool operator()(T d) { return (std::find(from, to, d) != to); }
private:
	const It from;
	const It to;
};

const char *glob2NameToFilename(const char *dir, const char *name, const char *extension)
{
	const char* pattern = " \t";
	const char* endPattern = strchr(pattern, '\0');
	std::string fileName = name;
	//std::replace(fileName.begin(), fileName.end(), ' ', '_');
	//std::replace(fileName.begin(), fileName.end(), '\t', '_');
	std::replace_if(fileName.begin(), fileName.end(), contains<const char*, char>(pattern, endPattern), '_');
	std::string fullFileName = dir;
	fullFileName += DIR_SEPARATOR + fileName;
	if (extension && (*extension != '\0'))
	{
		fullFileName += '.';
		fullFileName += extension;
	}
	return Utilities::strdup(fullFileName.c_str());
}
