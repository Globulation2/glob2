// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// The client's relay connection: turn frames as a length-prefixed byte stream over a
// binary WebSocket, split and coalesced arbitrarily, and the relay URLs it accepts.

#include "Glob2Test.h"

#include <algorithm>
#include <deque>
#include <memory>

#include "NetFrame.h"
#include "NetTransport.h"
#include "RelayTransport.h"
#include "TurnMessages.h"

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
	int polls = 0; ///< state() calls: WssTransport runs its I/O there
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
	State state() const override
	{
		++link.polls;
		return link.state;
	}
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
		// flush() runs the link's pending I/O now and leaves received data queued.
		const int polls = link.polls;
		link.incoming.push_back({0, 1, 4});
		transport.flush();
		CHECK(link.polls == polls + 1);
		CHECK(link.incoming.size() == 1);
		link.incoming.clear();
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

		// After a Quit the relay closes the connection; it stays open (and is read)
		// until then, even once everything is written.
		FakeLink quitting;
		{
			Online::RelayTransport transport("wss://play.example.org/relay/relay-1",
			                                 [&] { return std::make_unique<FakeTransport>(quitting); });
			transport.connect();
			quitting.state = NetTransport::State::Connected;
			CHECK(transport.send(Turn::TurnCodec::encode(Turn::Quit())));
		}
		quitting.incoming.push_back({0, 1, 7}); // a bundle still on its way
		CHECK(Online::lingeringRelayConnections() == 1);
		CHECK(quitting.incoming.empty());
		CHECK_FALSE(quitting.closed);
		quitting.state = NetTransport::State::Closed; // the relay closed it
		CHECK(Online::lingeringRelayConnections() == 0);

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

TEST_SUITE("NetFrame")
{
	TEST_CASE("the reader refuses bytes beyond its limit and keeps what it had")
	{
		NetFrame::Reader reader;
		const std::vector<uint8_t> frame = NetFrame::encode(std::vector<uint8_t>(10, 3));
		REQUIRE(frame.size() == 12);
		CHECK(reader.append(frame.data(), 5, 12));
		CHECK_FALSE(reader.append(frame.data() + 5, 8, 12)); // 5 + 8 > 12
		CHECK(reader.buffered() == 5);
		CHECK(reader.append(frame.data() + 5, 7, 12));
		std::vector<uint8_t> out;
		REQUIRE(reader.next(out));
		CHECK(out == std::vector<uint8_t>(10, 3));
		CHECK(reader.buffered() == 0);
	}

	TEST_CASE("the reader hands out frames in place and stays bounded over a long stream")
	{
		NetFrame::Reader reader;
		std::vector<uint8_t> stream;
		for (int i = 0; i < 5000; ++i)
		{
			const std::vector<uint8_t> payload(1 + i % 200, static_cast<uint8_t>(i));
			REQUIRE(NetFrame::append(stream, payload.data(), payload.size()));
		}
		std::size_t frames = 0, peak = 0;
		for (std::size_t at = 0; at < stream.size(); at += 997)
		{
			const std::size_t chunk = std::min<std::size_t>(997, stream.size() - at);
			REQUIRE(reader.append(stream.data() + at, chunk, NetTransport::queueLimit));
			const uint8_t* data = nullptr;
			std::size_t size = 0;
			while (reader.next(data, size))
			{
				CHECK(size == 1 + frames % 200);
				CHECK(data[0] == static_cast<uint8_t>(frames));
				++frames;
			}
			peak = std::max(peak, reader.buffered());
		}
		CHECK(frames == 5000);
		CHECK(peak < 2 * 202);
		CHECK(reader.buffered() == 0);
	}

	TEST_CASE("frames carry at most 65535 bytes")
	{
		std::vector<uint8_t> out;
		CHECK(NetFrame::append(out, std::vector<uint8_t>(65535).data(), 65535));
		CHECK(out.size() == 65537);
		CHECK(out[0] == 0xFF);
		CHECK(out[1] == 0xFF);
		CHECK_FALSE(NetFrame::append(out, std::vector<uint8_t>(65536).data(), 65536));
		CHECK(out.size() == 65537);
		CHECK(NetFrame::encode(std::vector<uint8_t>(65536)).empty());
	}
}
