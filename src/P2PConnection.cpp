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
}



void P2PConnection::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetSendP2PInformation)
	{
		shared_ptr<NetSendP2PInformation> info = static_pointer_cast<NetSendP2PInformation>(message);
		updateP2PInformation(info->getGroupInfo());
	}
}



void P2PConnection::sendMessage(boost::shared_ptr<NetMessage> message)
{
	//For now, just send it up the chute
	client->sendNetMessage(message);
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
		outgoing[i]->update();
	}

	for(int i=0; i<outgoing.size(); ++i)
	{
		if(outgoingStates[i] == ReadyToTry)
		{
			std::string ip = group.getPlayerInformation(i).getIPAddress();
			int port = group.getPlayerInformation(i).getPort();
			outgoing[i]->openConnection(ip, port);
			outgoingStates[i] = Attempting;
		}
		else if(outgoingStates[i] == Attempting)
		{
			if(!outgoing[i]->isConnecting())
			{
				if(outgoing[i]->isConnected())
				{
					outgoingStates[i] = Connected;
				}
				else
				{
					outgoingStates[i] = ReadyToTry;
				}
			}
		}
		else if(outgoingStates[i] == Connected)
		{
			if(!outgoing[i]->isConnected())
			{
				outgoingStates[i] = ReadyToTry;
			}
		}
	}
	
	while(listener.attemptConnection(*localIncoming))
	{
		incoming.push_back(localIncoming);
		localIncoming.reset(new NetConnection);
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

