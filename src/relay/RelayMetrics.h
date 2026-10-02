// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Process-wide relay counters, rendered in the Prometheus text format at /metrics.
// Updated from any thread; reads are relaxed snapshots.

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace Relay
{
	class RelayMetrics
	{
	public:
		using Counter = std::atomic<std::uint64_t>;

		Counter connectionsAccepted{0};
		Counter connectionsRefused{0};   ///< limits, origin, route or draining
		Counter framesIn{0};
		Counter framesOut{0};
		Counter bytesIn{0};
		Counter bytesOut{0};
		Counter slowReadersDropped{0};
		Counter floodingDropped{0};
		Counter matchesStarted{0};
		Counter ordersSequenced{0};
		Counter bundlesSent{0};
		Counter jwksRefreshOk{0};
		Counter jwksRefreshFailed{0};
		Counter heartbeatsOk{0};
		Counter heartbeatsFailed{0};
		Counter registrationsOk{0};
		Counter registrationsFailed{0};
		Counter uploadsOk{0};
		Counter uploadsFailed{0};
		std::atomic<std::int64_t> connections{0};
		std::atomic<std::int64_t> matches{0};
		std::atomic<std::int64_t> pendingUploads{0};
		std::atomic<std::int64_t> jwksKeys{0};
		std::atomic<bool> draining{false};
		std::atomic<bool> registered{false};

		/// Labelled counters: tickets rejected by reason, matches ended by reason.
		void ticketRejected(const std::string& reason);
		void matchEnded(const std::string& reason);

		std::string render() const;

	private:
		mutable std::mutex labelled;
		std::map<std::string, std::uint64_t> rejectedTickets;
		std::map<std::string, std::uint64_t> endedMatches;
	};
}
