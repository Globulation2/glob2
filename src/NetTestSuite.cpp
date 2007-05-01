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


NetTestSuite::NetTestSuite()
{

}




int NetTestSuite::testNetMessages()
{
	///The first few tests work with NetSendOrder
	///Test that the initial states of two NetSendOrder are the same
	shared_ptr<NetSendOrder> netSendOrder1 = new NetSendOrder;
	shared_ptr<NetSendOrder> netSendOrder2 = new NetSendOrder;
	if((*netSendOrder1) != (*netSendOrder2))
	{
		return 1;
	}
	///Test that encoding and decoding a NetSendOrder will come out the same
	netSendOrder1.changeOrder(new OrderDelete(1));
	Uint8* netSendOrderData = netSendOrder1.encodeData();
	shared_ptr<NetMessage> decodedMessage = NetMessage::getNetMessage(netSendOrderData, netSendOrder1.getDataLength());
	delete netSendOrderData;
	if((*netSendOrder1) != (*decodedMessage))
	{
		return 2;
	}

	///Now, test NetSendClientInformation's serialization utilities
	shared_ptr<NetSendClientInformation> clientInfo1 = new NetSendClientInformation;
	Uint8* clientInfoData = clientInfo1.encodeData();
	decodedMessage.reset(NetMessage::getNetMessage(clientInfoData, clientInfo1.getDataLength()));
	delete clientInfoData;
	if((*clientInfo1) != (*decodedMessage))
	{
		return 3;
	}
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
	shared_ptr<NetSendOrder> netSendOrder1 = new NetSendOrder;
	netSendOrder1.changeOrder(new OrderDelete(1));
	nc_client.sendMessage(newSendOrder1);
	//Recieves the message on the other end
	shared_ptr<NetSendOrder> netSendOrder2 = nc_server.getMessage();
	if(!netSendOrder2)
	{
		return 4;
	}
	//Makes sure the two are equal
	if((*netSendOrder1) != (*netSendOrder2))
	{
		return 5;
	}
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

	return !failed;
}
