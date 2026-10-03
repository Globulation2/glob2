// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Shared helpers for the turn protocol tests: an Order that carries raw wire bytes
// (the unit binary links only stubbed Order factories), a scripted transport, and the
// simulated network the unit harness (TurnHarnessTest) and the full-engine harness
// (TurnEngineHarness) run their relay and clients over.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <random>
#include <memory>
#include <vector>

#include "Order.h"
#include "TurnMessages.h"
#include "TurnSequencer.h"
#include "TurnSession.h"

namespace turntest
{
	/// An order whose wire form is exactly `bytes` (type byte first).
	class BytesOrder : public MiscOrder
	{
	public:
		std::vector<Uint8> bytes;
		explicit BytesOrder(std::vector<Uint8> b) : bytes(std::move(b)) {}
		Uint8 getOrderType(void) override { return bytes.at(0); }
		Uint8* getData(void) override { return bytes.data() + 1; }
		bool setData(const Uint8*, int, Uint32) override { return true; }
		int getDataLength(void) override { return static_cast<int>(bytes.size()) - 1; }
	};

	inline std::shared_ptr<Order> makeBytesOrder(std::vector<Uint8> bytes)
	{
		return std::make_shared<BytesOrder>(std::move(bytes));
	}

	inline std::vector<std::uint8_t> wireBytes(Order& order)
	{
		std::vector<std::uint8_t> bytes{order.getOrderType()};
		const Uint8* data = order.getData();
		if (data)
			bytes.insert(bytes.end(), data, data + order.getDataLength());
		return bytes;
	}

	inline Turn::OrderCodec bytesOrderCodec()
	{
		Turn::OrderCodec codec;
		codec.encode = [](Order& order) { return wireBytes(order); };
		codec.decode = [](const std::uint8_t* data, std::size_t size) -> std::shared_ptr<Order> {
			if (!size)
				return nullptr;
			return makeBytesOrder(std::vector<Uint8>(data, data + size));
		};
		return codec;
	}

	/// A transport the test drives by hand.
	struct ScriptedTransport : Turn::TurnTransport
	{
		State linkState = State::Disconnected;
		int connectCalls = 0;
		int closeCalls = 0;
		std::vector<std::vector<std::uint8_t>> sent;
		std::deque<std::vector<std::uint8_t>> incoming;

		State state() override { return linkState; }
		void connect() override
		{
			++connectCalls;
			if (linkState == State::Disconnected)
				linkState = State::Connecting;
		}
		void close() override
		{
			++closeCalls;
			linkState = State::Disconnected;
		}
		bool send(const std::vector<std::uint8_t>& payload) override
		{
			sent.push_back(payload);
			return true;
		}
		bool receive(std::vector<std::uint8_t>& payload) override
		{
			if (incoming.empty())
				return false;
			payload = std::move(incoming.front());
			incoming.pop_front();
			return true;
		}
		void deliver(const NetMessage& message) { incoming.push_back(Turn::TurnCodec::encode(message)); }

		/// Decoded messages of one type sent so far; removes them from `sent`.
		template <typename T>
		std::vector<std::shared_ptr<T>> sentOf(std::uint8_t type)
		{
			std::vector<std::shared_ptr<T>> found;
			for (auto it = sent.begin(); it != sent.end();)
			{
				auto message = Turn::TurnCodec::decode(*it);
				if (message && message->getMessageType() == type)
				{
					found.push_back(std::static_pointer_cast<T>(message));
					it = sent.erase(it);
				}
				else
					++it;
			}
			return found;
		}
	};

	// ---- Simulated network: a TurnSequencer and clients over links with latency,
	// jitter, loss (as TCP-style retransmission delay) and outages. Times in microseconds.
	using namespace Turn;
	inline constexpr std::uint64_t MS = 1000;
	inline constexpr std::uint64_t SECOND = 1000 * MS;
	inline constexpr std::uint64_t FRAME = 5 * MS;
	inline constexpr std::uint64_t TICK_PERIOD = 40 * MS; ///< TurnSession's default tick period

	struct LinkProfile
	{
		std::uint64_t latency = 20 * MS;   ///< one way
		std::uint64_t jitter = 0;          ///< extra one-way delay, uniform in [0, jitter]
		double loss = 0;                   ///< probability a frame needs a retransmission
		std::uint64_t retransmit = 200 * MS;
	};

	/// One direction of a reliable, ordered stream: frames never overtake each other.
	struct Pipe
	{
		std::deque<std::pair<std::uint64_t, std::vector<std::uint8_t>>> frames;
		std::uint64_t lastDelivery = 0;

		void push(std::uint64_t now, const LinkProfile& p, std::mt19937& rng, std::vector<std::uint8_t> payload)
		{
			std::uint64_t at = now + p.latency;
			if (p.jitter)
				at += std::uniform_int_distribution<std::uint64_t>(0, p.jitter)(rng);
			if (p.loss > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < p.loss)
				at += p.retransmit;
			at = std::max(at, lastDelivery);
			lastDelivery = at;
			frames.emplace_back(at, std::move(payload));
		}
		bool pop(std::uint64_t now, std::vector<std::uint8_t>& out)
		{
			if (frames.empty() || frames.front().first > now)
				return false;
			out = std::move(frames.front().second);
			frames.pop_front();
			return true;
		}
	};

	class SimNetwork;

	/// A connection between one client transport and the relay.
	struct Connection
	{
		PeerId peer = 0;
		Pipe up, down;
		std::uint64_t openAt = 0;    ///< handshake completes (one round trip)
		bool opened = false;         ///< relay told
		bool dead = false;           ///< cut: nothing more flows
		bool relayClosed = false;    ///< relay closed it; client sees it after draining
		std::uint64_t clientNoticesAt = UINT64_MAX;
		std::uint64_t relayNoticesAt = UINT64_MAX;
	};

	class SimTransport : public TurnTransport
	{
	public:
		SimTransport(SimNetwork& net, int client) : net(net), client(client) {}
		State state() override;
		void connect() override;
		void close() override;
		bool send(const std::vector<std::uint8_t>& payload) override;
		bool receive(std::vector<std::uint8_t>& payload) override;
		std::shared_ptr<Connection> current;
	private:
		SimNetwork& net;
		int client;
	};

	class SimNetwork : public SequencerOutput
	{
	public:
		std::uint64_t now = 0;
		std::mt19937 rng{12345};
		std::vector<LinkProfile> links;
		std::vector<std::uint64_t> outageUntil;
		std::map<PeerId, std::shared_ptr<Connection>> connections;
		std::unique_ptr<TurnSequencer> relay;
		PeerId nextPeer = 1;
		std::uint64_t detectDelay = 1 * SECOND; ///< how long a dead link takes to notice

		void send(PeerId peer, const std::vector<std::uint8_t>& payload) override
		{
			auto it = connections.find(peer);
			if (it == connections.end() || it->second->dead || it->second->relayClosed)
				return;
			const int client = clientOf(peer);
			it->second->down.push(now, links[client], rng, payload);
		}
		void close(PeerId peer) override
		{
			auto it = connections.find(peer);
			if (it != connections.end())
				it->second->relayClosed = true;
		}

		std::map<PeerId, int> peerClient;
		int clientOf(PeerId peer) const { return peerClient.at(peer); }

		std::shared_ptr<Connection> open(int client)
		{
			auto c = std::make_shared<Connection>();
			c->peer = nextPeer++;
			peerClient[c->peer] = client;
			if (now < outageUntil[client])
			{
				c->dead = true; // the attempt times out
				c->clientNoticesAt = now + detectDelay;
			}
			else
				c->openAt = now + 2 * links[client].latency;
			connections[c->peer] = c;
			return c;
		}

		/// Severs a client's link for `duration`; both ends notice after detectDelay.
		void outage(int client, std::uint64_t duration, SimTransport& transport)
		{
			outageUntil[client] = now + duration;
			if (auto c = transport.current)
			{
				c->dead = true;
				c->up.frames.clear();
				c->down.frames.clear();
				c->clientNoticesAt = now + detectDelay;
				if (c->opened)
					c->relayNoticesAt = now + detectDelay;
			}
		}

		void step()
		{
			std::vector<PeerId> finished;
			for (auto& [peer, c] : connections)
			{
				if (!c->opened && !c->dead && now >= c->openAt)
				{
					c->opened = true;
					relay->onConnect(peer, now);
				}
				if (c->opened && !c->dead && !c->relayClosed)
				{
					std::vector<std::uint8_t> payload;
					while (c->up.pop(now, payload))
					{
						relay->onReceive(peer, payload, now);
						if (c->relayClosed)
							break;
					}
				}
				if (c->dead && now >= c->relayNoticesAt)
				{
					c->relayNoticesAt = UINT64_MAX;
					relay->onDisconnect(peer, now);
				}
			}
			relay->update(now);
		}
	};

	inline TurnTransport::State SimTransport::state()
	{
		if (!current)
			return State::Disconnected;
		auto& c = *current;
		if (c.dead)
			return net.now >= c.clientNoticesAt ? State::Disconnected : (c.opened ? State::Connected : State::Connecting);
		if (c.relayClosed && c.down.frames.empty())
			return State::Disconnected;
		if (!c.opened)
			return State::Connecting;
		return State::Connected;
	}

	inline void SimTransport::connect()
	{
		if (current && !current->dead && !current->relayClosed && current->opened)
			return;
		current = net.open(client);
	}

	inline void SimTransport::close()
	{
		if (current && current->opened && !current->dead && !current->relayClosed)
			net.relay->onDisconnect(current->peer, net.now);
		if (current)
			current->dead = true, current->clientNoticesAt = 0;
		current.reset();
	}

	inline bool SimTransport::send(const std::vector<std::uint8_t>& payload)
	{
		if (!current || current->dead || current->relayClosed || !current->opened)
			return false;
		current->up.push(net.now, net.links[client], net.rng, payload);
		return true;
	}

	inline bool SimTransport::receive(std::vector<std::uint8_t>& payload)
	{
		if (!current || current->dead)
			return false;
		return current->down.pop(net.now, payload);
	}
}
