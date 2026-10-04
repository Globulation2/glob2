// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Per-stage input-delay breakdown for the turn protocol harnesses. One trace follows
// one player's own orders through every stage between a click and its execution:
//
//   queued     the GUI (or bot) queues the order
//   submitted  TurnSession::addLocalOrder hands it to the transport
//   sequenced  the relay receives it and assigns an execution tick
//   emitted    the relay broadcasts a horizon above that tick
//   received   the client applies a bundle with a horizon above that tick
//   executed   the engine executes that tick
//
// The probes are TurnSequencer::onSequenced/onEmitted and TurnSession::onSubmitted/
// onHorizon. The caller passes one clock for every stage; the methods are thread-safe
// because a LAN host's relay runs on its network thread.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace turntest
{
	class LatencyTrace
	{
	public:
		struct Order
		{
			std::uint64_t queued = 0, submitted = 0, sequenced = 0, emitted = 0, received = 0, executed = 0;
			std::uint32_t tick = 0;
			std::uint32_t relayTickAtArrival = 0;
			std::uint32_t executedTickAtSubmit = 0;
			bool measured = false;
			bool complete() const { return queued && submitted && sequenced && emitted && received && executed; }
		};

		/// Only orders queued while this is true count.
		bool measuring = false;

		void queued(std::uint64_t now)
		{
			std::lock_guard<std::mutex> g(m);
			unsubmitted.push_back(now);
		}
		/// A local submission; orders the trace never saw queued (AI, quit) are ignored.
		void submitted(std::uint32_t sequence, std::uint64_t now, std::uint32_t executedTick)
		{
			std::lock_guard<std::mutex> g(m);
			if (unsubmitted.empty())
				return;
			Order& o = orders[sequence];
			o.queued = unsubmitted.front();
			unsubmitted.pop_front();
			o.submitted = now;
			o.executedTickAtSubmit = executedTick;
			o.measured = measuring;
		}
		void sequenced(std::uint32_t sequence, std::uint32_t tick, std::uint32_t relayTick, std::uint64_t now)
		{
			std::lock_guard<std::mutex> g(m);
			auto it = orders.find(sequence);
			if (it == orders.end() || it->second.sequenced)
				return;
			it->second.sequenced = now;
			it->second.tick = tick;
			it->second.relayTickAtArrival = relayTick;
			byTick[tick] = sequence;
		}
		void emitted(std::uint32_t horizon, std::uint64_t now) { stamp(horizon, now, &Order::emitted); }
		void received(std::uint32_t horizon, std::uint64_t now) { stamp(horizon, now, &Order::received); }
		void executed(std::uint32_t tick, std::uint64_t now)
		{
			std::lock_guard<std::mutex> g(m);
			auto it = byTick.find(tick);
			if (it == byTick.end())
				return;
			Order& o = orders[it->second];
			if (!o.executed)
				o.executed = now;
			byTick.erase(it);
		}

		std::vector<Order> completed() const
		{
			std::lock_guard<std::mutex> g(m);
			std::vector<Order> out;
			for (const auto& [seq, o] : orders)
				if (o.measured && o.complete())
					out.push_back(o);
			return out;
		}

		struct Stage
		{
			double mean = 0, p95 = 0;
		};
		static Stage summarize(std::vector<double> v)
		{
			Stage s;
			if (v.empty())
				return s;
			std::sort(v.begin(), v.end());
			for (double x : v)
				s.mean += x;
			s.mean /= static_cast<double>(v.size());
			s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
			return s;
		}

		/// Mean / p95 in milliseconds of every stage and the total, plus the mean lead
		/// of the assigned tick over the relay tick at arrival and over the client's
		/// executed tick at submission.
		struct Breakdown
		{
			std::size_t samples = 0;
			Stage pickup, uplink, relayWait, downlink, bufferWait, total;
			double assignLeadTicks = 0, executionLeadTicks = 0;
		};
		Breakdown breakdown() const
		{
			const auto all = completed();
			Breakdown b;
			b.samples = all.size();
			std::vector<double> pickup, uplink, relayWait, downlink, bufferWait, total;
			for (const auto& o : all)
			{
				auto ms = [](std::uint64_t from, std::uint64_t to) {
					return to >= from ? (to - from) / 1000.0 : -((from - to) / 1000.0);
				};
				pickup.push_back(ms(o.queued, o.submitted));
				uplink.push_back(ms(o.submitted, o.sequenced));
				relayWait.push_back(ms(o.sequenced, o.emitted));
				downlink.push_back(ms(o.emitted, o.received));
				bufferWait.push_back(ms(o.received, o.executed));
				total.push_back(ms(o.queued, o.executed));
				b.assignLeadTicks += static_cast<double>(o.tick) - static_cast<double>(o.relayTickAtArrival);
				b.executionLeadTicks += static_cast<double>(o.tick) - static_cast<double>(o.executedTickAtSubmit);
			}
			if (b.samples)
			{
				b.assignLeadTicks /= static_cast<double>(b.samples);
				b.executionLeadTicks /= static_cast<double>(b.samples);
			}
			b.pickup = summarize(pickup);
			b.uplink = summarize(uplink);
			b.relayWait = summarize(relayWait);
			b.downlink = summarize(downlink);
			b.bufferWait = summarize(bufferWait);
			b.total = summarize(total);
			return b;
		}

		static std::string header()
		{
			return "samples | pickup | uplink | relay wait | downlink | buffer wait | total (mean/p95 ms) | "
			       "tick - relay tick | tick - executed tick";
		}
		static std::string row(const Breakdown& b)
		{
			std::ostringstream o;
			o.precision(3);
			auto st = [&](const Stage& s) { o << s.mean << "/" << s.p95; };
			o << b.samples << " | ";
			st(b.pickup);
			o << " | ";
			st(b.uplink);
			o << " | ";
			st(b.relayWait);
			o << " | ";
			st(b.downlink);
			o << " | ";
			st(b.bufferWait);
			o << " | ";
			st(b.total);
			o << " | " << b.assignLeadTicks << " | " << b.executionLeadTicks;
			return o.str();
		}

	private:
		void stamp(std::uint32_t horizon, std::uint64_t now, std::uint64_t Order::*field)
		{
			std::lock_guard<std::mutex> g(m);
			for (auto it = byTick.begin(); it != byTick.end() && it->first < horizon; ++it)
			{
				Order& o = orders[it->second];
				if (!(o.*field))
					o.*field = now;
			}
		}

		mutable std::mutex m;
		std::deque<std::uint64_t> unsubmitted;
		std::map<std::uint32_t, Order> orders;          ///< by client sequence
		std::map<std::uint32_t, std::uint32_t> byTick; ///< sequenced, not yet executed
	};
}
