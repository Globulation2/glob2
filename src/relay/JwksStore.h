// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay's trusted ticket keys. Either a static JWKS file (tests, LAN-less
// set-ups; re-read when a ticket names an unknown kid) or the platform's
// /.well-known/jwks.json, cached, refreshed periodically and refreshed early when a
// ticket names a kid the cache lacks (key rotation). Concurrent refreshes coalesce.
// Single-threaded: every call runs on the relay's event loop.

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "TicketVerifier.h"

namespace Relay
{
	class HttpClient;
	class RelayMetrics;

	class JwksStore
	{
	public:
		JwksStore(boost::asio::any_io_executor executor, HttpClient& http, RelayMetrics& metrics,
		          std::chrono::seconds minRefresh, std::chrono::seconds refreshEvery);

		/// Uses a static file. Throws if it cannot be read or parsed.
		void useFile(const std::string& path);
		/// Uses a URL; the first load happens in run() or on demand.
		void useUrl(const std::string& url);
		const std::string& url() const { return jwksUrl; }

		std::shared_ptr<const KeySet> current() const { return keys; }
		bool ready() const { return keys && keys->size() > 0; }

		/// Reloads unless a load happened less than minRefresh ago (or one is under
		/// way, which it then joins). Used when a ticket names an unknown kid.
		boost::asio::awaitable<void> refreshForUnknownKid(const std::string& kid);
		/// Loads now, or joins a load already under way.
		boost::asio::awaitable<bool> refresh();
		/// Periodic refresh loop for URL mode; returns after stop().
		boost::asio::awaitable<void> run();
		void stop();

	private:
		boost::asio::awaitable<bool> load();

		boost::asio::any_io_executor executor;
		HttpClient& http;
		RelayMetrics& metrics;
		std::chrono::seconds minRefresh;
		std::chrono::seconds refreshEvery;
		std::string file;
		std::string jwksUrl;
		std::shared_ptr<const KeySet> keys;
		bool loading = false;
		bool stopped = false;
		std::chrono::steady_clock::time_point lastAttempt{};
		std::vector<std::shared_ptr<boost::asio::steady_timer>> waiters;
		boost::asio::steady_timer periodic;
	};
}
