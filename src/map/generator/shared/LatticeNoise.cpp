#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LatticeNoise.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>
namespace MapGeneration
{
namespace
{
int wrapIndex(int value, int period)
{
	return ((value % period) + period) % period;
}
} // namespace

PeriodicNoise::PeriodicNoise(int width, int height, double cell, std::mt19937 &random)
	: width(width), height(height),
	  columns(std::max(2, int(::MapGeneration::Numeric::lround(width / cell)))),
	  rows(std::max(2, int(::MapGeneration::Numeric::lround(height / cell)))),
	  lattice(size_t(columns) * rows)
{
	for (double &value : lattice)
	{
		::MapGeneration::generationCheckpoint();
		value = random() / 4294967296.0;
	}
}

double PeriodicNoise::at(double x, double y) const
{
	const double fx = x * columns / width, fy = y * rows / height;
	const double x0 = ::MapGeneration::Numeric::floor(fx), y0 = ::MapGeneration::Numeric::floor(fy);
	double tx = fx - x0, ty = fy - y0;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	const int ix = wrapIndex(int(x0), columns), iy = wrapIndex(int(y0), rows);
	const int jx = (ix + 1) % columns, jy = (iy + 1) % rows;
	const double top =
		lattice.at(iy * columns + ix) * (1 - tx) + lattice.at(iy * columns + jx) * tx;
	const double bottom =
		lattice.at(jy * columns + ix) * (1 - tx) + lattice.at(jy * columns + jx) * tx;
	return top * (1 - ty) + bottom * ty;
}

std::vector<int> periodicNoise(int w, int h, int period, std::mt19937 &rng)
{
	const PeriodicNoise noise(w, h, period, rng);
	std::vector<int> field(size_t(w) * h);
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			field.at(size_t(y) * w + x) = std::min(65535, int(noise.at(x, y) * 65536.0));
		}
	}
	return field;
}

std::vector<int> fractalNoise(int w, int h, int period, int octaves, std::mt19937 &rng)
{
	std::vector<int> sum(size_t(w) * h, 0);
	int total = 0;
	for (int octave = 0; octave < octaves; ++octave)
	{
		::MapGeneration::generationCheckpoint();
		const int weight = 1 << (octaves - 1 - octave);
		const std::vector<int> layer = periodicNoise(w, h, std::max(2, period >> octave), rng);
		for (size_t i = 0; i < sum.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			sum.at(i) += layer.at(i) * weight;
		}
		total += weight;
	}
	for (int &v : sum)
	{
		::MapGeneration::generationCheckpoint();
		v /= total;
	}
	return sum;
}

std::vector<float> torusNoise(int width, int height, std::mt19937 &rng)
{
	// Octaves from 32 tiles down to 4, weights falling 0.4 to 0.1: the broad swings of a coastline
	// dominate, with detail down to a few tiles, about the size of a building.
	static const std::pair<int, double> octaves[] = {{32, 0.4}, {16, 0.3}, {8, 0.2}, {4, 0.1}};
	std::vector<double> field(size_t(width) * height, 0.0);
	for (const auto &octave : octaves)
	{
		::MapGeneration::generationCheckpoint();
		const PeriodicNoise noise(width, height, std::min({octave.first, width, height}), rng);
		for (int y = 0; y < height; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < width; ++x)
			{
				::MapGeneration::generationCheckpoint();
				field.at(size_t(y) * width + x) += octave.second * (2 * noise.at(x, y) - 1);
			}
		}
	}
	double peak = 0;
	for (double value : field)
	{
		::MapGeneration::generationCheckpoint();
		peak = std::max(peak, std::abs(value));
	}
	std::vector<float> result(field.size(), 0.0f);
	if (peak > 0)
		for (size_t i = 0; i < field.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			result.at(i) = float(field.at(i) / peak);
		}
	return result;
}

int percentile(std::vector<int> samples, int percent)
{
	if (samples.empty())
		return 0;
	const size_t k = std::min(samples.size() - 1, samples.size() * size_t(percent) / 100);
	std::nth_element(samples.begin(), samples.begin() + k, samples.end());
	return samples.at(k);
}
} // namespace MapGeneration

namespace MapGeneration
{
std::vector<unsigned char> noisyShare(const std::vector<unsigned char> &region,
									  const std::vector<int> &noise, int percent)
{
	std::vector<unsigned char> result(region.size(), 0);
	if (percent <= 0)
		return result;
	std::vector<int> levels;
	for (size_t i = 0; i < region.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (region.at(i))
			levels.push_back(noise.at(i));
	}
	if (levels.empty())
		return result;
	const int level = percent >= 100 ? INT_MIN : percentile(levels, 100 - percent);
	for (size_t i = 0; i < region.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		result.at(i) = region.at(i) && noise.at(i) >= level;
	}
	return result;
}
} // namespace MapGeneration
