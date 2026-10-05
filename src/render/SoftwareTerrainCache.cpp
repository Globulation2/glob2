// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoftwareTerrainCache.h"
#include "GlobalContainer.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef HAVE_OPENGL
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(GLOB2_WEBGL2)
#include <GL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
namespace
{
int chunkOf(int tile)
{
	return int(std::floor(double(tile) / SoftwareTerrainCache::ChunkTiles));
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
			throw std::runtime_error(SDL_GetError());
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

bool SoftwareTerrainCache::prepare(const SceneMap &map, GAGCore::Sprite &, int left, int top,
								   int right, int bottom, int vx, int vy, Uint32 visibleTeams,
								   bool wholeMap, int time)
{
	PERF_SCOPE_TIME(TerrainCache);
	copies.clear();
	++frame;
	if (!enabled || map.getW() < ChunkTiles || map.getH() < ChunkTiles)
		return false;
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	auto &compositor = globalContainer->terrainCompositor();
	const bool nextGPU = globalContainer->gfx->getOptionFlags() &
						 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(nextGPU, time);
	int nextResolution = compositor.scale();
	const auto budget = nextGPU ? GPUBudget : Budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx), y0 = chunkOf(top + vy),
			  y1 = chunkOf(bottom + vy);
	const auto visiblePages = std::uint64_t(std::min(x1 - x0 + 1, map.getW() / ChunkTiles)) *
							  std::min(y1 - y0 + 1, map.getH() / ChunkTiles);
	int textureLimit = ChunkPixels * nextResolution;
#ifdef HAVE_OPENGL
	if (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU)
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
#endif
	const auto pageBytes = [&](int scale)
	{
		return ChunkStorageBytes +
			   (scale * scale * (nextGPU ? 3 : 1) - 1) * ChunkPixels * ChunkPixels * 4;
	};
	// Reduce oversampling before giving up the cache. A large HD viewport must
	// not repeatedly recompose every tile simply because four 2048px pages exceed
	// the budget. Geometry stays in native coordinates at every sample density.
	while (nextResolution > 1 && (ChunkPixels * nextResolution > textureLimit ||
								  visiblePages * pageBytes(nextResolution) > budget))
		nextResolution /= 2;
	if (ChunkPixels * nextResolution > textureLimit ||
		visiblePages * pageBytes(nextResolution) > budget)
		return false;
	if (resolution != nextResolution || gpu != nextGPU)
	{
		chunks.clear();
		resolution = nextResolution;
		gpu = nextGPU;
	}
	const auto storage = pageBytes(resolution);
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
				bool unchanged = entry && entry->valid &&
								 entry->revision == compositor.revision() &&
								 entry->sources == sources;
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
							return false;
						chunks.erase(oldest);
					}
					auto fresh = std::make_unique<Chunk>();
					fresh->x = wx;
					fresh->y = wy;
					fresh->scale = resolution;
					auto *pixels =
						SDL_CreateSurface(ChunkPixels * resolution, ChunkPixels * resolution,
										  SDL_PIXELFORMAT_ARGB8888);
					if (!pixels)
						throw std::bad_alloc();
					fresh->image = std::make_unique<TerrainVisual::Surface>(pixels, resolution > 1);
					entry = fresh.get();
					chunks.push_back(std::move(fresh));
				}
				entry->used = frame;
				if (!entry->valid || entry->revision != compositor.revision() ||
					entry->tiles != tiles)
				{
					auto *target = entry->image->getSDLSurface();
					const bool wasValid = entry->valid;
					entry->valid = false;
					entry->opaqueRuns.clear();
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const int i = y * ChunkTiles + x, n = 32 * resolution;
							if (wasValid && entry->revision == compositor.revision() &&
								entry->tiles[i] == tiles[i])
								continue;
							if (tiles[i].discovered)
								compositor.compose(tiles[i].recipe, target, x * n, y * n,
												   resolution);
							else
							{
								SDL_Rect area{x * n, y * n, n, n};
								SDL_FillSurfaceRect(target, &area, 0);
							}
							bool opaque = true;
							for (int py = 0; py < n && opaque; ++py)
								for (int px = 0; px < n; ++px)
									if ((*(reinterpret_cast<Uint32 *>(
											   static_cast<Uint8 *>(target->pixels) +
											   (y * n + py) * target->pitch) +
										   x * n + px) >>
										 24) != 255)
									{
										opaque = false;
										break;
									}
							entry->opaque[i] = opaque;
						}
					entry->image->markPixelsChanged();
					entry->tiles = tiles;
					entry->revision = compositor.revision();
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
		return false;
	}
	return true;
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
				if (copy.chunk->opaque[y * ChunkTiles + x])
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
