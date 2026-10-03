// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// The relay's side of the platform's /internal/v1 API (platform/packages/protocol/
// src/relay.ts): registration and heartbeat, the match setup lookup, and the upload
// of each finished match (record, then RelayMatchEnded). Every call carries the
// operator-configured relay key as a bearer token. Records are written to the spool
// directory first when one is configured, so a platform outage or a restart loses
// nothing; spooled matches are retried at start-up.

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MatchReport.h"
#include "RelayConfig.h"

namespace Relay
{
	class HttpClient;
	class RelayMetrics;

	struct LoadSnapshot
	{
		RelayLoad load;
		bool draining = false;
		std::vector<std::string> activeMatchIds;
	};

	struct FinishedMatch
	{
		std::string matchId;
		std::vector<std::uint8_t> record;
		std::string endedJson;
	};

	class PlatformLink
	{
	public:
		PlatformLink(boost::asio::any_io_executor executor, const RelayConfig& config, HttpClient& http,
		             RelayMetrics& metrics, std::function<LoadSnapshot()> snapshot);

		bool enabled() const { return !config.platformUrl.empty(); }
		/// Called with the jwksUrl from each accepted registration.
		std::function<void(const std::string&)> onJwksUrl;
		/// RelayRegistration.build.
		std::string build = "glob2-relay";

		/// Registration and heartbeat loop; returns after stop().
		boost::asio::awaitable<void> run();
		/// Sends a heartbeat now (e.g. when draining starts).
		void poke();
		void stop();

		/// GET /internal/v1/matches/{matchId}/setup: the MatchSetup document. A failed
		/// lookup (platform down, restarting, overloaded) is retried with backoff, 1 s
		/// doubling to 30 s, until it succeeds, giveUpAt or the shutdown deadline
		/// passes, the link stops, or wanted() (when given) turns false.
		boost::asio::awaitable<std::optional<std::string>> fetchSetup(
			const std::string& matchId, std::chrono::steady_clock::time_point giveUpAt,
			std::function<bool()> wanted = {});

		/// Spools (if configured) and uploads a finished match in the background.
		void submit(FinishedMatch match);
		/// Re-submits matches left in the spool directory by an earlier run.
		void resubmitSpooled();
		std::size_t pendingUploads() const { return pending; }
		/// While shutting down, uploads stop retrying after this many seconds.
		void setShutdownDeadline(std::chrono::steady_clock::time_point deadline) { shutdownDeadline = deadline; }

	private:
		boost::asio::awaitable<bool> registerRelay();
		boost::asio::awaitable<bool> heartbeat(bool& reregister);
		boost::asio::awaitable<void> upload(FinishedMatch match);
		boost::asio::awaitable<bool> uploadOnce(const FinishedMatch& match);
		boost::asio::awaitable<std::optional<std::string>> fetchSetupOnce(const std::string& matchId);
		std::string url(const std::string& path) const { return config.platformUrl + path; }
		std::map<std::string, std::string> authHeaders(const std::string& contentType) const;
		std::string spoolPath(const std::string& matchId, const char* suffix) const;

		boost::asio::any_io_executor executor;
		const RelayConfig& config;
		HttpClient& http;
		RelayMetrics& metrics;
		std::function<LoadSnapshot()> snapshot;
		boost::asio::steady_timer wake;
		std::chrono::seconds heartbeatInterval{15};
		std::size_t pending = 0;
		bool stopped = false;
		std::optional<std::chrono::steady_clock::time_point> shutdownDeadline;
	};
}
