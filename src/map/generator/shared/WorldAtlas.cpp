// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldAtlas.h"
#include <cstring>
namespace MapGeneration
{
const AtlasRegion *atlasRegion(const char *id)
{
	for (int k = 0; k < kAtlasRegionCount; ++k)
		if (std::strcmp(kAtlasRegions[k].id, id) == 0)
			return &kAtlasRegions[k];
	return nullptr;
}

std::vector<unsigned char> decodeAtlas(const AtlasRegion &region)
{
	std::vector<unsigned char> cells;
	cells.reserve(size_t(region.width) * region.height);
	for (unsigned k = 0; k + 1 < region.encodedBytes; k += 2)
		cells.insert(cells.end(), region.encoded[k + 1], region.encoded[k]);
	// A file that encodes the wrong number of cells is a build error, not a runtime one, but
	// never hand back a short buffer: pad with sea or cut to size so callers can index freely.
	cells.resize(size_t(region.width) * region.height, 0);
	return cells;
}
} // namespace MapGeneration
