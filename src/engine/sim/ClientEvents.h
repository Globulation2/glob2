// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>

#include "GameEvent.h"
#include "sim/ClientCommandSink.h"
#include "sim/EntityRef.h"

class Order;
class OrderVoiceData;
class MapMarkOrder;

/// Lossless FIFO between the simulation owner and client. drain() takes one
/// batch under the lock, then invokes callbacks without holding it. Items pushed
/// during delivery belong to the next batch; a busy producer cannot extend the
/// current client frame indefinitely. A channel has exactly one consumer.
template <typename T>
class LosslessQueue
{
public:
	void push(T value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(std::move(value));
	}
	//! Deliver the batch available at entry, oldest first.
	template <typename F>
	void drain(F &&consume)
	{
		std::optional<std::deque<T>> batch;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (items.empty()) return;
			batch.emplace().swap(items);
		}
		for (auto &value : *batch) consume(std::move(value));
	}
	bool empty() const { std::lock_guard<std::mutex> lock(mutex); return items.empty(); }
	size_t size() const { std::lock_guard<std::mutex> lock(mutex); return items.size(); }
	//! Lifecycle operation: producers and the consumer must be stopped first.
	void clear()
	{
		std::deque<T> discarded;
		{
			std::lock_guard<std::mutex> lock(mutex);
			discarded.swap(items);
		}
	}

private:
	mutable std::mutex mutex;
	std::deque<T> items;
};

/// Notices the simulation publishes for the client. Producers are Game and the
/// code it runs (ticks, order execution); the consumer is GameGUI. The payloads
/// are self-contained values so the client never needs to look up the entity
/// or player that caused them.
namespace ClientEvent
{
	//! A Team::pushGameEvent notification (any team; the client shows its own).
	struct TeamEvent { int team; GameEvent event; };
	//! A chat or private message order was executed. Names are captured at
	//! execution time so the client formats without reading Game::players.
	struct ChatMessage
	{
		Uint32 messageOrderType; //!< MessageOrder::*_MESSAGE_TYPE
		int sender;
		std::string senderName;
		std::string text;
		Uint32 recipientsMask;
		//! Names of the live players set in recipientsMask, in player order.
		std::vector<std::string> recipientNames;
	};
	struct VoiceData { std::shared_ptr<OrderVoiceData> order; };
	struct PlayerQuit { int player; std::string name; };
	//! `markingTeamAllies` is the marking team's alliance mask at execution.
	struct MapMark { std::shared_ptr<MapMarkOrder> order; Uint32 markingTeamAllies; };
	struct PauseChanged { bool paused; };
	struct ReplayEnded {};
	//! An ORDER_CREATE reached the simulation (whether or not it succeeded).
	struct BuildingRequested { int team; Sint32 posX, posY; };
	//! Any order finished executing; the client reconciles its pending shadows.
	struct OrderExecuted { std::shared_ptr<Order> order; Uint64 revision = 0; };
	//! A building was deleted. Its gid may be reused immediately.
	struct BuildingRemoved { Uint16 gid; };
	//! A unit kept its identity but moved to another team's slot (conversion).
	struct UnitConverted { UnitRef from, to; };
}

using ClientEventVariant = std::variant<ClientEvent::TeamEvent, ClientEvent::ChatMessage, ClientEvent::VoiceData,
	ClientEvent::PlayerQuit, ClientEvent::MapMark, ClientEvent::PauseChanged, ClientEvent::BuildingRequested,
	ClientEvent::ReplayEnded, ClientEvent::OrderExecuted, ClientEvent::BuildingRemoved, ClientEvent::UnitConverted, ScriptPresentation>;

/// Simulation → client channel: a lossless event queue plus latest-value state
/// that is cheaper to overwrite each tick than to queue.
class ClientEvents
{
public:
	static constexpr int MaxTeams = 32; // >= Team::MAX_COUNT; checked in Game.cpp

	/// Published at the end of every simulated tick.
	struct TickPulse
	{
		//! Game::stepCounter during the tick (before its increment); the
		//! reference Team::updateEvents ages events against.
		Uint32 tick = 0;
		bool valid = false;
		//! Team::wasRecentEvent(type) for every team at the end of the tick.
		std::array<std::array<bool, GESize>, MaxTeams> recentEvents{};
	};

	void push(ClientEventVariant event)
    {
        if (auto* order=std::get_if<ClientEvent::OrderExecuted>(&event)) order->revision=++orderRevision;
        events.push(std::move(event));
    }
    Uint64 executedOrderRevision() const { return orderRevision.load(); }
	template <typename F>
	void drain(F &&consume) { events.drain(std::forward<F>(consume)); }
	bool empty() const { return events.empty(); }
	size_t size() const { return events.size(); }

	void publishPulse(const TickPulse &value) { std::lock_guard<std::mutex> lock(pulseMutex); pulseValue = value; }
	TickPulse pulse() const { std::lock_guard<std::mutex> lock(pulseMutex); return pulseValue; }

	//! Forget everything, e.g. when another game is loaded into the client.
	void reset()
	{
		events.clear();
        orderRevision=0;
		publishPulse(TickPulse());
	}

private:
	LosslessQueue<ClientEventVariant> events;
    std::atomic<Uint64> orderRevision{0};
	mutable std::mutex pulseMutex;
	TickPulse pulseValue;
};
