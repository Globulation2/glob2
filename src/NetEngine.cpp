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

#include "NetEngine.h"

NetEngine::NetEngine(int numberOfPlayers)
	: numberOfPlayers(numberOfPlayers)
{
	step=0;
}



void NetEngine::advanceStep()
{
	for(int p=0; p<numberOfPlayers; ++p)
	{
		orders.erase(hash(p, step));
	}
	step+=1;
}

void NetEngine::pushOrder(Order* order, int playerNumber, int targetStep)
{
	if(targetStep==-1)
		targetStep=step;
	order->sender=playerNumber;
	order->ustep=targetStep;
	orders[hash(playerNumber, targetStep)] = order;
}



Order* NetEngine::retrieveOrder(int playerNumber)
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
			return false;
	}
	return true;
}
