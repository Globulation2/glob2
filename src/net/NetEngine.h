// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "LockstepSession.h"
#include "Order.h"
#include <memory>
#include <vector>
#include <queue>

///The local order queue for single player and replay playback: it sorts
///Orders and hands them out in the correct time slot. The engine drives it
///through the LockstepSession interface; online and LAN games use the turn
///protocol session instead.
class NetEngine : public LockstepSession
{
public:
	///Constructs the NetEngine
	NetEngine(int numberOfPlayers, int localPlayer);

	///Advances the step
	void advanceStep(Uint32 checksum) override;

	///Clears all the orders at the top of the queues
	void clearTopOrders() override;

	//Pushes an order to the NetEngine
	void pushOrder(std::shared_ptr<Order> order, int playerNumber, bool isAI) override;
	
	///Retrieves the order for the given player for this turn
	std::shared_ptr<Order> retrieveOrder(int playerNumber) override;

	///Adds a order from the local player, queued for its turn
	void addLocalOrder(std::shared_ptr<Order> order) override;
	
	///Tells whether the network is ready at the current tick. For
	///the network to be ready, all Orders from all players must be
	///present, otherwise it will have to hold for received Orders.
	bool allOrdersReceived();

	///LockstepSession: the tick is ready once all orders are received
	bool tickReady() override { return allOrdersReceived(); }
	
	///Returns the current step number
	int getStep();

	///Queues all pending local orders without a checksum. This is used if the game has to end immediately
	void flushAllOrders() override;
	
	///Returns true if the given player has provided an order and is ready to go
	bool orderReceived(int playerNumber) override;
	
	///Returns the mask representing each player that the NetEngine is waiting
	///on for this step
	Uint32 getWaitingOnMask() override;

	///Checks the checksums of all players for this step.
	///returns false if they don't match
	bool matchCheckSums() override;

	///Set the localPlayer, only necessary in replays
	void setLocalPlayer(int player) override;
	
private:

	///This stores the queues with the orders from each player
	std::vector<std::vector<std::shared_ptr<Order> > > orders;
	///This queue stores all of the local orders that have to be sent out
	///on their turn
	std::queue<std::shared_ptr<Order> > outgoing;
	int step;
	int numberOfPlayers;
	int localPlayer;
	///Latency of the recorded legacy network game, which AdjustLatency
	///orders in old replays change
	int currentLatency;
};


