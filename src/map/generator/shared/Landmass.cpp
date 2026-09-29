// SPDX-License-Identifier: GPL-3.0-or-later
#include "Landmass.h"
#include "Morphology.h"
#include <algorithm>
namespace MapGeneration
{
std::vector<unsigned char> cleanLandmass(const Torus &t, const std::vector<unsigned char> &land,
										 const CoastCleaning &cleaning)
{
	std::vector<unsigned char> result =
		cleaning.sliverRadius > 0 ? openMask(t, land, cleaning.sliverRadius) : land;
	if (cleaning.minimumWaterTiles > 1)
	{
		std::vector<unsigned char> sea(result.size());
		for (size_t i = 0; i < result.size(); ++i)
			sea[i] = !result[i];
		const std::vector<unsigned char> kept =
			dropSmallRegions(t, sea, cleaning.minimumWaterTiles, GridNeighbors::Eight);
		for (size_t i = 0; i < result.size(); ++i)
			result[i] = !kept[i];
	}
	if (cleaning.minimumIslandTiles > 1)
		result = dropSmallRegions(t, result, cleaning.minimumIslandTiles, GridNeighbors::Eight);
	return result;
}

std::vector<unsigned char> largestRegion(const Torus &t, const std::vector<unsigned char> &mask,
										 GridNeighbors neighbours)
{
	const std::vector<int> labels = connectedRegions(mask, t.w, t.h, true, neighbours);
	std::vector<int> sizes;
	for (int label : labels)
		if (label >= 0)
		{
			if (label >= int(sizes.size()))
				sizes.resize(size_t(label) + 1, 0);
			++sizes[label];
		}
	int best = -1;
	for (int label = 0; label < int(sizes.size()); ++label)
		if (best < 0 || sizes[label] > sizes[best])
			best = label;
	std::vector<unsigned char> result(mask.size(), 0);
	if (best >= 0)
		for (size_t i = 0; i < mask.size(); ++i)
			result[i] = labels[i] == best;
	return result;
}

std::vector<unsigned char> inheritLabels(const Torus &t, const std::vector<unsigned char> &labels,
										 const std::vector<unsigned char> &filled,
										 unsigned char fallback)
{
	std::vector<unsigned char> result = labels;
	int counts[256];
	for (int i = 0; i < t.size(); ++i)
	{
		if (!filled[i])
			continue;
		std::fill(std::begin(counts), std::end(counts), 0);
		const int x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if ((dx || dy) && !filled[j])
					++counts[labels[j]];
			}
		int best = -1;
		for (int v = 0; v < 256; ++v)
			if (counts[v] > 0 && (best < 0 || counts[v] > counts[best]))
				best = v;
		result[i] = best < 0 ? fallback : (unsigned char)best;
	}
	return result;
}
} // namespace MapGeneration
