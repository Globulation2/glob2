// SPDX-License-Identifier: GPL-3.0-or-later
#include "LatticeNoise.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
	const int gw = std::max(1, (w + period / 2) / std::max(1, period));
	const int gh = std::max(1, (h + period / 2) / std::max(1, period));
	std::vector<int> lattice(size_t(gw) * gh);
	for (int &v : lattice)
		v = int(rng() >> 16);
	const auto smooth = [](int t)
	{ return int(std::int64_t(t) * t * (3 * 1024 - 2 * t) / (1024 * 1024)); };
	std::vector<int> field(size_t(w) * h);
	for (int y = 0; y < h; ++y)
	{
		const std::int64_t fy = std::int64_t(y) * gh * 1024 / h;
		const int y0 = int(fy >> 10), y1 = (y0 + 1) % gh, ty = smooth(int(fy & 1023));
		for (int x = 0; x < w; ++x)
		{
			const std::int64_t fx = std::int64_t(x) * gw * 1024 / w;
			const int x0 = int(fx >> 10), x1 = (x0 + 1) % gw, tx = smooth(int(fx & 1023));
			const int a = lattice[size_t(y0) * gw + x0], b = lattice[size_t(y0) * gw + x1];
			const int c = lattice[size_t(y1) * gw + x0], d = lattice[size_t(y1) * gw + x1];
			const int top = a + (b - a) * tx / 1024, bottom = c + (d - c) * tx / 1024;
			field[size_t(y) * w + x] = top + (bottom - top) * ty / 1024;
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
	const auto ease = [](double t) { return t * t * (3 - 2 * t); };
	for (const auto &octave : octaves)
	{
		const int cell = std::min({octave.first, width, height});
		const int columns = width / cell, rows = height / cell;
		std::vector<double> lattice(size_t(columns) * rows);
		for (double &value : lattice)
			value = 2 * (rng() / 4294967296.0) - 1;
		for (int y = 0; y < height; ++y)
		{
			const int r0 = y / cell, r1 = (r0 + 1) % rows;
			const double ty = ease(double(y % cell) / cell);
			for (int x = 0; x < width; ++x)
			{
				const int c0 = x / cell, c1 = (c0 + 1) % columns;
				const double tx = ease(double(x % cell) / cell);
				const double top =
					lattice[r0 * columns + c0] * (1 - tx) + lattice[r0 * columns + c1] * tx;
				const double bottom =
					lattice[r1 * columns + c0] * (1 - tx) + lattice[r1 * columns + c1] * tx;
				field[size_t(y) * width + x] += octave.second * (top * (1 - ty) + bottom * ty);
			}
		}
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
