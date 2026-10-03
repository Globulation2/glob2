// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Per-client delay maths for the turn protocol (docs/multiplayer/turn-protocol.md,
// "Timing model and per-client delay"). Pure: callers pass every time and sample in.

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Turn
{
	/// Measures bundle-arrival jitter as p95(offset) - min(offset) over a sliding window,
	/// where offset = arrival - horizon * tickPeriod.
	class JitterEstimator
	{
	public:
		explicit JitterEstimator(std::size_t window = 128) : window(window ? window : 1) {}
		/// Records one live bundle arrival.
		void addSample(std::int64_t arrivalMicros, std::uint32_t horizonTick, std::uint64_t tickPeriodMicros);
		/// Records a raw offset sample (used by tests and by addSample).
		void addOffset(std::int64_t offsetMicros);
		/// Current jitter estimate in microseconds; 0 until two samples exist.
		std::int64_t jitterMicros() const;
		std::size_t sampleCount() const { return samples.size(); }
		void clear() { samples.clear(); }
	private:
		std::size_t window;
		std::deque<std::int64_t> samples;
	};

	struct JitterBufferConfig
	{
		std::uint32_t bundleInterval = 2;
		std::uint32_t safetyTicks = 1;
		std::uint32_t minTargetTicks = 2;
		std::uint32_t maxTargetTicks = 50;
		std::uint64_t decreaseHoldMicros = 5000000; // 5 s
	};

	/// Turns a jitter estimate into a target buffer level (ticks of authorized but
	/// unexecuted work). Rises immediately; falls one tick per hold period.
	class JitterBuffer
	{
	public:
		explicit JitterBuffer(JitterBufferConfig config = {});
		/// The level the current jitter calls for, before hysteresis.
		std::uint32_t requiredTicks(std::int64_t jitterMicros, std::uint64_t tickPeriodMicros) const;
		/// Feeds the current jitter estimate and returns the target after hysteresis.
		std::uint32_t update(std::int64_t jitterMicros, std::uint64_t tickPeriodMicros, std::uint64_t nowMicros);
		std::uint32_t targetTicks() const { return target; }
		void reset();
	private:
		JitterBufferConfig config;
		std::uint32_t target;
		bool holding = false;
		std::uint64_t holdStart = 0;
	};

	struct DelayControllerConfig
	{
		double emaAlpha = 0.05;
		double deadbandTicks = 0.5;
		double gainPerTick = 0.02;
		double maxNudge = 0.05;
		std::uint32_t catchUpEnterTicks = 25;
		std::uint32_t catchUpExitTicks = 2;
	};

	/// Holds the buffer at its target by nudging the tick rate (within +/-5%), and
	/// switches to uncapped catch-up when a client falls a second behind.
	class DelayController
	{
	public:
		explicit DelayController(DelayControllerConfig config = {}) : config(config) {}
		/// Samples the buffer level once per executed tick.
		void onTick(std::uint32_t bufferedTicks, std::uint32_t targetTicks);
		/// Re-evaluates catch-up on any change of buffer level (e.g. a bundle arrival).
		void observe(std::uint32_t bufferedTicks, std::uint32_t targetTicks);
		/// Tick-rate multiplier in [1 - maxNudge, 1 + maxNudge]; 1 means nominal speed.
		double rateMultiplier(std::uint32_t targetTicks) const;
		/// Interval to wait before the next tick, in microseconds (0 while catching up).
		std::uint64_t tickIntervalMicros(std::uint64_t tickPeriodMicros, std::uint32_t targetTicks) const;
		bool catchingUp() const { return catchUp; }
		/// Forces catch-up, e.g. after a reload from tick 0.
		void startCatchUp() { catchUp = true; }
		double averageBuffered() const { return ema; }
		void reset() { ema = 0; primed = false; catchUp = false; }
	private:
		DelayControllerConfig config;
		double ema = 0;
		bool primed = false;
		bool catchUp = false;
	};
}
