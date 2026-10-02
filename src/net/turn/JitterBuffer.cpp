// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "JitterBuffer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Turn
{
void JitterEstimator::addSample(std::int64_t arrivalMicros, std::uint32_t horizonTick, std::uint64_t tickPeriodMicros)
{
	addOffset(arrivalMicros - static_cast<std::int64_t>(horizonTick * tickPeriodMicros));
}

void JitterEstimator::addOffset(std::int64_t offsetMicros)
{
	samples.push_back(offsetMicros);
	while (samples.size() > window)
		samples.pop_front();
}

std::int64_t JitterEstimator::jitterMicros() const
{
	if (samples.size() < 2)
		return 0;
	std::vector<std::int64_t> sorted(samples.begin(), samples.end());
	std::sort(sorted.begin(), sorted.end());
	// Nearest-rank 95th percentile.
	const std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * sorted.size()));
	return sorted[std::max<std::size_t>(rank, 1) - 1] - sorted.front();
}

JitterBuffer::JitterBuffer(JitterBufferConfig config) : config(config), target(config.minTargetTicks) {}

std::uint32_t JitterBuffer::requiredTicks(std::int64_t jitterMicros, std::uint64_t tickPeriodMicros) const
{
	// Jitter within the tolerance needs no buffer: a bundle that late costs a stall that
	// short, and the engine then moves its schedule back rather than bursting.
	const std::uint64_t jitter = jitterMicros > 0 ? static_cast<std::uint64_t>(jitterMicros) : 0;
	std::uint64_t required = 0;
	if (jitter > config.jitterToleranceMicros && tickPeriodMicros)
		required = (jitter + tickPeriodMicros - 1) / tickPeriodMicros + config.safetyTicks;
	return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(required, config.minTargetTicks, config.maxTargetTicks));
}

std::uint32_t JitterBuffer::update(std::int64_t jitterMicros, std::uint64_t tickPeriodMicros, std::uint64_t nowMicros)
{
	const std::uint32_t required = requiredTicks(jitterMicros, tickPeriodMicros);
	if (required >= target)
	{
		target = required;
		holding = false;
	}
	else if (!holding)
	{
		holding = true;
		holdStart = nowMicros;
	}
	else if (nowMicros - holdStart >= config.decreaseHoldMicros)
	{
		--target;
		holdStart = nowMicros;
		if (target <= required)
			holding = false;
	}
	return target;
}

void JitterBuffer::reset()
{
	target = config.minTargetTicks;
	holding = false;
}

void DelayController::onTick(std::uint32_t bufferedTicks, std::uint32_t targetTicks)
{
	// Average each bundle period first: the sawtooth's mean over a whole period does
	// not depend on where the period starts, and its swing would otherwise ripple
	// the average across the deadband.
	periodSum += bufferedTicks;
	if (++periodCount >= period)
	{
		const double sample = periodSum / period;
		periodSum = 0;
		periodCount = 0;
		if (!primed)
		{
			ema = sample;
			primed = true;
		}
		else
			ema += periodAlpha * (sample - ema);
	}
	observe(bufferedTicks, targetTicks);
}

void DelayController::observe(std::uint32_t bufferedTicks, std::uint32_t targetTicks)
{
	if (!catchUp && bufferedTicks > targetTicks + config.catchUpEnterTicks)
		catchUp = true;
	else if (catchUp && bufferedTicks <= targetTicks + config.catchUpExitTicks)
	{
		catchUp = false;
		ema = bufferedTicks;
		primed = true;
	}
}

void DelayController::setBundleInterval(std::uint32_t ticks)
{
	period = ticks ? ticks : 1;
	sawtoothMean = (period - 1) / 2.0;
	// The same time constant per tick: one update per period of `period` ticks.
	periodAlpha = 1.0 - std::pow(1.0 - config.emaAlpha, static_cast<double>(period));
	periodSum = 0;
	periodCount = 0;
}

double DelayController::rateMultiplier(std::uint32_t targetTicks) const
{
	if (!primed)
		return 1.0;
	double error = ema - static_cast<double>(targetTicks) - sawtoothMean;
	if (std::fabs(error) <= config.deadbandTicks)
		return 1.0;
	return 1.0 + std::clamp(config.gainPerTick * error, -config.maxNudge, config.maxNudge);
}

std::uint64_t DelayController::tickIntervalMicros(std::uint64_t tickPeriodMicros, std::uint32_t targetTicks) const
{
	if (catchUp)
		return 0;
	return static_cast<std::uint64_t>(std::llround(static_cast<double>(tickPeriodMicros) / rateMultiplier(targetTicks)));
}
}
