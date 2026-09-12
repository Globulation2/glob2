// SPDX-License-Identifier: GPL-3.0-or-later
#include "LatticeNoise.h"
#include <algorithm>
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
	: width(width), height(height), columns(std::max(2, int(std::lround(width / cell)))),
	  rows(std::max(2, int(std::lround(height / cell)))), lattice(size_t(columns) * rows)
{
	for (double &value : lattice)
		value = random() / 4294967296.0;
}

double PeriodicNoise::at(double x, double y) const
{
	const double fx = x * columns / width, fy = y * rows / height;
	const double x0 = std::floor(fx), y0 = std::floor(fy);
	double tx = fx - x0, ty = fy - y0;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	const int ix = wrapIndex(int(x0), columns), iy = wrapIndex(int(y0), rows);
	const int jx = (ix + 1) % columns, jy = (iy + 1) % rows;
	const double top = lattice[iy * columns + ix] * (1 - tx) + lattice[iy * columns + jx] * tx;
	const double bottom = lattice[jy * columns + ix] * (1 - tx) + lattice[jy * columns + jx] * tx;
	return top * (1 - ty) + bottom * ty;
}

std::vector<int> periodicNoise(int w, int h, int period, std::mt19937 &rng)
{
	const PeriodicNoise noise(w, h, period, rng);
	std::vector<int> field(size_t(w) * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			field[size_t(y) * w + x] = std::min(65535, int(noise.at(x, y) * 65536.0));
	return field;
}

std::vector<int> fractalNoise(int w, int h, int period, int octaves, std::mt19937 &rng)
{
	std::vector<int> sum(size_t(w) * h, 0);
	int total = 0;
	for (int octave = 0; octave < octaves; ++octave)
	{
		const int weight = 1 << (octaves - 1 - octave);
		const std::vector<int> layer = periodicNoise(w, h, std::max(2, period >> octave), rng);
		for (size_t i = 0; i < sum.size(); ++i)
			sum[i] += layer[i] * weight;
		total += weight;
	}
	for (int &v : sum)
		v /= total;
	return sum;
}

std::vector<float> torusNoise(int width, int height, std::mt19937 &rng)
{
	static const std::pair<int, double> octaves[] = {{32, 0.4}, {16, 0.3}, {8, 0.2}, {4, 0.1}};
	std::vector<double> field(size_t(width) * height, 0.0);
	for (const auto &octave : octaves)
	{
		const PeriodicNoise noise(width, height, std::min({octave.first, width, height}), rng);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				field[size_t(y) * width + x] += octave.second * (2 * noise.at(x, y) - 1);
	}
	double peak = 0;
	for (double value : field)
		peak = std::max(peak, std::abs(value));
	std::vector<float> result(field.size(), 0.0f);
	if (peak > 0)
		for (size_t i = 0; i < field.size(); ++i)
			result[i] = float(field[i] / peak);
	return result;
}

int percentile(std::vector<int> samples, int percent)
{
	if (samples.empty())
		return 0;
	const size_t k = std::min(samples.size() - 1, samples.size() * size_t(percent) / 100);
	std::nth_element(samples.begin(), samples.begin() + k, samples.end());
	return samples[k];
}
} // namespace MapGeneration
