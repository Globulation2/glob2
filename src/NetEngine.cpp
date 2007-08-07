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


NetEngine::NetEngine(int numberOfPlayers)
	: numberOfPlayers(numberOfPlayers)
{
	step=0;
	stepNumber.resize(numberOfPlayers);
}



void NetEngine::advanceStep()
{
	for(int p=0; p<numberOfPlayers; ++p)
	{
		orders.erase(hash(p, step));
	}
	step+=1;
}

void NetEngine::pushOrder(boost::shared_ptr<Order> order, int playerNumber)
{
	int targetStep = stepNumber[playerNumber];
	stepNumber[playerNumber] += 1;
	order->sender=playerNumber;
	orders[hash(playerNumber, targetStep)] = order;
}



boost::shared_ptr<Order> NetEngine::retrieveOrder(int playerNumber)
{
	return orders[hash(playerNumber, step)];
}



Uint16 NetEngine::hash(int playerNumber, int step)
{
	return Uint16(playerNumber) | (Uint16(step%2048)<<5);
}



bool NetEngine::allOrdersRecieved()
{
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(orders.find(hash(p, step)) == orders.end())
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



void NetEngine::prepareForLatency(int playerNumber, int latency)
{
	for(int s=0; s<latency; ++s)
	{
		pushOrder(boost::shared_ptr<Order>(new NullOrder), playerNumber);
	}
}



bool NetEngine::orderRecieved(int playerNumber)
{
	if(orders.find(hash(playerNumber, step)) == orders.end())
		return false;
	return true;
}



Uint32 NetEngine::getWaitingOnMask()
{
	Uint32 mask = 0x0;
	for(int p=0; p<numberOfPlayers; ++p)
	{
		if(orders.find(hash(p, step)) == orders.end())
		{
			mask |= (1<<p);
		}
	}
	return mask;
}



bool NetEngine::matchCheckSums()
{
	Uint32 checksum = -1;
	for(int p=0; p<numberOfPlayers; ++p)
	{
		Uint32 playerCheckSum = orders.find(hash(p, step))->second->gameCheckSum;
		if(playerCheckSum != -1)
		{
			if(checksum == -1)
				checksum = playerCheckSum;
			else if(playerCheckSum != checksum)
			{				
				return false;
			}
		}
	}
	return true;
}

