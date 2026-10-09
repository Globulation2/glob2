#include "GenerationWork.h"
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			open.at(size_t(y) * t.w + x) = map.isFreeForBuilding(x, y);
		}
	}
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			int length = 0;
			while (length < size && buildable.at(t.at(x + length, y)))
			{
				::MapGeneration::generationCheckpoint();
				++length;
			}
			run.at(size_t(y) * t.w + x) = length;
		}
	}
	std::vector<unsigned char> anchors(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			bool fits = true;
			for (int dy = 0; dy < size && fits; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				fits = run.at(t.at(x, y + dy)) >= size;
			}
			anchors.at(size_t(y) * t.w + x) = fits;
		}
	}
	return anchors;
}

int buildSites(const Torus &t, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &region, int size)
{
	std::vector<unsigned char> usable(buildable.size(), 0);
	for (size_t i = 0; i < usable.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		usable.at(i) = buildable.at(i) && region.at(i);
	}
	const std::vector<unsigned char> anchors = buildAnchors(t, usable, size);
	int count = 0;
	for (unsigned char a : anchors)
	{
		::MapGeneration::generationCheckpoint();
		count += a;
	}
	return count;
}

std::vector<unsigned char> potentialBuildingTiles(const Map &map)
{
	const Torus t(map);
	std::vector<unsigned char> open(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		open.at(i) = map.terrainPropertiesAt(i).buildable && !map.isResource(t.remainderX(i), i / t.w) &&
					 map.getBuilding(t.remainderX(i), i / t.w) == NOGBID;
	}
	return open;
}
BuildingArrangement arrangeBuildingGrid(const Torus &t, const std::vector<unsigned char> &buildable,
										const std::vector<unsigned char> &walking,
										const BuildingGrid &grid, const std::vector<int> &entrances)
{
	BuildingArrangement result;
	const auto b = grid.bounds;
	const int64_t extentX = int64_t(b.x1) - b.x0, extentY = int64_t(b.y1) - b.y0;
	if (buildable.size() != size_t(t.size()) || walking.size() != size_t(t.size()) || extentX < 1 ||
		extentY < 1 || extentX > t.w || extentY > t.h || grid.width < 1 || grid.height < 1 ||
		grid.width > t.w || grid.height > t.h || grid.gap < 1 || grid.gap > std::max(t.w, t.h) ||
		grid.inset < 1 || grid.inset > std::max(t.w, t.h))
	{
		result.failure = "Invalid building grid bounds, footprint, gap or masks.";
		return result;
	}
	auto remaining = walking;
	// Fixed row-major order makes the result reproducible and reviewable. This is a
	// feasible layout fixture, not a maximal rectangle-packing solver.
	for (int64_t y = int64_t(b.y0) + grid.inset; y + grid.height <= int64_t(b.y1) - grid.inset;
		 y += grid.height + grid.gap)
	{
		::MapGeneration::generationCheckpoint();
		for (int64_t x = int64_t(b.x0) + grid.inset; x + grid.width <= int64_t(b.x1) - grid.inset;
			 x += grid.width + grid.gap)
		{
			::MapGeneration::generationCheckpoint();
			bool fits = true;
			for (int dy = 0; fits && dy < grid.height; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = 0; fits && dx < grid.width; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					fits = buildable.at(t.at(int(x) + dx, int(y) + dy));
				}
			}
			if (!fits)
				continue;
			result.footprints.push_back(
				{int(x), int(y), int(x) + grid.width, int(y) + grid.height});
			for (int dy = 0; dy < grid.height; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = 0; dx < grid.width; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					remaining.at(t.at(int(x) + dx, int(y) + dy)) = 0;
				}
			}
		}
	}
	// Only reachability matters here. Stop once every proposed building has a
	// reachable cardinal face instead of flooding the entire world for a local grid.
	std::vector<unsigned char> visited(t.size(), 0);
	std::vector<int> queue;
	for (int i : entrances)
	{
		::MapGeneration::generationCheckpoint();
		if (i < 0 || i >= t.size())
		{
			result.failure = "Invalid building-arrangement entrance.";
			return result;
		}
		if (remaining.at(i) && !visited.at(i))
		{
			visited.at(i) = 1;
			queue.push_back(i);
		}
	}
	std::vector<int> footprintAt(t.size(), -1);
	for (size_t id = 0; id < result.footprints.size(); ++id)
	{
		::MapGeneration::generationCheckpoint();
		const auto &box = result.footprints.at(id);
		for (int y = box.y0; y < box.y1; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = box.x0; x < box.x1; ++x)
			{
				::MapGeneration::generationCheckpoint();
				footprintAt.at(t.at(x, y)) = int(id);
			}
		}
	}
	std::vector<unsigned char> accessible(result.footprints.size(), 0);
	size_t missing = accessible.size();
	for (size_t head = 0; missing && head < queue.size(); ++head)
	{
		::MapGeneration::generationCheckpoint();
		const int tile = queue.at(head), x = t.remainderX(tile), y = tile / t.w;
		for (int n : {t.at(x - 1, y), t.at(x + 1, y), t.at(x, y - 1), t.at(x, y + 1)})
		{
			::MapGeneration::generationCheckpoint();
			const int id = footprintAt.at(n);
			if (id >= 0 && !accessible.at(id))
			{
				accessible.at(id) = 1;
				--missing;
			}
		}
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int n = t.at(x + dx, y + dy);
				if (remaining.at(n) && !visited.at(n))
				{
					visited.at(n) = 1;
					queue.push_back(n);
				}
			}
		}
	}
	if (missing)
		result.failure = "A proposed building has no reachable gathering/circulation face.";
	return result;
}
} // namespace MapGeneration
