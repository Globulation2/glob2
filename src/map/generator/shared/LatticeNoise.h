// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <random>
#include <vector>
namespace MapGeneration
{
// Smooth value noise on a lattice that tiles the map's torus exactly, so a field wraps like the
// map does and the seam can't be seen. HeightMap (Perlin noise faded across the wrap) is the
// other option; these are cheaper and exactly periodic.
//
// Three flavours are in use, each with its own arithmetic, and a generator's output depends on
// which it draws from. They should converge on one once a revision bump is acceptable; until
// then, a new generator should take PeriodicNoise, which samples at any continuous point.

/// A lattice of random values about `cell` tiles apart (a whole number of cells spans the map),
/// blended with a smoothstep and sampled at any point. Values are in [0, 1).
class PeriodicNoise
{
  public:
	PeriodicNoise(int width, int height, double cell, std::mt19937 &random);
	double at(double x, double y) const;

  private:
	int width, height, columns, rows;
	std::vector<double> lattice;
};

/// The same idea in integer arithmetic, one value per tile: a lattice about `period` tiles
/// apart, smoothstep-blended. Values are 0..65535.
std::vector<int> periodicNoise(int w, int h, int period, std::mt19937 &rng);

/// periodicNoise at period, period/2, ... weighted 2:1 per octave, normalised back to 0..65535.
std::vector<int> fractalNoise(int w, int h, int period, int octaves, std::mt19937 &rng);

/// Four fixed octaves (32, 16, 8 and 4 tiles, or the map if smaller) whose cells divide the map
/// exactly, normalised to [-1, 1] by the field's own peak.
std::vector<float> torusNoise(int width, int height, std::mt19937 &rng);

/// The value below which `percent` of the samples fall; 0 with no samples.
int percentile(std::vector<int> samples, int percent);
} // namespace MapGeneration
