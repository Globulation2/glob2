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

#ifndef __NetEngine_h
#define __NetEngine_h

#include "Order.h"
#include <boost/shared_ptr.hpp>
#include <vector>
#include <queue>
#include "YOGClient.h"

///The purpose of this class is to sort Orders, and hand them out in
///the correct time slot. It serves partially to hide latency, Orders
///are set to execute a fixed number of ticks ahead, and this class
///handles that discrepency. It is used always, local or net games,
///as the central message pump for Orders.
class NetEngine
{
public:
	///Constructs the NetEngine
	NetEngine(int numberOfPlayers, int localPlayer, int networkOrderRate = 1, boost::shared_ptr<YOGClient> client = boost::shared_ptr<YOGClient>());

	///Sets the network game info
	void setNetworkInfo(int networkOrderRate, boost::shared_ptr<YOGClient> client);

	///Advances the step
	void advanceStep(Uint32 checksum);

	///Clears all the orders at the top of the queues
	void clearTopOrders();

	//Pushes an order to the NetEngine. AI's are special because they don't have padding arround orders
	void pushOrder(boost::shared_ptr<Order> order, int playerNumber, bool isAI);
	
	///Retrieves the order for the given player for this turn
	boost::shared_ptr<Order> retrieveOrder(int playerNumber);

	///Adds a order from the local player, which will be queued and sent across the network when needed
	void addLocalOrder(boost::shared_ptr<Order> order);
	
	///Tells whether the network is ready at the current tick. For
	///the network to be ready, all Orders from all players must be
	///present, otherwise it will have to hold for recieved Orders.
	bool allOrdersRecieved();
	
	///Returns the current step number
	int getStep();
	
	///Adds padding for the player for the given latency,
	///this is used because with latency, there aren't any
	///orders for the first few frames
	void prepareForLatency(int playerNumber, int latency);
	
	///Returns true if the given player has provided an order and is ready to go
	bool orderRecieved(int playerNumber);
	
	///Returns the mask representing each player that the NetEngine is waiting
	///on for this step
	Uint32 getWaitingOnMask();

	///Checks the checksums of all players for this step.
	///returns false if they don't match
	bool matchCheckSums();
	
private:

	///This stores the queues with the orders from each player
	std::vector<std::queue<boost::shared_ptr<Order> > > orders;
	///This queue stores all of the local orders that have to be sent out
	///on their turn
	std::queue<boost::shared_ptr<Order> > outgoing;
	int step;
	int numberOfPlayers;
	///This count-downs steps until an order is sent across the network
	int localOrderSendCountdown;
	int localPlayer;
	boost::shared_ptr<YOGClient> client;
	int networkOrderRate;
};


#endif
