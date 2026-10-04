// SPDX-License-Identifier: GPL-3.0-or-later
#include "MetricSeries.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stats
{
std::vector<double> ratePerMinute(const std::vector<Uint32> &ticks, const std::vector<double> &cumulative, int window)
{
	const std::size_t count = std::min(ticks.size(), cumulative.size());
	std::vector<double> rates(count, 0.0);
	window = std::max(1, window);
	for (std::size_t i = 1; i < count; ++i)
	{
		const std::size_t from = i > std::size_t(window) ? i - std::size_t(window) : 0;
		const double span = double(ticks[i]) - double(ticks[from]);
		if (span > 0)
			rates[i] = std::max(0.0, cumulative[i] - cumulative[from]) * TICKS_PER_MINUTE / span;
	}
	// The first sample has no interval behind it: show the rate that follows.
	if (count > 1)
		rates[0] = rates[1];
	return rates;
}

std::vector<double> percentOf(const std::vector<double> &values, const std::vector<double> &whole)
{
	std::vector<double> out(values.size(), 0.0);
	for (std::size_t i = 0; i < values.size() && i < whole.size(); ++i)
		if (whole[i] > 0)
			out[i] = std::clamp(values[i] * 100.0 / whole[i], 0.0, 100.0);
	return out;
}

std::vector<double> ratioPercent(const std::vector<double> &values, const std::vector<double> &whole)
{
	std::vector<double> out(values.size(), std::nan(""));
	for (std::size_t i = 0; i < values.size() && i < whole.size(); ++i)
		if (whole[i] > 0)
			out[i] = std::clamp(values[i] * 100.0 / whole[i], 0.0, 100.0);
	return out;
}

std::vector<double> meanOf(const std::vector<double> &values, const std::vector<double> &count)
{
	std::vector<double> out(values.size(), std::nan(""));
	for (std::size_t i = 0; i < values.size() && i < count.size(); ++i)
		if (count[i] > 0)
			out[i] = values[i] / count[i];
	return out;
}

std::vector<double> movingAverage(const std::vector<double> &values, int window)
{
	std::vector<double> out(values.size(), 0.0);
	window = std::max(1, window);
	double sum = 0;
	for (std::size_t i = 0; i < values.size(); ++i)
	{
		sum += values[i];
		if (i >= std::size_t(window))
			sum -= values[i - std::size_t(window)];
		out[i] = sum / double(std::min(i + 1, std::size_t(window)));
	}
	return out;
}

std::vector<std::vector<double>> disjointBands(const std::vector<std::vector<double>> &nested,
											   const std::vector<double> &total)
{
	std::vector<std::vector<double>> bands(nested.size() + 1, std::vector<double>(total.size(), 0.0));
	for (std::size_t i = 0; i < total.size(); ++i)
	{
		double below = 0;
		for (std::size_t b = 0; b < nested.size(); ++b)
		{
			// Thresholds are nested, and none can hold more than the total.
			const double upTo = std::clamp(i < nested[b].size() ? nested[b][i] : 0.0, below, std::max(below, total[i]));
			bands[b][i] = upTo - below;
			below = upTo;
		}
		bands[nested.size()][i] = std::max(0.0, total[i] - below);
	}
	return bands;
}

void normaliseToPercent(std::vector<std::vector<double>> &bands)
{
	if (bands.empty())
		return;
	for (std::size_t i = 0; i < bands.front().size(); ++i)
	{
		double sum = 0;
		for (const auto &band : bands)
			sum += i < band.size() ? std::max(0.0, band[i]) : 0.0;
		for (auto &band : bands)
			if (i < band.size())
				band[i] = sum > 0 ? std::max(0.0, band[i]) * 100.0 / sum : 0.0;
	}
}

Axis niceAxis(double low, double high, int maxSteps, bool whole)
{
	Axis axis;
	low = std::min(0.0, low);
	high = std::max(0.0, high);
	if (high - low <= 0)
		high = 1;
	maxSteps = std::max(1, maxSteps);
	const double raw = (high - low) / maxSteps;
	const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
	axis.step = magnitude * 10;
	for (double factor : {1.0, 2.0, 5.0})
		if (magnitude * factor >= raw * (1 - 1e-9))
		{
			axis.step = magnitude * factor;
			break;
		}
	if (whole)
		axis.step = std::max(1.0, axis.step);
	axis.minimum = std::floor(low / axis.step + 1e-9) * axis.step;
	axis.maximum = std::ceil(high / axis.step - 1e-9) * axis.step;
	return axis;
}

int timeStep(int seconds, int maxLabels)
{
	maxLabels = std::max(1, maxLabels);
	for (int step : {30, 60, 120, 300, 600, 900, 1800})
		if (seconds / step <= maxLabels)
			return step;
	return 3600 * std::max(1, (seconds / 3600 + maxLabels - 1) / maxLabels);
}

std::string timeText(int seconds)
{
	char text[32];
	seconds = std::max(0, seconds);
	if (seconds >= 3600)
		std::snprintf(text, sizeof text, "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
	else
		std::snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
	return text;
}

std::string valueText(double value, bool decimals)
{
	char text[32];
	const double size = std::fabs(value);
	if (size >= 999950)
		std::snprintf(text, sizeof text, "%.1fM", value / 1e6);
	else if (size >= 9995)
		std::snprintf(text, sizeof text, "%.1fk", value / 1e3);
	else if (decimals && size < 99.95 && std::fabs(value - std::round(value)) >= 0.05)
		std::snprintf(text, sizeof text, "%.1f", value);
	else
		std::snprintf(text, sizeof text, "%.0f", value);
	return text;
}
std::string tickText(double value, double axisMaximum, bool decimals)
{
	const double size = std::fabs(axisMaximum);
	const double scale = size >= 1e6 ? 1e6 : size >= 1e4 ? 1e3 : 1;
	if (scale == 1)
		return valueText(value, decimals);
	char text[32];
	const double scaled = value / scale;
	// One decimal only where the step needs it: "2.5k", but "10k" not "10.0k".
	std::snprintf(text, sizeof text, std::fabs(scaled - std::round(scaled)) >= 0.05 ? "%.1f%s" : "%.0f%s", scaled, scale == 1e6 ? "M" : "k");
	return text;
}
} // namespace Stats
