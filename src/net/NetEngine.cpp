// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetEngine.h"
#include "Team.h"
#include <stdexcept>


NetEngine::NetEngine(int numberOfPlayers, int localPlayer)
	: numberOfPlayers(numberOfPlayers), localPlayer(localPlayer)
{
	if (numberOfPlayers < 1 || numberOfPlayers > Team::MAX_COUNT || localPlayer < 0 || localPlayer >= numberOfPlayers)
		throw std::runtime_error("Invalid network game configuration");
	step=0;
	orders.resize(numberOfPlayers);
	currentLatency = 0;
}



void NetEngine::advanceStep(Uint32 checksum)
{
	step+=1;
	std::shared_ptr<Order> localOrder;

	if(outgoing.empty())
	{
		localOrder = std::shared_ptr<Order>(new NullOrder);
	}
	else
	{
		localOrder = outgoing.front();
		outgoing.pop();
	}

	localOrder->gameCheckSum = checksum;
	pushOrder(localOrder, localPlayer, false);
}



void NetEngine::clearTopOrders()
{
	if (!allOrdersReceived()) return;
	for(int p=0; p<numberOfPlayers; ++p)
	{
		std::shared_ptr<Order> o = orders[p].front();
		///Old replays of YOG games carry latency adjustment orders: replay them
		if(o->getOrderType() == ORDER_ADJUST_LATENCY)
		{
			std::shared_ptr<AdjustLatency> al = std::static_pointer_cast<AdjustLatency>(o);
			int diff = (al->latencyAdjustment) - currentLatency;
			if(diff>0)
			{
				for(int i=0; i<diff; ++i)
				{
					for(unsigned int p=0; p<orders.size(); ++p)
					{
						std::shared_ptr<Order> order = std::shared_ptr<Order>(new NullOrder);
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



void NetEngine::pushOrder(std::shared_ptr<Order> order, int playerNumber, bool)
{
	if (!order || playerNumber < 0 || playerNumber >= numberOfPlayers) return;
	order->sender=playerNumber;
	orders[playerNumber].push_back(order); 
}



std::shared_ptr<Order> NetEngine::retrieveOrder(int playerNumber)
{
  if (playerNumber < 0 || playerNumber >= numberOfPlayers || orders[playerNumber].empty()) return {};
  return orders[playerNumber].front();
}



void NetEngine::addLocalOrder(std::shared_ptr<Order> order)
{
	if(order->getOrderType() != ORDER_NULL)
	{
		outgoing.push(order);
	}
}



bool NetEngine::allOrdersReceived()
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
		std::shared_ptr<Order> localOrder;
		localOrder = outgoing.front();
		outgoing.pop();
		localOrder->gameCheckSum = ORDER_CHECKSUM_NONE;
		pushOrder(localOrder, localPlayer, false);
	}
}



bool NetEngine::orderReceived(int playerNumber)
{
	if (playerNumber < 0 || playerNumber >= numberOfPlayers) return false;
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
			mask |= Team::teamNumberToMask(p);
		}
	}
	return mask;
}



bool NetEngine::matchCheckSums()
{
	Uint32 checksum = ORDER_CHECKSUM_NONE;
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(!orders[p].empty())
		{
			Uint32 playerCheckSum = orders[p].front()->gameCheckSum;
			if(playerCheckSum != ORDER_CHECKSUM_NONE)
			{
				if(checksum == ORDER_CHECKSUM_NONE)
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



void NetEngine::setLocalPlayer(int player)
{
	localPlayer = player;
}
