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


MultiplayerGame::MultiplayerGame(boost::shared_ptr<YOGClient> client)
	: client(client), gjcState(NothingYet), creationState(YOGCreateRefusalUnknown), joinState(YOGJoinRefusalUnknown)
{

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



const MapHeader& MultiplayerGame::getMapHeader() const
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
	}
}



GameHeader& MultiplayerGame::getGameHeader()
{
	return gameHeader;
}



void MultiplayerGame::updateGameHeader()
{
	//unimplemented
}
