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

#include "NetEngine.h"
#include <iostream>
#include "NetMessage.h"


NetEngine::NetEngine(int numberOfPlayers, int localPlayer, int networkOrderRate, boost::shared_ptr<NetConnection> router)
	: numberOfPlayers(numberOfPlayers), localPlayer(localPlayer), router(router), networkOrderRate(networkOrderRate)
{
	step=0;
	orders.resize(numberOfPlayers);
	localOrderSendCountdown = 0;
	currentLatency = 0;
}



void NetEngine::setNetworkInfo(int nnetworkOrderRate, boost::shared_ptr<NetConnection> nrouter)
{
	networkOrderRate = nnetworkOrderRate;
	router = nrouter;
}



void NetEngine::advanceStep(Uint32 checksum)
{
	step+=1;
	if(localOrderSendCountdown == 0)
	{
		boost::shared_ptr<Order> localOrder;

		if(outgoing.empty())
		{
			localOrder = shared_ptr<Order>(new NullOrder);
		}
		else
		{
			localOrder = outgoing.front();
			outgoing.pop();
		}

		localOrder->gameCheckSum = checksum;
		if(router)
		{
			localOrder->sender = localPlayer;
			shared_ptr<NetSendOrder> message(new NetSendOrder(localOrder));
			router->sendMessage(message);
		}
		pushOrder(localOrder, localPlayer, false);
		localOrderSendCountdown = networkOrderRate - 1;
	}
	else
	{
		localOrderSendCountdown -= 1;
	}
}



void NetEngine::clearTopOrders()
{
	for(int p=0; p<numberOfPlayers; ++p)
	{
		boost::shared_ptr<Order> o = orders[p].front();
		///Handle latency adjustment order
		if(o->getOrderType() == ORDER_ADJUST_LATENCY)
		{
			boost::shared_ptr<AdjustLatency> al = boost::static_pointer_cast<AdjustLatency>(o);
			int diff = (al->latencyAdjustment) - currentLatency;
			if(diff>0)
			{
				for(int i=0; i<diff; ++i)
				{
					for(unsigned int p=0; p<orders.size(); ++p)
					{
						boost::shared_ptr<Order> order = boost::shared_ptr<Order>(new NullOrder);
						order->sender=p;
						orders[p].insert(orders[p].begin(), order);
					}
				}
			}
			currentLatency = al->latencyAdjustment;
		}
		orders[p].erase(orders[p].begin());
	}
}



void NetEngine::pushOrder(boost::shared_ptr<Order> order, int playerNumber, bool isAI)
{
	assert(playerNumber>=0);
	order->sender=playerNumber;
	orders[playerNumber].push_back(order); 

	///The local player and network players all have padding arround their order
	if(! isAI)
	{
		for(int i=0; i<(networkOrderRate - 1); ++i)
		{
			shared_ptr<Order> norder(new NullOrder);
			norder->sender=playerNumber;
			orders[playerNumber].push_back(norder);
		}
	}
}



boost::shared_ptr<Order> NetEngine::retrieveOrder(int playerNumber)
{
  return *orders[playerNumber].begin();
}



void NetEngine::addLocalOrder(boost::shared_ptr<Order> order)
{
	if(order->getOrderType() != ORDER_NULL)
	{
		outgoing.push(order);
	}
}



bool NetEngine::allOrdersRecieved()
{
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(orders[p].empty())
		{
			return false;
		}
	}
	return true;
}



int NetEngine::getStep()
{
	return step;
}



void NetEngine::flushAllOrders()
{
	while(!outgoing.empty())
	{
		boost::shared_ptr<Order> localOrder;
		localOrder = outgoing.front();
		outgoing.pop();
		localOrder->gameCheckSum = static_cast<unsigned int>(-1);

		if(router)
		{
			localOrder->sender = localPlayer;
			shared_ptr<NetSendOrder> message(new NetSendOrder(localOrder));
			router->sendMessage(message);
		}
		pushOrder(localOrder, localPlayer, false);

	}
	localOrderSendCountdown = networkOrderRate - 1;
}



void NetEngine::prepareForLatency(int playerNumber, int latency)
{
	currentLatency = latency;
	for(int s=0; s<latency; ++s)
	{
		pushOrder(boost::shared_ptr<Order>(new NullOrder), playerNumber, true);
	}
}



bool NetEngine::orderRecieved(int playerNumber)
{
	if(orders[playerNumber].empty())
		return false;
	return true;
}



Uint32 NetEngine::getWaitingOnMask()
{
	Uint32 mask = 0x0;
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(orders[p].empty())
		{
			mask |= (1<<p);
		}
	}
	return mask;
}



bool NetEngine::matchCheckSums()
{
	Uint32 checksum = static_cast<unsigned int>(-1);
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(!orders[p].empty())
		{
			Uint32 playerCheckSum = orders[p].front()->gameCheckSum;
			if(playerCheckSum != static_cast<unsigned int>(-1))
			{
				if(checksum == static_cast<unsigned int>(-1))
					checksum = playerCheckSum;
				else if(playerCheckSum != checksum)
				{
					return false;
				}
			}
		}
	}
	return true;
}



void NetEngine::increaseLatencyAdjustment()
{
	boost::shared_ptr<AdjustLatency> latency(new AdjustLatency(currentLatency+1));
	addLocalOrder(latency);
}



void NetEngine::setLocalPlayer(int player)
{
	localPlayer = player;
}
