// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayMetrics.h"

#include <sstream>

namespace Relay
{
void RelayMetrics::ticketRejected(const std::string& reason)
{
	std::lock_guard<std::mutex> lock(labelled);
	++rejectedTickets[reason];
}

void RelayMetrics::matchEnded(const std::string& reason)
{
	std::lock_guard<std::mutex> lock(labelled);
	++endedMatches[reason];
}

void RelayMetrics::matchNetwork(const Turn::SequencerTelemetry& telemetry)
{
	std::lock_guard<std::mutex> lock(labelled);
	network.add(telemetry);
}

std::string RelayMetrics::render() const
{
	std::ostringstream out;
	auto metric = [&](const char* name, const char* type, const char* help, auto value) {
		out << "# HELP " << name << ' ' << help << "\n# TYPE " << name << ' ' << type << '\n'
		    << name << ' ' << value << '\n';
	};
	metric("glob2_relay_connections", "gauge", "Open match WebSocket connections.", connections.load());
	metric("glob2_relay_matches", "gauge", "Matches in progress.", matches.load());
	metric("glob2_relay_draining", "gauge", "1 while the relay drains before exit.", draining.load() ? 1 : 0);
	metric("glob2_relay_registered", "gauge", "1 once the platform accepted the registration.", registered.load() ? 1 : 0);
	metric("glob2_relay_pending_uploads", "gauge", "Match records waiting for upload.", pendingUploads.load());
	metric("glob2_relay_jwks_keys", "gauge", "Ed25519 ticket keys currently trusted.", jwksKeys.load());
	metric("glob2_relay_connections_accepted_total", "counter", "WebSocket connections accepted.", connectionsAccepted.load());
	metric("glob2_relay_connections_refused_total", "counter",
	       "Upgrades refused by limits, origin, route or drain.", connectionsRefused.load());
	metric("glob2_relay_frames_in_total", "counter", "Turn frames received.", framesIn.load());
	metric("glob2_relay_frames_out_total", "counter", "Turn frames sent.", framesOut.load());
	metric("glob2_relay_bytes_in_total", "counter", "Turn frame bytes received.", bytesIn.load());
	metric("glob2_relay_bytes_out_total", "counter", "Turn frame bytes sent.", bytesOut.load());
	metric("glob2_relay_slow_readers_dropped_total", "counter",
	       "Connections closed because their send queue overflowed.", slowReadersDropped.load());
	metric("glob2_relay_flooding_dropped_total", "counter", "Connections closed for exceeding the frame rate.",
	       floodingDropped.load());
	metric("glob2_relay_matches_started_total", "counter", "Matches created.", matchesStarted.load());
	metric("glob2_relay_orders_sequenced_total", "counter", "Orders sequenced in finished matches.", ordersSequenced.load());
	metric("glob2_relay_bundles_sent_total", "counter", "Turn bundles sent in finished matches.", bundlesSent.load());
	metric("glob2_relay_jwks_refresh_ok_total", "counter", "Successful JWKS loads.", jwksRefreshOk.load());
	metric("glob2_relay_jwks_refresh_failed_total", "counter", "Failed JWKS loads.", jwksRefreshFailed.load());
	metric("glob2_relay_registrations_ok_total", "counter", "Accepted platform registrations.", registrationsOk.load());
	metric("glob2_relay_registrations_failed_total", "counter", "Failed platform registrations.",
	       registrationsFailed.load());
	metric("glob2_relay_heartbeats_ok_total", "counter", "Accepted heartbeats.", heartbeatsOk.load());
	metric("glob2_relay_heartbeats_failed_total", "counter", "Failed heartbeats.", heartbeatsFailed.load());
	metric("glob2_relay_uploads_ok_total", "counter", "Match records uploaded and reported.", uploadsOk.load());
	metric("glob2_relay_uploads_failed_total", "counter", "Match record upload attempts that failed.",
	       uploadsFailed.load());
	std::lock_guard<std::mutex> lock(labelled);
	out << "# HELP glob2_relay_tickets_rejected_total Tickets refused, by reason.\n"
	    << "# TYPE glob2_relay_tickets_rejected_total counter\n";
	for (const auto& r : rejectedTickets)
		out << "glob2_relay_tickets_rejected_total{reason=\"" << r.first << "\"} " << r.second << '\n';
	out << "# HELP glob2_relay_matches_ended_total Matches ended, by reason.\n"
	    << "# TYPE glob2_relay_matches_ended_total counter\n";
	for (const auto& r : endedMatches)
		out << "glob2_relay_matches_ended_total{reason=\"" << r.first << "\"} " << r.second << '\n';
	network.writePrometheus(out);
	return out.str();
}
}
