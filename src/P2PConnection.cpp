/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "P2PConnection.h"
#include "YOGClient.h"
#include "NetMessage.h"

#include "P2PConnectionListener.h"
#include "P2PConnectionEvent.h"
#include "P2PConnection.h"

P2PConnection::P2PConnection(YOGClient* client)
	: client(client)
{
	int toTryPort = P2P_CONNECTION_PORT_FIRST;
	while(!listener.isListening() && toTryPort < P2P_CONNECTION_PORT_LAST)
	{
		listener.startListening(toTryPort);
		toTryPort += 1;
	}
	if(!listener.isListening())
		localPort = 0;
	else
		localPort = toTryPort;
	localIncoming = boost::shared_ptr<NetConnection>(new NetConnection);
	isConnecting=true;
	std::cout<<"attempting "<<localPort<<std::endl;
}



void P2PConnection::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetSendP2PInformation)
	{
		shared_ptr<NetSendP2PInformation> info = static_pointer_cast<NetSendP2PInformation>(message);
		updateP2PInformation(info->getGroupInfo());
	}
	else
	{
		sendNetMessageToListeners(message);
	}
}



void P2PConnection::sendMessage(boost::shared_ptr<NetMessage> message)
{
	//if(isConnecting)
	{
		client->sendNetMessage(message);
	}
	/*
	else
	{
		for(int i=0; i<outgoing.size(); ++i)
		{
			if(outgoing[i]->isConnected())
			{
				std::cout<<"sending"<<std::endl;
				outgoing[i]->sendMessage(message);
			}
		}
	}
	*/
}



int P2PConnection::getPort()
{
	return localPort;
}


void P2PConnection::reset()
{
	updateP2PInformation(P2PInformation());
}



void P2PConnection::update()
{
	for(int i=0; i<outgoing.size(); ++i)
	{
		if(outgoingStates[i]!=Failed)
			outgoing[i]->update();
	}

	if(isConnecting)
	{
		for(int i=0; i<outgoing.size(); ++i)
		{
			if(outgoingStates[i] == ReadyToTry)
			{
				std::string ip = group.getPlayerInformation(i).getIPAddress();
				int port = group.getPlayerInformation(i).getPort();
				outgoing[i]->openConnection(ip, port);
				outgoingStates[i] = Attempting;
				std::cout<<"Attempting"<<std::endl;
			}
			else if(outgoingStates[i] == Attempting)
			{
				if(!outgoing[i]->isConnecting())
				{
					if(outgoing[i]->isConnected())
					{
						outgoingStates[i] = Connected;
						std::cout<<"Connected"<<std::endl;
					}
					else
					{
						outgoingStates[i] = ReadyToTry;
						std::cout<<"Failed"<<std::endl;
					}
				}
			}
			else if(outgoingStates[i] == Connected)
			{
				if(!outgoing[i]->isConnected())
				{
					outgoingStates[i] = ReadyToTry;
					std::cout<<"Lost"<<std::endl;
				}
			}
		}
	}
	
	if(isConnecting)
	{
		while(listener.attemptConnection(*localIncoming))
		{
			incoming.push_back(localIncoming);
			localIncoming.reset(new NetConnection);
			std::cout<<"recieved"<<std::endl;
		}
	}
	
	
	for(int i=0; i<incoming.size();)
	{
		if(!incoming[i]->isConnected())
		{
			incoming.erase(incoming.begin() + i);
		}
		else
		{
			i++;
		}
	}
	
	for(int i=0; i<incoming.size(); ++i)
	{
		incoming[i]->update();
	}
	
	/*
	for(int i=0; i<outgoing.size(); ++i)
	{
		if(outgoingStates[i]==Connected)
		{
			while(true)
			{
				shared_ptr<NetMessage> message = outgoing[i]->getMessage();
				if(!message)
					break;
				sendNetMessageToListeners(message);
			}
		}
	}
	for(int i=0; i<incoming.size(); ++i)
	{
		std::cout<<"atempting to recieve"<<std::endl;
		while(true)
		{
			shared_ptr<NetMessage> message = incoming[i]->getMessage();
			if(!message)
				break;
			sendNetMessageToListeners(message);
		}
	}
	*/
}



void P2PConnection::stopConnections()
{
	//Update one last time
	update();
	isConnecting=false;
	for(int i=0; i<outgoing.size(); ++i)
	{
		if(outgoingStates[i]==Attempting)
		{
			outgoing[i]->closeConnection();
			outgoingStates[i]=Failed;
		}
	}
	listener.stopListening();
}

	

void P2PConnection::addEventListener(P2PConnectionListener* listener)
{
	listeners.push_back(listener);
}



void P2PConnection::removeEventListener(P2PConnectionListener* listener)
{
	listeners.remove(listener);
}



void P2PConnection::updateP2PInformation(const P2PInformation& newGroup)
{
	///Go through each of the ones in old group, if its not in new group, remove them
	int old_group_n = 0;
	int new_group_n = 0;
	while(old_group_n < group.getNumberOfP2PPlayer() && new_group_n < newGroup.getNumberOfP2PPlayer())
	{
		if(group.getPlayerInformation(old_group_n) != newGroup.getPlayerInformation(new_group_n))
		{
			outgoing.erase(outgoing.begin() + old_group_n);
			outgoingStates.erase(outgoingStates.begin() + old_group_n);
			old_group_n += 1;
		}
		else if(group.getPlayerInformation(old_group_n) == newGroup.getPlayerInformation(new_group_n))
		{
			old_group_n+=1;
			new_group_n+=1;
		}
	}
	while(old_group_n < group.getNumberOfP2PPlayer())
	{
		outgoing.erase(outgoing.begin() + old_group_n);
		outgoingStates.erase(outgoingStates.begin() + old_group_n);
		old_group_n += 1;
	}
	while(new_group_n < newGroup.getNumberOfP2PPlayer())
	{
		outgoing.push_back(boost::shared_ptr<NetConnection>(new NetConnection));
		if(newGroup.getPlayerInformation(new_group_n).getPlayerID() == client->getPlayerID())
		{
			outgoingStates.push_back(Local);
		}
		else
		{
			outgoingStates.push_back(ReadyToTry);
		}
		new_group_n+=1;
	}
	group = newGroup;
}



void P2PConnection::sendEventToListeners(boost::shared_ptr<P2PConnectionEvent> event)
{
	for(std::list<P2PConnectionListener*>::iterator i=listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->recieveP2PEvent(event);
	}
}



void P2PConnection::sendNetMessageToListeners(boost::shared_ptr<NetMessage> message)
{
	boost::shared_ptr<P2PRecievedMessage> event(new P2PRecievedMessage(message));
	sendEventToListeners(event);
}


