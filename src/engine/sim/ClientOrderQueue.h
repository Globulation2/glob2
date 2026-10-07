// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Order.h"
#include <list>
#include <mutex>
#include <vector>

// The client publishes complete orders; only the simulation takes them. No
// iterator or reference into the queue escapes its lock. Tests may inspect copies.
class ClientOrderQueue
{
	mutable std::mutex mutex;
	std::list<std::shared_ptr<Order>> items;
public:
	void push_back(std::shared_ptr<Order> order) { std::lock_guard lock(mutex); items.push_back(std::move(order)); }
	std::shared_ptr<Order> take()
	{
		std::lock_guard lock(mutex);
		if (items.empty()) return {};
		auto value = std::move(items.front()); items.pop_front(); return value;
	}
	std::shared_ptr<Order> front() const { std::lock_guard lock(mutex); return items.front(); }
	void pop_front() { (void)take(); }
	bool empty() const { std::lock_guard lock(mutex); return items.empty(); }
	size_t size() const { std::lock_guard lock(mutex); return items.size(); }
	void clear() { std::list<std::shared_ptr<Order>> discarded; { std::lock_guard lock(mutex); discarded.swap(items); } }
	std::vector<std::shared_ptr<Order>> snapshot() const { std::lock_guard lock(mutex); return {items.begin(), items.end()}; }
	void append(std::list<std::shared_ptr<Order>>& batch) { std::lock_guard lock(mutex); items.splice(items.end(), batch); }
	void moveFlag(std::shared_ptr<OrderMoveFlag> order)
	{
		std::lock_guard lock(mutex);
		for (auto& queued : items)
			if (queued->getOrderType() == ORDER_MOVE_FLAG &&
				std::static_pointer_cast<OrderMoveFlag>(queued)->gid == order->gid && queued->clientTarget == order->clientTarget && queued->clientWorld == order->clientWorld)
			{ queued = std::move(order); return; }
		items.push_back(std::move(order));
	}
};
