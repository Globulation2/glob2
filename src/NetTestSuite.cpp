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

#include "NetTestSuite.h"
#include <iostream>

#include "StreamBackend.h"
#include "BinaryStream.h"
#include "NetReteamingInformation.h"


using namespace GAGCore;

NetTestSuite::NetTestSuite()
{

}



template<typename t> bool NetTestSuite::testSerialize(shared_ptr<t> message)
{
	MemoryStreamBackend* msb = new MemoryStreamBackend;
	BinaryOutputStream* bos = new BinaryOutputStream(msb);

	shared_ptr<t> decodedMessage(new t);
	message->encodeData(bos);

	MemoryStreamBackend* msb2 = new MemoryStreamBackend(*msb);
	msb2->seekFromStart(0);
	BinaryInputStream* bis = new BinaryInputStream(msb2);
	
	decodedMessage->decodeData(bis);

	delete bos;
	delete bis;
	if((*message) != (*decodedMessage))
	{
		return false;
	}
	return true;
}



template<typename t> bool NetTestSuite::testInitial()
{
	shared_ptr<t> message1(new t);
	shared_ptr<t> message2(new t);
	if((*message1) != (*message2))
		return false;
	return true;
}



int NetTestSuite::testNetMessages()
{
	int n = testNetSendOrder();
	if(n != 0)
	{
		std::cout<<"testNetSendOrder() test # "<<n<<" failed."<<std::endl;
		return 1;
	}
	
	n = testNetSendClientInformation();
	if(n != 0)
	{
		std::cout<<"testNetSendClientInformation() test # "<<n<<" failed."<<std::endl;
		return 2;
	}

	
	n = testNetSendServerInformation();
	if(n != 0)
	{
		std::cout<<"testNetSendServerInformation() test # "<<n<<" failed."<<std::endl;
		return 3;
	}

	
	n = testNetAttemptLogin();
	if(n != 0)
	{
		std::cout<<"testNetAttemptLogin() test # "<<n<<" failed."<<std::endl;
		return 4;
	}

	
	n = testNetLoginSuccessful();
	if(n != 0)
	{
		std::cout<<"testNetLoginSuccessful() test # "<<n<<" failed."<<std::endl;
		return 5;
	}

	
	n = testNetRefuseLogin();
	if(n != 0)
	{
		std::cout<<"testNetRefuseLogin() test # "<<n<<" failed."<<std::endl;
		return 6;
	}

	
	n = testNetDisconnect();
	if(n != 0)
	{
		std::cout<<"testNetDisconnect() test # "<<n<<" failed."<<std::endl;
		return 7;
	}

	
	n = testNetAttemptRegistration();
	if(n != 0)
	{
		std::cout<<"testNetAttemptRegistration() test # "<<n<<" failed."<<std::endl;
		return 8;
	}

	
	n = testNetAcceptRegistration();
	if(n != 0)
	{
		std::cout<<"testNetAcceptRegistration() test # "<<n<<" failed."<<std::endl;
		return 9;
	}

	
	n = testNetRefuseRegistration();
	if(n != 0)
	{
		std::cout<<"testNetRefuseRegistration() test # "<<n<<" failed."<<std::endl;
		return 10;
	}
		
	//Test NetCreateGame
	if(!testInitial<NetCreateGame>())
		return 23;

	shared_ptr<NetCreateGame> createGame1(new NetCreateGame("my game"));
	if(!testSerialize(createGame1))
		return 24;

	shared_ptr<NetCreateGame> createGame2(new NetCreateGame("haha my first game woot"));
	if(!testSerialize(createGame2))
		return 25;
	
	//Test NetAttemptJoinGame
	if(!testInitial<NetAttemptJoinGame>())
		return 26;

	shared_ptr<NetAttemptJoinGame> attemptJoin1(new NetAttemptJoinGame(1));
	if(!testSerialize(attemptJoin1))
		return 27;

	shared_ptr<NetAttemptJoinGame> attemptJoin2(new NetAttemptJoinGame(6627));
	if(!testSerialize(attemptJoin2))
		return 27;
		
	//Test NetGameJoinAccepted
	if(!testInitial<NetGameJoinAccepted>())
		return 28;

	shared_ptr<NetGameJoinAccepted> joinAccepted1(new NetGameJoinAccepted(12));
	if(!testSerialize(joinAccepted1))
		return 29;
		
	//Test NetGameJoinRefused
	if(!testInitial<NetGameJoinRefused>())
		return 30;

	shared_ptr<NetGameJoinRefused> joinRefused1(new NetGameJoinRefused(YOGJoinRefusalUnknown));
	if(!testSerialize(joinRefused1))
		return 31;
		
	//Test NetSendYOGMessage
	if(!testInitial<NetSendYOGMessage>())
		return 32;

	shared_ptr<YOGMessage> m(new YOGMessage);
	m->setSender("bob");
	m->setMessage("hello alice");
	m->setMessageType(YOGNormalMessage);
	shared_ptr<NetSendYOGMessage> sendYOGMessage1(new NetSendYOGMessage(7, m));
	if(!testSerialize(sendYOGMessage1))
		return 33;

	//Test NetUpdateGameList
	YOGGameInfo gi1("bobs game", 12);
	YOGGameInfo gi2("jills game", 17);
	YOGGameInfo gi3("farces game", 35);
	YOGGameInfo gi4("globulation2 is kick ass", 92);
	std::vector<YOGGameInfo> lgi1;
	std::vector<YOGGameInfo> lgi2;

	//Test initial
	if(!testInitial<NetUpdateGameList>())
		return 34;
	
	//Add a few to the list, make sure it sends and it reconstructs right
	shared_ptr<NetUpdateGameList> updateGameList1(new NetUpdateGameList);
	lgi1.push_back(gi1);
	lgi1.push_back(gi2);
	updateGameList1->updateDifferences(lgi2, lgi1);
	if(!testSerialize(updateGameList1))
		return 35;
	updateGameList1->applyDifferences(lgi2);
	if(lgi1 != lgi2)
		return 36;
	
	//Remove one from the list
	lgi1.erase(lgi1.begin()+1);
	updateGameList1->updateDifferences(lgi2, lgi1);
	if(!testSerialize(updateGameList1))
		return 37;
	updateGameList1->applyDifferences(lgi2);
	
	
	if(lgi1 != lgi2)
		return 38;
	
	//Add two, remove one
	lgi1.push_back(gi3);
	lgi1.push_back(gi4);
	lgi1.erase(lgi1.begin());
	updateGameList1->updateDifferences(lgi2, lgi1);
	if(!testSerialize(updateGameList1))
		return 39;
	updateGameList1->applyDifferences(lgi2);
	if(lgi1 != lgi2)
		return 40;
	
	//Test NetUpdatePlayerList
	YOGPlayerSessionInfo pi1("bob", 12);
	YOGPlayerSessionInfo pi2("jill", 17);
	YOGPlayerSessionInfo pi3("farce", 35);
	YOGPlayerSessionInfo pi4("globulation2 is kick ass", 92);
	std::vector<YOGPlayerSessionInfo> lpi1;
	std::vector<YOGPlayerSessionInfo> lpi2;
	
	
	//Test initial
	if(!testInitial<NetUpdatePlayerList>())
		return 41;
	
	
	//Add a couple players
	lpi1.push_back(pi1);
	lpi1.push_back(pi2);
	shared_ptr<NetUpdatePlayerList> updatePlayerList1(new NetUpdatePlayerList);
	updatePlayerList1->updateDifferences(lpi2, lpi1);
	if(!testSerialize(updatePlayerList1))
		return 42;
	updatePlayerList1->applyDifferences(lpi2);
	if(lpi1 != lpi2)
		return 43;
		
	//Remove a player
	lpi1.erase(lpi1.begin()+1);
	updatePlayerList1->updateDifferences(lpi2, lpi1);
	if(!testSerialize(updatePlayerList1))
		return 44;
	updatePlayerList1->applyDifferences(lpi2);
	if(lpi1 != lpi2)
		return 45;
		
	//Add two remove one
	lpi1.push_back(pi3);
	lpi1.push_back(pi4);
	lpi1.erase(lpi1.begin());
	updatePlayerList1->updateDifferences(lpi2, lpi1);
	if(!testSerialize(updatePlayerList1))
		return 46;
	updatePlayerList1->applyDifferences(lpi2);
	if(lpi1 != lpi2)
		return 47;

	//Test NetSendMapHeader
	if(!testInitial<NetSendMapHeader>())
		return 48;

	MapHeader mh;
	mh.setMapName("Bobs Map");
	shared_ptr<NetSendMapHeader> sendHeader1(new NetSendMapHeader(mh));
	if(!testSerialize(sendHeader1))
		return 49;

	//Test NetCreateGameAccepted
	if(!testInitial<NetCreateGameAccepted>())
		return 50;

	shared_ptr<NetCreateGameAccepted> createGameAccepted1(new NetCreateGameAccepted(35, 72, "127.0.0.1", 27));
	if(!testSerialize(createGameAccepted1))
		return 51;
		
	//Test NetCreateGameRefused
	if(!testInitial<NetCreateGameRefused>())
		return 52;

	shared_ptr<NetCreateGameRefused> createGameRefused1(new NetCreateGameRefused(YOGCreateRefusalUnknown));
	if(!testSerialize(createGameRefused1))
		return 53;

	//Test NetSendGameHeader
	if(!testInitial<NetSendGameHeader>())
		return 54;

	GameHeader gh;
	gh.setNumberOfPlayers(17);
	shared_ptr<NetSendGameHeader> sendGameHeader1(new NetSendGameHeader(gh));
	if(!testSerialize(sendGameHeader1))
		return 55;

	//Test NetStartGame
	if(!testInitial<NetStartGame>())
		return 62;

	shared_ptr<NetStartGame> startGame1(new NetStartGame);
	if(!testSerialize(startGame1))
		return 63;

	//Test NetRequestFile
	if(!testInitial<NetRequestFile>())
		return 64;

	shared_ptr<NetRequestFile> requestMap1(new NetRequestFile);
	if(!testSerialize(requestMap1))
		return 65;

	//Test NetSendFileInformation
	if(!testInitial<NetSendFileInformation>())
		return 66;

	shared_ptr<NetSendFileInformation> sendFileInformation1(new NetSendFileInformation(14194, 10));
	if(!testSerialize(sendFileInformation1))
		return 67;
		
	//Test NetSendFileChunk
	if(!testInitial<NetSendFileChunk>())
		return 68;
		
	//Test NetKickPlayer
	if(!testInitial<NetKickPlayer>())
		return 71;

	shared_ptr<NetKickPlayer> kickPlayer1(new NetKickPlayer(77, YOGKickedByHost));
	if(!testSerialize(kickPlayer1))
		return 72;
		
	//Test NetLeaveGame
	if(!testInitial<NetLeaveGame>())
		return 73;

	shared_ptr<NetLeaveGame> leaveGame1(new NetLeaveGame);
	if(!testSerialize(leaveGame1))
		return 74;
		
	//Test NetReadyToLaunch
	if(!testInitial<NetReadyToLaunch>())
		return 75;

	shared_ptr<NetReadyToLaunch> readyToLaunch1(new NetReadyToLaunch(1773));
	if(!testSerialize(readyToLaunch1))
		return 76;

	return 0;

}



int NetTestSuite::testNetSendOrder()
{
	///Test NetSendOrder
	if(!testInitial<NetSendOrder>())
		return 1;

	shared_ptr<NetSendOrder> netSendOrder1(new NetSendOrder);
	netSendOrder1->changeOrder(boost::shared_ptr<Order>(new OrderDelete(1)));
	if(!testSerialize(netSendOrder1))
		return 2;
	
	return 0;
}



int NetTestSuite::testNetSendClientInformation()
{
	///Test NetSendClientInformation
	if(!testInitial<NetSendClientInformation>())
		return 1;

	shared_ptr<NetSendClientInformation> clientInfo1(new NetSendClientInformation);
	if(!testSerialize(clientInfo1))
		return 2;
		
	return 0;
}



int NetTestSuite::testNetSendServerInformation()
{
	///Test NetSendServerInformation
	if(!testInitial<NetSendServerInformation>())
		return 1;
	
	shared_ptr<NetSendServerInformation> serverInfo1(new NetSendServerInformation(YOGRequirePassword, YOGSingleGame, 17));
	if(!testSerialize(serverInfo1))
		return 2;
	
	return 0;
}



int NetTestSuite::testNetAttemptLogin()
{	
	///Test NetAttemptLogin
	if(!testInitial<NetAttemptLogin>())
		return 1;
	
	shared_ptr<NetAttemptLogin> attemptLogin1(new NetAttemptLogin("joe", "bob"));
	if(!testSerialize(attemptLogin1))
		return 2;
	
	shared_ptr<NetAttemptLogin> attemptLogin2(new NetAttemptLogin("joe bob", ""));
	if(!testSerialize(attemptLogin2))
		return 3;
	
	return 0;
}



int NetTestSuite::testNetLoginSuccessful()
{	
	///Test NetLoginSuccessful
	if(!testInitial<NetLoginSuccessful>())
		return 1;
		
	shared_ptr<NetLoginSuccessful> loginSuccess1(new NetLoginSuccessful);
	if(!testSerialize(loginSuccess1))
		return 2;
	
	return 0;
}



int NetTestSuite::testNetRefuseLogin()
{	
	//Test NetRefuseLogin
	if(!testInitial<NetRefuseLogin>())
		return 1;

	shared_ptr<NetRefuseLogin> refuseLogin1(new NetRefuseLogin(YOGPasswordIncorrect));
	if(!testSerialize(refuseLogin1))
		return 2;
	
	return 0;
}



int NetTestSuite::testNetDisconnect()
{	
	///Test NetDisconnect
	if(!testInitial<NetDisconnect>())
		return 1;
		
	shared_ptr<NetDisconnect> disconnect1(new NetDisconnect);
	if(!testSerialize(disconnect1))
		return 2;
	
	return 0;
}



int NetTestSuite::testNetAttemptRegistration()
{	
	///Test NetAttemptRegistration
	if(!testInitial<NetAttemptRegistration>())
		return 1;
	
	shared_ptr<NetAttemptRegistration> registration1(new NetAttemptRegistration("joe", "bob"));
	if(!testSerialize(registration1))
		return 2;
	
	shared_ptr<NetAttemptRegistration> registration2(new NetAttemptRegistration("joe bob", ""));
	if(!testSerialize(registration2))
		return 3;
	return 0;
}



int NetTestSuite::testNetAcceptRegistration()
{	
	///Test NetAcceptRegistration
	if(!testInitial<NetAcceptRegistration>())
		return 1;
		
	shared_ptr<NetAcceptRegistration> acceptRegistration1(new NetAcceptRegistration);
	if(!testSerialize(acceptRegistration1))
		return 2;
	return 0;
}



int NetTestSuite::testNetRefuseRegistration()
{	
	//Test NetRefuseRegistration
	if(!testInitial<NetRefuseRegistration>())
		return 1;

	shared_ptr<NetRefuseRegistration> refuseRegistration1(new NetRefuseRegistration(YOGPasswordIncorrect));
	if(!testSerialize(refuseRegistration1))
		return 2;
	return 0;
}



int NetTestSuite::testYOGGameInfo()
{
	shared_ptr<YOGGameInfo> ygi(new YOGGameInfo);
	//Test the initial state
	if(!testInitial<YOGGameInfo>())
		return 1;
	
	///Test the game name encoding	
	ygi->setGameName("Bobs Game");
	if(!testSerialize(ygi))
		return 2;
		
	//Test the game id encoding
	ygi->setGameID(1223);
	if(!testSerialize(ygi))
		return 3;
		
	///Test the game state
	ygi->setGameState(YOGGameInfo::GameRunning);
	if(!testSerialize(ygi))
		return 4;
	
	return 0;
}



int NetTestSuite::testYOGMessage()
{
	shared_ptr<YOGMessage> m1(new YOGMessage);
	//Test initial state
	if(!testInitial<YOGMessage>())
		return 1;
	
	//Test set message
	m1->setMessage("HAHA!");
	if(!testSerialize(m1))
		return 2;
		
	//Test set sender
	m1->setSender("Bob!");
	if(!testSerialize(m1))
		return 3;
	
	//Test set message type
	m1->setMessageType(YOGAdministratorMessage);
	if(!testSerialize(m1))
		return 4;

	return 0;
}



int NetTestSuite::testYOGPlayerSessionInfo()
{
	shared_ptr<YOGPlayerSessionInfo> ypi(new YOGPlayerSessionInfo);
	//Test the initial state
	if(!testInitial<YOGPlayerSessionInfo>())
		return 1;
	
	///Test the game name encoding	
	ypi->setPlayerName("Bob");
	if(!testSerialize(ypi))
		return 2;
		
	//Test the game id encoding
	ypi->setPlayerID(1023);
	if(!testSerialize(ypi))
		return 3;

	return 0;
}



int NetTestSuite::testNetReteamingInformation()
{
	shared_ptr<NetReteamingInformation> info(new NetReteamingInformation);
	//Test the intial state
	if(!testInitial<NetReteamingInformation>())
		return 1;
	
	//Test adding a few players
	info->setPlayerToTeam("Bob", 3);
	if(!testSerialize(info))
		return 2;

	info->setPlayerToTeam("Joe", 1);
	if(!testSerialize(info))
		return 3;

	info->setPlayerToTeam("Hawaii", 2);
	if(!testSerialize(info))
		return 4;
		
	return 0;
}



int NetTestSuite::testListenerConnection()
{
	//Creates the NetListener at port 7480
	NetListener nl;
	nl.startListening(7480);
	//Creates a NetConnection representing the client
	NetConnection nc_client;
	nc_client.openConnection("127.0.0.1", 7480);
	//Give it time to proccess the request
	SDL_Delay(40);
	//The server connection
	NetConnection nc_server;
	
	//Causes NetListener to accept the connection
	if(!nl.attemptConnection(nc_server))
	{
		return 1;
	}
	//Checks if the connection was established on the client side
	if(!nc_client.isConnected())
	{
		return 2;
	}
	//Checks if the connection was established for the server side
	if(!nc_server.isConnected())
	{
		return 3;
	}

	//Attempts to transmit a NetSendOrder over the connection
	shared_ptr<NetLoginSuccessful> netSendLogin1(new NetLoginSuccessful);
	nc_client.sendMessage(netSendLogin1);
	//Allow time for the request to be proccessed
	SDL_Delay(100);
	
	nc_client.update();
	nc_server.update();
	
	//Recieves the message on the other end
	shared_ptr<NetMessage> netSendLogin2 = nc_server.getMessage();
	if(!netSendLogin2)
	{
		return 4;
	}
	//Makes sure the two are equal
	if((*netSendLogin1) != (*netSendLogin2))
	{
		return 5;
	}
	return 0;
}



bool NetTestSuite::runAllTests()
{
	std::cout<<"Running tests: "<<std::endl;
	bool failed = false;
	
	int failNumber = testNetMessages();
	if(failNumber == 0)
	{
		std::cout<<"NetMessage serialization tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"NetMessage serialization test #"<<failNumber<<" failed."<<std::endl;
	}

	failNumber = testListenerConnection();
	if(failNumber == 0)
	{
		std::cout<<"NetListener & NetConnection tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"NetListener & NetConnection test #"<<failNumber<<" failed."<<std::endl;
	}
	
	failNumber = testYOGGameInfo();
	if(failNumber == 0)
	{
		std::cout<<"YOGGameInfo tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"YOGGameInfo test #"<<failNumber<<" failed."<<std::endl;
	}

	failNumber = testYOGMessage();
	if(failNumber == 0)
	{
		std::cout<<"YOGMessage tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"YOGMessage test #"<<failNumber<<" failed."<<std::endl;
	}	

	failNumber = testYOGPlayerSessionInfo();
	if(failNumber == 0)
	{
		std::cout<<"YOGPlayerSessionInfo tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"YOGPlayerSessionInfo test #"<<failNumber<<" failed."<<std::endl;
	}	

	failNumber = testNetReteamingInformation();
	if(failNumber == 0)
	{
		std::cout<<"NetReteamingInformation tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"NetReteamingInformation test #"<<failNumber<<" failed."<<std::endl;
	}

	return !failed;
}
