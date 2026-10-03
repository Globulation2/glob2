// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>

#include "GameEvent.h"
#include "sim/EntityRef.h"

class Order;
class OrderVoiceData;
class MapMarkOrder;

/// Lossless single-producer/single-consumer FIFO. Every transfer between the
/// simulation and the client goes through push() and drain(), so making the
/// channel thread-safe later (a mutex, or a lock-free SPSC ring) only changes
/// this class.
template <typename T>
class LosslessQueue
{
public:
	void push(T value) { items.push_back(std::move(value)); }
	//! Hand every queued item, oldest first, to `consume`.
	template <typename F>
	void drain(F &&consume)
	{
		while (!items.empty())
		{
			T value = std::move(items.front());
			items.pop_front();
			consume(std::move(value));
		}
	}
	bool empty() const { return items.empty(); }
	size_t size() const { return items.size(); }
	void clear() { items.clear(); }

private:
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
	struct PauseChanged { bool paused; int sender = -1; };
	//! An ORDER_CREATE reached the simulation (whether or not it succeeded).
	struct BuildingRequested { int team; Sint32 posX, posY; };
	//! Any order finished executing; the client reconciles its pending shadows.
	struct OrderExecuted { std::shared_ptr<Order> order; };
	//! A building was deleted. Its gid may be reused immediately.
	struct BuildingRemoved { Uint16 gid; };
	//! A unit kept its identity but moved to another team's slot (conversion).
	struct UnitConverted { UnitRef from, to; };
}

using ClientEventVariant = std::variant<ClientEvent::TeamEvent, ClientEvent::ChatMessage, ClientEvent::VoiceData,
	ClientEvent::PlayerQuit, ClientEvent::MapMark, ClientEvent::PauseChanged, ClientEvent::BuildingRequested,
	ClientEvent::OrderExecuted, ClientEvent::BuildingRemoved, ClientEvent::UnitConverted>;

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

	void push(ClientEventVariant event) { events.push(std::move(event)); }
	template <typename F>
	void drain(F &&consume) { events.drain(std::forward<F>(consume)); }
	bool empty() const { return events.empty(); }
	size_t size() const { return events.size(); }

	void publishPulse(const TickPulse &value) { pulseValue = value; }
	TickPulse pulse() const { return pulseValue; }

	//! Forget everything, e.g. when another game is loaded into the client.
	void reset()
	{
		events.clear();
		pulseValue = TickPulse();
	}

private:
	LosslessQueue<ClientEventVariant> events;
	TickPulse pulseValue;
};
