/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "MultiplayerGame.h"
#include <iostream>
#include "Engine.h"
#include "MapAssembler.h"
#include "FormatableString.h"
#include "Toolkit.h"
#include "StringTable.h"

MultiplayerGame::MultiplayerGame(boost::shared_ptr<YOGClient> client)
	: client(client), gjcState(NothingYet), creationState(YOGCreateRefusalUnknown), joinState(YOGJoinRefusalUnknown)
{
	netEngine=NULL;
	kickReason = YOGUnknownKickReason;
	haveMapHeader = false;
	haveGameHeader = false;
	for(int i=0; i<32; ++i)
		readyToStart[i] = true;
	listener = NULL;
	sendReadyToStart=false;
}



MultiplayerGame::~MultiplayerGame()
{
	if(assembler)
		client->setMapAssembler(boost::shared_ptr<MapAssembler>());
}



void MultiplayerGame::update()
{
	client->update();
	if(assembler)
		assembler->update();
	if(haveGameHeader && haveMapHeader && gjcState == JoinedGame)
	{
		bool sendReady = false;
		if(assembler && assembler->isTransferComplete())
			sendReady = true;
		else if(!assembler)
			sendReady = true;
		if(sendReady && !sendReadyToStart)
		{
			shared_ptr<NetReadyToLaunch> message(new NetReadyToLaunch(client->getPlayerID()));
			client->sendNetMessage(message);
			sendReadyToStart=true;
		}
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



YOGGameCreateRefusalReason MultiplayerGame::getGameCreationState()
{
	return creationState;
}




YOGGameJoinRefusalReason MultiplayerGame::getGameJoinState()
{
	return joinState;
}



void MultiplayerGame::setMapHeader(MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
	shared_ptr<NetSendMapHeader> message(new NetSendMapHeader(mapHeader));
	client->sendNetMessage(message);
}



MapHeader& MultiplayerGame::getMapHeader()
{
	return mapHeader;
}



GameHeader& MultiplayerGame::getGameHeader()
{
	return gameHeader;
}



void MultiplayerGame::updateGameHeader()
{
	shared_ptr<NetSendGameHeader> message(new NetSendGameHeader(gameHeader));
	client->sendNetMessage(message);
}



void MultiplayerGame::setNetEngine(NetEngine* nnetEngine)
{
	netEngine = nnetEngine;
}



void MultiplayerGame::pushOrder(shared_ptr<Order> order, int playerNum)
{
	order->sender = playerNum;
	shared_ptr<NetSendOrder> message(new NetSendOrder(order));
	client->sendNetMessage(message);
}



void MultiplayerGame::startGame()
{
	shared_ptr<NetStartGame> message(new NetStartGame);
	client->sendNetMessage(message);
	startEngine();
}



bool MultiplayerGame::isGameReadyToStart()
{
	for(int x=0; x<32; ++x)
	{
		if(readyToStart[x] == false)
		{
			std::cout<<"ready to start "<<x<<" false"<<std::endl;
			return false;
		}
	}

	if(assembler)
	{
		if(assembler->isTransferComplete())
			return true;
		return false;
	}
	return true;
}



void MultiplayerGame::addAIPlayer(AI::ImplementitionID type)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type == BasePlayer::P_NONE)
		{
			FormatableString name("%0 %1");
			name.arg(Toolkit::getStringTable()->getString("[AI]", type)).arg(x-1);
			bp = BasePlayer(x, name, x, Player::playerTypeFromImplementitionID(type));
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers()+1);
	if(listener)
	{
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		listener->handleMultiplayerGameEvent(event);
	}
	updateGameHeader();
}



void MultiplayerGame::kickPlayer(int playerNum)
{
	BasePlayer& bp = gameHeader.getBasePlayer(playerNum);
	if(bp.type==BasePlayer::P_AI)
	{
		bp = BasePlayer();
	}
	else if(bp.type==BasePlayer::P_IP)
	{
		shared_ptr<NetKickPlayer> message(new NetKickPlayer(bp.playerID, YOGKickedByHost));
		removePerson(bp.playerID);
		client->sendNetMessage(message);
	}
	updateGameHeader();
}



void MultiplayerGame::changeTeam(int playerNum, int teamNum)
{
	BasePlayer& bp = gameHeader.getBasePlayer(playerNum);
	bp.teamNumber = teamNum;
	updateGameHeader();
}



void MultiplayerGame::sendMessage(const std::string& message)
{
	boost::shared_ptr<YOGMessage> tmessage(new YOGMessage);
	tmessage->setSender(client->getUsername());
	tmessage->setMessage(message);
	tmessage->setMessageType(YOGGameMessage);
	client->sendMessage(tmessage);
}



YOGKickReason MultiplayerGame::getKickReason() const
{
	return kickReason;
}



void MultiplayerGame::setEventListener(MultiplayerGameEventListener* alistener)
{
	listener = alistener;
}



void MultiplayerGame::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	//This recieves responces to creating a game
	if(type==MNetCreateGameAccepted)
	{
		//shared_ptr<NetCreateGameAccepted> info = static_pointer_cast<NetCreateGameAccepted>(message);
		gjcState = HostingGame;
		addPerson(client->getPlayerID());
	}
	if(type==MNetCreateGameRefused)
	{
		shared_ptr<NetCreateGameRefused> info = static_pointer_cast<NetCreateGameRefused>(message);
		gjcState = NothingYet;
		creationState = info->getRefusalReason();
	}
	//This recieves responces to joining a game
	if(type==MNetGameJoinAccepted)
	{
		//shared_ptr<NetGameJoinAccepted> info = static_pointer_cast<NetGameJoinAccepted>(message);
		gjcState = JoinedGame;
	}
	if(type==MNetGameJoinRefused)
	{ 
		shared_ptr<NetGameJoinRefused> info = static_pointer_cast<NetGameJoinRefused>(message);
		gjcState = NothingYet;
		joinState = info->getRefusalReason();
	}
	if(type==MNetSendMapHeader)
	{
		shared_ptr<NetSendMapHeader> info = static_pointer_cast<NetSendMapHeader>(message);
		mapHeader = info->getMapHeader();

		if(listener)
		{
			shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
			listener->handleMultiplayerGameEvent(event);
		}

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
		gameHeader = info->getGameHeader();
		
		if(listener)
		{
			shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
			listener->handleMultiplayerGameEvent(event);
		}
		
		haveGameHeader = true;
	}
	if(type==MNetPlayerJoinsGame)
	{
		shared_ptr<NetPlayerJoinsGame> info = static_pointer_cast<NetPlayerJoinsGame>(message);
		addPerson(info->getPlayerID());
	}
	if(type==MNetPlayerLeavesGame)
	{
		shared_ptr<NetPlayerLeavesGame> info = static_pointer_cast<NetPlayerLeavesGame>(message);
		removePerson(info->getPlayerID());
	}
	if(type==MNetStartGame)
	{
		//shared_ptr<NetStartGame> info = static_pointer_cast<NetStartGame>(message);
		startEngine();
	}
	if(type==MNetSendOrder)
	{
		shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
		shared_ptr<Order> order = info->getOrder();
		netEngine->pushOrder(order, order->sender);
		for(int i=0; i<(gameHeader.getOrderRate() - 1); ++i)
			netEngine->pushOrder(shared_ptr<Order>(new NullOrder), order->sender);
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
		kickReason = info->getReason();
		gjcState = NothingYet;
	}
	if(type==MNetReadyToLaunch)
	{
		shared_ptr<NetReadyToLaunch> info = static_pointer_cast<NetReadyToLaunch>(message);
		Uint16 id = info->getPlayerID();
		for(int x=0; x<32; ++x)
		{
			BasePlayer& bp = gameHeader.getBasePlayer(x);
			if(bp.playerID == id)
			{
				readyToStart[x] = true;
				break;
			}
		}
	}
}



void MultiplayerGame::addPerson(Uint16 playerID)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type == BasePlayer::P_NONE)
		{
			bp = BasePlayer(x, client->findPlayerName(playerID).c_str(), x, BasePlayer::P_IP);
			bp.playerID = playerID;
			if(playerID != client->getPlayerID())	
				readyToStart[x] = false;
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers()+1);
	if(listener)
	{
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		listener->handleMultiplayerGameEvent(event);
	}
	updateGameHeader();
}



void MultiplayerGame::removePerson(Uint16 playerID)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.playerID == playerID)
		{
			readyToStart[x] = true;
			bp = BasePlayer();
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers()-1);
	if(listener)
	{
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		listener->handleMultiplayerGameEvent(event);
	}
	updateGameHeader();
}



void MultiplayerGame::startEngine()
{
	Engine engine;
	// host game and wait for players. This clever trick is meant to get a proper shared_ptr
	// to (this), because shared_ptr's must be copied from the original
	int rc=engine.initMultiplayer(client->getMultiplayerGame(), getLocalPlayer());
	// execute game
	if (rc==Engine::EE_NO_ERROR)
	{
		engine.run();
//		if (engine.run()==-1)
//			executionMode=-1;
	}
//	else if (rc==-1)
//		executionMode=-1;
	// redraw all stuff
}



void MultiplayerGame::setDefaultGameHeaderValues()
{
	gameHeader.setGameLatency(12);
	gameHeader.setOrderRate(4);
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
}
