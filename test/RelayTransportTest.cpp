// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// The client's relay connection: turn frames as a length-prefixed byte stream over a
// binary WebSocket, split and coalesced arbitrarily, and the relay URLs it accepts.

#include "Glob2Test.h"

#include <deque>
#include <memory>

#include "NetTransport.h"
#include "RelayTransport.h"

namespace
{
struct FakeLink
{
	std::deque<std::vector<uint8_t>> incoming;
	std::vector<uint8_t> sent;
	std::string opened;
	NetTransport::State state = NetTransport::State::Connecting;
	int opens = 0;
	std::size_t pending = 0; ///< bytes the fake has not "written" yet
	bool closed = false;
};

class FakeTransport : public NetTransport
{
public:
	explicit FakeTransport(FakeLink& link) : link(link) {}
	void open(const std::string& endpoint, uint16_t) override
	{
		NetEndpoint::parse(endpoint);
		link.opened = endpoint;
		++link.opens;
	}
	void close() override
	{
		link.state = State::Closed;
		link.closed = true;
	}
	State state() const override { return link.state; }
	size_t pendingOutgoing() const override { return link.pending; }
	bool send(std::vector<uint8_t> bytes) override
	{
		link.sent.insert(link.sent.end(), bytes.begin(), bytes.end());
		return true;
	}
	bool receive(std::vector<uint8_t>& bytes) override
	{
		if (link.incoming.empty())
			return false;
		bytes = std::move(link.incoming.front());
		link.incoming.pop_front();
		return true;
	}

private:
	FakeLink& link;
};
}

TEST_SUITE("RelayTransport")
{
	TEST_CASE("frames split across and coalesced within messages arrive whole")
	{
		Online::RelayFrameReader reader;
		const std::vector<uint8_t> a = {1, 2, 3}, b(300, 7), c = {};
		std::vector<uint8_t> stream;
		for (const auto* payload : {&a, &b, &c})
		{
			const auto frame = Online::relayFrame(*payload);
			stream.insert(stream.end(), frame.begin(), frame.end());
		}
		std::vector<uint8_t> out;
		// One byte at a time.
		for (auto byte : stream)
			reader.append(&byte, 1);
		REQUIRE(reader.next(out));
		CHECK(out == a);
		REQUIRE(reader.next(out));
		CHECK(out == b);
		REQUIRE(reader.next(out));
		CHECK(out.empty());
		CHECK_FALSE(reader.next(out));
		// All at once.
		reader.append(stream.data(), stream.size());
		REQUIRE(reader.next(out));
		CHECK(out == a);
		REQUIRE(reader.next(out));
		CHECK(out == b);
		CHECK(Online::relayFrame(std::vector<uint8_t>(70000)).empty());
	}

	TEST_CASE("the transport frames what it sends and reconnects on demand")
	{
		FakeLink link;
		Online::RelayTransport transport("wss://play.example.org/relay/relay-1",
		                                 [&] { return std::make_unique<FakeTransport>(link); });
		CHECK(transport.state() == Turn::TurnTransport::State::Disconnected);
		transport.connect();
		CHECK(link.opened == "wss://play.example.org/relay/relay-1");
		CHECK(transport.state() == Turn::TurnTransport::State::Connecting);
		CHECK_FALSE(transport.send({9}));
		link.state = NetTransport::State::Connected;
		CHECK(transport.state() == Turn::TurnTransport::State::Connected);
		CHECK(transport.send({9, 8}));
		CHECK(link.sent == std::vector<uint8_t>{0, 2, 9, 8});
		link.incoming.push_back({0, 3, 1, 2});
		std::vector<uint8_t> out;
		CHECK_FALSE(transport.receive(out));
		link.incoming.push_back({3, 0, 1, 5});
		REQUIRE(transport.receive(out));
		CHECK(out == std::vector<uint8_t>{1, 2, 3});
		REQUIRE(transport.receive(out));
		CHECK(out == std::vector<uint8_t>{5});
		link.state = NetTransport::State::Closed;
		CHECK(transport.state() == Turn::TurnTransport::State::Disconnected);
		link.state = NetTransport::State::Connecting;
		transport.connect();
		CHECK(link.opens == 2);
	}

	TEST_CASE("a transport destroyed with frames still queued writes them before closing")
	{
		FakeLink link;
		{
			Online::RelayTransport transport("wss://play.example.org/relay/relay-1",
			                                 [&] { return std::make_unique<FakeTransport>(link); });
			transport.connect();
			link.state = NetTransport::State::Connected;
			CHECK(transport.send({1}));
			link.pending = 3; // the Quit is still in the socket's queue
		}
		CHECK_FALSE(link.closed);
		CHECK(Online::lingeringRelayConnections() == 1);
		Online::pumpLingeringRelayConnections();
		CHECK_FALSE(link.closed);
		link.pending = 0;
		CHECK(Online::lingeringRelayConnections() == 0);
		CHECK(link.closed);

		// Nothing queued: it closes at once.
		FakeLink idle;
		{
			Online::RelayTransport transport("wss://play.example.org/relay/relay-1",
			                                 [&] { return std::make_unique<FakeTransport>(idle); });
			transport.connect();
			idle.state = NetTransport::State::Connected;
		}
		CHECK(idle.closed);
		CHECK(Online::lingeringRelayConnections() == 0);
	}

	TEST_CASE("relay URLs are accepted WebSocket endpoints")
	{
		CHECK(NetEndpoint::parse("wss://play.example.org/relay/abc-123").route == "/relay/abc-123");
		CHECK(NetEndpoint::parse("wss://127.0.0.1:7495/relay").route == "/relay");
		CHECK_THROWS(NetEndpoint::parse("wss://play.example.org/relay/"));
		CHECK_THROWS(NetEndpoint::parse("wss://play.example.org/relay/Upper"));
		CHECK_THROWS(NetEndpoint::parse("wss://play.example.org/relay/a.b"));
		CHECK_THROWS(NetEndpoint::parse("wss://play.example.org/relay/a/b"));
		CHECK_THROWS(NetEndpoint::parse("wss://play.example.org/other"));
	}
}
