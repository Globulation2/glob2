// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Shared helpers for the turn protocol tests: an Order that carries raw wire bytes
// (the unit binary links only stubbed Order factories) and a scripted transport.
#pragma once

#include <deque>
#include <memory>
#include <vector>

#include "Order.h"
#include "TurnMessages.h"
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
}
