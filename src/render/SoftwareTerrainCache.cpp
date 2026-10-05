// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoftwareTerrainCache.h"
#include "GlobalContainer.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace
{
int chunkOf(int tile)
{
	return int(std::floor(double(tile) / SoftwareTerrainCache::ChunkTiles));
}
std::size_t pageStorage(int scale, bool gpu)
{
	return SoftwareTerrainCache::ChunkStorageBytes + (scale * scale * (gpu ? 3 : 1) - 1) *
														 SoftwareTerrainCache::ChunkPixels *
														 SoftwareTerrainCache::ChunkPixels * 4;
}
struct SamplingPlan
{
	int resolution;
	std::size_t budget;
	bool pageFits, viewFits;
};
SamplingPlan samplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						  int vy, int preferredResolution)
{
	const bool gpu = globalContainer->gfx->getOptionFlags() &
					 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	const auto budget = gpu ? SoftwareTerrainCache::GPUBudget : SoftwareTerrainCache::Budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	// A map smaller than one page repeats within that canonical page. All map
	// dimensions are powers of two, so its period divides the page dimensions.
	const auto visiblePages =
		std::uint64_t(
			std::min(x1 - x0 + 1, std::max(1, map.getW() / SoftwareTerrainCache::ChunkTiles))) *
		std::min(y1 - y0 + 1, std::max(1, map.getH() / SoftwareTerrainCache::ChunkTiles));
	const int limit = globalContainer->gfx->maximumTextureSize();
	const auto fitsDevice = [&](int resolution)
	{ return limit <= 0 || SoftwareTerrainCache::ChunkPixels * resolution <= limit; };
	int resolution = preferredResolution;
	// Cache and streaming draws choose density from the complete view, not from
	// each submitted page. Memory pressure must not change sampling page by page.
	while (resolution > 1 &&
		   (!fitsDevice(resolution) || visiblePages * pageStorage(resolution, gpu) > budget))
		resolution /= 2;
	const bool pageFits = fitsDevice(resolution) && pageStorage(resolution, gpu) <= budget;
	return {resolution, budget, pageFits,
			pageFits && visiblePages * pageStorage(resolution, gpu) <= budget};
}
void drawEmergencyTiles(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						int vy, Uint32 visibleTeams, bool wholeMap, int preferredResolution)
{
	auto &compositor = globalContainer->terrainCompositor();
	auto &target = *globalContainer->gfx;
	const int limit = target.maximumTextureSize();
	int scale = preferredResolution;
	while (scale > 1 && limit > 0 && 32 * scale > limit)
		scale /= 2;
	if (limit > 0 && 32 * scale > limit)
		throw std::runtime_error("Device cannot fit a native terrain tile");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
		SDL_CreateSurface(32 * scale, 32 * scale, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
	if (!pixels)
		throw std::bad_alloc();
	TerrainVisual::Surface tile(pixels.get(), scale > 1);
	pixels.release(); // Surface owns pixels only after successful construction.
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x)
			if (wholeMap || map.isMapPartiallyDiscovered(x + vx - 1, y + vy - 1, x + vx + 1,
														 y + vy + 1, visibleTeams))
			{
				compositor.compose(compositor.describe(map, x + vx, y + vy), tile.getSDLSurface(),
								   0, 0, scale);
				tile.markPixelsChanged();
				target.drawSurface(x * 32, y * 32, 32, 32, &tile);
			}
}
// A CPU-only view owns its SDL descriptor, but borrows immutable chunk pixels.
// The producer has already verified every pixel in this rectangle is opaque.
class OpaqueView final : public GAGCore::DrawableSurface
{
  public:
	OpaqueView(SDL_Surface *owner, SDL_Rect rect)
	{
		sdlsurface = SDL_CreateSurfaceFrom(
			rect.w, rect.h, owner->format,
			static_cast<char *>(owner->pixels) + rect.y * owner->pitch + rect.x * 4, owner->pitch);
		if (!sdlsurface)
			throw std::bad_alloc();
		opaquePixels = true;
		opacityRevision = pixelRevision;
	}
};
void buildOpaqueRuns(SoftwareTerrainCache::Chunk &chunk)
{
	std::vector<SDL_Rect> runs;
	for (int y = 0; y < SoftwareTerrainCache::ChunkTiles; ++y)
		for (int x = 0; x < SoftwareTerrainCache::ChunkTiles;)
		{
			auto opaque = [&](int column)
			{ return chunk.opaque[y * SoftwareTerrainCache::ChunkTiles + column]; };
			if (!opaque(x))
			{
				++x;
				continue;
			}
			const int start = x++;
			while (x < SoftwareTerrainCache::ChunkTiles && opaque(x))
				++x;
			SDL_Rect rect{start * 32, y * 32, (x - start) * 32, 32};
			auto previous = std::find_if(
				runs.begin(), runs.end(), [&](const SDL_Rect &run)
				{ return run.x == rect.x && run.w == rect.w && run.y + run.h == rect.y; });
			if (previous == runs.end())
				runs.push_back(rect);
			else
				previous->h += rect.h;
		}
	for (const auto &rect : runs)
		chunk.opaqueRuns.push_back(
			{rect, std::make_unique<OpaqueView>(chunk.image->getSDLSurface(), rect)});
}
} // namespace

bool SoftwareTerrainCache::prepare(const SceneMap &map, GAGCore::Sprite &sprite, int left, int top,
								   int right, int bottom, int vx, int vy, Uint32 visibleTeams,
								   bool wholeMap, int time)
{
	return prepareAtResolution(map, sprite, left, top, right, bottom, vx, vy, visibleTeams,
							   wholeMap, time, 0);
}
bool SoftwareTerrainCache::prepareAtResolution(const SceneMap &map, GAGCore::Sprite &, int left,
											   int top, int right, int bottom, int vx, int vy,
											   Uint32 visibleTeams, bool wholeMap, int time,
											   int preferredResolution)
{
	if (registry.get() != &map.terrainRegistry())
	{
		chunks.clear();
		registry = map.frozenTerrainRegistry();
	}

	PERF_SCOPE_TIME(TerrainCache);
	copies.clear();
	++frame;
	if (!enabled)
		return false;
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	auto &compositor = globalContainer->terrainCompositor();
	const bool nextGPU = globalContainer->gfx->getOptionFlags() &
						 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(nextGPU, time);
	const auto plan =
		samplingPlan(map, left, top, right, bottom, vx, vy,
					 preferredResolution > 0 ? preferredResolution : compositor.scale());
	if (!plan.viewFits)
	{
		chunks.clear(); // Release cache storage before the caller streams pages.
		return false;
	}
	const int nextResolution = plan.resolution;
	const auto budget = plan.budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	if (resolution != nextResolution || gpu != nextGPU)
	{
		chunks.clear();
		resolution = nextResolution;
		gpu = nextGPU;
	}
	const auto storage = pageStorage(resolution, gpu);
	try
	{
		for (int cy = y0; cy <= y1; ++cy)
			for (int cx = x0; cx <= x1; ++cx)
			{
				const int wx = (cx * ChunkTiles) & map.getMaskW(),
						  wy = (cy * ChunkTiles) & map.getMaskH();
				Chunk *entry = nullptr;
				for (auto &c : chunks)
					if (c->x == wx && c->y == wy)
					{
						entry = c.get();
						break;
					}
				// Compare the source neighborhood once per page, rather than
				// decoding sixteen overlapping lattice samples for every tile.
				std::array<Uint32, (ChunkTiles + 2) * (ChunkTiles + 2)> sources{};
				for (int y = -1; y <= ChunkTiles; ++y)
					for (int x = -1; x <= ChunkTiles; ++x)
						sources[(y + 1) * (ChunkTiles + 2) + x + 1] =
							map.getTerrain(wx + x, wy + y) |
							(Uint32(map.terrainTypeAt(wx + x, wy + y)) << 16);
				const auto revisionChanged = [&](const auto &revision)
				{ return compositor.materialRevision(revision.first) != revision.second; };
				bool unchanged = entry && entry->valid && entry->sources == sources &&
								 std::none_of(entry->materialRevisions.begin(),
											  entry->materialRevisions.end(), revisionChanged);
				if (unchanged)
					for (int y = 0; y < ChunkTiles && unchanged; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const bool discovered =
								wholeMap ||
								map.isMapPartiallyDiscovered(wx + x - 1, wy + y - 1, wx + x + 1,
															 wy + y + 1, visibleTeams);
							if (discovered != entry->tiles[y * ChunkTiles + x].discovered)
							{
								unchanged = false;
								break;
							}
						}
				if (unchanged)
				{
					entry->used = frame;
					++hits;
					copies.push_back(
						{entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
					continue;
				}
				std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
				for (int y = 0; y < ChunkTiles; ++y)
					for (int x = 0; x < ChunkTiles; ++x)
					{
						auto &t = tiles[y * ChunkTiles + x];
						t.discovered = wholeMap || map.isMapPartiallyDiscovered(
													   wx + x - 1, wy + y - 1, wx + x + 1,
													   wy + y + 1, visibleTeams);
						if (t.discovered)
							t.recipe = compositor.describe(map, wx + x, wy + y);
					}
				if (!entry)
				{
					if (bytes() + storage > budget)
					{
						auto oldest = chunks.end();
						for (auto it = chunks.begin(); it != chunks.end(); ++it)
							if ((*it)->used != frame &&
								(oldest == chunks.end() || (*it)->used < (*oldest)->used))
								oldest = it;
						if (oldest == chunks.end())
						{
							copies.clear();
							chunks.clear();
							return false;
						}
						chunks.erase(oldest);
					}
					auto fresh = std::make_unique<Chunk>();
					fresh->x = wx;
					fresh->y = wy;
					fresh->scale = resolution;
					std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
						SDL_CreateSurface(ChunkPixels * resolution, ChunkPixels * resolution,
										  SDL_PIXELFORMAT_ARGB8888),
						SDL_DestroySurface);
					if (!pixels)
						throw std::bad_alloc();
					fresh->image =
						std::make_unique<TerrainVisual::Surface>(pixels.get(), resolution > 1);
					pixels.release(); // Keep ownership if the drawable allocation fails.
					entry = fresh.get();
					chunks.push_back(std::move(fresh));
				}
				entry->used = frame;
				const auto materialChanged = [&](TerrainVisual::MaterialId id)
				{
					const auto previous = std::find_if(
						entry->materialRevisions.begin(), entry->materialRevisions.end(),
						[id](const auto &revision) { return revision.first == id; });
					return previous == entry->materialRevisions.end() || revisionChanged(*previous);
				};
				const bool sourcesChanged =
					std::any_of(entry->materialRevisions.begin(), entry->materialRevisions.end(),
								revisionChanged);
				if (!entry->valid || sourcesChanged || entry->tiles != tiles)
				{
					auto *target = entry->image->getSDLSurface();
					const bool wasValid = entry->valid;
					entry->valid = false;
					entry->opaqueRuns.clear();
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const int i = y * ChunkTiles + x, n = 32 * resolution;
							if (wasValid && entry->tiles[i] == tiles[i] &&
								(!tiles[i].discovered ||
								 std::none_of(tiles[i].recipe.samples.begin(),
											  tiles[i].recipe.samples.end(), materialChanged)))
								continue;
							if (tiles[i].discovered)
								compositor.compose(tiles[i].recipe, target, x * n, y * n,
												   resolution);
							else
							{
								SDL_Rect area{x * n, y * n, n, n};
								SDL_FillSurfaceRect(target, &area, 0);
							}
							bool opaque = true, empty = true;
							for (int py = 0; py < n && (opaque || empty); ++py)
								for (int px = 0; px < n; ++px)
								{
									const auto *row = reinterpret_cast<const Uint32 *>(
										static_cast<const Uint8 *>(target->pixels) +
										(y * n + py) * target->pitch);
									const auto alpha = row[x * n + px] >> 24;
									opaque &= alpha == 255;
									empty &= alpha == 0;
									if (!opaque && !empty)
										break;
								}
							entry->opaque[i] = opaque;
							entry->empty[i] = empty;
						}
					entry->image->markPixelsChanged();
					entry->tiles = tiles;
					entry->materialRevisions.clear();
					for (const auto &tile : tiles)
						if (tile.discovered)
							for (auto id : tile.recipe.samples)
								if (std::none_of(entry->materialRevisions.begin(),
												 entry->materialRevisions.end(),
												 [id](const auto &revision)
												 { return revision.first == id; }))
									entry->materialRevisions.emplace_back(
										id, compositor.materialRevision(id));
					if (!gpu)
						buildOpaqueRuns(*entry);
					entry->valid = true;
					++rebuilds;
				}
				else
					++hits;
				entry->sources = sources;
				copies.push_back({entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
			}
	}
	catch (const std::bad_alloc &)
	{
		copies.clear();
		chunks.clear();
		return false;
	}
	return true;
}
void SoftwareTerrainCache::drawUncached(const SceneMap &map, GAGCore::Sprite &sprite, int left,
										int top, int right, int bottom, int vx, int vy,
										Uint32 visibleTeams, bool wholeMap, int time,
										FallbackMode mode)
{
	auto &compositor = globalContainer->terrainCompositor();
	const bool gpu = globalContainer->gfx->getOptionFlags() &
					 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(gpu, time);
	const auto plan = samplingPlan(map, left, top, right, bottom, vx, vy, compositor.scale());
	if (mode == FallbackMode::EmergencyTiles || !plan.pageFits)
	{
		drawEmergencyTiles(map, left, top, right, bottom, vx, vy, visibleTeams, wholeMap,
						   plan.resolution);
		return;
	}
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	for (int cy = y0; cy <= y1; ++cy)
		for (int cx = x0; cx <= x1; ++cx)
		{
			const int pageLeft = std::max(left, cx * ChunkTiles - vx);
			const int pageTop = std::max(top, cy * ChunkTiles - vy);
			const int pageRight = std::min(right, (cx + 1) * ChunkTiles - vx - 1);
			const int pageBottom = std::min(bottom, (cy + 1) * ChunkTiles - vy - 1);
			// Exactly one temporary page is alive. Reuse preparation/drawing so
			// software run sampling and GPU mip neighborhoods match the cache.
			SoftwareTerrainCache page;
			if (page.prepareAtResolution(map, sprite, pageLeft, pageTop, pageRight, pageBottom, vx,
										 vy, visibleTeams, wholeMap, time, plan.resolution))
				page.draw(*globalContainer->gfx);
			else
				drawEmergencyTiles(map, pageLeft, pageTop, pageRight, pageBottom, vx, vy,
								   visibleTeams, wholeMap, plan.resolution);
		}
}
void SoftwareTerrainCache::draw(GAGCore::GraphicContext &target)
{
	for (const auto &copy : copies)
	{
		if (gpu)
		{
			SDL_Rect area{copy.x, copy.y, ChunkPixels, ChunkPixels}, visible;
			if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
				target.drawSurface(visible.x, visible.y, visible.w, visible.h,
								   copy.chunk->image.get(), (visible.x - area.x) * resolution,
								   (visible.y - area.y) * resolution, visible.w * resolution,
								   visible.h * resolution);
			continue;
		}
		for (const auto &run : copy.chunk->opaqueRuns)
		{
			SDL_Rect destination{copy.x + run.rect.x, copy.y + run.rect.y, run.rect.w, run.rect.h},
				visible;
			if (SDL_GetRectIntersection(&destination, &paintBounds, &visible))
				target.drawSurface(visible.x, visible.y, run.image.get(), visible.x - destination.x,
								   visible.y - destination.y, visible.w, visible.h);
		}
		for (int y = 0; y < ChunkTiles; ++y)
			for (int x = 0; x < ChunkTiles; ++x)
			{
				if (copy.chunk->opaque[y * ChunkTiles + x] || copy.chunk->empty[y * ChunkTiles + x])
					continue;
				SDL_Rect area{copy.x + x * 32, copy.y + y * 32, 32, 32}, visible;
				if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
					target.drawSurface(visible.x, visible.y, copy.chunk->image.get(),
									   x * 32 + visible.x - area.x, y * 32 + visible.y - area.y,
									   visible.w, visible.h);
			}
	}
}
std::vector<SDL_Rect> SoftwareTerrainCache::waterRegions(SDL_Rect bounds) const
{
	std::vector<SDL_Rect> remaining{bounds};
	for (const auto &copy : copies)
		for (const auto &run : copy.chunk->opaqueRuns)
		{
			SDL_Rect destination{copy.x + run.rect.x, copy.y + run.rect.y, run.rect.w, run.rect.h},
				coverage;
			if (!SDL_GetRectIntersection(&destination, &paintBounds, &coverage))
				continue;
			std::vector<SDL_Rect> next;
			for (const auto &region : remaining)
			{
				SDL_Rect cut;
				if (!SDL_GetRectIntersection(&region, &coverage, &cut))
				{
					next.push_back(region);
					continue;
				}
				const auto append = [&](SDL_Rect rect)
				{
					if (rect.w > 0 && rect.h > 0)
						next.push_back(rect);
				};
				append({region.x, region.y, region.w, cut.y - region.y});
				append({region.x, cut.y + cut.h, region.w, region.y + region.h - cut.y - cut.h});
				append({region.x, cut.y, cut.x - region.x, cut.h});
				append({cut.x + cut.w, cut.y, region.x + region.w - cut.x - cut.w, cut.h});
				// Bound bookkeeping even for a fragmented or repeated map.
				if (next.size() > 64)
					return {bounds};
			}
			remaining = std::move(next);
		}
	return remaining;
}
