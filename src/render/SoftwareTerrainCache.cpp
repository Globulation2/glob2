// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoftwareTerrainCache.h"
#include "GlobalContainer.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace
{
constexpr int NativeTilePixels =
	SoftwareTerrainCache::ChunkPixels / SoftwareTerrainCache::ChunkTiles;
int chunkOf(int tile)
{
	return int(std::floor(double(tile) / SoftwareTerrainCache::ChunkTiles));
}
std::size_t pageStorage(int scale, bool gpu, int downsample = 1)
{
	// Retain the fixed recipe/run allowance at every density. GPU accounting
	// also reserves texture and mip storage alongside the CPU surface.
	const std::size_t pixels = SoftwareTerrainCache::ChunkPixels * scale / downsample;
	return SoftwareTerrainCache::ChunkStorageBytes -
		   SoftwareTerrainCache::ChunkPixels * SoftwareTerrainCache::ChunkPixels * 4 +
		   pixels * pixels * 4 * (gpu ? 3 : 1);
}
// Normal surface destruction flushes queued renderer references and releases
// textures on both GPU backends. Keep the composed pixels owned by the page.
void retireTexture(SoftwareTerrainCache::Chunk &chunk)
{
	chunk.parked.reset(chunk.image->takePixels());
	chunk.image.reset();
}
struct SamplingPlan
{
	int resolution;
	int downsample;
	std::size_t budget;
	bool pageFits, viewFits;
};
SamplingPlan samplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						  int vy, int preferredResolution, int preferredDownsample = 0)
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
	// A zoomed-out view can exceed the budget even at native density. Streaming
	// then recomposes and uploads the entire view every frame. Retain filtered
	// pages instead, down to the nearest power-of-two display density. Always
	// rounding up would leave a 2x HiDPI crossfade streaming just above half
	// density. Nearest-level selection limits magnification to sqrt(2).
	// A forced density keeps streaming pages identical to the complete-view plan.
	int downsample = preferredDownsample > 0 ? preferredDownsample : 1;
	if (gpu && !preferredDownsample && resolution == 1)
	{
		auto &target = *globalContainer->gfx;
		// Offscreen captures can override the window DPI. Use the active
		// target's raster scale so atlas detail is independent of window size.
		const double density = target.mapTransformScale() * target.getRasterScale();
		while (downsample < NativeTilePixels && density <= std::sqrt(2.0) / (downsample * 2) &&
			   visiblePages * pageStorage(resolution, gpu, downsample) > budget)
			downsample *= 2;
	}
	const auto storage = pageStorage(resolution, gpu, downsample);
	const bool pageFits =
		(limit <= 0 || SoftwareTerrainCache::ChunkPixels * resolution / downsample <= limit) &&
		storage <= budget;
	// The entire view needs CPU pixels, but its textures can be streamed from
	// those pixels under the cache budget. Reserve room for at least one upload.
	const auto cpuStorage = pageStorage(resolution, false, downsample);
	const auto minimumStorage = gpu ? visiblePages * cpuStorage + storage - cpuStorage
		: visiblePages * storage;
	const auto cacheBudget = gpu ? SoftwareTerrainCache::GPUCacheBudget : budget;
	return {resolution, downsample, cacheBudget, pageFits, pageFits && minimumStorage <= cacheBudget};
}

// Box-filter composed coverage in premultiplied space, then retain straight
// alpha for the ordinary surface upload. Transparent (undiscovered) cells must not
// darken their neighbours.
// Both surfaces are ARGB8888; the power-of-two divisor divides one native tile.
// Filtering each tile independently preserves exact tile-aligned page crops.
void reduceTile(SDL_Surface *source, SDL_Surface *target, int ox, int oy, int divisor)
{
	const int size = NativeTilePixels / divisor, count = divisor * divisor;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			unsigned alpha = 0, rgb[3] = {};
			for (int dy = 0; dy < divisor; ++dy)
			{
				const auto *row =
					reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(source->pixels) +
													 (y * divisor + dy) * source->pitch);
				for (int dx = 0; dx < divisor; ++dx)
				{
					const Uint32 p = row[x * divisor + dx];
					const unsigned a = p >> 24;
					alpha += a;
					for (int k = 0; k < 3; ++k)
						rgb[k] += ((p >> (k * 8)) & 255) * a;
				}
			}
			Uint32 pixel = ((alpha + count / 2) / count) << 24;
			for (int k = 0; k < 3; ++k)
				pixel |= (alpha ? (rgb[k] + alpha / 2) / alpha : 0) << (k * 8);
			auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
												   (oy + y) * target->pitch);
			row[ox + x] = pixel;
		}
}
SamplingPlan viewSamplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
							  int vy, int preferredResolution, bool tiledCapture,
							  int preferredDownsample = 0)
{
	// A narrow atlas edge must not select HD pages after its wider neighbor
	// selected native pages. That changes seams and repeatedly clears the cache.
	// Select density from the complete capture, then admit each bounded tile.
	if (tiledCapture)
	{
		const auto capture =
			samplingPlan(map, 0, 0, map.getW() - 1, map.getH() - 1, 0, 0, preferredResolution);
		preferredResolution = capture.resolution;
		preferredDownsample = capture.downsample;
	}
	return samplingPlan(map, left, top, right, bottom, vx, vy, preferredResolution,
						preferredDownsample);
}
void drawEmergencyTiles(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						int vy, Uint32 visibleTeams, bool wholeMap, int preferredResolution)
{
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
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
								   bool wholeMap, int time, bool tiledCapture)
{
	return prepareAtResolution(map, sprite, left, top, right, bottom, vx, vy, visibleTeams,
							   wholeMap, time, 0, tiledCapture);
}
bool SoftwareTerrainCache::prepareAtResolution(const SceneMap &map, GAGCore::Sprite &, int left,
											   int top, int right, int bottom, int vx, int vy,
											   Uint32 visibleTeams, bool wholeMap, int time,
											   int preferredResolution, bool tiledCapture,
											   int preferredDownsample)
{
	if (registry.get() != &map.terrainRegistry() || assets != map.frozenAssetBundle())
	{
		chunks.clear();
		registry = map.frozenTerrainRegistry();
        assets = map.frozenAssetBundle();
	}

	PERF_SCOPE_TIME(TerrainCache);
	copies.clear();
	++frame;
	if (!enabled)
		return false;
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	const bool nextGPU = globalContainer->gfx->getOptionFlags() &
						 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(nextGPU, time);
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy,
						 preferredResolution > 0 ? preferredResolution : compositor.scale(),
						 tiledCapture, preferredDownsample);
	if (!plan.viewFits)
	{
		chunks.clear(); // Release cache storage before the caller streams pages.
		return false;
	}
	const int nextResolution = plan.resolution;
	const auto budget = plan.budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	if (gpu != nextGPU)
	{
		chunks.clear();
	}
	resolution = nextResolution;
	gpu = nextGPU;
	downsample = plan.downsample;
	const auto storage = pageStorage(resolution, false, downsample);
	const auto uploadReserve = gpu ? pageStorage(resolution, true, downsample) - storage : 0;
	try
	{
		// Protect the complete requested view before admitting its first page.
		// Otherwise a pan can evict a page we are about to visit later this frame.
		const int periodX = std::max(1, map.getW() / ChunkTiles);
		const int periodY = std::max(1, map.getH() / ChunkTiles);
		for (auto &c : chunks)
		{
			const bool selected = c->scale == resolution && c->downsample == downsample &&
				((c->x / ChunkTiles - x0) & (periodX - 1)) <= x1 - x0 &&
				((c->y / ChunkTiles - y0) & (periodY - 1)) <= y1 - y0;
			if (selected) c->used = frame;
		}
		const auto makeRoom = [&](std::size_t needed)
		{
			while (bytes() + needed > budget)
			{
				auto oldest = chunks.end();
				// Retire idle renderer storage before discarding any composed pixels.
				if (gpu)
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used != frame && (*it)->image &&
							(oldest == chunks.end() || (*it)->textureUsed < (*oldest)->textureUsed)) oldest = it;
				if (oldest == chunks.end())
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used != frame &&
							(oldest == chunks.end() || (*it)->used < (*oldest)->used)) oldest = it;
				if (oldest == chunks.end())
				{
					// Visible CPU pages are protected; their renderer allocations
					// can still be retired and uploaded later during drawing.
					if (!gpu) return false;
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->image && (oldest == chunks.end() ||
							(*it)->textureUsed < (*oldest)->textureUsed)) oldest = it;
					if (oldest == chunks.end()) return false;
				}
				// First retire GPU storage; evict CPU pixels only if still needed.
				if (gpu && (*oldest)->image)
				{
					retireTexture(**oldest);
				}
				else chunks.erase(oldest);
			}
			return true;
		};
		// Allocate scratch pixels only when a reduced tile actually changes. Warm
		// frames reuse pages without allocating or recomposing native pixels.
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> scratch(nullptr,
																			SDL_DestroySurface);
		for (int cy = y0; cy <= y1; ++cy)
			for (int cx = x0; cx <= x1; ++cx)
			{
				const int wx = (cx * ChunkTiles) & map.getMaskW(),
						  wy = (cy * ChunkTiles) & map.getMaskH();
				Chunk *entry = nullptr;
				for (auto &c : chunks)
					if (c->x == wx && c->y == wy && c->scale == resolution && c->downsample == downsample)
					{
						entry = c.get();
						break;
					}
				// Compare the complete vertex window including the contour halo.
				std::array<Uint32, (ChunkTiles + 3) * (ChunkTiles + 3)> sources{};
				const int halo = compositor.contextualBorders() ? 1 : 0;
				for (int y = -halo; y <= ChunkTiles + halo; ++y)
					for (int x = -halo; x <= ChunkTiles + halo; ++x)
						sources[(y + 1) * (ChunkTiles + 3) + x + 1] = map.vertexTerrainAt(wx + x, wy + y);
				const auto revisionChanged = [&](const auto &revision)
				{ return compositor.materialRevision(revision.first) != revision.second; };
				bool unchanged = entry && entry->valid && entry->sources == sources &&
								 entry->seed == map.terrainSeed() &&
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
				// Reduced pages average native composition. A retained native
				// page is already that exact input, including coverage and alpha;
				// do not sample every contour again just to change zoom density.
				Chunk *native = nullptr;
				std::uint64_t nativeUsed = 0;
				if (downsample > 1)
					for (auto &c : chunks)
						if (c->x == wx && c->y == wy && c->scale == 1 && c->downsample == 1 &&
							c->valid && c->tiles == tiles &&
							std::none_of(c->materialRevisions.begin(), c->materialRevisions.end(), revisionChanged))
						{
							native = c.get();
							nativeUsed = native->used;
							// Admission may evict idle pages; retain this input until
							// its reduction has completed.
							native->used = frame;
							break;
						}
				if (!entry)
				{
					if (!makeRoom(storage + uploadReserve))
					{
						// Native reuse is optional. Do not reject a view that fits
						// only after its idle reduction input is evicted.
						if (native)
						{
							native->used = nativeUsed;
							native = nullptr;
						}
						if (!makeRoom(storage + uploadReserve)) throw std::bad_alloc();
					}
					auto fresh = std::make_unique<Chunk>();
					fresh->x = wx;
					fresh->y = wy;
					fresh->scale = resolution;
					fresh->downsample = downsample;
					std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
						SDL_CreateSurface(ChunkPixels * resolution / downsample,
										  ChunkPixels * resolution / downsample,
										  SDL_PIXELFORMAT_ARGB8888),
						SDL_DestroySurface);
					if (!pixels)
						throw std::bad_alloc();
					if (gpu) fresh->parked = std::move(pixels);
					else
					{
						fresh->image = std::make_unique<TerrainVisual::Surface>(pixels.get(), false);
						pixels.release(); // Keep ownership if the drawable allocation fails.
					}
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
					auto *target = entry->image ? entry->image->getSDLSurface() : entry->parked.get();
					const bool wasValid = entry->valid;
					entry->valid = false;
					entry->opaqueRuns.clear();
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const int i = y * ChunkTiles + x, n = 32 * resolution / downsample;
							if (wasValid && entry->tiles[i] == tiles[i] &&
								(!tiles[i].discovered ||
								 std::none_of(tiles[i].recipe.corners.begin(),
											  tiles[i].recipe.corners.end(), materialChanged)))
								continue;
							auto &held = entry->masks[i];
							const auto &recipe = tiles[i].recipe;
							const int maskScale = downsample > 1 ? 1 : resolution;
							if (held && (!tiles[i].discovered || held->mask.recipe != recipe ||
										 held->mask.scale != maskScale))
								held.reset();
							std::optional<TerrainVisual::Compositor::CellMask> passing;
							const TerrainVisual::Compositor::CellMask *cellMask =
								held ? &held->mask : nullptr;
							const auto &corners = recipe.corners;
							if (tiles[i].discovered && !native && !cellMask &&
								std::any_of(corners.begin(), corners.end(),
											[&](auto id) { return id != corners[0]; }) &&
								std::any_of(corners.begin(), corners.end(),
											[&](auto id) { return compositor.animated(id); }))
							{
								passing = compositor.mask(recipe, maskScale);
								if (releaseMasks(passing->bytes()))
								{
									held = std::make_unique<HeldMask>(std::move(*passing), maskTotal);
									passing.reset();
									cellMask = &held->mask;
								}
								else
									cellMask = &*passing;
							}
							if (tiles[i].discovered)
							{
								if (downsample > 1)
								{
									if (native)
									{
										auto *pixels = native->image ? native->image->getSDLSurface() : native->parked.get();
										auto tile = *pixels;
										tile.pixels = static_cast<Uint8 *>(pixels->pixels) + y * 32 * pixels->pitch + x * 32 * 4;
										reduceTile(&tile, target, x * n, y * n, downsample);
									}
									else
									{
										if (!scratch) scratch.reset(SDL_CreateSurface(NativeTilePixels,
																		NativeTilePixels,
																		SDL_PIXELFORMAT_ARGB8888));
										if (!scratch)
											throw std::bad_alloc();
										compositor.compose(recipe, scratch.get(), 0, 0, 1, cellMask);
										reduceTile(scratch.get(), target, x * n, y * n, downsample);
									}
								}
								else
									compositor.compose(recipe, target, x * n, y * n, resolution,
													   cellMask);
							}
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
					if (entry->image) entry->image->markPixelsChanged();
					entry->tiles = tiles;
					entry->materialRevisions.clear();
					for (const auto &tile : tiles)
						if (tile.discovered)
							for (auto id : tile.recipe.corners)
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
					if (native) ++reductions;
				}
				else
					++hits;
				entry->sources = sources;
				entry->seed = map.terrainSeed();
				if (native) native->used = nativeUsed;
				copies.push_back({entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
			}
		if (gpu && std::any_of(copies.begin(), copies.end(), [](const auto &copy) { return !copy.chunk->image; }) &&
			!makeRoom(uploadReserve)) throw std::bad_alloc();
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
										FallbackMode mode, bool tiledCapture)
{
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	const bool gpu = globalContainer->gfx->getOptionFlags() &
					 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(gpu, time);
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy, compositor.scale(), tiledCapture);
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
										 vy, visibleTeams, wholeMap, time, plan.resolution, false,
										 plan.downsample))
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
			if (!copy.chunk->image)
			{
				const auto needed = pageStorage(copy.chunk->scale, true, copy.chunk->downsample) -
					pageStorage(copy.chunk->scale, false, copy.chunk->downsample);
				while (bytes() + needed > GPUCacheBudget || residentTextureBytes() + needed > GPUBudget)
				{
					Chunk *oldest = nullptr;
					for (auto &chunk : chunks)
						if (chunk->image && (!oldest ||
							(chunk->used != frame && oldest->used == frame) ||
							((chunk->used == frame) == (oldest->used == frame) &&
							 chunk->textureUsed < oldest->textureUsed)))
							oldest = chunk.get();
					if (!oldest) throw std::bad_alloc();
					retireTexture(*oldest);
				}
				copy.chunk->image = std::make_unique<TerrainVisual::Surface>(copy.chunk->parked.get(),
					copy.chunk->scale > 1 || copy.chunk->downsample > 1);
				copy.chunk->parked.release();
			}
			copy.chunk->textureUsed = ++textureUse;
			SDL_Rect area{copy.x, copy.y, ChunkPixels, ChunkPixels}, visible;
			if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
				target.drawSurface(
					visible.x, visible.y, visible.w, visible.h, copy.chunk->image.get(),
					(visible.x - area.x) * resolution / downsample,
					(visible.y - area.y) * resolution / downsample,
					visible.w * resolution / downsample, visible.h * resolution / downsample);
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
bool SoftwareTerrainCache::releaseMasks(std::size_t needed)
{
	const std::size_t budget = gpu ? GPUMaskBudget : MaskBudget;
	if (needed > budget)
		return false;
	if (*maskTotal + needed <= budget)
		return true;
	std::vector<Chunk *> idle;
	for (auto &chunk : chunks)
		if (chunk->used != frame)
			idle.push_back(chunk.get());
	std::sort(idle.begin(), idle.end(), [](auto *a, auto *b) { return a->used < b->used; });
	for (auto *chunk : idle)
	{
		if (*maskTotal + needed <= budget)
			break;
		for (auto &held : chunk->masks)
			held.reset();
	}
	return *maskTotal + needed <= budget;
}
std::size_t SoftwareTerrainCache::bytes() const
{
	std::size_t total = 0;
	for (const auto &chunk : chunks)
		total += pageStorage(chunk->scale, gpu && bool(chunk->image), chunk->downsample);
	return total;
}

std::size_t SoftwareTerrainCache::residentTextureBytes() const
{
	std::size_t total = 0;
	if (gpu)
		for (const auto &chunk : chunks)
			if (chunk->image)
				total += pageStorage(chunk->scale, true, chunk->downsample) -
					pageStorage(chunk->scale, false, chunk->downsample);
	return total;
}
