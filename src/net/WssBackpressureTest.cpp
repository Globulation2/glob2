// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// A WebSocket reader that falls behind (a backgrounded phone app, a long reload) is
// not dropped: once its queue of unread messages is full, reading pauses and TCP
// holds the rest at the sender until the reader catches up.

#include "Glob2Test.h"

#include <chrono>
#include <thread>

#include "NetTransport.h"
#include "NetworkConfig.h"

TEST_SUITE("WssTransport")
{
	TEST_CASE("a reader that stops reading resumes with every message in order")
	{
		auto network = makeNetworkConfig(true);
		auto config = network.lobby;
		config.bindAddress = "127.0.0.1";
		config.port = 0;
		auto listener = makeNetTransportListener(config);
		const auto pin = network.lobbyEndpoint.substr(network.lobbyEndpoint.find('#'));
		auto client = makeNetTransport();
		client->open("wss://localhost:" + std::to_string(listener->localPort()) + config.route + pin);

		std::unique_ptr<NetTransport> server;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		auto pump = [&] {
			if (!server)
				server = listener->accept();
			client->state();
			if (server)
				server->state();
			// A one-millisecond sleep can round up to a scheduler tick on Windows.
			// Keep pumping ready I/O without consuming the test deadline in sleeps.
			std::this_thread::yield();
		};
		while ((!server || client->state() != NetTransport::State::Connected) && std::chrono::steady_clock::now() < deadline)
			pump();
		REQUIRE(server);
		REQUIRE(client->state() == NetTransport::State::Connected);

		// Far more messages than the reader keeps unread (4096), in batches the
		// sender's own queue accepts, while the client only polls and never takes one.
		constexpr int total = 10000;
		int sent = 0;
		while (sent < total && std::chrono::steady_clock::now() < deadline)
		{
			if (server->pendingOutgoing() < 256 * 1024)
				for (int i = 0; i < 200 && sent < total; ++i, ++sent)
					REQUIRE(server->send({std::uint8_t(sent >> 8), std::uint8_t(sent), 1, 2, 3, 4, 5, 6}));
			pump();
		}
		REQUIRE(sent == total);
		// Poll without consuming messages for the intended half-second pause.
		// Counting 500 sleeps made this phase depend on the OS timer quantum.
		const auto pausedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (std::chrono::steady_clock::now() < pausedUntil)
			pump();
		CHECK(client->state() == NetTransport::State::Connected);

		// Now it reads: everything arrives, in order, and the connection holds.
		int received = 0;
		std::vector<uint8_t> bytes;
		while (received < total && std::chrono::steady_clock::now() < deadline)
		{
			while (client->receive(bytes))
			{
				REQUIRE(bytes.size() == 8);
				CHECK(((bytes[0] << 8) | bytes[1]) == (received & 0xFFFF));
				++received;
			}
			pump();
		}
		CHECK(received == total);
		CHECK(client->state() == NetTransport::State::Connected);
		CHECK(client->error().empty());
	}
}
