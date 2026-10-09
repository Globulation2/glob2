#include "GenerationWork.h"
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
		{
			::MapGeneration::generationCheckpoint();
			sea.at(i) = !result.at(i);
		}
		const std::vector<unsigned char> kept =
			dropSmallRegions(t, sea, cleaning.minimumWaterTiles, GridNeighbors::Eight);
		for (size_t i = 0; i < result.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			result.at(i) = !kept.at(i);
		}
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
	{
		::MapGeneration::generationCheckpoint();
		if (label >= 0)
		{
			if (label >= int(sizes.size()))
				sizes.resize(size_t(label) + 1, 0);
			++sizes.at(label);
		}
	}
	int best = -1;
	for (int label = 0; label < int(sizes.size()); ++label)
	{
		::MapGeneration::generationCheckpoint();
		if (best < 0 || sizes.at(label) > sizes.at(best))
			best = label;
	}
	std::vector<unsigned char> result(mask.size(), 0);
	if (best >= 0)
		for (size_t i = 0; i < mask.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			result.at(i) = labels.at(i) == best;
		}
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
		::MapGeneration::generationCheckpoint();
		if (!filled.at(i))
			continue;
		std::fill(std::begin(counts), std::end(counts), 0);
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int j = t.at(x + dx, y + dy);
				if ((dx || dy) && !filled.at(j))
					++counts[labels.at(j)];
			}
		}
		int best = -1;
		for (int v = 0; v < 256; ++v)
		{
			::MapGeneration::generationCheckpoint();
			if (counts[v] > 0 && (best < 0 || counts[v] > counts[best]))
				best = v;
		}
		result.at(i) = best < 0 ? fallback : (unsigned char)best;
	}
	return result;
}
} // namespace MapGeneration
