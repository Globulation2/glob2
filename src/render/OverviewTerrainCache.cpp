// SPDX-License-Identifier: GPL-3.0-or-later
#include "OverviewTerrainCache.h"
#include "TerrainBuiltins.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <limits>
#include <stdexcept>

int OverviewTerrainCache::samplesFor(double pixelsPerCell, int current, int cells,
									int maximumTexture)
{
	// Many samples show five distinct stock sizes; below about six device
	// pixels per cell they would only quadruple the image for no visible gain.
	if (maximumTexture > 0 && std::int64_t(cells) * ManySamples > maximumTexture)
		return FewSamples;
	return pixelsPerCell >= (current == ManySamples ? 5.0 : 6.0) ? ManySamples : FewSamples;
}

int OverviewTerrainCache::resourceSquare(unsigned stock, unsigned capacity, int samples)
{
	const int largest = samples / 2;
	if (!capacity)
		return largest;
	return 1 + int((largest - 1) * std::min(stock, capacity) / capacity);
}

void OverviewTerrainCache::update(const SceneMap &map, int left, int top, int right, int bottom,
								  int vx, int vy, Uint32 visibleTeams, bool wholeMap, int samples,
								  GAGCore::DrawableSurface &target)
{
	if (samples != FewSamples && samples != ManySamples)
		throw std::invalid_argument("Overview samples must be 4 or 8 per cell");
	if (samples != pageSamples)
	{
		pages.clear();
		output.reset();
		pageSamples = samples;
	}
	Output next{
		.x = (left + vx) & map.getMaskW(),
		.y = (top + vy) & map.getMaskH(),
		.columns = right - left + 1,
		.rows = bottom - top + 1,
		.samples = samples,
		.seed = map.terrainSeed(),
		.visibleTeams = wholeMap ? 0u : visibleTeams,
		.wholeMap = wholeMap,
		.terrainRevision = map.terrainRevision(),
		.resourceRevision = map.resourcePresentationRevision(),
		.discoveryRevision = wholeMap ? 0 : map.discoveryRevision(),
		.surfaceIdentity = target.lifetimeIdentity(),
		.surfaceRevision = target.contentRevision(),
	};
	if (output == next && mapIdentity == map.identity() && terrain == map.frozenTerrainRegistry() &&
		resources == map.frozenResourceRegistry() && assets == map.frozenAssetBundle())
		return;
	output.reset();
	copy(map, left, top, right, bottom, vx, vy, visibleTeams, wholeMap, target.getSDLSurface());
	target.markPixelsChanged();
	next.surfaceRevision = target.contentRevision();
	output = next;
}

void OverviewTerrainCache::copy(const SceneMap &map, int left, int top, int right, int bottom,
								int vx, int vy, Uint32 visibleTeams, bool wholeMap,
								SDL_Surface *target)
{
	if (mapIdentity != map.identity() || terrain != map.frozenTerrainRegistry() ||
		resources != map.frozenResourceRegistry() || assets != map.frozenAssetBundle())
	{
		pages.clear();
		capacities.clear();
		mapIdentity = map.identity();
		terrain = map.frozenTerrainRegistry();
		resources = map.frozenResourceRegistry();
		assets = map.frozenAssetBundle();
	}
	auto &compositor = globalContainer->terrainCompositor(assets);
	if (capacities.empty())
	{
		const auto &registry = map.resourceRegistry();
		capacities.resize(registry.size());
		for (unsigned id = 0; id < registry.size(); ++id)
			for (const auto &yield : registry.yields(static_cast<ResourceId>(id)))
				capacities[id] += yield.capacity;
	}
	const int Samples = pageSamples;
	const int Pixels = Tiles * Samples;
	constexpr std::size_t Budget = 32u * 1024u * 1024u;
	const std::size_t PageBytes = sizeof(Page) + std::size_t(Pixels) * Pixels * 4;
	const auto chunkOf = [](int tile) { return int(std::floor(double(tile) / Tiles)); };
	const int cy0 = chunkOf(top + vy), cy1 = chunkOf(bottom + vy);
	const int cx0 = chunkOf(left + vx), cx1 = chunkOf(right + vx);
	// The budget always admits the whole view, so a frame never evicts a page it
	// is about to draw and recomposes it every frame.
	const std::size_t pageLimit = std::max(Budget / PageBytes,
										   std::size_t(cy1 - cy0 + 1) * std::size_t(cx1 - cx0 + 1));
	for (int cy = cy0; cy <= cy1; ++cy)
		for (int cx = cx0; cx <= cx1; ++cx)
		{
			const int wx = (cx * Tiles) & map.getMaskW(), wy = (cy * Tiles) & map.getMaskH();
			const auto key = std::size_t(wy) * map.getW() + wx;
			auto found = pages.find(key);
			if (found == pages.end())
			{
				if (pages.size() + 1 > pageLimit)
					pages.erase(std::min_element(pages.begin(), pages.end(),
												 [](const auto &a, const auto &b)
												 { return a.second.used < b.second.used; }));
				Page candidate;
				candidate.pixels.reset(
					SDL_CreateSurface(Pixels, Pixels, SDL_PIXELFORMAT_ARGB8888));
				if (!candidate.pixels)
					throw std::bad_alloc();
				found = pages.emplace(key, std::move(candidate)).first;
			}
			auto *page = &found->second;
			page->used = ++use;
			std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
			for (int y = -1; y <= Tiles + 1; ++y)
				for (int x = -1; x <= Tiles + 1; ++x)
					vertices[(y + 1) * (Tiles + 3) + x + 1] = map.vertexTerrainAt(wx + x, wy + y);
			const bool changed =
				!page->valid || page->seed != map.terrainSeed() || page->vertices != vertices;
			const int x0 = std::max(left + vx, cx * Tiles),
					  x1 = std::min(right + vx + 1, (cx + 1) * Tiles);
			const int y0 = std::max(top + vy, cy * Tiles),
					  y1 = std::min(bottom + vy + 1, (cy + 1) * Tiles);
			// Keep unseen cells invalid after an edit. A pan can fill them on demand
			// without recomposing the already visible portion of this page.
			if (changed)
				page->resources.fill(std::numeric_limits<int>::min());
			for (int y = y0 - cy * Tiles; y < y1 - cy * Tiles; ++y)
				for (int x = x0 - cx * Tiles; x < x1 - cx * Tiles; ++x)
				{
					const int mx = wx + x, my = wy + y, i = y * Tiles + x;
					const auto &resource = map.getResource(mx, my);
					const int type =
						resource.type != NO_RES_TYPE &&
								(wholeMap || map.isMapPartiallyDiscovered(mx - 1, my - 1, mx + 1,
																		  my + 1, visibleTeams))
							? resource.type
							: NO_RES_TYPE;
					int square = 0;
					if (type != NO_RES_TYPE)
						square = resourceSquare(
							resource.amount,
							unsigned(type) < capacities.size() ? capacities[type] : 0, Samples);
					// The type and square size identify everything composed for the cell.
					const int composed = type == NO_RES_TYPE ? -1 : type << 4 | square;
					if (page->resources[i] == composed)
						continue;
					const auto corners = map.cellCorners(mx, my);
					std::array<std::array<unsigned char, 3>, 4> colors{};
					TerrainVisual::Compositor::CornerColors custom{};
					bool anyCustom = false;
					for (unsigned k = 0; k < corners.size(); ++k)
						if (unsigned(corners[k]) >= TERRAIN_COUNT)
						{
							const auto color = map.terrainPresentation(corners[k]).overview;
							colors[k] = {color.r, color.g, color.b};
							custom[k] = &colors[k];
							anyCustom = true;
						}
					auto *pixels = page->pixels.get();
					compositor.composeOverview(compositor.describeOverview(map, mx, my), pixels,
											   x * Samples, y * Samples, Samples,
											   anyCustom ? &custom : nullptr);
					if (type != NO_RES_TYPE)
					{
						const auto &color = map.resourceRegistry()
												.presentation(static_cast<ResourceId>(type))
												.minimap;
						const Uint32 packed = 0xFF000000u | Uint32(color[0]) << 16 |
											  Uint32(color[1]) << 8 | color[2];
						// Centred, with any odd sample left over below and to the right.
						const int inset = (Samples - square) / 2;
						SDL_Rect area{x * Samples + inset, y * Samples + inset, square, square};
						SDL_FillSurfaceRect(pixels, &area, packed);
					}
					page->resources[i] = composed;
					++compositions;
				}
			page->vertices = vertices;
			page->seed = map.terrainSeed();
			page->valid = true;
			for (int y = 0; y < (y1 - y0) * Samples; ++y)
				std::memcpy(static_cast<Uint8 *>(target->pixels) +
								((y0 - vy - top) * Samples + y) * target->pitch +
								(x0 - vx - left) * Samples * 4,
							static_cast<const Uint8 *>(page->pixels->pixels) +
								((y0 - cy * Tiles) * Samples + y) * page->pixels->pitch +
								(x0 - cx * Tiles) * Samples * 4,
							(x1 - x0) * Samples * 4);
		}
	// A view larger than the budget leaves its pages behind. Once the view is
	// smaller again, return to the budget by dropping the least recently drawn.
	if (pages.size() > pageLimit)
	{
		std::vector<std::pair<std::uint64_t, std::size_t>> ages;
		ages.reserve(pages.size());
		for (const auto &[key, page] : pages)
			ages.emplace_back(page.used, key);
		const auto excess = pages.size() - pageLimit;
		std::nth_element(ages.begin(), ages.begin() + excess, ages.end());
		for (std::size_t i = 0; i < excess; ++i)
			pages.erase(ages[i].second);
	}
}
