// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapImage.h"
#include "TerrainPresentation.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorDefinition.h"
#include "Settlements.h"
#include "StartQuality.h"
#include "Utilities.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
struct Category
{
	Uint8 r, g, b;
	TerrainType terrain;
	int resource;
};
constexpr Category terrainCategory(TerrainType type)
{
    const auto c=terrainPresentation(type).image;
    return {c.r,c.g,c.b,type,NO_RES};
}
// Explicit legacy image-format palette: these named built-in content choices
// define nearest-color and cell-majority ties. Keep in sync with CLI.md.
constexpr auto palette = [] {
    constexpr std::array<Category,12> legacy{{terrainCategory(GRASS), terrainCategory(SAND), terrainCategory(WATER),
        {0,64,0,GRASS,WOOD}, {255,255,0,GRASS,WHEAT}, {128,128,128,GRASS,STONE},
        {0,255,255,WATER,ALGA}, {255,0,255,GRASS,PAPYRUS}, {255,0,0,GRASS,CHERRY},
        {255,128,0,GRASS,ORANGE}, {128,0,255,GRASS,PRUNE}, {255,255,255,GRASS,NO_RES}}};
    constexpr auto authoredCount = [] {
        unsigned count=0;
        for (const auto& p : TerrainCompatibilityTable) count += !p.legacyCorners;
        return count;
    }();
    std::array<Category,legacy.size()+authoredCount> result{};
    unsigned cursor=0;
    for (const auto& entry : legacy) result[cursor++]=entry;
    for (unsigned type=0;type<TERRAIN_COUNT;++type)
        if (!terrainUsesLegacyCorners(static_cast<TerrainType>(type)))
            result[cursor++]=terrainCategory(static_cast<TerrainType>(type));
    return result;
}();
constexpr int marker = 11;
std::array<int, palette.size()> paletteResourceIds(const Map& map)
{
    // Resolve the legacy colors by content key, never by a runtime dense ID.
    static constexpr const char* keys[] = {
        "trees", "wheat", "rocks", "algae", "papyrus", "cherry-tree", "orange-tree", "prune-tree"};
    std::array<int, palette.size()> ids;
    ids.fill(NO_RES);
    for (unsigned i = 0; i < std::size(keys); ++i)
        if (const auto id = map.resourceRegistry().find(keys[i])) ids[i + 3] = resourceIndex(*id);
    return ids;
}

using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface surface(SDL_Surface *p)
{
	if (!p)
		throw std::runtime_error(std::string("Map image: ") + SDL_GetError());
	return Surface(p, SDL_DestroySurface);
}
// Classic categories plus ice and trail classify by nearest colour, as they always
// have. Catalogue terrains are recognised only by their exact exported colour, so an
// off-palette shade in an existing image still becomes classic ground rather than a
// gated catalogue type.
int nearest(Uint8 r, Uint8 g, Uint8 b)
{
	int best = 0, distance = std::numeric_limits<int>::max();
	for (int i = 0; i < int(palette.size()); ++i)
	{
		const auto &c = palette[i];
		int d = (int(r) - c.r) * (int(r) - c.r) + (int(g) - c.g) * (int(g) - c.g) +
				(int(b) - c.b) * (int(b) - c.b);
		if (unsigned(c.terrain) >= TERRAIN_COUNT_BEFORE_CATALOGUE && d != 0)
			continue;
		if (d < distance)
		{
			best = i;
			distance = d;
		}
	}
	return best;
}

struct SeamAxis
{
	bool vertical;
	int w, h;

	int extent() const { return vertical ? w : h; }
	int transverse() const { return vertical ? h : w; }
	int index(int along, int across) const
	{
		across = (across % transverse() + transverse()) % transverse();
		return vertical ? across * w + along : along * w + across;
	}
};

template<typename Value>
bool seamEdgesDiffer(const std::vector<Value> &values, const SeamAxis &axis)
{
	for (int across = 0; across < axis.transverse(); ++across)
		if (values[axis.index(0, across)] != values[axis.index(axis.extent() - 1, across)])
			return true;
	return false;
}

template<typename Value>
int signedCrossSectionDistance(const std::vector<Value> &values, const SeamAxis &axis, int band,
							   int along, int across, Value type)
{
	const bool inside = values[axis.index(along, across)] == type;
	int distance = 2 * band + 1;
	for (int delta = 1; delta <= 2 * band; ++delta)
		if ((values[axis.index(along, across - delta)] == type) != inside ||
			(values[axis.index(along, across + delta)] == type) != inside)
		{
			distance = delta;
			break;
		}
	return inside ? distance : -distance;
}

template<typename Value>
int seamProfile(const std::vector<Value> &values, const SeamAxis &axis, int band, int along,
				int across, Value type)
{
	if (!axis.vertical && (across == 0 || across == axis.transverse() - 1))
		return signedCrossSectionDistance(values, axis, band, along, 0, type) +
			   signedCrossSectionDistance(values, axis, band, along,
										  axis.transverse() - 1, type);
	return 2 * signedCrossSectionDistance(values, axis, band, along, across, type);
}

bool inInteriorSeamStrip(int x, int y, int w, int h, int band)
{
	return x > 0 && x < w - 1 && y > 0 && y < h - 1 &&
		   (std::min(x, w - 1 - x) < band || std::min(y, h - 1 - y) < band);
}

int resourceNeighborCount(const std::vector<int> &resources, int w, int h, int i, int type)
{
	const int x = i % w, y = i / w;
	int count = 0;
	for (const auto &d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
		count += resources[((y + d.second + h) % h) * w + (x + d.first + w) % w] == type;
	return count;
}

// Interpolate signed terrain distances in cross-sections through each wrap seam.
// Integer weights keep import repeatable; the two seam cells share the midpoint.
// This joins offset contours rather than copying a row and moving the discontinuity.
template<typename Value>
void repairSeams(std::vector<Value> &terrain, int w, int h, int band,
				 const std::vector<unsigned char> &protectedCells, const std::vector<Value> &types)
{
	if (!band)
		return;
	for (bool vertical : {true, false})
	{
		const auto before = terrain;
		const SeamAxis axis{vertical, w, h};
		// Already matching edges need no repair (in particular all-grass exports).
		if (!seamEdgesDiffer(before, axis))
			continue;
		const int extent = axis.extent();
		for (int b = 0; b < axis.transverse(); ++b)
		{
			// A mismatch elsewhere must not reshape an already aligned section.
			if (before[axis.index(0, b)] == before[axis.index(extent - 1, b)])
				continue;
			for (int j = 0; j < 2 * band; ++j)
			{
				const int a = (extent - band + j) % extent, i = axis.index(a, b);
				if (protectedCells[i])
					continue;
				const int t = j < band ? j + 1 : j;
				int best = std::numeric_limits<int>::min();
				Value chosen = types.front();
				for (const auto type : types)
				{
					// Use the same paired profile at corners so the second axis
					// cannot reopen a seam the first axis already closed.
					const int score =
						(2 * band - t) * seamProfile(before, axis, band, extent - band - 1, b, type) +
						t * seamProfile(before, axis, band, band, b, type);
					if (score > best)
					{
						best = score;
						chosen = type;
					}
				}
				terrain[i] = chosen;
			}
		}
	}
}
// Preserve each legal resource's tile budget while reshaping only the seam strip.
// Boundary groups are locked together; balancing cells are internal to the strip.
// If the protected/terrain constraints leave insufficient room, retain the input.
void repairResourceSeams(std::vector<int> &resources, Map &map, int band,
						 const std::vector<unsigned char> &protectedCells,
						 MapImageImportReport &report)
{
	if (!band)
		return;
	const int w = map.getW(), h = map.getH();
	const auto before = resources;
	std::vector<int> types{NO_RES};
	const auto paletteIds = paletteResourceIds(map);
	for (int c = 3; c < marker; ++c)
		if (paletteIds[c] != NO_RES) types.push_back(paletteIds[c]);
	std::vector<unsigned char> protectedPatches = protectedCells;
	// Budget restoration is confined to cross-sections with an input mismatch.
	// Otherwise repairing a small defect could consume a distant, aligned grove.
	std::vector<unsigned char> repairable(w * h);
	for (bool vertical : {true, false})
	{
		const SeamAxis axis{vertical, w, h};
		for (int across = 0; across < axis.transverse(); ++across)
			if (before[axis.index(0, across)] != before[axis.index(axis.extent() - 1, across)])
				for (int j = 0; j < 2 * band; ++j)
					repairable[axis.index((axis.extent() - band + j) % axis.extent(), across)] = 1;
	}
	for (int i = 0; i < w * h; ++i)
		protectedPatches[i] |= !repairable[i];
	// The palette chooses content; configured spreading determines which deposits
	// can form a renewable seam. Fixed deposits retain their authored locations.
    std::vector<int> spreading{NO_RES};
    for (const int type : types) if (type!=NO_RES) {
        const auto& properties=map.resourcePropertiesByIndex(type);
        if (properties.growthRate && properties.spreadRate && properties.ecology!=ResourceEcology::None)
            spreading.push_back(type);
    }
	for (int i = 0; i < w * h; ++i)
        if (std::find(spreading.begin(),spreading.end(),before[i])==spreading.end())
			protectedPatches[i] = 1;
	repairSeams(resources, w, h, band, protectedPatches, spreading);
	const auto legal = [&](int i, int type) {
		return type == NO_RES || map.isResourceAllowed(i % w, i / w, type);
	};
	for (int i = 0; i < w * h; ++i)
		if (!legal(i, resources[i]))
			resources[i] = NO_RES;
	const auto join = [&](const std::vector<int> &indices) {
		int type = resources[indices.front()];
		for (int i : indices)
		{
			if (protectedPatches[i])
				return;
			if (resources[i] != type || !legal(i, type))
				type = NO_RES;
		}
		for (int i : indices)
			resources[i] = type;
	};
	join({0, w - 1, (h - 1) * w, w * h - 1});
	for (int y = 1; y < h - 1; ++y)
		join({y * w, y * w + w - 1});
	for (int x = 1; x < w - 1; ++x)
		join({x, (h - 1) * w + x});
	std::vector<std::vector<int>> delta;
    std::vector<std::uint64_t> habitats;
	std::vector<int> buckets(w * h);
	const auto category = [&](int type) {
		return int(std::find(types.begin(), types.end(), type) - types.begin());
	};
	const auto bucketFor = [&](int i) {
        std::uint64_t permissions=0;
        for (unsigned t=0;t<types.size();++t)
            if (legal(i,types[t])) permissions|=std::uint64_t(1)<<t;
        const auto found=std::find(habitats.begin(),habitats.end(),permissions);
        if (found!=habitats.end()) return int(found-habitats.begin());
        habitats.push_back(permissions);
        delta.emplace_back(types.size());
        return int(habitats.size()-1);
    };
	for (int i = 0; i < w * h; ++i)
	{
		buckets[i] = bucketFor(i);
		++delta[buckets[i]][category(resources[i])];
		--delta[buckets[i]][category(before[i])];
	}
	// Clear surplus deposits first, making temporary empty slots. A surplus wood
	// patch and a wheat deficit may be far apart; a direct label swap would
	// otherwise refuse a feasible, budget-preserving trade through empty cells.
	for (int bucket = 0; bucket < int(delta.size()); ++bucket)
		for (int source = 1; source < int(types.size()); ++source)
		{
			std::vector<int> candidates;
			for (int i = 0; i < w * h; ++i)
			{
				const int x = i % w, y = i / w;
				if (buckets[i] == bucket && resources[i] == types[source] && !protectedPatches[i] &&
					inInteriorSeamStrip(x, y, w, h, band))
					candidates.push_back(i);
			}
			std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
				return std::make_pair(before[a] != NO_RES,
									  resourceNeighborCount(resources, w, h, a, types[source])) <
					   std::make_pair(before[b] != NO_RES,
									  resourceNeighborCount(resources, w, h, b, types[source]));
			});
			for (int i : candidates)
			{
				if (delta[bucket][source] <= 0)
					break;
				resources[i] = NO_RES;
				--delta[bucket][source];
				++delta[bucket][0];
			}
		}
	// Separate empty budgets by legal terrain. This permits an empty grass cell
	// to become wood while an algae surplus is independently cleared on water.
	for (int bucket = 0; bucket < int(delta.size()); ++bucket)
		for (int target = 0; target < int(types.size()); ++target)
		{
			std::vector<int> candidates;
			for (int i = 0; i < w * h; ++i)
			{
				const int x = i % w, y = i / w;
				if (buckets[i] != bucket || protectedPatches[i] ||
					!inInteriorSeamStrip(x, y, w, h, band) || !legal(i, types[target]))
					continue;
				// Restore an original deposit, or extend an existing patch, never
				// create a disconnected new resource blob merely to meet a budget.
				if (types[target] != NO_RES && before[i] != types[target] &&
					resourceNeighborCount(resources, w, h, i, types[target]) == 0)
					continue;
				candidates.push_back(i);
			}
			std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
				const auto rank = [&](int i) {
					return std::make_pair(before[i] != types[target],
										  -resourceNeighborCount(resources, w, h, i, types[target]));
				};
				return rank(a) < rank(b);
			});
			for (int i : candidates)
			{
				if (delta[bucket][target] >= 0)
					break;
				const int source = category(resources[i]);
				if (delta[bucket][source] <= 0)
					continue;
				resources[i] = types[target];
				--delta[bucket][source];
				++delta[bucket][target];
			}
		}
	for (const auto &d : delta)
		if (std::any_of(d.begin(), d.end(), [](int v) { return v != 0; }))
		{
			resources = before;
			report.resourceSeamFallback = true;
			return;
		}
	for (int i = 0; i < w * h; ++i)
		report.seamResourceChanges += resources[i] != before[i];
}


int wrapCoord(int v, int extent)
{
	return (v % extent + extent) % extent;
}

struct DecodedMapImage
{
	int width = 0;
	int height = 0;
	std::vector<int> cells;
};

DecodedMapImage decodeMapImage(const std::string &path, int mapW, int mapH)
{
	auto source = surface(IMG_Load(path.c_str()));
	// Check before allocating the converted surface and classification grid.
	if (source->w > 8192 || source->h > 8192)
		throw std::runtime_error("Map image dimensions must not exceed 8192 pixels");
	auto pixels = surface(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32));
	if (std::int64_t(pixels->w) * mapH != std::int64_t(pixels->h) * mapW)
		throw std::runtime_error("Map image and requested map must have matching aspect ratios");
	std::vector<int> sourceCells(size_t(pixels->w) * pixels->h);
	for (int y = 0; y < pixels->h; ++y)
		for (int x = 0; x < pixels->w; ++x)
		{
			Uint32 pixel;
			Uint8 r, g, b, a;
			std::memcpy(&pixel, static_cast<Uint8 *>(pixels->pixels) + y * pixels->pitch + x * 4,
						4);
			SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(pixels->format), SDL_GetSurfacePalette(pixels.get()), &r, &g, &b, &a);
			if (a != 255)
				throw std::runtime_error("Map images must be fully opaque");
			sourceCells[y * pixels->w + x] = nearest(r, g, b);
		}
	DecodedMapImage decoded{mapW, mapH, std::vector<int>(mapW * mapH)};
	for (int y = 0; y < mapH; ++y)
		for (int x = 0; x < mapW; ++x)
		{
			std::array<int, palette.size()> votes{};
			const int x0 = x * pixels->w / mapW;
			const int y0 = y * pixels->h / mapH;
			for (int sy = y0; sy < std::max(y0 + 1, (y + 1) * pixels->h / mapH); ++sy)
				for (int sx = x0; sx < std::max(x0 + 1, (x + 1) * pixels->w / mapW); ++sx)
					++votes[sourceCells[sy * pixels->w + sx]];
			decoded.cells[y * mapW + x] =
				int(std::max_element(votes.begin(), votes.end()) - votes.begin());
		}
	return decoded;
}

std::vector<MapGeneratorPoint> findImageMarkers(const std::vector<int> &cells, int w, int h,
											MapImageImportReport &report)
{
	std::vector<unsigned char> visited(w * h);
	std::vector<MapGeneratorPoint> anchors;
	for (int i = 0; i < w * h; ++i)
	{
		if (cells[i] != marker || visited[i])
			continue;
		// Unwrap coordinates along the flood so a marker crossing the seam has a local centroid.
		std::vector<MapGeneratorPoint> queue{{i % w, i / w}};
		visited[i] = 1;
		std::int64_t sumX = 0, sumY = 0;
		for (size_t j = 0; j < queue.size(); ++j)
		{
			const auto p = queue[j];
			sumX += p.x;
			sumY += p.y;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int n = wrapCoord(p.y + dy, h) * w + wrapCoord(p.x + dx, w);
					if (cells[n] == marker && !visited[n])
					{
						visited[n] = 1;
						queue.emplace_back(p.x + dx, p.y + dy);
					}
				}
		}
		if (queue.size() < 4)
		{
			++report.ignoredMarkers;
			continue;
		}
		anchors.emplace_back(wrapCoord(int(std::lround(double(sumX) / queue.size())) - 2, w),
							 wrapCoord(int(std::lround(double(sumY) / queue.size())) - 2, h));
	}
	report.markers = int(anchors.size());
	return anchors;
}

std::vector<unsigned char> clearImageHomes(Map &map, const std::vector<int> &cells,
										 const std::vector<MapGeneratorPoint> &anchors,
										 MapImageImportReport &report)
{
	const int w = map.getW(), h = map.getH();
	std::vector<unsigned char> homes(w * h);
	for (const auto &at : anchors)
		for (int dy = -1; dy <= 5; ++dy)
			for (int dx = -1; dx <= 5; ++dx)
			{
				const int x = wrapCoord(at.x + dx, w);
				const int y = wrapCoord(at.y + dy, h);
				const int i = y * w + x;
				if (homes[i])
					throw std::runtime_error("Colony starting patches overlap");
				homes[i] = 1;
				map.setUMTerrain(x, y, GRASS);
				if (palette[cells[i]].resource != NO_RES)
					++report.clearedResources;
			}
	return homes;
}

std::vector<unsigned char> makeProtectedResources(const std::vector<unsigned char> &homes,
											 const std::vector<MapGeneratorPoint> &anchors, int w, int h)
{
	std::vector<unsigned char> protectedResources = homes;
	for (const auto &at : anchors)
		for (int dy = -20; dy <= 20; ++dy)
			for (int dx = -20; dx <= 20; ++dx)
				protectedResources[wrapCoord(at.y + dy, h) * w + wrapCoord(at.x + dx, w)] = 1;
	return protectedResources;
}

std::vector<TerrainType> applyImportedTerrain(Map &map, const std::vector<int> &cells,
										 const std::vector<unsigned char> &homes,
										 const std::vector<unsigned char> &protectedResources,
										 MapImageImportReport &report)
{
	const int w = map.getW(), h = map.getH();
	std::vector<TerrainType> original(w * h);
	for (int i = 0; i < w * h; ++i)
		original[i] = palette[cells[i]].terrain;
	std::vector<TerrainType> repaired = original;
    // The old corner lattice remains a legacy editing adapter. Explicit
    // materials are restored as whole cells after legacy shore reconciliation.
    for (auto &type : repaired)
		if (!map.terrainUsesLegacyCorners(type))
			type = SAND;
	const auto legacyOriginal = repaired;
	repairSeams(repaired, w, h, report.seamWidth, protectedResources, {GRASS, SAND, WATER});
	for (int i = 0; i < w * h; ++i)
	{
		report.seamTerrainChanges += repaired[i] != legacyOriginal[i];
		map.setUMTerrain(i % w, i / w, homes[i] ? GRASS : repaired[i]);
	}
	map.controlSand();
	if (report.seamWidth)
	{
		// controlSand is in-place and may give identical edge cells different
		// shores. Reconcile with sand: this cannot introduce water/grass contact.
		const auto join = [&](const std::vector<int> &indices) {
			const auto type = map.getUMTerrain(indices.front() % w, indices.front() / w);
			bool mismatch = false;
			for (int i : indices)
			{
				if (protectedResources[i])
					return;
				mismatch |= map.getUMTerrain(i % w, i / w) != type;
			}
			if (mismatch)
				for (int i : indices)
					if (map.getUMTerrain(i % w, i / w) != SAND)
					{
						map.setUMTerrain(i % w, i / w, SAND);
						++report.seamShoreChanges;
					}
		};
		join({0, w - 1, (h - 1) * w, w * h - 1});
		for (int y = 1; y < h - 1; ++y)
			join({y * w, y * w + w - 1});
		for (int x = 1; x < w - 1; ++x)
			join({x, (h - 1) * w + x});
	}
	map.rebuildTerrain();
    // Whole-cell materials are not sand corners. Restore neighboring uniform
    // legacy cells whose only foreign corner was an authored material, so a
    // trail in an ocean does not create walkable shores outside the trail cell.
    // Genuinely mixed classic corners retain the existing shore adapter.
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        const int i=y*w+x;
		if (homes[i] || !map.terrainUsesLegacyCorners(original[i]) ||
			repaired[i] != legacyOriginal[i])
			continue;
		bool authored=false,uniform=true;
        for (int dy=0; dy<=1; ++dy) for(int dx=0; dx<=1; ++dx) {
            const int j=((y+dy)%h)*w+(x+dx)%w;
            if (homes[j]) { uniform=false; continue; }
			if (!map.terrainUsesLegacyCorners(original[j]))
				authored = true;
			else if (original[j]!=original[i] || repaired[j]!=legacyOriginal[j]) uniform=false;
        }
        if (authored && uniform) map.setCellTerrain(x,y,original[i]);
    }
    for (int i=0; i<w*h; ++i) {
		if (!homes[i] && !map.terrainUsesLegacyCorners(original[i]))
			map.setCellTerrain(i%w,i/w,original[i]);
    }
	return original;
}

std::vector<int> collectImportedResources(Map &map, const std::vector<int> &cells,
										 const std::vector<unsigned char> &homes,
										 const std::vector<TerrainType> &original,
										 MapImageImportReport &report)
{
	const int w = map.getW(), h = map.getH();
	std::vector<int> resources(w * h, NO_RES);
	const auto paletteIds = paletteResourceIds(map);
	for (int i = 0; i < w * h; ++i)
	{
		const int x = i % w;
		const int y = i / w;
		const int type = paletteIds[cells[i]];
		const auto material = map.terrainTypeAt(x,y);
		report.terrainChanges += (map.terrainUsesLegacyCorners(material) ? map.getUMTerrain(x, y)
																		 : material) != original[i];
		if (type == NO_RES || homes[i])
			continue;
		if (!map.isResourceAllowed(x, y, type))
		{
			++report.droppedResources;
			continue;
		}
		resources[i] = type;
	}
	return resources;
}

void applyImportedResources(Map &map, GenerationContext &context, const std::vector<int> &resources)
{
	const int w = map.getW(), h = map.getH();
	for (int i = 0; i < w * h; ++i)
	{
		const int x = i % w;
		const int y = i / w;
		const int type = resources[i];
		if (type == NO_RES)
			continue;
		auto resource = map.getResource(x, y);
		resource.type = Uint16(type);
		const auto& properties = map.resourcePropertiesByIndex(type);
		const auto& yield = map.resourceRegistry().yields(static_cast<ResourceId>(type))[materialIndex(properties.primaryMaterial)];
		// Match normal authored deposits: valid amounts are 1..sizesCount-1.
		// Dense patch interiors start mature; fringes include younger deposits.
		int neighbors = 0;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if ((dx || dy) && resources[wrapCoord(y + dy, h) * w + wrapCoord(x + dx, w)] == type)
					++neighbors;
		const int maximum = std::max(1, int(yield.capacity) - 1);
		const int minimum =
			properties.ecology == ResourceEcology::Land && properties.spreadRate && neighbors >= 5 ? std::min(2, maximum) : 1;
		resource.amount = Uint32(minimum + context.bounded("map-image-amounts", maximum - minimum + 1));
		resource.variety = 0;
		map.replaceResource(x, y, resource);
	}
}

void placeImportedSettlements(Game &game, GenerationContext &context, const GenerationRequest &request,
							  const std::vector<MapGeneratorPoint> &anchors)
{
	const int w = game.map.getW(), h = game.map.getH();
	for (int team = 0; team < request.nbTeams; ++team)
		game.addTeam();
	for (int team = 0; team < request.nbTeams; ++team)
	{
		std::vector<unsigned char> home(w * h);
		const auto at = anchors[team];
		for (int dy = -1; dy <= 5; ++dy)
			for (int dx = -1; dx <= 5; ++dx)
				home[wrapCoord(at.y + dy, h) * w + wrapCoord(at.x + dx, w)] = 1;
		if (!MapGeneration::placeSettlement(game, context, team, home, at, "map-image-settlements"))
			throw std::runtime_error(context.detail);
	}
}

void validateImportedMap(Game &game, const GenerationRequest &request)
{
	GeneratorDefinition definition{};
	definition.hasStartingColonies = true;
	const auto error = validateGeneratedWorld(game, request, definition);
	if (!error.empty())
		throw std::runtime_error(error);
	MapGeneration::scoreStarts(game, request.nbTeams);
	const auto script = game.sgslScript.compileScript(&game);
	if (script.type != ErrorReport::ET_OK)
		throw std::runtime_error(script.getErrorString());
}
} // namespace

std::string MapImageImportReport::json() const
{
	return "{\"seam_width\":" + std::to_string(seamWidth) +
		   ",\"seam_terrain_changes\":" + std::to_string(seamTerrainChanges) +
		   ",\"seam_shore_changes\":" + std::to_string(seamShoreChanges) +
		   ",\"seam_resource_changes\":" + std::to_string(seamResourceChanges) +
		   ",\"resource_seam_fallback\":" + (resourceSeamFallback ? "true" : "false") +
		   ",\"markers\":" + std::to_string(markers) +
		   ",\"ignored_markers\":" + std::to_string(ignoredMarkers) +
		   ",\"terrain_changes\":" + std::to_string(terrainChanges) +
		   ",\"cleared_resources\":" + std::to_string(clearedResources) +
		   ",\"dropped_resources\":" + std::to_string(droppedResources) + "}";
}

void exportMapImage(const Game &game, const std::string &path)
{
	const auto &map = game.map;
	const auto paletteIds = paletteResourceIds(map);
	const int w = map.getW(), h = map.getH();
	auto out = surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
	std::vector<int> cells(w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const auto material = map.terrainTypeAt(x,y);
			const auto terrain = map.terrainUsesLegacyCorners(material)
									 ? map.getUMTerrain(x, y)
									 : map.terrainRegistry().appearance(material);
			int c = 0;
            for (int candidate=0; candidate<int(palette.size()); ++candidate)
                if (candidate!=marker && palette[candidate].resource==NO_RES && palette[candidate].terrain==terrain)
                { c=candidate; break; }
			if (unsigned(material) >= TERRAIN_COUNT) c = -1-int(material);
			const auto &resource = map.getResource(x, y);
			for (int r = 3; r < marker; ++r)
				if (paletteIds[r] != NO_RES && resource.type == paletteIds[r])
				{
					c = r;
					break;
				}
			cells[y * w + x] = c;
		}
	for (int team = 0; team < game.teamsCount(); ++team)
		for (int dy = -1; dy <= 5; ++dy)
			for (int dx = -1; dx <= 5; ++dx)
				cells[map.normalizeY(game.teams[team]->startPosY + dy) * w +
					  map.normalizeX(game.teams[team]->startPosX + dx)] = marker;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int index = cells[y * w + x];
			const auto c = index < 0 ? map.terrainPresentation(TerrainType(-1-index)).image
				: TerrainColor{palette[index].r, palette[index].g, palette[index].b};
			const Uint32 pixel = SDL_MapRGBA(SDL_GetPixelFormatDetails(out->format), SDL_GetSurfacePalette(out.get()), c.r, c.g, c.b, 255);
			std::memcpy(static_cast<Uint8 *>(out->pixels) + y * out->pitch + x * 4, &pixel, 4);
		}
	if (!IMG_SavePNG(out.get(), path.c_str()))
		throw std::runtime_error(std::string("Cannot write map image: ") + SDL_GetError());
}

void importMapImage(Game &game, const std::string &path, GenerationRequest &request,
					int expectedTeams, MapImageImportReport &report, int seamWidth)
{
	if (game.teamsCount() != 0 || request.wDec < 6 || request.wDec > 9 || request.hDec < 6 ||
		request.hDec > 9 || request.nbWorkers < 1 || request.nbWorkers > 8)
		throw std::runtime_error(
			"Image import requires a fresh game and valid map dimensions/workers");
	const int w = 1 << request.wDec;
	const int h = 1 << request.hDec;
	const auto decoded = decodeMapImage(path, w, h);
	const auto anchors = findImageMarkers(decoded.cells, w, h, report);
	if (anchors.empty() || anchors.size() > Team::MAX_COUNT)
		throw std::runtime_error("Map image requires 1.." + std::to_string(Team::MAX_COUNT) + " colony markers (observed " +
								 std::to_string(anchors.size()) + ")");
	if (expectedTeams && expectedTeams != report.markers)
		throw std::runtime_error("Expected " + std::to_string(expectedTeams) +
							 " colony markers; observed " + std::to_string(report.markers));
	request.nbTeams = report.markers;
	GenerationContext context(request);
	struct RestoreRandom
	{
		MersenneTwister saved = syncRandEngine();
		~RestoreRandom() { syncRandEngine() = saved; }
	} restore;
	setSyncRandSeed(GenerationContext::deriveSeed(request.seed, "map-image-engine"));
	game.gameHeader.setRandomSeed(request.seed);
	game.map.setSize(request.wDec, request.hDec);
	game.map.setGame(&game);
	auto &map = game.map;
	const auto homes = clearImageHomes(map, decoded.cells, anchors, report);
	const auto protectedResources = makeProtectedResources(homes, anchors, w, h);
	report.seamWidth = seamWidth < 0 ? std::min(12, std::max(2, std::min(w, h) / 32)) : seamWidth;
	if (report.seamWidth > 16 || report.seamWidth < 0 || 2 * report.seamWidth + 2 >= std::min(w, h))
		throw std::runtime_error("Image seam width does not fit the map");
	const auto original = applyImportedTerrain(map, decoded.cells, homes, protectedResources, report);
	auto resources = collectImportedResources(map, decoded.cells, homes, original, report);
	repairResourceSeams(resources, map, report.seamWidth, protectedResources, report);
	applyImportedResources(map, context, resources);
	placeImportedSettlements(game, context, request, anchors);
	validateImportedMap(game, request);
}
