// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __NetEngine_h
#define __NetEngine_h

#include "Order.h"
#include <memory>
#include <vector>
#include <queue>
#include "NetConnection.h"

///The purpose of this class is to sort Orders, and hand them out in
///the correct time slot. It serves partially to hide latency, Orders
///are set to execute a fixed number of ticks ahead, and this class
///handles that discrepency. It is used always, local or net games,
///as the central message pump for Orders.
class NetEngine
{
public:
	///Constructs the NetEngine
	NetEngine(int numberOfPlayers, int localPlayer, int networkOrderRate = 1, std::shared_ptr<NetConnection> router = std::shared_ptr<NetConnection>());

	///Sets the network game info
	void setNetworkInfo(int networkOrderRate, std::shared_ptr<NetConnection> client);

	///Advances the step
	void advanceStep(Uint32 checksum);

	///Clears all the orders at the top of the queues
	void clearTopOrders();

	//Pushes an order to the NetEngine. AI's are special because they don't have padding arround orders
	void pushOrder(std::shared_ptr<Order> order, int playerNumber, bool isAI);
	
	///Retrieves the order for the given player for this turn
	std::shared_ptr<Order> retrieveOrder(int playerNumber);

	///Adds a order from the local player, which will be queued and sent across the network when needed
	void addLocalOrder(std::shared_ptr<Order> order);
	
	///Tells whether the network is ready at the current tick. For
	///the network to be ready, all Orders from all players must be
	///present, otherwise it will have to hold for recieved Orders.
	bool allOrdersRecieved();
	
	///Returns the current step number
	int getStep();

	///Sends all pending orders across the network without a checksum. This is used if the game has to end immediettly
	void flushAllOrders();
	
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

	///This sends an order through the network that causes the latency adjustment to be increased
	void increaseLatencyAdjustment();
	
	///Set the localPlayer, only necessary in replays
	void setLocalPlayer(int player);
	
private:

	///This stores the queues with the orders from each player
	std::vector<std::vector<std::shared_ptr<Order> > > orders;
	///This queue stores all of the local orders that have to be sent out
	///on their turn
	std::queue<std::shared_ptr<Order> > outgoing;
	int step;
	int numberOfPlayers;
	///This count-downs steps until an order is sent across the network
	int localOrderSendCountdown;
	int localPlayer;
	std::shared_ptr<NetConnection> router;
	int networkOrderRate;
	int currentLatency;
};


#endif
