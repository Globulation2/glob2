// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Order execution. Split out of Game.cpp; see Game.cpp for the rest of the
// Game class implementation.


#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>



#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include <SDL3/SDL.h>
#include "Player.h"
#include "net/message/MessageRecipients.h"


#include "Brush.h"

#include "ReplayWriter.h"

Building* Game::lookupBuilding(Uint16 gid) const
{
	// Central order boundary: GIDtoID is modulo MAX_COUNT, but the decoded
	// team must be checked before indexing. A missing building is also invalid.
	int team=Building::GIDtoTeam(gid);
	int id=Building::GIDtoID(gid);
	if (team >= mapHeader.getNumberOfTeams() || !teams[team]) return nullptr;
	return teams[team]->myBuildings[id];
}

bool Game::isOrderTeam(Sint64 teamNumber) const
{
	return teamNumber >= 0 && teamNumber < mapHeader.getNumberOfTeams() && teams[teamNumber];
}

void Game::executeOrder(std::shared_ptr<Order> order, int localPlayer)
{
	const auto random = bindRandom();
	if (!order || order->sender < 0 || order->sender >= gameHeader.getNumberOfPlayers() ||
		!players[order->sender] || !players[order->sender]->team) return;

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
		case ORDER_ALTER_FORBIDDEN:
			executeAlterForbidden(*std::static_pointer_cast<OrderAlterForbidden>(order), localPlayer);
			break;
		case ORDER_ALTER_GUARD_AREA:
			executeAlterGuardArea(*std::static_pointer_cast<OrderAlterGuardArea>(order), localPlayer);
			break;
		case ORDER_ALTER_CLEAR_AREA:
			executeAlterClearArea(*std::static_pointer_cast<OrderAlterClearArea>(order), localPlayer);
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
			executeCancelConstruction(*std::static_pointer_cast<OrderCancelConstruction>(order));
			break;
		case ORDER_SET_ALLIANCE:
			executeSetAlliance(*std::static_pointer_cast<SetAllianceOrder>(order));
			break;
		case ORDER_PLAYER_QUIT_GAME:
			executePlayerQuitGame(*std::static_pointer_cast<PlayerQuitsGameOrder>(order));
			break;
	}
}

void Game::executeOrderAndNotify(std::shared_ptr<Order> order, int localPlayer)
{
	if (!order || order->sender < 0 || order->sender >= gameHeader.getNumberOfPlayers() ||
		!players[order->sender] || !players[order->sender]->team) return;
	// Each client-visible effect is published before executeOrder runs, from the
	// state the order found, so names and alliances match what GameGUI used to
	// read when it handled the order itself.
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE:
		{
			auto mo = std::static_pointer_cast<MessageOrder>(order);
			if (clientEvents)
			{
				ClientEvent::ChatMessage message;
				message.messageOrderType = mo->messageOrderType;
				message.sender = mo->sender;
				message.senderName = players[mo->sender]->name;
				message.text = mo->getText();
				message.recipientsMask = mo->recipientsMask;
				for (int k : messageRecipientPlayers(mo->recipientsMask, gameHeader.getNumberOfPlayers()))
					message.recipientNames.push_back(players[k]->name);
				publishClientEvent(std::move(message));
			}
			executeOrder(order, localPlayer);
			break;
		}
		case ORDER_VOICE_DATA:
			publishClientEvent(ClientEvent::VoiceData{std::static_pointer_cast<OrderVoiceData>(order)});
			executeOrder(order, localPlayer);
			break;
		case ORDER_PLAYER_QUIT_GAME:
			if (clientEvents)
				publishClientEvent(ClientEvent::PlayerQuit{order->sender, players[order->sender]->name});
			executeOrder(order, localPlayer);
			break;
		case ORDER_MAP_MARK:
		{
			// Client-only: never reaches executeOrder (or the replay/dataset writers).
			auto mmo = std::static_pointer_cast<MapMarkOrder>(order);
			if (mmo->teamNumber >= static_cast<unsigned>(mapHeader.getNumberOfTeams()) || !teams[mmo->teamNumber]) return;
			publishClientEvent(ClientEvent::MapMark{mmo, teams[mmo->teamNumber]->allies});
			break;
		}
		case ORDER_PAUSE_GAME:
			// Client-only, like map marks.
			publishClientEvent(ClientEvent::PauseChanged{std::static_pointer_cast<PauseGameOrder>(order)->pause});
			break;
		case ORDER_CREATE:
		{
			auto oc = std::static_pointer_cast<OrderCreate>(order);
			publishClientEvent(ClientEvent::BuildingRequested{oc->teamNumber, oc->posX, oc->posY});
			executeOrder(order, localPlayer);
			break;
		}
		default:
			executeOrder(order, localPlayer);
			break;
	}
	publishClientEvent(ClientEvent::OrderExecuted{order});
}

void Game::executeCreate(const OrderCreate& oc, int localPlayer)
{
	int posX=(oc.posX)&map.getMaskW();
	int posY=(oc.posY)&map.getMaskH();
	if (oc.teamNumber != players[oc.sender]->team->teamNumber || oc.typeNum < 0 ||
		static_cast<size_t>(oc.typeNum) >= globalContainer->buildingsTypes.size() ||
		oc.unitWorking < 0 || oc.unitWorking > MAX_BUILDING_WORKER_REQUEST ||
		oc.unitWorkingFuture < 0 || oc.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST ||
		(oc.flagRadius && (*oc.flagRadius < 0 || *oc.flagRadius > 32767))) return;
	BuildingType *bt=globalContainer->buildingsTypes.get(oc.typeNum);
	if(!mapscript.buildingAllowed(IntBuildingType::typeFromShortNumber(bt->shortTypeNum),bt->isVirtual))return;
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
			if(isVirtual && oc.flagRadius.has_value())
			{
				b->unitStayRange = *oc.flagRadius;
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
		for (int y=posY; y<posY+h; y++)
			for (int x=posX; x<posX+w; x++)
			{
				map.addForbidden(x, y, oc.teamNumber);
				if (oc.teamNumber == players[localPlayer]->teamNumber)
					map.displayedForbiddenView.set(map.coordToIndex(x, y), true);
			}
		map.updateForbiddenGradient(oc.teamNumber);
	}
}

void Game::executeModifyBuilding(const OrderModifyBuilding& omb, int localPlayer)
{
	Building *b=lookupBuilding(omb.gid);
	if ((b) && (b->buildingState==Building::ALIVE))
	{
		if (omb.numberRequested > MAX_BUILDING_WORKER_REQUEST) return;
		b->maxUnitWorking=omb.numberRequested;
		b->maxUnitWorkingPreferred=b->maxUnitWorking;
		b->update();
	}
}

void Game::executeModifyExchange(const OrderModifyExchange& ome, int localPlayer)
{
	Building *b=lookupBuilding(ome.gid);
	if ((b) && (b->buildingState==Building::ALIVE))
	{
		b->receiveResourceMask=ome.receiveResourceMask;
		b->sendResourceMask=ome.sendResourceMask;
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
		if (newRange < 0 || newRange > 32767) return;
		b->unitStayRange=newRange;

		if (b->type->zonableForbidden)
		{
			if (newRange<oldRange)
				b->owner->dirtyGlobalGradient();
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
		if (omcf.clearingResources[STONE]) return;
		memcpy(b->clearingResources, omcf.clearingResources, sizeof(bool)*BASIC_COUNT);
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
		if (omwf.minLevelToFlag >= NB_UNIT_LEVELS) return;
		b->minLevelToFlag = omwf.minLevelToFlag;

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
	}
}

void Game::executeAlterForbidden(const OrderAlterForbidden& oaa, int localPlayer)
{
	if (oaa.teamNumber >= mapHeader.getNumberOfTeams() || !teams[oaa.teamNumber] ||
		(oaa.type != BrushTool::MODE_ADD && oaa.type != BrushTool::MODE_DEL)) return;
	assert(oaa.type == BrushTool::MODE_ADD || oaa.type == BrushTool::MODE_DEL);
	const bool adding = oaa.type == BrushTool::MODE_ADD;
	const Uint32 oldGeneration = map.topologyGeneration;
	const Uint32 teamMask = teams[oaa.teamNumber]->me;
	bool changed = false, walkingChanged = false, clearingChanged = false;
	size_t maskIndex = 0;
	for (int y=oaa.centerY+oaa.minY; y<oaa.centerY+oaa.maxY; ++y)
		for (int x=oaa.centerX+oaa.minX; x<oaa.centerX+oaa.maxX; ++x, ++maskIndex)
		{
			if (!oaa.mask.get(maskIndex)) continue;
			const Tile& tile = map.getTile(x, y);
			if (bool(tile.forbidden & teamMask) != adding)
			{
				changed = true;
				// Resources already block walking, but can be harvesting/clearing goals.
				walkingChanged |= tile.resource.type == NO_RES_TYPE;
				clearingChanged |= (tile.clearArea & teamMask) != 0;
				if (adding) map.addForbidden(x, y, oaa.teamNumber);
				else map.removeForbidden(x, y, oaa.teamNumber);
			}
			if (oaa.teamNumber == players[localPlayer]->teamNumber)
				map.displayedForbiddenView.set(map.coordToIndex(x, y), adding);
		}
	if (!changed) return;

	for (int team=0; team<mapHeader.getNumberOfTeams(); ++team)
		for (int id=0; id<Building::MAX_COUNT; ++id)
		{
			Building* building = teams[team]->myBuildings[id];
			if (!building) continue;
			const bool ownTeam = team == oaa.teamNumber;
			const bool clearingFlag = building->type->isVirtual && building->type->zonable[WORKER];
			if (ownTeam && (walkingChanged || clearingFlag))
				building->resetPathfindGradients();
			else
			{
				// A team-local edit must not newly stale unrelated walking fields.
				// Keep earlier staleness, dirty flags and unfinished searches intact.
				for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
					if (building->gradientGeneration[swim] == oldGeneration)
						building->gradientGeneration[swim] = map.topologyGeneration;
				if (ownTeam) building->resetRoundTripGradients();
			}
		}
	if (walkingChanged)
	{
		map.updateForbiddenGradient(oaa.teamNumber);
		map.updateGuardAreasGradient(oaa.teamNumber);
	}
	if (walkingChanged || clearingChanged)
		map.updateClearAreasGradient(oaa.teamNumber);
}

void Game::executeAlterGuardArea(const OrderAlterGuardArea& oaa, int localPlayer)
{
	if (oaa.teamNumber >= mapHeader.getNumberOfTeams() || !teams[oaa.teamNumber] ||
		(oaa.type != BrushTool::MODE_ADD && oaa.type != BrushTool::MODE_DEL)) return;
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
					map.tiles[index].guardArea |= teamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.displayedGuardAreaView.set(index, true);
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
					map.tiles[index].guardArea &= notTeamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.displayedGuardAreaView.set(index, false);
				}
				orderMaskIndex++;
			}
	}
	else
		assert(false);
	map.updateGuardAreasGradient(oaa.teamNumber);
}

void Game::executeAlterClearArea(const OrderAlterClearArea& oaa, int localPlayer)
{
	if (oaa.teamNumber >= mapHeader.getNumberOfTeams() || !teams[oaa.teamNumber] ||
		(oaa.type != BrushTool::MODE_ADD && oaa.type != BrushTool::MODE_DEL)) return;
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
					map.tiles[index].clearArea |= teamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.displayedClearAreaView.set(index, true);
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
					map.tiles[index].clearArea &= notTeamMask;
					// Update local map
					if (oaa.teamNumber == players[localPlayer]->teamNumber)
						map.displayedClearAreaView.set(index, false);
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
	for (int ratio : oms.ratio) if (ratio < 0 || ratio > 32767) return;
	Building *b=lookupBuilding(oms.gid);
	if ((b) && (b->buildingState==Building::ALIVE) && (b->type->unitProductionTime))
	{
		for (int j=0; j<NB_UNIT_TYPE; j++)
		{
			b->ratio[j]=oms.ratio[j];
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
			b->owner->dirtyGlobalGradient();
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
	if (oc.unitWorking > MAX_BUILDING_WORKER_REQUEST || oc.unitWorkingFuture > MAX_BUILDING_WORKER_REQUEST) return;
	Building *b=lookupBuilding(oc.gid);
	if (b)
	{
		b->launchConstruction(oc.unitWorking, oc.unitWorkingFuture);
	}
}

void Game::executeCancelConstruction(const OrderCancelConstruction& oc)
{
	if (oc.unitWorking > MAX_BUILDING_WORKER_REQUEST) return;
	Building *b=lookupBuilding(oc.gid);
	if (b)
	{
		b->cancelConstruction(oc.unitWorking);
	}
}

void Game::executeSetAlliance(const SetAllianceOrder& sao)
{
	Uint32 team=sao.teamNumber;
	if (team >= static_cast<unsigned>(mapHeader.getNumberOfTeams()) || !teams[team]) return;
	teams[team]->allies=sao.alliedMask;
	teams[team]->enemies=sao.enemyMask;
	teams[team]->sharedVisionExchange=sao.visionExchangeMask;
	teams[team]->sharedVisionFood=sao.visionFoodMask;
	teams[team]->sharedVisionOther=sao.visionOtherMask;
}

void Game::executePlayerQuitGame(const PlayerQuitsGameOrder& pqgo)
{
	if (pqgo.player < 0 || pqgo.player >= gameHeader.getNumberOfPlayers() || !players[pqgo.player]) return;
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
