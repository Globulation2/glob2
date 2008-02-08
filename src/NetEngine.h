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

///The purpose of this class is to sort Orders, and hand them out in
///the correct time slot. It serves partially to hide latency, Orders
///are set to execute a fixed number of ticks ahead, and this class
///handles that discrepency. It is used always, local or net games,
///as the central message pump for Orders.
class NetEngine
{
public:
	//Constructs the NetEngine
	NetEngine(int numberOfPlayers);

	///Advances the step, clearing Orders at the previous step
	void advanceStep();

	//Pushes an order to the NetEngine
	void pushOrder(boost::shared_ptr<Order> order, int playerNumber);
	
	///Retrieves the order for the given player for this turn
	boost::shared_ptr<Order> retrieveOrder(int playerNumber);
	
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
	int step;
	int numberOfPlayers;
};


#endif
