// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnTelemetry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>

#include <nlohmann/json.hpp>

#include "MatchRecord.h"

using nlohmann::json;

namespace Turn
{
namespace
{
	unsigned bitLength(std::uint64_t v)
	{
		unsigned n = 0;
		while (v)
		{
			++n;
			v >>= 1;
		}
		return n;
	}

	std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size)
	{
		std::uint32_t h = 2166136261u;
		for (std::size_t i = 0; i < size; ++i)
			h = (h ^ data[i]) * 16777619u;
		return h;
	}

	const char* presenceName(unsigned state)
	{
		static const char* const names[PRESENCE_STATES] = {"not_connected", "connected", "lagging",
		                                                   "reconnecting", "resyncing", "left"};
		return state < PRESENCE_STATES ? names[state] : "unknown";
	}

	double round3(double v)
	{
		return std::round(v * 1000.0) / 1000.0;
	}

	json summaryJson(const SessionTelemetry::Summary& s)
	{
		return {{"count", s.count}, {"mean", round3(s.mean)}, {"p50", s.p50}, {"p95", s.p95}, {"max", s.max}};
	}
}

// --- Histogram ---

unsigned Histogram::bin(std::uint64_t value)
{
	if (value < SUB)
		return static_cast<unsigned>(value);
	const unsigned e = bitLength(value) - 1;
	if (e >= MAX_EXPONENT)
		return BINS - 1;
	const unsigned sub = static_cast<unsigned>((value >> (e - SUB_BITS)) & (SUB - 1));
	return SUB + (e - SUB_BITS) * SUB + sub;
}

std::uint64_t Histogram::binLow(unsigned index)
{
	if (index < SUB)
		return index;
	const unsigned k = index - SUB;
	const unsigned e = k / SUB + SUB_BITS;
	return (static_cast<std::uint64_t>(SUB + k % SUB)) << (e - SUB_BITS);
}

std::uint64_t Histogram::binWidth(unsigned index)
{
	if (index < SUB)
		return 1;
	const unsigned e = (index - SUB) / SUB + SUB_BITS;
	return std::uint64_t(1) << (e - SUB_BITS);
}

void Histogram::add(std::uint64_t value)
{
	++bins[bin(value)];
	lo = n ? std::min(lo, value) : value;
	hi = std::max(hi, value);
	++n;
	sum += value;
}

void Histogram::merge(const Histogram& o)
{
	if (!o.n)
		return;
	for (unsigned i = 0; i < BINS; ++i)
		bins[i] += o.bins[i];
	lo = n ? std::min(lo, o.lo) : o.lo;
	hi = std::max(hi, o.hi);
	n += o.n;
	sum += o.sum;
}

std::uint64_t Histogram::quantile(double q) const
{
	if (!n)
		return 0;
	q = std::clamp(q, 0.0, 1.0);
	std::uint64_t rank = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(n)));
	rank = std::clamp<std::uint64_t>(rank, 1, n);
	if (rank == 1)
		return lo;
	if (rank == n)
		return hi;
	std::uint64_t seen = 0;
	for (unsigned i = 0; i < BINS; ++i)
	{
		seen += bins[i];
		if (seen >= rank)
		{
			const std::uint64_t mid = binLow(i) + binWidth(i) / 2;
			return std::clamp(mid, lo, hi);
		}
	}
	return hi;
}

json summarize(const Histogram& h)
{
	return summaryJson(SessionTelemetry::Summary::of(h));
}

// --- Client session ---

SessionTelemetry::Summary SessionTelemetry::Summary::of(const Histogram& h)
{
	Summary s;
	s.count = h.count();
	s.mean = h.mean();
	s.p50 = h.quantile(0.5);
	s.p95 = h.quantile(0.95);
	s.max = h.max();
	return s;
}

void SessionTelemetry::Counters::merge(const Counters& o)
{
	rttMicros.merge(o.rttMicros);
	jitterMicros.merge(o.jitterMicros);
	inputDelayMicros.merge(o.inputDelayMicros);
	bufferTicks.merge(o.bufferTicks);
	targetTicks.merge(o.targetTicks);
	stallMicros.merge(o.stallMicros);
	framesSent += o.framesSent;
	bytesSent += o.bytesSent;
	framesReceived += o.framesReceived;
	bytesReceived += o.bytesReceived;
	bundlesReceived += o.bundlesReceived;
	bundleBytes += o.bundleBytes;
	bundleEntries += o.bundleEntries;
	ordersSubmitted += o.ordersSubmitted;
	orderFramesSent += o.orderFramesSent;
	ordersResent += o.ordersResent;
	ordersQueuedOffline += o.ordersQueuedOffline;
	ordersDroppedLocal += o.ordersDroppedLocal;
	ordersCoalesced += o.ordersCoalesced;
	ordersQueueDropped += o.ordersQueueDropped;
	voiceSent += o.voiceSent;
	voiceSentBytes += o.voiceSentBytes;
	voiceReceived += o.voiceReceived;
	voiceReceivedBytes += o.voiceReceivedBytes;
	ticksExecuted += o.ticksExecuted;
	liveTicks += o.liveTicks;
	ticksFaster += o.ticksFaster;
	ticksSlower += o.ticksSlower;
	nudgeSum += o.nudgeSum;
	nudgeAbsMax = std::max(nudgeAbsMax, o.nudgeAbsMax);
	stallMicrosTotal += o.stallMicrosTotal;
	catchUps += o.catchUps;
	catchUpTicks += o.catchUpTicks;
	catchUpMicros += o.catchUpMicros;
	reconnects += o.reconnects;
	downtimeMicros += o.downtimeMicros;
	reloads += o.reloads;
	desyncRejoins += o.desyncRejoins;
	desyncFlags += o.desyncFlags;
	resyncRequests += o.resyncRequests;
	presenceTransitions += o.presenceTransitions;
}

void SessionTelemetry::start(std::uint64_t nowMicros)
{
	if (begun)
		return;
	begun = true;
	startMicros = windowStart = nowMicros;
	seatState.fill(PresenceState::NotConnected);
	seatSince.fill(nowMicros);
}

void SessionTelemetry::frameSent(std::size_t bytes)
{
	for (Counters* c : {&window, &all})
	{
		++c->framesSent;
		c->bytesSent += bytes;
	}
}

void SessionTelemetry::frameReceived(std::size_t bytes)
{
	for (Counters* c : {&window, &all})
	{
		++c->framesReceived;
		c->bytesReceived += bytes;
	}
}

void SessionTelemetry::bundle(std::size_t bytes, std::size_t entries)
{
	for (Counters* c : {&window, &all})
	{
		++c->bundlesReceived;
		c->bundleBytes += bytes;
		c->bundleEntries += entries;
	}
}

void SessionTelemetry::voiceReceived(std::size_t bytes)
{
	for (Counters* c : {&window, &all})
	{
		++c->voiceReceived;
		c->voiceReceivedBytes += bytes;
	}
}

void SessionTelemetry::pong(std::uint64_t rtt, std::int64_t jitter)
{
	for (Counters* c : {&window, &all})
	{
		c->rttMicros.add(rtt);
		c->jitterMicros.add(jitter > 0 ? static_cast<std::uint64_t>(jitter) : 0);
	}
}

void SessionTelemetry::orderSubmitted(std::uint8_t type, std::size_t bytes, bool sentNow)
{
	for (Counters* c : {&window, &all})
	{
		++c->ordersSubmitted;
		if (!sentNow)
			++c->ordersQueuedOffline;
		if (type == ORDER_TYPE_VOICE)
		{
			++c->voiceSent;
			c->voiceSentBytes += bytes;
		}
	}
}

void SessionTelemetry::pendingInput(const std::uint8_t* bytes, std::size_t size, std::uint32_t executedTick,
                                    std::uint64_t nowMicros)
{
	if (!size || bytes[0] == ORDER_TYPE_VOICE)
		return;
	if (pendingInputs.size() >= MAX_PENDING_INPUTS)
		pendingInputs.erase(pendingInputs.begin());
	pendingInputs.push_back({nowMicros, executedTick, fnv1a(bytes, size), static_cast<std::uint32_t>(size)});
}

void SessionTelemetry::orderCoalesced(const std::uint8_t* bytes, std::size_t size)
{
	for (Counters* c : {&window, &all})
		++c->ordersCoalesced;
	if (!size)
		return;
	const std::uint32_t h = fnv1a(bytes, size);
	for (std::size_t i = pendingInputs.size(); i-- > 0;)
		if (pendingInputs[i].hash == h && pendingInputs[i].size == size)
		{
			pendingInputs.erase(pendingInputs.begin() + static_cast<std::ptrdiff_t>(i));
			return;
		}
}

void SessionTelemetry::outstandingDepth(std::size_t depth)
{
	outstandingPeak = std::max<std::uint64_t>(outstandingPeak, depth);
}

void SessionTelemetry::orderFrameSent(bool resend)
{
	for (Counters* c : {&window, &all})
	{
		++c->orderFramesSent;
		if (resend)
			++c->ordersResent;
	}
}

void SessionTelemetry::ownOrderExecuted(const std::uint8_t* bytes, std::size_t size, std::uint32_t tick,
                                        std::uint64_t nowMicros)
{
	if (!size || bytes[0] == ORDER_TYPE_VOICE || pendingInputs.empty())
		return;
	const std::uint32_t h = fnv1a(bytes, size);
	// The relay sequences a seat's orders in submission order, so the match is near the
	// front; an order the relay dropped is skipped. Orders the relay made for us (a
	// quit after the grace period) and replays of older ticks after a reload never match.
	const std::size_t limit = std::min<std::size_t>(pendingInputs.size(), 8);
	for (std::size_t i = 0; i < limit; ++i)
	{
		const PendingInput& p = pendingInputs[i];
		if (p.hash == h && p.size == size && tick >= p.tick)
		{
			const std::uint64_t delay = nowMicros > p.at ? nowMicros - p.at : 0;
			for (Counters* c : {&window, &all})
				c->inputDelayMicros.add(delay);
			pendingInputs.erase(pendingInputs.begin(), pendingInputs.begin() + static_cast<std::ptrdiff_t>(i) + 1);
			return;
		}
	}
}

void SessionTelemetry::closeStall(std::uint64_t nowMicros)
{
	const std::uint64_t d = nowMicros > stallStart ? nowMicros - stallStart : 0;
	for (Counters* c : {&window, &all})
	{
		c->stallMicros.add(d);
		c->stallMicrosTotal += d;
	}
	stalled = false;
}

void SessionTelemetry::readiness(bool starved, bool ready, std::uint64_t nowMicros)
{
	if (ready)
	{
		if (stalled)
			closeStall(nowMicros);
		everReady = true;
		return;
	}
	// The wait for every player to load, before the first tick, is not a stall.
	if (starved && everReady && !stalled)
	{
		stalled = true;
		stallStart = nowMicros;
	}
}

void SessionTelemetry::tickExecuted(std::uint32_t buffered, std::uint32_t target, double rateMultiplier,
                                    bool catchingUp, std::uint32_t, std::uint64_t)
{
	const double nudge = rateMultiplier - 1.0;
	for (Counters* c : {&window, &all})
	{
		++c->ticksExecuted;
		if (catchingUp)
		{
			++c->catchUpTicks;
			continue;
		}
		++c->liveTicks;
		c->bufferTicks.add(buffered);
		c->targetTicks.add(target);
		if (nudge > 1e-9)
			++c->ticksFaster;
		else if (nudge < -1e-9)
			++c->ticksSlower;
		c->nudgeSum += nudge;
		c->nudgeAbsMax = std::max(c->nudgeAbsMax, std::fabs(nudge));
	}
	if (catchingUp && catchFromReload)
		++reloadFfTicks;
}

void SessionTelemetry::catchUpState(bool catchingUp, std::uint32_t, std::uint64_t nowMicros)
{
	if (catchingUp == catching)
		return;
	catching = catchingUp;
	if (catchingUp)
	{
		catchStart = nowMicros;
		catchStartTicks = all.catchUpTicks;
		catchFromReload = reloadSeen;
		for (Counters* c : {&window, &all})
			++c->catchUps;
		return;
	}
	const std::uint64_t d = nowMicros > catchStart ? nowMicros - catchStart : 0;
	for (Counters* c : {&window, &all})
		c->catchUpMicros += d;
	if (catchFromReload)
	{
		reloadFfMicros += d;
		// A reload's fast-forward can start before the log arrives (an empty catch-up
		// that ends at once); the reload is done once an episode has replayed ticks.
		if (all.catchUpTicks > catchStartTicks)
			reloadSeen = false;
	}
	catchFromReload = false;
}

void SessionTelemetry::linkLost(std::uint64_t nowMicros)
{
	if (!welcomedOnce || linkDown)
		return;
	linkDown = true;
	linkDownAt = nowMicros;
	for (Counters* c : {&window, &all})
		++c->reconnects;
}

void SessionTelemetry::welcomed(std::uint64_t nowMicros)
{
	welcomedOnce = true;
	if (!linkDown)
		return;
	linkDown = false;
	const std::uint64_t d = nowMicros > linkDownAt ? nowMicros - linkDownAt : 0;
	for (Counters* c : {&window, &all})
		c->downtimeMicros += d;
	downtimeMax = std::max(downtimeMax, d);
}

void SessionTelemetry::reloadRequested()
{
	reloadSeen = true;
	for (Counters* c : {&window, &all})
		++c->reloads;
}

void SessionTelemetry::desync(bool flagged)
{
	for (Counters* c : {&window, &all})
		++(flagged ? c->desyncFlags : c->desyncRejoins);
}

void SessionTelemetry::presence(unsigned seat, PresenceState state, std::uint64_t nowMicros)
{
	if (seat >= MAX_SEATS || static_cast<unsigned>(state) >= PRESENCE_STATES)
		return;
	if (!begun)
		start(nowMicros);
	seatsSeen |= 1u << seat;
	if (seatState[seat] == state)
		return;
	const unsigned old = static_cast<unsigned>(seatState[seat]);
	seatTime[seat][old] += nowMicros > seatSince[seat] ? nowMicros - seatSince[seat] : 0;
	seatSince[seat] = nowMicros;
	seatState[seat] = state;
	++seatTransitions[seat][static_cast<unsigned>(state)];
	for (Counters* c : {&window, &all})
		++c->presenceTransitions;
}

std::uint32_t SessionTelemetry::transitions(unsigned seat, PresenceState state) const
{
	if (seat >= MAX_SEATS || static_cast<unsigned>(state) >= PRESENCE_STATES)
		return 0;
	return seatTransitions[seat][static_cast<unsigned>(state)];
}

std::uint64_t SessionTelemetry::timeIn(unsigned seat, PresenceState state, std::uint64_t nowMicros) const
{
	if (seat >= MAX_SEATS || static_cast<unsigned>(state) >= PRESENCE_STATES)
		return 0;
	std::uint64_t t = seatTime[seat][static_cast<unsigned>(state)];
	if (begun && seatState[seat] == state && nowMicros > seatSince[seat])
		t += nowMicros - seatSince[seat];
	return t;
}

void SessionTelemetry::update(std::uint32_t executedTick, std::uint64_t nowMicros)
{
	if (!begun)
	{
		start(nowMicros);
		windowTick = executedTick;
		return;
	}
	if (intervalMicros && nowMicros >= windowStart + intervalMicros)
		closeInterval(executedTick, nowMicros);
}

void SessionTelemetry::closeInterval(std::uint32_t executedTick, std::uint64_t nowMicros)
{
	Point p;
	p.startMicros = windowStart - startMicros;
	p.endMicros = nowMicros - startMicros;
	p.startTick = windowTick;
	p.endTick = executedTick;
	const Counters& w = window;
	p.rtt = Summary::of(w.rttMicros);
	p.jitter = Summary::of(w.jitterMicros);
	p.inputDelay = Summary::of(w.inputDelayMicros);
	p.buffer = Summary::of(w.bufferTicks);
	p.target = Summary::of(w.targetTicks);
	p.stall = Summary::of(w.stallMicros);
	p.bytesSent = w.bytesSent;
	p.bytesReceived = w.bytesReceived;
	p.framesSent = w.framesSent;
	p.framesReceived = w.framesReceived;
	p.ticksExecuted = w.ticksExecuted;
	p.liveTicks = w.liveTicks;
	p.ticksFaster = w.ticksFaster;
	p.ticksSlower = w.ticksSlower;
	p.meanNudge = w.liveTicks ? w.nudgeSum / static_cast<double>(w.liveTicks) : 0.0;
	p.catchUpTicks = w.catchUpTicks;
	p.reconnects = w.reconnects;
	p.downtimeMicros = w.downtimeMicros;
	p.ordersSubmitted = w.ordersSubmitted;
	p.voiceSent = w.voiceSent;
	p.voiceReceived = w.voiceReceived;
	p.presenceTransitions = w.presenceTransitions;
	if (points.size() < MAX_POINTS)
		points.push_back(p);
	else
		++dropped;
	window = Counters();
	windowStart = nowMicros;
	windowTick = executedTick;
}

void SessionTelemetry::writeFields(std::ostream& out, const Counters& c)
{
	const auto flags = out.flags();
	const auto precision = out.precision();
	out << std::dec << std::defaultfloat << std::setprecision(9);
	auto dist = [&](const char* name, const Histogram& h) {
		out << ' ' << name << ".count=" << h.count();
		if (!h.count())
		{
			out << ' ' << name << ".mean=na " << name << ".p50=na " << name << ".p95=na " << name << ".max=na";
			return;
		}
		out << ' ' << name << ".mean=" << h.mean() << ' ' << name << ".p50=" << h.quantile(0.5) << ' ' << name
		    << ".p95=" << h.quantile(0.95) << ' ' << name << ".max=" << h.max();
	};
	dist("rtt_us", c.rttMicros);
	dist("jitter_us", c.jitterMicros);
	dist("input_delay_us", c.inputDelayMicros);
	dist("buffer_ticks", c.bufferTicks);
	dist("target_ticks", c.targetTicks);
	dist("stall_us", c.stallMicros);
	out << " stall_total_us=" << c.stallMicrosTotal << " frames_sent=" << c.framesSent << " bytes_sent=" << c.bytesSent
	    << " frames_received=" << c.framesReceived << " bytes_received=" << c.bytesReceived
	    << " bundles_received=" << c.bundlesReceived << " bundle_bytes=" << c.bundleBytes
	    << " bundle_entries=" << c.bundleEntries << " orders_submitted=" << c.ordersSubmitted
	    << " order_frames_sent=" << c.orderFramesSent << " orders_resent=" << c.ordersResent
	    << " orders_queued_offline=" << c.ordersQueuedOffline << " orders_dropped_local=" << c.ordersDroppedLocal
	    << " orders_coalesced=" << c.ordersCoalesced << " orders_queue_dropped=" << c.ordersQueueDropped
	    << " voice_sent=" << c.voiceSent << " voice_sent_bytes=" << c.voiceSentBytes
	    << " voice_received=" << c.voiceReceived << " voice_received_bytes=" << c.voiceReceivedBytes
	    << " ticks_executed=" << c.ticksExecuted << " live_ticks=" << c.liveTicks << " ticks_faster=" << c.ticksFaster
	    << " ticks_slower=" << c.ticksSlower
	    << " mean_nudge=" << (c.liveTicks ? c.nudgeSum / static_cast<double>(c.liveTicks) : 0.0)
	    << " max_abs_nudge=" << c.nudgeAbsMax << " catch_ups=" << c.catchUps << " catch_up_ticks=" << c.catchUpTicks
	    << " catch_up_us=" << c.catchUpMicros << " reconnects=" << c.reconnects << " downtime_us=" << c.downtimeMicros
	    << " reloads=" << c.reloads << " desync_rejoins=" << c.desyncRejoins << " desync_flags=" << c.desyncFlags
	    << " resync_requests=" << c.resyncRequests << " presence_transitions=" << c.presenceTransitions;
	out.flags(flags);
	out.precision(precision);
}

json SessionTelemetry::toJson(std::uint64_t nowMicros, bool includeSeries) const
{
	const Counters& c = all;
	const std::uint64_t elapsed = elapsedMicros(nowMicros);
	json j;
	j["elapsed_us"] = begun ? elapsed : 0;
	j["rtt_us"] = summarize(c.rttMicros);
	j["jitter_us"] = summarize(c.jitterMicros);
	j["input_delay_us"] = summarize(c.inputDelayMicros);
	j["input_delay_unmatched"] = pendingInputs.size();
	j["jitter_buffer"] = {{"buffered_ticks", summarize(c.bufferTicks)}, {"target_ticks", summarize(c.targetTicks)}};
	j["tick_rate_nudge"] = {{"live_ticks", c.liveTicks},
	                        {"ticks_faster", c.ticksFaster},
	                        {"ticks_slower", c.ticksSlower},
	                        {"mean", round3(c.liveTicks ? c.nudgeSum / static_cast<double>(c.liveTicks) : 0.0)},
	                        {"max_abs", round3(c.nudgeAbsMax)}};
	j["stalls"] = {{"count", c.stallMicros.count()},
	               {"total_us", c.stallMicrosTotal},
	               {"longest_us", c.stallMicros.max()},
	               {"duration_us", summarize(c.stallMicros)}};
	j["catch_up"] = {{"episodes", c.catchUps},
	                 {"ticks", c.catchUpTicks},
	                 {"wall_us", c.catchUpMicros + (catching && nowMicros > catchStart ? nowMicros - catchStart : 0)}};
	j["reconnects"] = {{"count", c.reconnects},
	                   {"downtime_us", c.downtimeMicros},
	                   {"longest_downtime_us", downtimeMax},
	                   {"down_now", linkDown}};
	j["reloads"] = {{"count", c.reloads},
	                {"load_us", reloadLoadMicros},
	                {"fast_forward_ticks", reloadFfTicks},
	                {"fast_forward_us", reloadFfMicros}};
	j["traffic"] = {{"frames_sent", c.framesSent},
	                {"bytes_sent", c.bytesSent},
	                {"frames_received", c.framesReceived},
	                {"bytes_received", c.bytesReceived},
	                {"bundles_received", c.bundlesReceived},
	                {"bundle_bytes", c.bundleBytes},
	                {"bundle_entries", c.bundleEntries}};
	j["orders"] = {{"submitted", c.ordersSubmitted},
	               {"frames_sent", c.orderFramesSent},
	               {"resent", c.ordersResent},
	               {"queued_offline", c.ordersQueuedOffline},
	               {"dropped_local", c.ordersDroppedLocal},
	               {"outstanding_max", outstandingPeak},
	               {"coalesced", c.ordersCoalesced},
	               {"queue_dropped", c.ordersQueueDropped},
	               {"queued_max", queuedPeak}};
	j["voice"] = {{"sent", c.voiceSent},
	              {"sent_bytes", c.voiceSentBytes},
	              {"received", c.voiceReceived},
	              {"received_bytes", c.voiceReceivedBytes}};
	j["desync"] = {{"rejoins", c.desyncRejoins}, {"flagged", c.desyncFlags}, {"resync_requests", c.resyncRequests}};
	json seats = json::array();
	for (unsigned s = 0; s < MAX_SEATS; ++s)
	{
		if (!(seatsSeen & (1u << s)))
			continue;
		json transitionsJson = json::object(), timeJson = json::object();
		for (unsigned k = 0; k < PRESENCE_STATES; ++k)
		{
			const auto state = static_cast<PresenceState>(k);
			if (transitions(s, state))
				transitionsJson[presenceName(k)] = transitions(s, state);
			if (const auto t = timeIn(s, state, nowMicros))
				timeJson[presenceName(k)] = t;
		}
		seats.push_back({{"seat", s},
		                 {"final_state", presenceName(static_cast<unsigned>(seatState[s]))},
		                 {"transitions", transitionsJson},
		                 {"time_us", timeJson}});
	}
	j["presence"] = {{"transitions", c.presenceTransitions}, {"seats", seats}};
	j["ticks"] = {{"executed", c.ticksExecuted}, {"live", c.liveTicks}};
	if (includeSeries)
	{
		json series = json::array();
		for (const Point& p : points)
			series.push_back({{"start_us", p.startMicros},
			                  {"end_us", p.endMicros},
			                  {"start_tick", p.startTick},
			                  {"end_tick", p.endTick},
			                  {"rtt_us", summaryJson(p.rtt)},
			                  {"jitter_us", summaryJson(p.jitter)},
			                  {"input_delay_us", summaryJson(p.inputDelay)},
			                  {"buffered_ticks", summaryJson(p.buffer)},
			                  {"target_ticks", summaryJson(p.target)},
			                  {"stall_us", summaryJson(p.stall)},
			                  {"bytes_sent", p.bytesSent},
			                  {"bytes_received", p.bytesReceived},
			                  {"frames_sent", p.framesSent},
			                  {"frames_received", p.framesReceived},
			                  {"ticks_executed", p.ticksExecuted},
			                  {"live_ticks", p.liveTicks},
			                  {"ticks_faster", p.ticksFaster},
			                  {"ticks_slower", p.ticksSlower},
			                  {"mean_nudge", round3(p.meanNudge)},
			                  {"catch_up_ticks", p.catchUpTicks},
			                  {"reconnects", p.reconnects},
			                  {"downtime_us", p.downtimeMicros},
			                  {"orders_submitted", p.ordersSubmitted},
			                  {"voice_sent", p.voiceSent},
			                  {"voice_received", p.voiceReceived},
			                  {"presence_transitions", p.presenceTransitions}});
		j["series"] = {{"interval_us", intervalMicros}, {"dropped_points", dropped}, {"points", series}};
	}
	return j;
}

json clientNetworkSummary(const SessionTelemetry& telemetry, const ClientNetworkContext& context, std::uint64_t nowMicros,
                          bool includeSeries)
{
	json j = {{"schema", "ClientNetworkSummary"}, {"schema_version", CLIENT_NETWORK_SUMMARY_VERSION}};
	json ctx = {{"sim_version", context.simVersion},
	            {"platform", context.platform},
	            {"transport", context.transport},
	            {"seat", context.seat},
	            {"human_seat_mask", context.humanSeatMask},
	            {"players", context.players},
	            {"tick_rate_millihz", context.tickRateMilliHz},
	            {"final_tick", context.finalTick}};
	ctx["relay_id"] = context.relayId.empty() ? json(nullptr) : json(context.relayId);
	ctx["relay_region"] = context.relayRegion.empty() ? json(nullptr) : json(context.relayRegion);
	j["match"] = ctx;
	j.update(telemetry.toJson(nowMicros, includeSeries));
	if (context.orderValidationAvailable)
	{
		json seats = json::array();
		for (const auto& v : context.orderValidation)
		{
			json reasons = json::object();
			for (const auto& [name, count] : v.reasons)
				reasons[name] = count;
			seats.push_back({{"seat", v.seat},
			                 {"accepted", v.accepted},
			                 {"stale", v.stale},
			                 {"rejected", v.rejected},
			                 {"voice_rejected", v.voiceRejected},
			                 {"rejected_by_reason", reasons}});
		}
		j["order_validation"] = {{"seats", seats}};
	}
	else
		j["order_validation"] = nullptr;
	return j;
}

// --- Relay side ---

namespace
{
	std::uint64_t ticksToMs(std::uint64_t ticks, std::uint32_t rateMilliHz)
	{
		return ticksToMicros(ticks, rateMilliHz) / 1000;
	}
}

json sequencerSummaryJson(const SequencerTelemetry& t, std::uint32_t humanSeatMask, std::uint32_t tickRateMilliHz,
                          std::uint32_t endTick)
{
	json seats = json::array();
	for (unsigned s = 0; s < t.seats.size() && s < MAX_SEATS; ++s)
	{
		if (!(humanSeatMask & (1u << s)))
			continue;
		const auto& seat = t.seats[s];
		json entry = {
			{"seat", s},
			{"orders", {{"sequenced", seat.ordersSequenced},
			            {"bytes", seat.orderBytes},
			            {"deferred", seat.ordersDeferred},
			            {"defer_ticks", summarize(seat.deferTicks)},
			            {"duplicates_ignored", seat.duplicatesIgnored},
			            {"dropped", seat.ordersDropped},
			            {"flood_rejections", seat.floodRejections},
			            {"max_queued_ahead_ticks", seat.maxQueuedAhead}}},
			{"voice", {{"sequenced", seat.voiceSequenced}, {"bytes", seat.voiceBytes}}},
			{"traffic", {{"frames_received", seat.framesReceived},
			             {"bytes_received", seat.bytesReceived},
			             {"bundles_sent", seat.bundlesSent},
			             {"bundle_bytes_sent", seat.bundleBytes},
			             {"log_bundles_sent", seat.logBundlesSent},
			             {"log_bundle_bytes_sent", seat.logBundleBytes}}},
			{"lag_ticks", summarize(seat.lagTicks)},
			{"checksums", {{"reports", seat.checksumReports},
			               {"lateness_ticks", summarize(seat.reportLatenessTicks)},
			               {"told_to_rejoin", seat.toldToRejoin},
			               {"flagged", seat.flagged},
			               {"late_mismatches", seat.lateMismatches}}},
			{"connection", {{"connects", seat.connects},
			                {"disconnects", seat.disconnects},
			                {"grace_used_ms", seat.graceMicrosTotal / 1000},
			                {"longest_absence_ms", seat.graceMicrosMax / 1000},
			                {"left_by_grace", seat.leftByGrace},
			                {"left_by_quit", seat.leftByQuit}}},
		};
		if (seat.leftTick >= 0)
			entry["connection"]["left_tick"] = seat.leftTick;
		// Optional (still version 1): only hosts that measure a transport round trip.
		if (seat.rttMicros.count())
			entry["rtt_us"] = summarize(seat.rttMicros);
		seats.push_back(entry);
	}
	return {{"schema", "RelayNetworkSummary"},
	        {"schema_version", 1},
	        {"tick_rate_millihz", tickRateMilliHz},
	        {"end_tick", endTick},
	        {"duration_ms", ticksToMs(endTick, tickRateMilliHz)},
	        {"bundles", {{"broadcast", t.bundlesBroadcast}, {"bytes", t.bundleBytesBroadcast}}},
	        {"arbitration", {{"ticks", t.arbitrations},
	                         {"unanimous", t.unanimous},
	                         {"majority", t.majority},
	                         {"flagged", t.flaggedTicks},
	                         {"timed_out", t.timedOut}}},
	        {"peak_backlog", {{"pending_ticks", t.peakPendingTicks},
	                          {"pending_entries", t.peakPendingEntries},
	                          {"pending_bytes", t.peakPendingBytes}}},
	        {"rejected_peers", t.rejectedPeers},
	        {"seats", seats}};
}

void RelayNetworkTotals::add(const SequencerTelemetry& m)
{
	for (const auto& s : m.seats)
	{
		ordersSequenced += s.ordersSequenced;
		ordersDeferred += s.ordersDeferred;
		deferTicks += s.deferTicks.total();
		deferTicksHistogram.merge(s.deferTicks);
		ordersDropped += s.ordersDropped;
		voiceSequenced += s.voiceSequenced;
		bundles += s.bundlesSent;
		bundleBytes += s.bundleBytes;
		logBundles += s.logBundlesSent;
		logBundleBytes += s.logBundleBytes;
		checksumReports += s.checksumReports;
		disconnects += s.disconnects;
		graceExpiries += s.leftByGrace ? 1 : 0;
		graceMicros += s.graceMicrosTotal;
		rejoins += s.toldToRejoin;
		lagTicks.merge(s.lagTicks);
		reportLatenessTicks.merge(s.reportLatenessTicks);
		rttMicros.merge(s.rttMicros);
	}
	arbitrations += m.arbitrations;
	unanimous += m.unanimous;
	majority += m.majority;
	flagged += m.flaggedTicks;
	timedOut += m.timedOut;
	peakPendingBytes = std::max(peakPendingBytes, m.peakPendingBytes);
}

void RelayNetworkTotals::writePrometheus(std::ostream& out) const
{
	auto counter = [&](const char* name, const char* help, std::uint64_t v) {
		out << "# HELP glob2_relay_net_" << name << ' ' << help << "\n# TYPE glob2_relay_net_" << name
		    << " counter\nglob2_relay_net_" << name << ' ' << v << '\n';
	};
	auto histogram = [&](const char* name, const char* help, const Histogram& h) {
		out << "# HELP glob2_relay_net_" << name << ' ' << help << "\n# TYPE glob2_relay_net_" << name
		    << " summary\n";
		for (double q : {0.5, 0.95, 0.99})
			out << "glob2_relay_net_" << name << "{quantile=\"" << q << "\"} " << h.quantile(q) << '\n';
		out << "glob2_relay_net_" << name << "_sum " << h.total() << "\nglob2_relay_net_" << name << "_count "
		    << h.count() << '\n';
	};
	counter("orders_sequenced_total", "Human orders sequenced into turns (voice excluded).", ordersSequenced);
	counter("orders_deferred_total", "Orders assigned later than the next tick (one order per seat per tick, byte budget).",
	        ordersDeferred);
	counter("order_defer_ticks_total", "Ticks of delay the deferral added, summed.", deferTicks);
	counter("orders_dropped_total", "Orders the relay refused to sequence (null, latency adjustment, forged quit, flood).",
	        ordersDropped);
	counter("voice_sequenced_total", "Voice packets passed through.", voiceSequenced);
	counter("bundles_sent_total", "Live turn bundles sent to clients.", bundles);
	counter("bundle_bytes_sent_total", "Payload bytes of live turn bundles sent.", bundleBytes);
	counter("log_bundles_sent_total", "Turn bundles sent replaying the log (resume, rejoin).", logBundles);
	counter("log_bundle_bytes_sent_total", "Payload bytes of log replay bundles.", logBundleBytes);
	counter("checksum_reports_total", "Checksum reports received.", checksumReports);
	counter("arbitrations_total", "Checksum ticks arbitrated.", arbitrations);
	counter("arbitrations_unanimous_total", "Arbitrated ticks where every report agreed.", unanimous);
	counter("arbitrations_majority_total", "Arbitrated ticks settled by majority (minority told to rejoin).", majority);
	counter("arbitrations_flagged_total", "Arbitrated ticks flagged for the verifier.", flagged);
	counter("arbitrations_timed_out_total", "Ticks arbitrated with partial reports after the timeout.", timedOut);
	counter("disconnects_total", "Seat transport losses during matches.", disconnects);
	counter("grace_expiries_total", "Seats that left because the reconnect grace period expired.", graceExpiries);
	counter("grace_used_seconds_total", "Seconds seats spent disconnected inside the grace period.", graceMicros / 1000000);
	counter("rejoins_total", "Seats told to rejoin after a checksum minority.", rejoins);
	out << "# HELP glob2_relay_net_peak_pending_bytes Largest pending (unsent) order bytes of any finished match.\n"
	       "# TYPE glob2_relay_net_peak_pending_bytes gauge\nglob2_relay_net_peak_pending_bytes "
	    << peakPendingBytes << '\n';
	histogram("lag_ticks", "Relay tick minus the client's executed tick, at each ping.", lagTicks);
	histogram("checksum_lateness_ticks", "Relay tick at a checksum report's arrival minus its tick.", reportLatenessTicks);
	histogram("order_defer_ticks", "Ticks an order was deferred beyond the next tick (deferred orders only).",
	          deferTicksHistogram);
	histogram("rtt_us", "Round trip to clients in microseconds (the relay's WebSocket ping, GLOB2_RELAY_RTT_PING_MS).",
	          rttMicros);
}

// --- Record-derived summary (verify-match) ---

json recordNetworkSummary(const MatchRecord& record)
{
	struct SeatFacts
	{
		std::uint64_t orders = 0, bytes = 0, reports = 0;
		std::uint64_t connects = 0, disconnects = 0, rejoins = 0, flagged = 0, resynced = 0;
		std::uint64_t disconnectedTicks = 0;
		std::int64_t downSince = -1;
		std::int64_t leftTick = -1;
		const char* leftBy = nullptr;
	};
	std::array<SeatFacts, MAX_SEATS> seats{};
	for (const auto& t : record.turns)
		if (t.seat < MAX_SEATS && !t.order.empty() && t.order[0] != ORDER_TYPE_PLAYER_QUIT)
		{
			++seats[t.seat].orders;
			seats[t.seat].bytes += t.order.size();
		}
	for (const auto& r : record.reports)
		if (r.seat < MAX_SEATS)
			++seats[r.seat].reports;
	for (const auto& e : record.events)
	{
		if (e.seat >= MAX_SEATS)
			continue;
		SeatFacts& s = seats[e.seat];
		switch (e.kind)
		{
		case MatchEventKind::Connected:
			++s.connects;
			if (s.downSince >= 0)
			{
				s.disconnectedTicks += e.tick - static_cast<std::uint32_t>(s.downSince);
				s.downSince = -1;
			}
			break;
		case MatchEventKind::Disconnected:
			++s.disconnects;
			s.downSince = e.tick;
			break;
		case MatchEventKind::LeftByQuit:
		case MatchEventKind::LeftByGrace:
			s.leftTick = e.tick;
			s.leftBy = e.kind == MatchEventKind::LeftByQuit ? "quit" : "grace";
			if (s.downSince >= 0)
			{
				s.disconnectedTicks += e.tick - static_cast<std::uint32_t>(s.downSince);
				s.downSince = -1;
			}
			break;
		case MatchEventKind::ToldToRejoin: ++s.rejoins; break;
		case MatchEventKind::Resynced: ++s.resynced; break;
		case MatchEventKind::Flagged: ++s.flagged; break;
		}
	}
	json out = json::array();
	for (unsigned i = 0; i < MAX_SEATS; ++i)
	{
		if (!(record.humanSeatMask & (1u << i)))
			continue;
		SeatFacts& s = seats[i];
		if (s.downSince >= 0)
			s.disconnectedTicks += record.endTick > static_cast<std::uint32_t>(s.downSince)
			                           ? record.endTick - static_cast<std::uint32_t>(s.downSince)
			                           : 0;
		json entry = {{"seat", i},
		              {"orders", s.orders},
		              {"order_bytes", s.bytes},
		              {"checksum_reports", s.reports},
		              {"connects", s.connects},
		              {"disconnects", s.disconnects},
		              {"reconnects", s.connects > 0 ? s.connects - 1 : 0},
		              {"disconnected_ticks", s.disconnectedTicks},
		              {"told_to_rejoin", s.rejoins},
		              {"resynced", s.resynced},
		              {"flagged", s.flagged}};
		if (s.leftBy)
		{
			entry["left_tick"] = s.leftTick;
			entry["left_by"] = s.leftBy;
		}
		out.push_back(entry);
	}
	return {{"schema", "RecordNetworkSummary"}, {"schema_version", 1}, {"source", "match_record"}, {"seats", out}};
}
}
