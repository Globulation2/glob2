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

MultiplayerGame::MultiplayerGame(boost::shared_ptr<YOGClient> client)
	: client(client), gjcState(NothingYet), creationState(YOGCreateRefusalUnknown), joinState(YOGJoinRefusalUnknown)
{
	playersChanged = false;
	netEngine=NULL;
}



void MultiplayerGame::update()
{
	client->update();
	if(assembler)
		assembler->update();
}



void MultiplayerGame::createNewGame(const std::string& name)
{
	shared_ptr<NetCreateGame> message(new NetCreateGame(name));
	client->sendNetMessage(message);
	gjcState=WaitingForCreateReply;
}



void MultiplayerGame::joinGame(Uint16 gameID)
{
	shared_ptr<NetAttemptJoinGame> message(new NetAttemptJoinGame(gameID));
	client->sendNetMessage(message);
	gjcState=WaitingForJoinReply;
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
		playersChanged=true;
	}
	if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		gameHeader = info->getGameHeader();
		playersChanged=true;
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
		netEngine->pushOrder(order, order->sender, order->ustep);
	}
	if(type==MNetRequestMap)
	{
		assembler.reset(new MapAssembler(client));
		assembler->startSendingFile(mapHeader.getFileName());
	}
}



void MultiplayerGame::addPerson(Uint16 playerID)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		std::cout<<"adding "<<playerID<<"; type["<<x<<"]="<<bp.type<<std::endl;
		if(bp.type == BasePlayer::P_NONE)
		{
			bp = BasePlayer(x, client->findPlayerName(playerID).c_str(), x, BasePlayer::P_IP);
			bp.playerID = playerID;
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers()+1);
	playersChanged=true;
	updateGameHeader();
}



void MultiplayerGame::removePerson(Uint16 playerID)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.playerID == playerID)
		{
			bp = BasePlayer();
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers()-1);
	playersChanged=true;
	updateGameHeader();
}



GameHeader& MultiplayerGame::getGameHeader()
{
	return gameHeader;
}



void MultiplayerGame::updateGameHeader()
{
	std::cout<<gameHeader.getNumberOfPlayers()<<std::endl;
	shared_ptr<NetSendGameHeader> message(new NetSendGameHeader(gameHeader));
	client->sendNetMessage(message);
}




bool MultiplayerGame::hasPlayersChanged()
{
	if(playersChanged)
	{
		playersChanged=false;
		return true;
	}
	return false;
}



void MultiplayerGame::setNetEngine(NetEngine* nnetEngine)
{
	netEngine = nnetEngine;
}



void MultiplayerGame::pushOrder(shared_ptr<Order> order, int playerNum, int ustep)
{
	order->sender = playerNum;
	order->ustep = ustep;
	shared_ptr<NetSendOrder> message(new NetSendOrder(order));
	client->sendNetMessage(message);
}



void MultiplayerGame::startGame()
{
	shared_ptr<NetStartGame> message(new NetStartGame);
	client->sendNetMessage(message);
	startEngine();
}



void MultiplayerGame::startEngine()
{
	std::cout<<gameHeader.getNumberOfPlayers()<<std::endl;
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
