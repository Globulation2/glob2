// SPDX-License-Identifier: GPL-3.0-or-later
#include "Rivers.h"
#include "GenerationContext.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
/// A draw in [0, 1) from a named stream, so a bed replays exactly from the seed.
double unitDraw(GenerationContext &context, const std::string &stream)
{
	constexpr std::uint32_t kSteps = 4096;
	return double(context.bounded(stream, kSteps)) / double(kSteps);
}
} // namespace

River drawRiver(const Torus &t, GenerationContext &context, const std::string &stream,
				bool vertical, double offset, const RiverStyle &style)
{
	River river;
	river.vertical = vertical;
	const double along = vertical ? t.h : t.w;     // the side the bed crosses
	const double across = vertical ? t.w : t.h;    // the side it strays over
	const int harmonics = std::max(1, style.harmonics);

	// The meander. Every harmonic completes a whole number of cycles over the crossing, so the
	// offset at the far end is exactly the offset at the near one and the loop closes with no jump -
	// no cross-fade needed, unlike a bed pressed along a straight line. Amplitudes fall as 1/k so
	// the bed makes a few long bends with small wrinkles on them, which is what a river looks like;
	// equal amplitudes give a wobble with no shape to it.
	std::vector<double> amplitude(harmonics), phase(harmonics);
	double total = 0;
	for (int k = 0; k < harmonics; ++k)
	{
		amplitude[k] = (0.35 + 0.65 * unitDraw(context, stream)) / double(k + 1);
		phase[k] = unitDraw(context, stream) * 2.0 * kPi;
		total += amplitude[k];
	}
	const double reach = style.wander * across;
	for (double &a : amplitude)
		a *= total > 0 ? reach / total : 0.0;
	const double swellPhase = unitDraw(context, stream) * 2.0 * kPi;

	const int points = std::max(8, int(std::lround(along / std::max(0.5, style.step))));
	river.line.reserve(points);
	river.radius.reserve(points);
	for (int i = 0; i < points; ++i)
	{
		const double s = along * double(i) / double(points); // 0 to just short of a full crossing
		double stray = 0;
		for (int k = 0; k < harmonics; ++k)
			stray += amplitude[k] * std::sin(2.0 * kPi * double(k + 1) * s / along + phase[k]);
		const double lateral = offset + stray;
		river.line.push_back(vertical ? ShapePoint{lateral, s} : ShapePoint{s, lateral});
		river.radius.push_back(
			style.halfWidth *
			(1.0 + style.swell * std::sin(2.0 * kPi * 2.0 * s / along + swellPhase)));
	}
	return river;
}

std::vector<unsigned char> riverWater(const Torus &t, const River &river)
{
	std::vector<StrokePoint> stroke;
	stroke.reserve(river.line.size() + 1);
	for (size_t i = 0; i < river.line.size(); ++i)
		stroke.push_back({river.line[i].x, river.line[i].y, river.radius[i]});
	// Closed across the seam by running past it, not by strokePath's `closed`.
	//
	// strokePath works in continuous coordinates and wraps only its writes - its own contract is
	// that a path may run past the edge - so asking it to close the loop draws a segment from the
	// last point back to the first the long way, straight across the whole map. Every bed came out
	// with a ruler-straight reach as well as its meander, and those tiles were scored as part of
	// the bed. The meander is periodic over the crossing, so the point one step past the end is
	// exactly the first point shifted by a whole map, and stepping to it closes the loop over the
	// seam with an ordinary segment.
	stroke.push_back({river.line[0].x + (river.vertical ? 0.0 : double(t.w)),
					  river.line[0].y + (river.vertical ? double(t.h) : 0.0), river.radius[0]});
	std::vector<unsigned char> water(t.size(), 0);
	strokePath(water, t, stroke, 1, false);
	return water;
}

std::vector<RiverFord> fordSites(const Torus &t, const River &river,
								 const std::vector<unsigned char> &walkable,
								 const std::vector<int> &components, double reach, int apart)
{
	std::vector<RiverFord> sites;
	const int n = int(river.line.size());
	int lastTaken = -apart;
	for (int i = 0; i < n; ++i)
	{
		if (i - lastTaken < apart)
			continue;
		const ShapePoint tangent = ChannelDetail::tangentAt(t, river.line, i, true);
		const double nx = -tangent.y, ny = tangent.x, probe = river.radius[i] + reach;
		const ShapePoint p = river.line[i];
		const int a = ChannelDetail::tileOf(t, p.x + nx * probe, p.y + ny * probe);
		const int b = ChannelDetail::tileOf(t, p.x - nx * probe, p.y - ny * probe);
		if (!walkable[a] || !walkable[b])
			continue;
		sites.push_back({i, components[a], components[b]});
		lastTaken = i;
	}
	return sites;
}

std::vector<int> fordsToRejoin(const std::vector<RiverFord> &sites, int components)
{
	std::vector<int> parent(std::max(0, components));
	for (size_t i = 0; i < parent.size(); ++i)
		parent[i] = int(i);
	const auto find = [&parent](int a)
	{
		while (parent[a] != a)
			a = parent[a] = parent[parent[a]];
		return a;
	};
	std::vector<int> taken;
	for (size_t i = 0; i < sites.size(); ++i)
	{
		const RiverFord &site = sites[i];
		if (site.near < 0 || site.far < 0 || site.near >= components || site.far >= components)
			continue;
		const int a = find(site.near), b = find(site.far);
		if (a == b)
			continue;
		parent[a] = b;
		taken.push_back(int(i));
	}
	return taken;
}

std::vector<int> fordsSpreadAlong(const std::vector<RiverFord> &sites, std::vector<int> taken,
								  int wanted, int points)
{
	const auto apart = [&](int a, int b)
	{
		const int gap = std::abs(sites[a].index - sites[b].index);
		return std::min(gap, points - gap);
	};
	std::vector<unsigned char> chosen(sites.size(), 0);
	for (const int i : taken)
		chosen[i] = 1;
	while (int(taken.size()) < wanted && taken.size() < sites.size())
	{
		int best = -1, bestGap = -1;
		for (size_t i = 0; i < sites.size(); ++i)
		{
			if (chosen[i])
				continue;
			// The first ford of all has nothing to stand clear of, so it takes the whole river.
			int gap = points;
			for (const int have : taken)
				gap = std::min(gap, apart(int(i), have));
			if (gap > bestGap)
			{
				bestGap = gap;
				best = int(i);
			}
		}
		if (best < 0)
			break;
		chosen[best] = 1;
		taken.push_back(best);
	}
	return taken;
}

void layFord(TerrainSketch &terrain, const Torus &t, const River &river, int index,
			 double halfWidth, double reach)
{
	stampFord(terrain, t, fordAlong(t, river.line, river.radius, index, true, halfWidth, reach));
}
} // namespace MapGeneration
