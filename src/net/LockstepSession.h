// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <SDL3/SDL_stdinc.h>
#include <memory>

class Order;

/// The order pump the engine drives once per tick. It collects the local
/// player's orders and the AI orders computed on this client, exchanges them
/// with any peers, and hands back each player's order for a tick only when the
/// tick may be executed. Every client must hand back identical orders at
/// identical ticks, so implementations decide which tick an order runs at.
///
/// NetEngine implements this for single player, replays and the legacy YOG and
/// LAN games. The engine calls these methods only from its own thread, in the
/// order documented on each method (see Engine::gatherAndAdvanceOrders and
/// Engine::executeOrdersAndStep in EngineRun.cpp).
class LockstepSession
{
  public:
	virtual ~LockstepSession() = default;

	/// The player whose orders addLocalOrder() and advanceStep() submit.
	/// Called every committed tick; only replays change it.
	virtual void setLocalPlayer(int player) = 0;

	/// Queues an order from the local player. NullOrders are ignored. Queued
	/// orders are scheduled and sent by the next advanceStep() that submits one.
	virtual void addLocalOrder(std::shared_ptr<Order> order) = 0;

	/// Supplies an order for a player computed on this client (isAI = true:
	/// the AI seats every client simulates). The engine calls it only while
	/// orderReceived(playerNumber) is false. Legacy network receive paths also
	/// pass remote human orders with isAI = false.
	virtual void pushOrder(std::shared_ptr<Order> order, int playerNumber, bool isAI) = 0;

	/// True if the given player's order for the next executable tick is present.
	virtual bool orderReceived(int playerNumber) = 0;

	/// Ends the local client's turn for the tick that just executed, submitting
	/// the queued local order (if one is due) stamped with the given state
	/// checksum. Called only after a tick was committed.
	virtual void advanceStep(Uint32 checksum) = 0;

	/// True once every player's order for the next tick is present, so the
	/// engine may execute it. (NetEngine::allOrdersReceived.)
	virtual bool tickReady() = 0;

	/// Bit p is set while the engine is still waiting for player p's order.
	virtual Uint32 getWaitingOnMask() = 0;

	/// Compares the checksums carried by the ready tick's orders; false on a
	/// desynchronisation. Only called when tickReady() is true.
	virtual bool matchCheckSums() = 0;

	/// The given player's order for the ready tick. Only called when
	/// tickReady() is true, for every player, before clearTopOrders().
	virtual std::shared_ptr<Order> retrieveOrder(int playerNumber) = 0;

	/// Drops the ready tick's orders after they were executed.
	virtual void clearTopOrders() = 0;

	/// Submits every queued local order immediately, without a checksum,
	/// because the game is ending (for example a quit order).
	virtual void flushAllOrders() = 0;
};
