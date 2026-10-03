// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Turn protocol messages travel through NetConnection's framing unchanged: the
// NetMessage factory decodes the reserved 0xA0-0xBF range with the turn codecs, and
// the frame payload NetConnection carries is byte-identical to TurnCodec's.

#include "Glob2Test.h"

#include <deque>
#include <memory>

#include "NetConnection.h"
#include "NetMessage.h"
#include "TurnMessages.h"

namespace
{
// Two ends of an in-memory byte stream.
struct Loopback
{
	std::deque<std::vector<uint8_t>> aToB, bToA;
};

class LoopbackTransport : public NetTransport
{
public:
	LoopbackTransport(std::deque<std::vector<uint8_t>>& out, std::deque<std::vector<uint8_t>>& in) : out(out), in(in) {}
	void open(const std::string&, uint16_t) override { open_ = true; }
	void close() override { open_ = false; }
	State state() const override { return open_ ? State::Connected : State::Closed; }
	bool send(std::vector<uint8_t> bytes) override
	{
		sent.insert(sent.end(), bytes.begin(), bytes.end());
		out.push_back(std::move(bytes));
		return true;
	}
	bool receive(std::vector<uint8_t>& bytes) override
	{
		if (in.empty())
			return false;
		bytes = std::move(in.front());
		in.pop_front();
		return true;
	}
	std::vector<uint8_t> sent;
private:
	std::deque<std::vector<uint8_t>>& out;
	std::deque<std::vector<uint8_t>>& in;
	bool open_ = false;
};
}

TEST_CASE("turn messages ride NetConnection framing")
{
	Loopback wire;
	auto aTransport = std::make_unique<LoopbackTransport>(wire.aToB, wire.bToA);
	auto* aRaw = aTransport.get();
	NetConnection a(std::move(aTransport));
	NetConnection b(std::make_unique<LoopbackTransport>(wire.bToA, wire.aToB));
	a.openConnection("loopback", 0);
	b.openConnection("loopback", 0);

	auto bundle = std::make_shared<Turn::TurnBundle>();
	bundle->fromTick = 100;
	bundle->horizonTick = 102;
	bundle->entries.push_back({100, 2, {20, 1, 2, 3}});
	bundle->entries.push_back({101, 0, {72, 9, 9}});
	a.sendMessage(bundle);
	auto hello = std::make_shared<Turn::Hello>();
	hello->ticket = "ticket";
	hello->haveHorizon = 7;
	a.sendMessage(hello);

	// The frame is NetConnection's u16 length prefix around TurnCodec's payload.
	const auto payload = Turn::TurnCodec::encode(*bundle);
	REQUIRE(aRaw->sent.size() >= payload.size() + 2);
	CHECK(aRaw->sent[0] == (payload.size() >> 8));
	CHECK(aRaw->sent[1] == (payload.size() & 0xFF));
	CHECK(std::vector<uint8_t>(aRaw->sent.begin() + 2, aRaw->sent.begin() + 2 + payload.size()) == payload);

	auto first = b.getMessage();
	REQUIRE(first);
	CHECK(first->getMessageType() == Turn::MSG_TURN_BUNDLE);
	CHECK(*first == *bundle);
	auto second = b.getMessage();
	REQUIRE(second);
	CHECK(*second == *hello);
	CHECK(b.isConnected());

	// A malformed turn frame closes the connection, as for any other message.
	std::vector<uint8_t> bad = {0, 2, Turn::MSG_QUIT, 9};
	wire.aToB.push_back(bad);
	CHECK_FALSE(b.getMessage());
	CHECK_FALSE(b.isConnected());
}
