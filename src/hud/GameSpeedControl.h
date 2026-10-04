// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's game-speed chevrons and its simulation tick-rate readout.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace GameSpeedControl
{
constexpr int CHEVRONS = 5;
/// The Settings::gameSpeed level each chevron selects: 1x, 2x, 4x, 8x and the
/// uncapped maximum. The presets between them stay on the settings slider and
/// shortcuts.
constexpr std::array<int, CHEVRONS> presets{0, 3, 5, 7, 10};

/// How many chevrons a speed level lights; none below normal speed.
constexpr int lit(int speed)
{
	int count = 0;
	for (int preset : presets)
		count += speed >= preset;
	return count;
}

/// The next chevron above a speed level; the last one wraps to normal speed.
constexpr int faster(int speed)
{
	for (int preset : presets)
		if (preset > speed)
			return preset;
	return presets.front();
}

/// The nearest chevron below a speed level; normal speed and slower stay put.
constexpr int slower(int speed)
{
	int result = speed < presets.front() ? speed : presets.front();
	for (int preset : presets)
		if (preset < speed)
			result = preset;
	return result;
}
} // namespace GameSpeedControl

/// Simulation ticks per second, averaged over a rolling window and refreshed at
/// most once per second so the readout holds still. Times are milliseconds and
/// may wrap.
class TickRateMeter
{
public:
	static constexpr std::uint32_t UPDATE_MS = 1000;
	/// The window spans this many updates.
	static constexpr std::size_t WINDOW_UPDATES = 3;

	/// Offer the simulation's progress: the time now, when the latest tick
	/// finished and how many ticks have run.
	void sample(std::uint32_t now, std::uint32_t tickTime, std::uint32_t ticks)
	{
		if (count && now - updated < UPDATE_MS)
			return;
		updated = now;
		// Measuring between tick completions keeps a steady simulation from
		// reading one tick high or low; a stalled one is measured up to now.
		const bool advanced = count ? ticks != samples[count - 1].ticks : ticks != 0;
		if (count == samples.size())
		{
			for (std::size_t i = 1; i < count; ++i)
				samples[i - 1] = samples[i];
			--count;
		}
		samples[count++] = {advanced ? tickTime : now, ticks};
	}

	/// The averaged rate, once a full update interval has been measured.
	std::optional<double> rate() const
	{
		if (count < 2)
			return std::nullopt;
		const auto elapsed = std::int32_t(samples[count - 1].time - samples[0].time);
		if (elapsed <= 0)
			return std::nullopt;
		return (samples[count - 1].ticks - samples[0].ticks) * 1000.0 / elapsed;
	}

	/// A whole number, with one decimal below 25 ticks per second.
	static std::string format(double rate)
	{
		const long tenths = std::lround(rate * 10);
		if (tenths >= 250)
			return std::to_string(std::lround(rate));
		return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
	}

private:
	struct Sample
	{
		std::uint32_t time, ticks;
	};
	std::array<Sample, WINDOW_UPDATES + 1> samples{};
	std::size_t count = 0;
	std::uint32_t updated = 0;
};
