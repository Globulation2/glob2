/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "MultiplayerGame.h"
#include <iostream>
#include "Engine.h"
#include "MapAssembler.h"
#include "FormatableString.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "NetMessage.h"
#include "P2PConnectionEvent.h"

MultiplayerGame::MultiplayerGame(boost::shared_ptr<YOGClient> client)
	: client(client), gjcState(NothingYet), creationState(YOGCreateRefusalUnknown), joinState(YOGJoinRefusalUnknown), playerManager(gameHeader)
{
	netEngine=NULL;
	kickReason = YOGUnknownKickReason;
	haveMapHeader = false;
	haveGameHeader = false;
	wasReadyToStart=false;
	sentReadyToStart=false;
	isEveryoneReadyToGo=false;
	isStarting=false;
	chatChannel=0;
	previousPercentage = 255;
	client->getP2PConnection()->addEventListener(this);
}



MultiplayerGame::~MultiplayerGame()
{
	client->getP2PConnection()->removeEventListener(this);
	client->getP2PConnection()->reset();
	if(assembler)
		client->setMapAssembler(boost::shared_ptr<MapAssembler>());
}



void MultiplayerGame::update()
{
	client->update();
	if(!client->isConnected())
	{
		shared_ptr<MGServerDisconnected> event(new MGServerDisconnected);
		sendToListeners(event);
	}

	if(assembler)
		assembler->update();

	if(isGameReadyToStart() && !wasReadyToStart)
	{
		shared_ptr<MGReadyToStartEvent> event(new MGReadyToStartEvent);
		sendToListeners(event);
		if(gjcState == JoinedGame)
		{
			shared_ptr<NetReadyToLaunch> message(new NetReadyToLaunch(client->getPlayerID()));
			client->sendNetMessage(message);
		}
		wasReadyToStart=true;
	}
	else if (!isGameReadyToStart() && wasReadyToStart)
	{
		shared_ptr<MGNotReadyToStartEvent> event(new MGNotReadyToStartEvent);
		sendToListeners(event);
		if(gjcState == JoinedGame)
		{
			shared_ptr<NetNotReadyToLaunch> message(new NetNotReadyToLaunch(client->getPlayerID()));
			client->sendNetMessage(message);
		}
		wasReadyToStart=false;
	}

	if(gjcState == JoinedGame && assembler && assembler->getPercentage() != previousPercentage)
	{
		previousPercentage = assembler->getPercentage();
		
		shared_ptr<MGDownloadPercentUpdate> event(new MGDownloadPercentUpdate(assembler->getPercentage()));
		sendToListeners(event);
	}
}




void MultiplayerGame::createNewGame(const std::string& name)
{
	shared_ptr<NetCreateGame> message(new NetCreateGame(name));
	client->sendNetMessage(message);
	gjcState=WaitingForCreateReply;
	setDefaultGameHeaderValues();
}



void MultiplayerGame::joinGame(Uint16 gameID)
{
	shared_ptr<NetAttemptJoinGame> message(new NetAttemptJoinGame(gameID));
	client->sendNetMessage(message);
	gjcState=WaitingForJoinReply;
}



void MultiplayerGame::leaveGame()
{	
	shared_ptr<NetLeaveGame> message(new NetLeaveGame);
	client->sendNetMessage(message);
}



MultiplayerGame::GameJoinCreationState MultiplayerGame::getGameJoinCreationState() const
{
	return gjcState;
}



YOGServerGameCreateRefusalReason MultiplayerGame::getGameCreationState()
{
	return creationState;
}




YOGServerGameJoinRefusalReason MultiplayerGame::getGameJoinState()
{
	return joinState;
}



void MultiplayerGame::setMapHeader(MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
		
	NetReteamingInformation info = constructReteamingInformation(mapHeader.getFileName());
	shared_ptr<NetSendReteamingInformation> message(new NetSendReteamingInformation(info));
	client->sendNetMessage(message);
	
	shared_ptr<NetSendMapHeader> message2(new NetSendMapHeader(mapHeader));
	client->sendNetMessage(message2);

	playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());
	playerManager.setReteamingInformation(info);
}



MapHeader& MultiplayerGame::getMapHeader()
{
	return mapHeader;
}



bool MultiplayerGame::isStillConnected() const
{
	return client->isConnected();
}



GameHeader& MultiplayerGame::getGameHeader()
{
	return gameHeader;
}



void MultiplayerGame::updateGameHeader()
{
	shared_ptr<NetSendGameHeader> message(new NetSendGameHeader(gameHeader));
	client->sendNetMessage(message);
	
	shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
	sendToListeners(event);
}



void MultiplayerGame::updatePlayerChanges()
{
	shared_ptr<NetSendGamePlayerInfo> message(new NetSendGamePlayerInfo(gameHeader));
	client->sendNetMessage(message);
}



void MultiplayerGame::setNetEngine(NetEngine* nnetEngine)
{
	netEngine = nnetEngine;
}



void MultiplayerGame::startGame()
{
	isStarting=true;
	//make sure the game headers are synced!
	updateGameHeader();
	shared_ptr<NetRequestGameStart> message(new NetRequestGameStart);
	client->sendNetMessage(message);
}



bool MultiplayerGame::isGameReadyToStart()
{
	if(gjcState == WaitingForCreateReply || gjcState == WaitingForJoinReply)
		return false;

	if(gjcState == HostingGame)
	{
		if(!isEveryoneReadyToGo)
			return false;
	}

	if(gjcState == JoinedGame && (!haveMapHeader || !haveGameHeader))
		return false;

	if(assembler)
	{
		if(assembler->getPercentage() == 100)
			return true;
		return false;
	}
	return true;
}



void MultiplayerGame::addAIPlayer(AI::ImplementitionID type)
{
	shared_ptr<NetAddAI> message(new NetAddAI((Uint8)type));
	client->sendNetMessage(message);

	playerManager.addAIPlayer(type);

	shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
	sendToListeners(event);
}



void MultiplayerGame::kickPlayer(int playerNum)
{
	BasePlayer& bp = gameHeader.getBasePlayer(playerNum);
	if(bp.type==BasePlayer::P_IP)
	{
		shared_ptr<NetKickPlayer> message(new NetKickPlayer(bp.playerID, YOGKickedByHost));
		client->sendNetMessage(message);
	}
	if(bp.type>=BasePlayer::P_AI)
	{
		shared_ptr<NetRemoveAI> message(new NetRemoveAI(playerNum));
		client->sendNetMessage(message);
	}

	playerManager.removePlayer(playerNum);

	shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
	sendToListeners(event);
}



void MultiplayerGame::changeTeam(int playerNum, int teamNum)
{
	playerManager.changeTeamNumber(playerNum, teamNum);
	
	shared_ptr<NetChangePlayersTeam> message(new NetChangePlayersTeam(playerNum, teamNum));
	client->sendNetMessage(message);
}

/*

void MultiplayerGame::sendMessage(const std::string& message)
{
	boost::shared_ptr<YOGMessage> tmessage(new YOGMessage);
	tmessage->setSender(client->getUsername());
	tmessage->setMessage(message);
	tmessage->setMessageType(YOGServerGameMessage);
	client->sendMessage(tmessage);
}

*/

YOGKickReason MultiplayerGame::getKickReason() const
{
	return kickReason;
}



void MultiplayerGame::addEventListener(MultiplayerGameEventListener* alistener)
{
	listeners.push_back(alistener);
}



void MultiplayerGame::removeEventListener(MultiplayerGameEventListener* alistener)
{
	listeners.remove(alistener);
}



int MultiplayerGame::getLocalPlayerNumber()
{
	return getLocalPlayer();
}



void MultiplayerGame::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	//This recieves responces to creating a game
	if(type==MNetCreateGameAccepted)
	{
		shared_ptr<NetCreateGameAccepted> info = static_pointer_cast<NetCreateGameAccepted>(message);
		chatChannel = info->getChatChannel();
		gjcState = HostingGame;
		updateGameHeader();
		
		shared_ptr<MGGameHostJoinAccepted> event(new MGGameHostJoinAccepted);
		sendToListeners(event);
	}
	if(type==MNetCreateGameRefused)
	{
		shared_ptr<NetCreateGameRefused> info = static_pointer_cast<NetCreateGameRefused>(message);
		gjcState = NothingYet;
		creationState = info->getRefusalReason();
		
		shared_ptr<MGGameRefusedEvent> event(new MGGameRefusedEvent);
		sendToListeners(event);
	}
	//This recieves responces to joining a game
	if(type==MNetGameJoinAccepted)
	{
		shared_ptr<NetGameJoinAccepted> info = static_pointer_cast<NetGameJoinAccepted>(message);
		chatChannel = info->getChatChannel();
		gjcState = JoinedGame;
		
		shared_ptr<MGGameHostJoinAccepted> event(new MGGameHostJoinAccepted);
		sendToListeners(event);
	}
	if(type==MNetGameJoinRefused)
	{ 
		shared_ptr<NetGameJoinRefused> info = static_pointer_cast<NetGameJoinRefused>(message);
		gjcState = NothingYet;
		joinState = info->getRefusalReason();
		
		shared_ptr<MGGameRefusedEvent> event(new MGGameRefusedEvent);
		sendToListeners(event);
	}
	if(type==MNetSendMapHeader)
	{
		shared_ptr<NetSendMapHeader> info = static_pointer_cast<NetSendMapHeader>(message);
		mapHeader = info->getMapHeader();

		playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());

		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);

		Engine engine;
		if(!engine.haveMap(mapHeader))
		{
			shared_ptr<NetRequestMap> message(new NetRequestMap);
			client->sendNetMessage(message);
			assembler.reset(new MapAssembler(client));
			assembler->startRecievingFile(mapHeader.getFileName());
			client->setMapAssembler(assembler);
		}
		haveMapHeader = true;
	}
	if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		info->downloadToGameHeader(gameHeader);
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
		
		haveGameHeader = true;
	}
	if(type==MNetSendGamePlayerInfo)
	{
		shared_ptr<NetSendGamePlayerInfo> info = static_pointer_cast<NetSendGamePlayerInfo>(message);
		info->downloadToGameHeader(gameHeader);
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetStartGame)
	{
		//shared_ptr<NetStartGame> info = static_pointer_cast<NetStartGame>(message);
		startEngine();
	}
	if(type==MNetRefuseGameStart)
	{
		//shared_ptr<NetRefuseGameStart> info = static_pointer_cast<NetRefuseGameStart>(message);
		isStarting=false;
		shared_ptr<MGGameStartRefused> event(new MGGameStartRefused);
		sendToListeners(event);
	}
	if(type==MNetSendOrder)
	{
		//ignore orders for when there is no NetEngine, this occurs when the
		//player has quit a game, there may still be a few orders in transit
		//before the quit message reaches the server
		if(netEngine)
		{
			shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
			shared_ptr<Order> order = info->getOrder();
			if(order->getOrderType() == ORDER_PLAYER_QUIT_GAME)
				order->gameCheckSum = static_cast<unsigned int>(-1);
			netEngine->pushOrder(order, order->sender, false);
		}
	}
	if(type==MNetRequestMap)
	{
		assembler.reset(new MapAssembler(client));
		assembler->startSendingFile(mapHeader.getFileName());
		client->setMapAssembler(assembler);
	}
	if(type==MNetKickPlayer)
	{
		shared_ptr<NetKickPlayer> info = static_pointer_cast<NetKickPlayer>(message);
		//Check if we are the ones being kicked
		if(info->getPlayerID() == client->getPlayerID())
		{
			kickReason = info->getReason();
			gjcState = NothingYet;
			
			if(kickReason == YOGKickedByHost)
			{
				shared_ptr<MGKickedByHostEvent> event(new MGKickedByHostEvent);
				sendToListeners(event);
			}
			else if(kickReason == YOGHostDisconnect)
			{
				shared_ptr<MGHostCancelledGameEvent> event(new MGHostCancelledGameEvent);
				sendToListeners(event);
			}
		}
		else
		{
			playerManager.removePerson(info->getPlayerID());

			shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
			sendToListeners(event);
		}
	}
	if(type==MNetEveryoneReadyToLaunch)
	{
		isEveryoneReadyToGo = true;
	}
	if(type==MNetNotEveryoneReadyToLaunch)
	{
		isEveryoneReadyToGo = false;
	}
	if(type==MNetSetLatencyMode)
	{
		shared_ptr<NetSetLatencyMode> info = static_pointer_cast<NetSetLatencyMode>(message);
		gameHeader.setGameLatency(info->getLatencyAdjustment());
		//std::cout<<"info->getLatencyAdjustment()="<<(int)(info->getLatencyAdjustment())<<std::endl;
	}
	if(type==MNetPlayerJoinsGame)
	{
		shared_ptr<NetPlayerJoinsGame> info = static_pointer_cast<NetPlayerJoinsGame>(message);
		playerManager.addPerson(info->getPlayerID(), info->getPlayerName());
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetAddAI)
	{
		shared_ptr<NetAddAI> info = static_pointer_cast<NetAddAI>(message);
		playerManager.addAIPlayer(static_cast<AI::ImplementitionID>(info->getType()));
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetRemoveAI)
	{
		shared_ptr<NetRemoveAI> info = static_pointer_cast<NetRemoveAI>(message);
		playerManager.removePlayer(info->getPlayerNumber());
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetChangePlayersTeam)
	{
		shared_ptr<NetChangePlayersTeam> info = static_pointer_cast<NetChangePlayersTeam>(message);
		playerManager.changeTeamNumber(info->getPlayer(), info->getTeam());
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetSendReteamingInformation)
	{
		shared_ptr<NetSendReteamingInformation> info = static_pointer_cast<NetSendReteamingInformation>(message);
		playerManager.setReteamingInformation(info->getReteamingInfo());
	}
}



void MultiplayerGame::startEngine()
{
	client->getP2PConnection()->stopConnections();
	Engine engine;
	// host game and wait for players. This clever trick is meant to get a proper shared_ptr
	// to (this), because shared_ptr's must be copied from the original
	int rc=engine.initMultiplayer(client->getMultiplayerGame(), client, getLocalPlayer());
	// execute game
	if (rc==Engine::EE_NO_ERROR)
	{
		shared_ptr<MGGameStarted> event(new MGGameStarted);
		sendToListeners(event);

		if (engine.run()==-1)
		{
			shared_ptr<MGGameExitEvent> event(new MGGameExitEvent);
			sendToListeners(event);	
		}
		else
		{
			shared_ptr<MGGameEndedNormallyEvent> event(new MGGameEndedNormallyEvent);
			sendToListeners(event);	
		}
	}
//	else if (rc==-1)
//		executionMode=-1;
	// redraw all stuff
	netEngine = NULL;
}



void MultiplayerGame::setDefaultGameHeaderValues()
{
	gameHeader.setGameLatency(12);
	gameHeader.setOrderRate(6);
}



void MultiplayerGame::sendToListeners(boost::shared_ptr<MultiplayerGameEvent> event)
{
	for(std::list<MultiplayerGameEventListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->handleMultiplayerGameEvent(event);
	}
}



NetReteamingInformation MultiplayerGame::constructReteamingInformation(const std::string& file)
{
	NetReteamingInformation info;
	GameHeader game = Engine::loadGameHeader(file);
	for(int i=0; i<32; ++i)
	{
		if(game.getBasePlayer(i).type == Player::P_IP)
		{
			info.setPlayerToTeam(game.getBasePlayer(i).name, game.getBasePlayer(i).teamNumber);
		}
	}
	return info;
}



int MultiplayerGame::getLocalPlayer()
{
	for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		if(gameHeader.getBasePlayer(i).playerID == client->getPlayerID())
		{
			return gameHeader.getBasePlayer(i).number;
		}
	}
	return -1;
}



std::string MultiplayerGame::getUsername() const
{
	return client->getUsername();
}



Uint32 MultiplayerGame::getChatChannel() const
{
	return chatChannel;
}



Uint8 MultiplayerGame::percentageDownloadFinished()
{
	if(!assembler)
		return 100;
	return assembler->getPercentage();
}



void MultiplayerGame::recieveP2PEvent(boost::shared_ptr<P2PConnectionEvent> event)
{
	Uint8 type = event->getEventType();
	if(type == P2PERecievedMessage)
	{
		boost::shared_ptr<P2PRecievedMessage> info = static_pointer_cast<P2PRecievedMessage>(event);
		recieveMessage(info->getMessage());
	}
}



bool MultiplayerGame::isGameStarting()
{
	return isStarting;
}



void MultiplayerGame::setGameResult(YOGGameResult result)
{
	shared_ptr<NetSendGameResult> message(new NetSendGameResult(result));
	client->sendNetMessage(message);
}


