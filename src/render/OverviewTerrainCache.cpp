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

void OverviewTerrainCache::update(const SceneMap &map, int left, int top, int right, int bottom,
								  int vx, int vy, Uint32 visibleTeams, bool wholeMap,
								  GAGCore::DrawableSurface &target)
{
	Output next{
		.x = (left + vx) & map.getMaskW(),
		.y = (top + vy) & map.getMaskH(),
		.columns = right - left + 1,
		.rows = bottom - top + 1,
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
		mapIdentity = map.identity();
		terrain = map.frozenTerrainRegistry();
		resources = map.frozenResourceRegistry();
		assets = map.frozenAssetBundle();
	}
	auto &compositor = globalContainer->terrainCompositor(assets);
	constexpr int Samples = TerrainVisual::Compositor::OverviewSamples;
	constexpr int Pixels = Tiles * Samples;
	constexpr std::size_t Budget = 32u * 1024u * 1024u;
	constexpr std::size_t PageBytes = sizeof(Page) + Pixels * Pixels * 4;
	const auto chunkOf = [](int tile) { return int(std::floor(double(tile) / Tiles)); };
	for (int cy = chunkOf(top + vy); cy <= chunkOf(bottom + vy); ++cy)
		for (int cx = chunkOf(left + vx); cx <= chunkOf(right + vx); ++cx)
		{
			const int wx = (cx * Tiles) & map.getMaskW(), wy = (cy * Tiles) & map.getMaskH();
			const auto key = std::size_t(wy) * map.getW() + wx;
			auto found = pages.find(key);
			if (found == pages.end())
			{
				if ((pages.size() + 1) * PageBytes > Budget)
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
					if (page->resources[i] == type)
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
											   x * Samples, y * Samples,
											   anyCustom ? &custom : nullptr);
					if (type != NO_RES_TYPE)
					{
						const auto &color = map.resourceRegistry()
												.presentation(static_cast<ResourceId>(type))
												.minimap;
						for (int py = y * Samples; py < (y + 1) * Samples; ++py)
						{
							auto *row = reinterpret_cast<Uint32 *>(
								static_cast<Uint8 *>(pixels->pixels) + py * pixels->pitch);
							for (int px = x * Samples; px < (x + 1) * Samples; ++px)
							{
								const auto ground = row[px];
								row[px] = 0xFF000000u |
										  (((((ground >> 16) & 255) + 3 * color[0]) / 4) << 16) |
										  (((((ground >> 8) & 255) + 3 * color[1]) / 4) << 8) |
										  ((ground & 255) + 3 * color[2]) / 4;
							}
						}
					}
					page->resources[i] = type;
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
}
