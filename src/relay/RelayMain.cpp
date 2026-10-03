// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// glob2-relay: the match relay of the online platform (docs/multiplayer/relay.md).

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

#include "HttpClient.h"
#include "JwksStore.h"
#include "PlatformLink.h"
#include "RelayConfig.h"
#include "RelayLog.h"
#include "RelayMetrics.h"
#include "RelayServer.h"
#include "TurnProtocol.h"

#include <boost/asio.hpp>

#include <csignal>
#include <cstring>
#include <iostream>

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

namespace asio = boost::asio;

namespace
{
	const char* usage =
		"Usage: glob2-relay [--help | --version]\n"
		"\n"
		"The Globulation 2 match relay. Configuration comes from GLOB2_RELAY_*\n"
		"environment variables; see docs/multiplayer/relay.md. At least one of\n"
		"GLOB2_RELAY_JWKS_FILE, GLOB2_RELAY_JWKS_URL or GLOB2_RELAY_PLATFORM_URL is\n"
		"required. SIGTERM or SIGINT drains (no new matches; running matches finish);\n"
		"a second signal aborts the remaining matches and exits.\n";
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h"))
		{
			std::cout << usage;
			return 0;
		}
		if (!std::strcmp(argv[i], "--version"))
		{
			std::cout << "glob2-relay " << PACKAGE_VERSION << " (turn protocol " << Turn::PROTOCOL_VERSION << ")\n";
			return 0;
		}
		std::cerr << usage;
		return 2;
	}
	std::signal(SIGPIPE, SIG_IGN);

	Relay::RelayConfig config;
	try
	{
		config = Relay::RelayConfig::fromEnvironment();
	}
	catch (const std::exception& e)
	{
		std::cerr << "glob2-relay: " << e.what() << '\n';
		return 2;
	}

	asio::io_context io(1);
	Relay::RelayMetrics metrics;
	std::unique_ptr<Relay::HttpClient> http;
	try
	{
		http = std::make_unique<Relay::HttpClient>(config.platformCaFile);
	}
	catch (const std::exception& e)
	{
		std::cerr << "glob2-relay: cannot set up HTTPS trust: " << e.what() << '\n';
		return 2;
	}
	Relay::JwksStore jwks(io.get_executor(), *http, metrics, std::chrono::seconds(config.jwksMinRefreshSeconds),
	                      std::chrono::seconds(config.jwksRefreshSeconds));
	try
	{
		if (!config.jwksFile.empty())
			jwks.useFile(config.jwksFile);
		else if (!config.jwksUrl.empty())
			jwks.useUrl(config.jwksUrl);
		else
			jwks.useUrl(config.platformUrl + "/.well-known/jwks.json");
	}
	catch (const std::exception& e)
	{
		std::cerr << "glob2-relay: " << e.what() << '\n';
		return 2;
	}

	Relay::RelayServer* serverPointer = nullptr;
	Relay::PlatformLink platform(io.get_executor(), config, *http, metrics, [&serverPointer]() {
		return serverPointer ? serverPointer->snapshot() : Relay::LoadSnapshot{};
	});
	platform.build = std::string("glob2-relay ") + PACKAGE_VERSION;
	if (config.jwksFile.empty() && config.jwksUrl.empty())
		platform.onJwksUrl = [&](const std::string& url) {
			if (url == jwks.url())
				return;
			jwks.useUrl(url);
			asio::co_spawn(io, [&jwks]() -> asio::awaitable<void> { co_await jwks.refresh(); }, asio::detached);
		};

	std::unique_ptr<Relay::RelayServer> server;
	std::uint16_t port = 0;
	try
	{
		server = std::make_unique<Relay::RelayServer>(io, config, metrics, jwks, platform);
		serverPointer = server.get();
		port = server->start();
	}
	catch (const std::exception& e)
	{
		std::cerr << "glob2-relay: cannot start: " << e.what() << '\n';
		return 1;
	}

	asio::signal_set signals(io, SIGINT, SIGTERM);
	int signalsSeen = 0;
	std::function<void(boost::system::error_code, int)> onSignal = [&](boost::system::error_code ec, int number) {
		if (ec)
			return;
		if (++signalsSeen == 1)
		{
			Relay::logLine("info", std::string("Signal ") + std::to_string(number) + ": draining");
			server->beginDrain();
			platform.poke();
		}
		else
		{
			Relay::logLine("info", "Second signal: aborting every match");
			server->abortAll();
		}
		signals.async_wait(onSignal);
	};
	signals.async_wait(onSignal);

	server->onDrained = [&]() {
		asio::co_spawn(
			io,
			[&]() -> asio::awaitable<void> {
				server->stopListening();
				// Uploads keep retrying for a bounded time; spooled records survive.
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
				platform.setShutdownDeadline(deadline);
				asio::steady_timer wait(io);
				while (platform.pendingUploads() > 0 && std::chrono::steady_clock::now() < deadline)
				{
					wait.expires_after(std::chrono::milliseconds(100));
					co_await wait.async_wait(asio::use_awaitable);
				}
				Relay::logLine("info", "Drained; exiting");
				jwks.stop();
				platform.stop();
				signals.cancel();
				io.stop();
			},
			asio::detached);
	};

	asio::co_spawn(io, jwks.run(), asio::detached);
	asio::co_spawn(io, platform.run(), asio::detached);
	platform.resubmitSpooled();

	Relay::logLine("info", std::string("glob2-relay ") + PACKAGE_VERSION + " listening on " + config.bindAddress + ":" +
	                           std::to_string(port) + config.route + (config.tlsEnabled() ? " (TLS)" : " (plain; terminate TLS in front)"));
	std::cout << "READY " << port << std::endl;
	io.run();
	return 0;
}
