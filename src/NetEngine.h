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

#ifndef __NetEngine_h
#define __NetEngine_h

#include <map>
#include "Order.h"
#include <boost/shared_ptr.hpp>

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

	//Pushes an order to the NetEngine for the given step
	void pushOrder(boost::shared_ptr<Order> order, int playerNumber, int targetStep=-1);
	
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
	
private:
	///This function produces the 16 bit hash for the stoarge of orders
	Uint16 hash(int playerNumber, int step);

	///This stores orders in an interesting fashion, hashing the playerNumber
	///together with the targetStep
	std::map<Uint16, boost::shared_ptr<Order> > orders;
	int step;
	int numberOfPlayers;
};


#endif
