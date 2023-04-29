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
#include "YOGClientFileAssembler.h"
#include "FormatableString.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "NetMessage.h"
#include "YOGClientGameListManager.h"

using boost::shared_ptr;
using boost::static_pointer_cast;

MultiplayerGame::MultiplayerGame(boost::shared_ptr<YOGClient> client)
	: client(client), creationState(YOGCreateRefusalUnknown), joinState(YOGJoinRefusalUnknown), playerManager(gameHeader)
{
	netEngine=NULL;
	mode = NoMode;
	state = NothingYet;
	kickReason = YOGUnknownKickReason;
	
	gameID=0;
	fileID=0;
	chatChannel=0;
	
	wasReadyToStart=false;
	sentReadyToStart=false;
	humanReadyToStart=false;
	
	isStarting=false;
	needToSendMapHeader=false;
	previousPercentage = 255;
	numberOfConnectionAttempts=0;
}



MultiplayerGame::~MultiplayerGame()
{
}



void MultiplayerGame::update()
{
	client->update();
	if(!client->isConnected())
	{
		shared_ptr<MGServerDisconnected> event(new MGServerDisconnected);
		sendToListeners(event);
		mode = NoMode;
		state=NothingYet;
		if(client->getGameConnection())
			client->getGameConnection()->closeConnection();
	}
	
	if(state == ConnectingToGameRouter)
	{
		//This is a special case, it means the router ip is the same as the yog ip
		if(gameRouterIP == "YOGIP")
		{
			gameRouterIP = 	client->getIPAddress();
		}
		if(!client->getGameConnection())
		{
			client->setGameConnection(boost::shared_ptr<NetConnection>(new NetConnection(gameRouterIP, YOG_ROUTER_PORT)));
		}
		if(!client->getGameConnection()->isConnected() && !client->getGameConnection()->isConnecting())
		{
			numberOfConnectionAttempts+=1;
			if(numberOfConnectionAttempts == 3)
			{
				leaveGame();
			}
			else
			{
				client->getGameConnection()->openConnection(gameRouterIP, YOG_ROUTER_PORT);
			}
		}
		if(client->getGameConnection()->isConnected())
		{
			state = ReadyToGo;
			shared_ptr<NetSetGameInRouter> message(new NetSetGameInRouter(gameID));
			client->getGameConnection()->sendMessage(message);
		}
		
	}
	
	if(state == SendingGameInformation)
	{
		if(needToSendMapHeader)
		{
			shared_ptr<NetSendReteamingInformation> message(new NetSendReteamingInformation(playerManager.getReteamingInformation()));
			client->sendNetMessage(message);
	
			shared_ptr<NetSendMapHeader> message2(new NetSendMapHeader(mapHeader));
			client->sendNetMessage(message2);
			
			state = ConnectingToGameRouter;
		}
	}
	
	updateReadyState();
	if(playerManager.isReadyToGo(client->getPlayerID()) && !wasReadyToStart)
	{
		shared_ptr<MGReadyToStartEvent> event(new MGReadyToStartEvent);
		sendToListeners(event);
		shared_ptr<MGPlayerReadyStatusChanged> event2(new MGPlayerReadyStatusChanged(client->getPlayerID()));
		sendToListeners(event2);
		shared_ptr<NetReadyToLaunch> message(new NetReadyToLaunch(client->getPlayerID()));
		client->sendNetMessage(message);
		wasReadyToStart=true;
	}
	else if (!playerManager.isReadyToGo(client->getPlayerID()) && wasReadyToStart)
	{
		shared_ptr<MGNotReadyToStartEvent> event(new MGNotReadyToStartEvent);
		sendToListeners(event);
		shared_ptr<MGPlayerReadyStatusChanged> event2(new MGPlayerReadyStatusChanged(client->getPlayerID()));
		sendToListeners(event2);
		shared_ptr<NetNotReadyToLaunch> message(new NetNotReadyToLaunch(client->getPlayerID()));
		client->sendNetMessage(message);
		wasReadyToStart=false;
	}

	if(mode == JoinedGame && client->getYOGClientFileAssembler(fileID) && client->getYOGClientFileAssembler(fileID)->getPercentage() != previousPercentage)
	{
		previousPercentage = client->getYOGClientFileAssembler(fileID)->getPercentage();
		
		shared_ptr<MGDownloadPercentUpdate> event(new MGDownloadPercentUpdate(client->getYOGClientFileAssembler(fileID)->getPercentage()));
		sendToListeners(event);
	}
}




void MultiplayerGame::createNewGame(const std::string& name)
{
	shared_ptr<NetCreateGame> message(new NetCreateGame(name));
	client->sendNetMessage(message);
	state=WaitingForInitialReply;
	mode = HostingGame;
	setDefaultGameHeaderValues();
}



void MultiplayerGame::joinGame(Uint16 ngameID)
{
	gameID=ngameID;
	shared_ptr<NetAttemptJoinGame> message(new NetAttemptJoinGame(gameID));
	client->sendNetMessage(message);
	state=WaitingForInitialReply;
	mode = JoinedGame;
}



void MultiplayerGame::leaveGame()
{	
	shared_ptr<NetLeaveGame> message(new NetLeaveGame);
	client->sendNetMessage(message);
	
	if(client->getGameConnection())
		client->getGameConnection()->closeConnection();
		
	mode = NoMode;
	state=NothingYet;
}



MultiplayerGame::MultiplayerMode MultiplayerGame::getMultiplayerMode() const
{
	return mode;	
}



MultiplayerGame::GameJoinCreationState MultiplayerGame::getGameJoinCreationState() const
{
	return state;
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
	playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());
	playerManager.setReteamingInformation(info);
	needToSendMapHeader=true;
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
	if(state != ReadyToGo)
	{
		return false;
	}
	
	if(mode == HostingGame)
	{
		if(!playerManager.isEveryoneReadyToGo())
			return false;
	}

	if(!playerManager.isReadyToGo(client->getPlayerID()))
	{
		return false;
	}
	
	return true;
}



void MultiplayerGame::updateReadyState()
{
	bool ready=true;
	
	if(state != ReadyToGo)
		ready = false;
	
	if(!client->getGameConnection() || !client->getGameConnection()->isConnected())
		ready = false;

	if(client->getYOGClientFileAssembler(fileID))
	{
		if(client->getYOGClientFileAssembler(fileID)->getPercentage() != 100)
			ready = false;
	}
	
	if(mode == JoinedGame && !humanReadyToStart)
		ready = false;
		
	playerManager.setReadyToGo(client->getPlayerID(), ready);
}



void MultiplayerGame::addAIPlayer(AI::ImplementationID type)
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
		
		state = SendingGameInformation;

		gameID=info->getGameID();
		fileID = info->getFileID();
		gameRouterIP = info->getGameRouterIP();
		chatChannel = info->getChatChannel();
		
		shared_ptr<MGGameHostJoinAccepted> event(new MGGameHostJoinAccepted);
		sendToListeners(event);
	}
	if(type==MNetCreateGameRefused)
	{
		shared_ptr<NetCreateGameRefused> info = static_pointer_cast<NetCreateGameRefused>(message);
		state = NothingYet;
		creationState = info->getRefusalReason();
		
		shared_ptr<MGGameRefusedEvent> event(new MGGameRefusedEvent);
		sendToListeners(event);
	}
	if(type==MNetGameJoinAccepted)
	{
		shared_ptr<NetGameJoinAccepted> info = static_pointer_cast<NetGameJoinAccepted>(message);
		
		state = WaitingForGameInformation;
		chatChannel = info->getChatChannel();

		shared_ptr<MGGameHostJoinAccepted> event(new MGGameHostJoinAccepted);
		sendToListeners(event);
	}
	if(type==MNetGameJoinRefused)
	{ 
		shared_ptr<NetGameJoinRefused> info = static_pointer_cast<NetGameJoinRefused>(message);
		
		state = NothingYet;
		joinState = info->getRefusalReason();
		
		shared_ptr<MGGameRefusedEvent> event(new MGGameRefusedEvent);
		sendToListeners(event);
	}
	if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		
		info->downloadToGameHeader(gameHeader);
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetSendGamePlayerInfo)
	{
		shared_ptr<NetSendGamePlayerInfo> info = static_pointer_cast<NetSendGamePlayerInfo>(message);
		
		info->downloadToGameHeader(gameHeader);
		
		shared_ptr<MGPlayerListChangedEvent> event(new MGPlayerListChangedEvent);
		sendToListeners(event);
	}
	if(type==MNetSendAfterJoinGameInformation)
	{
		shared_ptr<NetSendAfterJoinGameInformation> info = static_pointer_cast<NetSendAfterJoinGameInformation>(message);
		const YOGAfterJoinGameInformation& i = info->getAfterJoinGameInformation();
		//Change the state
		state = ConnectingToGameRouter;
		
		//Set game header
		gameHeader = i.getGameHeader();
		
		//Set file id
		fileID = i.getMapFileID();
		
		//Set map header
		mapHeader = i.getMapHeader();
		playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());
		Engine engine;
		if(!engine.haveMap(mapHeader))
		{
			shared_ptr<NetRequestFile> message(new NetRequestFile(fileID));
			client->sendNetMessage(message);
			boost::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
			assembler->startRecievingFile(mapHeader.getFileName());
			client->setYOGClientFileAssembler(fileID, assembler);
		}
		
		//Set reteam info
		playerManager.setReteamingInformation(i.getReteamingInformation());
		
		//Set latency
		gameHeader.setGameLatency(i.getLatencyAdjustment());
		
		//Connect to router ip
		gameRouterIP = i.getGameRouterIP();
		
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
	if(type==MNetRequestFile)
	{
		boost::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
		assembler->startSendingFile(mapHeader.getFileName());
		client->setYOGClientFileAssembler(fileID,assembler);
	}
	if(type==MNetKickPlayer)
	{
		shared_ptr<NetKickPlayer> info = static_pointer_cast<NetKickPlayer>(message);
		//Check if we are the ones being kicked
		if(info->getPlayerID() == client->getPlayerID())
		{
			kickReason = info->getReason();
			state = NothingYet;
			mode = NoMode;
			
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
	if(type==MNetReadyToLaunch)
	{
		shared_ptr<NetReadyToLaunch> info = static_pointer_cast<NetReadyToLaunch>(message);
		playerManager.setReadyToGo(info->getPlayerID(), true);
		shared_ptr<MGPlayerReadyStatusChanged> event(new MGPlayerReadyStatusChanged(info->getPlayerID()));
		sendToListeners(event);
	}
	if(type==MNetNotReadyToLaunch)
	{
		shared_ptr<NetNotReadyToLaunch> info = static_pointer_cast<NetNotReadyToLaunch>(message);
		playerManager.setReadyToGo(info->getPlayerID(), false);
		shared_ptr<MGPlayerReadyStatusChanged> event(new MGPlayerReadyStatusChanged(info->getPlayerID()));
		sendToListeners(event);
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
		playerManager.addAIPlayer(static_cast<AI::ImplementationID>(info->getType()));
		
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
	for(int i=0; i<Team::MAX_COUNT; ++i)
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
	if(!client->getYOGClientFileAssembler(fileID))
		return 100;
	return client->getYOGClientFileAssembler(fileID)->getPercentage();
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



bool MultiplayerGame::isReadyToStart(int playerID)
{
	return playerManager.isReadyToGo(playerID);
}


void MultiplayerGame::setHumanReady(bool isReady)
{
	humanReadyToStart=isReady;
}


bool MultiplayerGame::isFullyInGame()
{
	if(state != ReadyToGo)
		return false;
	return true;
}

