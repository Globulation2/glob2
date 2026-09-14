// SPDX-License-Identifier: GPL-3.0-or-later
#include "Room.h"
#include "Map.h"
namespace MapGeneration
{
std::vector<unsigned char> buildableTiles(const Map &map)
{
	const Torus t(map);
	std::vector<unsigned char> open(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			open[size_t(y) * t.w + x] = map.isFreeForBuilding(x, y);
	return open;
}

std::vector<unsigned char> buildAnchors(const Torus &t, const std::vector<unsigned char> &buildable,
										int size)
{
	// Rows first: how many buildable tiles run rightwards from each tile (capped at size), then
	// whether `size` rows below each tile all run that far.
	const int n = t.size();
	std::vector<int> run(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			int length = 0;
			while (length < size && buildable[t.at(x + length, y)])
				++length;
			run[size_t(y) * t.w + x] = length;
		}
	std::vector<unsigned char> anchors(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			bool fits = true;
			for (int dy = 0; dy < size && fits; ++dy)
				fits = run[t.at(x, y + dy)] >= size;
			anchors[size_t(y) * t.w + x] = fits;
		}
	return anchors;
}

int buildSites(const Torus &t, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &region, int size)
{
	std::vector<unsigned char> usable(buildable.size(), 0);
	for (size_t i = 0; i < usable.size(); ++i)
		usable[i] = buildable[i] && region[i];
	const std::vector<unsigned char> anchors = buildAnchors(t, usable, size);
	int count = 0;
	for (unsigned char a : anchors)
		count += a;
	return count;
}
} // namespace MapGeneration
