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

#include "NetTestSuite.h"
#include <iostream>

NetTestSuite::NetTestSuite()
{

}



template<typename t> bool NetTestSuite::testMessage(shared_ptr<t> message)
{
	shared_ptr<NetMessage> decodedMessage;
	Uint8* messageInfo = message->encodeData();
	decodedMessage = NetMessage::getNetMessage(messageInfo, message->getDataLength());
	delete messageInfo;
	if((*message) != (*decodedMessage))
	{
		return false;
	}
	return true;
}



template<typename t> bool NetTestSuite::testInitialMessageState()
{
	shared_ptr<t> message1(new t);
	shared_ptr<t> message2(new t);
	if((*message1) != (*message2))
		return false;
	return true;
}



int NetTestSuite::testNetMessages()
{
	///Test NetSendOrder
	if(!testInitialMessageState<NetSendOrder>())
		return 1;

	shared_ptr<NetSendOrder> netSendOrder1(new NetSendOrder);
	netSendOrder1->changeOrder(new OrderDelete(1));
	if(!testMessage(netSendOrder1))
		return 2;

	///Test NetSendClientInformation
	if(!testInitialMessageState<NetSendClientInformation>())
		return 3;

	shared_ptr<NetSendClientInformation> clientInfo1(new NetSendClientInformation);
	if(!testMessage(clientInfo1))
		return 4;
		
	///Test NetSendServerInformation
	if(!testInitialMessageState<NetSendServerInformation>())
		return 5;
	
	shared_ptr<NetSendServerInformation> serverInfo1(new NetSendServerInformation(YOGRequirePassword, YOGSingleGame));
	if(!testMessage(serverInfo1))
		return 6;
	
	///Test NetAttemptLogin
	if(!testInitialMessageState<NetAttemptLogin>())
		return 7;
	
	shared_ptr<NetAttemptLogin> attemptLogin1(new NetAttemptLogin("joe", "bob"));
	if(!testMessage(attemptLogin1))
		return 8;
	
	shared_ptr<NetAttemptLogin> attemptLogin2(new NetAttemptLogin("joe bob", ""));
	if(!testMessage(attemptLogin2))
		return 9;
	
	///Test NetLoginSuccessful
	if(!testInitialMessageState<NetLoginSuccessful>())
		return 10;
		
	shared_ptr<NetLoginSuccessful> loginSuccess1(new NetLoginSuccessful);
	if(!testMessage(loginSuccess1))
		return 11;

	//Test NetRefuseLogin
	if(!testInitialMessageState<NetRefuseLogin>())
		return 12;

	shared_ptr<NetRefuseLogin> refuseLogin1(new NetRefuseLogin(YOGPasswordIncorrect));
	if(!testMessage(refuseLogin1))
		return 13;	

	///Test NetDisconnect
	if(!testInitialMessageState<NetDisconnect>())
		return 14;
		
	shared_ptr<NetDisconnect> disconnect1(new NetDisconnect);
	if(!testMessage(disconnect1))
		return 15;
		
	///Test NetAttemptRegistration
	if(!testInitialMessageState<NetAttemptRegistration>())
		return 16;
	
	shared_ptr<NetAttemptRegistration> registration1(new NetAttemptRegistration("joe", "bob"));
	if(!testMessage(registration1))
		return 17;
	
	shared_ptr<NetAttemptRegistration> registration2(new NetAttemptRegistration("joe bob", ""));
	if(!testMessage(registration2))
		return 18;
		
	///Test NetAcceptRegistration
	if(!testInitialMessageState<NetAcceptRegistration>())
		return 19;
		
	shared_ptr<NetAcceptRegistration> acceptRegistration1(new NetAcceptRegistration);
	if(!testMessage(acceptRegistration1))
		return 20;

	//Test NetRefuseRegistration
	if(!testInitialMessageState<NetRefuseRegistration>())
		return 21;

	shared_ptr<NetRefuseRegistration> refuseRegistration1(new NetRefuseRegistration(YOGPasswordIncorrect));
	if(!testMessage(refuseRegistration1))
		return 22;
		
	//Test NetCreateGame
	if(!testInitialMessageState<NetCreateGame>())
		return 23;

	shared_ptr<NetCreateGame> createGame1(new NetCreateGame("my game"));
	if(!testMessage(createGame1))
		return 24;

	shared_ptr<NetCreateGame> createGame2(new NetCreateGame("haha my first game woot"));
	if(!testMessage(createGame2))
		return 25;
	
	//Test NetAttemptJoinGame
	if(!testInitialMessageState<NetAttemptJoinGame>())
		return 26;

	shared_ptr<NetAttemptJoinGame> attemptJoin1(new NetAttemptJoinGame(1));
	if(!testMessage(attemptJoin1))
		return 27;

	shared_ptr<NetAttemptJoinGame> attemptJoin2(new NetAttemptJoinGame(6627));
	if(!testMessage(attemptJoin2))
		return 27;
		
	//Test NetGameJoinAccepted
	if(!testInitialMessageState<NetGameJoinAccepted>())
		return 28;

	shared_ptr<NetGameJoinAccepted> joinAccepted1(new NetGameJoinAccepted);
	if(!testMessage(joinAccepted1))
		return 29;
		
	//Test NetGameJoinRefused
	if(!testInitialMessageState<NetGameJoinRefused>())
		return 30;

	shared_ptr<NetGameJoinRefused> joinRefused1(new NetGameJoinRefused(YOGJoinRefusalUnknown));
	if(!testMessage(joinRefused1))
		return 31;
	return 0;

}



int  NetTestSuite::testYOGGameInfo()
{
	YOGGameInfo ygi;
	YOGGameInfo decode;
	//Test the initial state
	if(decode != ygi)
		return 1;
	
	///Test the game name encoding	
	ygi.setGameName("Bobs Game");
	Uint8* messageInfo = ygi.encodeData();
	decode.decodeData(messageInfo, ygi.getDataLength());
	delete messageInfo;
	if(decode != ygi)
		return 2;
		
	//Test the game id encoding
	ygi.setGameID(1223);
	messageInfo = ygi.encodeData();
	decode.decodeData(messageInfo, ygi.getDataLength());
	delete messageInfo;
	if(decode != ygi)
		return 3;
	return 0;
}



int NetTestSuite::testListenerConnection()
{
	//Creates the NetListener at port 30
	NetListener nl(30);
	//Creates a NetConnection representing the client
	NetConnection nc_client;
	nc_client.openConnection("localhost", 30);
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
	shared_ptr<NetSendOrder> netSendOrder1(new NetSendOrder);
	netSendOrder1->changeOrder(new OrderDelete(1));
	nc_client.sendMessage(netSendOrder1);
	//Recieves the message on the other end
	shared_ptr<NetMessage> netSendOrder2 = nc_server.getMessage();
	if(!netSendOrder2)
	{
		return 4;
	}
	//Makes sure the two are equal
	if((*netSendOrder1) != (*netSendOrder2))
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
		std::cout<<"YOGameInfo tests passed."<<std::endl;
	}
	else
	{
		failed = true;
		std::cout<<"YOGameInfo test #"<<failNumber<<" failed."<<std::endl;
	}

	return !failed;
}
