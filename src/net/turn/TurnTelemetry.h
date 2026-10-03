// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Network and multiplayer telemetry for turn games (docs/development/network-telemetry.md).
// SessionTelemetry is the client side, owned by TurnSession; SequencerTelemetry is the
// relay side, owned by TurnSequencer (the online relay and the LAN host). Both are
// diagnostic only: nothing here enters saves, orders, RNG, checksums, match records or
// AI decisions, and collection never changes what the session sends or when.
//
// Collection follows the engine's telemetry contract: fixed-size counters and
// histograms updated at existing protocol events, no per-event allocation, no extra
// messages. The time series keeps one point per interval and is bounded.

#include <algorithm>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "TurnProtocol.h"

namespace Turn
{
	struct MatchRecord;

	/// Fixed-size log-linear histogram of non-negative integers. Values below 16 are
	/// exact; above, each power of two has 16 bins, so a quantile is within about 3%
	/// of a recorded value. Quantiles report the bin midpoint clamped to [min, max].
	class Histogram
	{
	public:
		void add(std::uint64_t value);
		void merge(const Histogram& other);
		void clear() { *this = Histogram(); }
		std::uint64_t count() const { return n; }
		std::uint64_t total() const { return sum; }
		std::uint64_t min() const { return n ? lo : 0; }
		std::uint64_t max() const { return hi; }
		double mean() const { return n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0; }
		/// The q-quantile (0 <= q <= 1); 0 when empty.
		std::uint64_t quantile(double q) const;

		static constexpr unsigned SUB_BITS = 4;
		static constexpr unsigned SUB = 1u << SUB_BITS;
		/// Values from 2^40 (about 12 days in microseconds) share the last bin.
		static constexpr unsigned MAX_EXPONENT = 40;
		static constexpr unsigned BINS = SUB + (MAX_EXPONENT - SUB_BITS) * SUB;
		static unsigned bin(std::uint64_t value);
		static std::uint64_t binLow(unsigned index);
		static std::uint64_t binWidth(unsigned index);

	private:
		std::array<std::uint32_t, BINS> bins{};
		std::uint64_t n = 0, sum = 0, lo = 0, hi = 0;
	};

	/// count/mean/p50/p95/max of a histogram, as a JSON object.
	nlohmann::json summarize(const Histogram& h);

	constexpr unsigned PRESENCE_STATES = PRESENCE_STATE_MAX + 1;

	/// Client-side measurements of one turn session (one match, across reconnects and
	/// reloads). Times are steady-clock microseconds from the session's own clock.
	class SessionTelemetry
	{
	public:
		/// Measurements that are also kept per interval for the time series.
		struct Counters
		{
			Histogram rttMicros;        ///< ping round trips (every pong)
			Histogram jitterMicros;     ///< the jitter estimate, sampled at every pong
			Histogram inputDelayMicros; ///< local order submitted -> executed locally
			Histogram bufferTicks;      ///< authorized but unexecuted ticks, per live tick
			Histogram targetTicks;      ///< jitter-buffer target, per live tick
			Histogram stallMicros;      ///< horizon starvation stalls
			std::uint64_t framesSent = 0, bytesSent = 0, framesReceived = 0, bytesReceived = 0;
			std::uint64_t bundlesReceived = 0, bundleBytes = 0, bundleEntries = 0;
			std::uint64_t ordersSubmitted = 0;   ///< accepted by addLocalOrder
			std::uint64_t orderFramesSent = 0;   ///< OrderSubmit frames, resends included
			std::uint64_t ordersResent = 0;      ///< resent after a reconnect
			std::uint64_t ordersQueuedOffline = 0; ///< submitted while the link was down
			std::uint64_t ordersDroppedLocal = 0;  ///< null, latency-adjust or oversized
			std::uint64_t ordersCoalesced = 0;     ///< replaced by a later order with the same target
			std::uint64_t ordersQueueDropped = 0;  ///< dropped because the local queue was full
			std::uint64_t voiceSent = 0, voiceSentBytes = 0, voiceReceived = 0, voiceReceivedBytes = 0;
			std::uint64_t ticksExecuted = 0;     ///< every executed tick
			std::uint64_t liveTicks = 0;         ///< executed outside catch-up
			std::uint64_t ticksFaster = 0, ticksSlower = 0; ///< live ticks with a rate nudge
			double nudgeSum = 0;                 ///< sum of (multiplier - 1) over live ticks
			double nudgeAbsMax = 0;
			std::uint64_t stallMicrosTotal = 0;
			std::uint64_t catchUps = 0, catchUpTicks = 0, catchUpMicros = 0;
			std::uint64_t reconnects = 0, downtimeMicros = 0;
			std::uint64_t reloads = 0;
			std::uint64_t desyncRejoins = 0, desyncFlags = 0;
			std::uint64_t resyncRequests = 0;
			std::uint64_t presenceTransitions = 0;
			void merge(const Counters& other);
		};

		/// count, mean, p50, p95 and max of one interval's histogram.
		struct Summary
		{
			std::uint64_t count = 0, p50 = 0, p95 = 0, max = 0;
			double mean = 0;
			static Summary of(const Histogram& h);
		};
		/// One closed interval of the time series (compact: no histograms).
		struct Point
		{
			std::uint64_t startMicros = 0, endMicros = 0; ///< session-relative
			std::uint32_t startTick = 0, endTick = 0;     ///< executed tick
			Summary rtt, jitter, inputDelay, buffer, target, stall;
			std::uint64_t bytesSent = 0, bytesReceived = 0, framesSent = 0, framesReceived = 0;
			std::uint64_t ticksExecuted = 0, liveTicks = 0, ticksFaster = 0, ticksSlower = 0;
			double meanNudge = 0;
			std::uint64_t catchUpTicks = 0, reconnects = 0, downtimeMicros = 0, ordersSubmitted = 0, voiceSent = 0,
			              voiceReceived = 0, presenceTransitions = 0;
		};

		static constexpr std::uint64_t DEFAULT_INTERVAL_MICROS = 5000000;
		static constexpr std::size_t MAX_POINTS = 4320; ///< six hours at 5 s

		std::uint64_t intervalMicros = DEFAULT_INTERVAL_MICROS;

		// --- Hooks called by TurnSession at existing events ---
		void start(std::uint64_t nowMicros);
		void frameSent(std::size_t bytes);
		void frameReceived(std::size_t bytes);
		void bundle(std::size_t bytes, std::size_t entries);
		void voiceReceived(std::size_t bytes);
		void pong(std::uint64_t rttMicros, std::int64_t jitterMicros);
		/// A local order accepted by addLocalOrder; `sentNow` false while the link is down.
		void orderSubmitted(std::uint8_t type, std::size_t bytes, bool sentNow);
		/// Remembers a submission for the input-delay match (voice is not matched).
		void pendingInput(const std::uint8_t* bytes, std::size_t size, std::uint32_t executedTick, std::uint64_t nowMicros);
		/// Orders submitted but not yet acknowledged by the relay.
		void outstandingDepth(std::size_t depth);
		void orderDropped() { ++window.ordersDroppedLocal; ++all.ordersDroppedLocal; }
		/// A queued order was replaced by a later one with the same target (latest wins):
		/// the replaced one never executes, so it leaves the input-delay match.
		void orderCoalesced(const std::uint8_t* bytes, std::size_t size);
		/// The local order queue was full and an order was dropped.
		void orderQueueDropped() { ++window.ordersQueueDropped; ++all.ordersQueueDropped; }
		/// Orders waiting in the local queue for the pacing budget.
		void queuedDepth(std::size_t depth) { queuedPeak = std::max<std::uint64_t>(queuedPeak, depth); }
		void orderFrameSent(bool resend);
		/// The local seat's own order executed; matched FIFO against submissions.
		void ownOrderExecuted(const std::uint8_t* bytes, std::size_t size, std::uint32_t tick, std::uint64_t nowMicros);
		/// Called by tickReady(): `starved` when the horizon is exhausted.
		void readiness(bool starved, bool ready, std::uint64_t nowMicros);
		void tickExecuted(std::uint32_t buffered, std::uint32_t target, double rateMultiplier, bool catchingUp,
		                  std::uint32_t executedTick, std::uint64_t nowMicros);
		void catchUpState(bool catchingUp, std::uint32_t executedTick, std::uint64_t nowMicros);
		void linkLost(std::uint64_t nowMicros);
		void welcomed(std::uint64_t nowMicros);
		void reloadRequested();
		void desync(bool flagged);
		void resyncRequested() { ++window.resyncRequests; ++all.resyncRequests; }
		void presence(unsigned seat, PresenceState state, std::uint64_t nowMicros);
		/// Called once per frame: closes the interval when it is due.
		void update(std::uint32_t executedTick, std::uint64_t nowMicros);
		/// The engine's own reload (initial state load) time, measured by the engine.
		void reloadLoad(std::uint64_t micros) { reloadLoadMicros += micros; }

		// --- Readouts ---
		bool started() const { return startMicros != 0 || begun; }
		const Counters& totals() const { return all; }
		const Counters& current() const { return window; }
		const std::vector<Point>& series() const { return points; }
		std::uint64_t droppedPoints() const { return dropped; }
		std::uint64_t longestStallMicros() const { return all.stallMicros.max(); }
		std::uint64_t longestDowntimeMicros() const { return downtimeMax; }
		std::uint64_t reloadLoadTotalMicros() const { return reloadLoadMicros; }
		std::uint64_t reloadFastForwardTicks() const { return reloadFfTicks; }
		std::uint64_t reloadFastForwardMicros() const { return reloadFfMicros; }
		std::uint64_t outstandingMax() const { return outstandingPeak; }
		std::uint64_t queuedMax() const { return queuedPeak; }
		std::uint64_t unmatchedInputDelay() const { return pendingInputs.size(); }
		std::uint64_t elapsedMicros(std::uint64_t nowMicros) const { return nowMicros > startMicros ? nowMicros - startMicros : 0; }
		/// Transitions into each presence state, per seat.
		std::uint32_t transitions(unsigned seat, PresenceState state) const;
		/// Time spent in each presence state, per seat (closed spans plus the current one).
		std::uint64_t timeIn(unsigned seat, PresenceState state, std::uint64_t nowMicros) const;
		std::uint32_t seenSeats() const { return seatsSeen; }

		/// The whole session as JSON (schema_version 1): totals, per-seat presence and
		/// the time series. `nowMicros` closes open spans for the report only.
		nlohmann::json toJson(std::uint64_t nowMicros, bool includeSeries = true) const;
		/// One key=value record body (no record name) for a Counters set.
		static void writeFields(std::ostream& out, const Counters& c);

	private:
		void closeStall(std::uint64_t nowMicros);
		void closeInterval(std::uint32_t executedTick, std::uint64_t nowMicros);
		struct PendingInput
		{
			std::uint64_t at;
			std::uint32_t tick;
			std::uint32_t hash;
			std::uint32_t size;
		};

		bool begun = false;
		std::uint64_t startMicros = 0;
		Counters window, all;
		std::vector<Point> points;
		std::uint64_t dropped = 0;
		std::uint64_t windowStart = 0;
		std::uint32_t windowTick = 0;

		// Input delay: FIFO of submissions not yet seen executing (bounded).
		static constexpr std::size_t MAX_PENDING_INPUTS = 1024;
		std::vector<PendingInput> pendingInputs;
		std::uint64_t outstandingPeak = 0;
		std::uint64_t queuedPeak = 0;

		bool stalled = false;
		bool everReady = false;
		std::uint64_t stallStart = 0;

		bool catching = false;
		bool catchFromReload = false;
		std::uint64_t catchStart = 0;
		std::uint64_t catchStartTicks = 0;
		bool reloadSeen = false;
		std::uint64_t reloadFfTicks = 0, reloadFfMicros = 0, reloadLoadMicros = 0;

		bool linkDown = false;
		bool welcomedOnce = false;
		std::uint64_t linkDownAt = 0;
		std::uint64_t downtimeMax = 0;

		std::array<PresenceState, MAX_SEATS> seatState{};
		std::array<std::uint64_t, MAX_SEATS> seatSince{};
		std::array<std::array<std::uint32_t, PRESENCE_STATES>, MAX_SEATS> seatTransitions{};
		std::array<std::array<std::uint64_t, PRESENCE_STATES>, MAX_SEATS> seatTime{};
		std::uint32_t seatsSeen = 0;
	};

	/// Identifies the match a ClientNetworkSummary describes. Deliberately no account
	/// ids, addresses, names or free text: only what classifies the connection.
	struct ClientNetworkContext
	{
		std::string simVersion;   ///< MatchSetup.simVersion key
		std::string platform;     ///< SDL platform name ("Linux", "Windows", "Emscripten", ...)
		std::string transport;    ///< "lan" or "online"
		std::string relayId;      ///< online relay id; empty on LAN or when unknown
		std::string relayRegion;  ///< online relay region; empty when unknown
		int seat = -1;            ///< the local seat
		std::uint32_t humanSeatMask = 0;
		int players = 0;
		std::uint32_t tickRateMilliHz = DEFAULT_TICK_RATE_MILLIHZ;
		std::uint32_t finalTick = 0;
		/// Verdicts of the deterministic order check, per human seat, when the engine
		/// validates remote orders (OrderValidation's orderAudit()). Without it the
		/// summary reports "order_validation": null.
		struct SeatVerdicts
		{
			int seat = 0;
			std::uint32_t accepted = 0, stale = 0, rejected = 0, voiceRejected = 0;
			std::vector<std::pair<std::string, std::uint32_t>> reasons; ///< nonzero only
		};
		bool orderValidationAvailable = false;
		std::vector<SeatVerdicts> orderValidation;
	};

	/// The client's per-match network summary: one stable, versioned JSON object
	/// (schema "ClientNetworkSummary", schema_version 1; field list in
	/// docs/development/network-telemetry.md). Written with the standard telemetry
	/// outputs; nothing sends it anywhere.
	nlohmann::json clientNetworkSummary(const SessionTelemetry& telemetry, const ClientNetworkContext& context,
	                                    std::uint64_t nowMicros, bool includeSeries = true);
	constexpr int CLIENT_NETWORK_SUMMARY_VERSION = 1;

	/// Relay-side measurements for one match (TurnSequencer). Ticks are relay ticks.
	struct SequencerTelemetry
	{
		struct Seat
		{
			std::uint64_t ordersSequenced = 0, orderBytes = 0;
			std::uint64_t voiceSequenced = 0, voiceBytes = 0;
			std::uint64_t ordersDeferred = 0;  ///< assigned later than the next tick
			Histogram deferTicks;              ///< ticks added by one-per-tick / byte budget
			std::uint64_t duplicatesIgnored = 0; ///< resubmissions already sequenced
			std::uint64_t ordersDropped = 0;   ///< null/latency/bad quit order
			std::uint64_t floodRejections = 0;
			std::uint64_t framesReceived = 0, bytesReceived = 0;
			std::uint64_t bundlesSent = 0, bundleBytes = 0;       ///< live bundles to this seat
			std::uint64_t logBundlesSent = 0, logBundleBytes = 0; ///< log replays (resume/rejoin)
			std::uint64_t checksumReports = 0;
			Histogram reportLatenessTicks; ///< relay tick at arrival - reported tick
			Histogram lagTicks;            ///< relay tick - client executed tick, at each ping
			/// Transport round trips the host measured itself (the online relay's WebSocket
			/// ping, TurnSequencer::transportRoundTrip); empty where it measures none (LAN).
			Histogram rttMicros;
			std::uint64_t pings = 0;
			std::uint64_t connects = 0, disconnects = 0;
			std::uint64_t graceMicrosTotal = 0, graceMicrosMax = 0;
			bool leftByGrace = false, leftByQuit = false;
			std::int64_t leftTick = -1;
			std::uint64_t toldToRejoin = 0, flagged = 0, lateMismatches = 0;
			std::uint64_t maxQueuedAhead = 0; ///< ticks between the relay clock and the seat's next free tick
			std::uint64_t graceSince = 0;     ///< internal: start of the current absence
			bool absent = false;              ///< internal
		};
		/// Indexed by seat, sized to the highest human seat + 1.
		std::vector<Seat> seats;
		std::uint64_t bundlesBroadcast = 0;   ///< distinct live bundles
		std::uint64_t bundleBytesBroadcast = 0;
		std::uint64_t arbitrations = 0, unanimous = 0, majority = 0, flaggedTicks = 0, timedOut = 0;
		std::uint64_t peakPendingTicks = 0, peakPendingEntries = 0, peakPendingBytes = 0;
		std::uint64_t rejectedPeers = 0;
		/// The seat's record, or null for a seat outside the table.
		Seat* seat(int index) { return index >= 0 && static_cast<std::size_t>(index) < seats.size() ? &seats[index] : nullptr; }
	};

	/// Per-seat network summary of a match from the relay's telemetry (schema_version 1):
	/// what the relay reports at match end and the LAN host writes next to its record.
	nlohmann::json sequencerSummaryJson(const SequencerTelemetry& t, std::uint32_t humanSeatMask,
	                                    std::uint32_t tickRateMilliHz, std::uint32_t endTick);

	/// Aggregates over every match a relay process ran, for its Prometheus /metrics.
	struct RelayNetworkTotals
	{
		std::uint64_t ordersSequenced = 0, ordersDeferred = 0, deferTicks = 0, ordersDropped = 0;
		std::uint64_t voiceSequenced = 0;
		std::uint64_t bundles = 0, bundleBytes = 0, logBundles = 0, logBundleBytes = 0;
		std::uint64_t checksumReports = 0, arbitrations = 0, unanimous = 0, majority = 0, flagged = 0, timedOut = 0;
		std::uint64_t disconnects = 0, graceExpiries = 0, graceMicros = 0, rejoins = 0;
		std::uint64_t peakPendingBytes = 0;
		Histogram lagTicks, reportLatenessTicks, deferTicksHistogram, rttMicros;
		void add(const SequencerTelemetry& match);
		/// Prometheus text exposition (glob2_relay_net_* metrics).
		void writePrometheus(std::ostream& out) const;
	};

	/// Per-seat network facts that the match record itself proves (events and turns),
	/// for verify-match's result.json. No wall-clock values: deterministic.
	nlohmann::json recordNetworkSummary(const MatchRecord& record);
}
