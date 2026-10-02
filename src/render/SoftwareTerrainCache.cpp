// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoftwareTerrainCache.h"
#include "Map.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
int chunkOf(int tile)
{
	return int(std::floor(double(tile) / SoftwareTerrainCache::ChunkTiles));
}
// Cache the source RGBA, not an alpha blend onto black. Otherwise a coastline
// would be blended twice and darken when the cached image is laid over water.
void copyRGBA(SDL_Surface *source, SDL_Surface *target, SDL_Rect destination)
{
	SDL_BlendMode blend;
	Uint8 alpha, r, g, b;
	SDL_GetSurfaceBlendMode(source, &blend);
	SDL_GetSurfaceAlphaMod(source, &alpha);
	SDL_GetSurfaceColorMod(source, &r, &g, &b);
	SDL_SetSurfaceColorMod(source, 255, 255, 255);
	SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceAlphaMod(source, 255);
	const int result = SDL_BlitSurface(source, nullptr, target, &destination);
	SDL_SetSurfaceBlendMode(source, blend);
	SDL_SetSurfaceAlphaMod(source, alpha);
	SDL_SetSurfaceColorMod(source, r, g, b);
	if (result < 0)
		throw std::runtime_error(SDL_GetError());
}
// A CPU-only view owns its SDL descriptor, but borrows immutable chunk pixels.
// The producer has already verified every pixel in this rectangle is opaque.
class OpaqueView final : public GAGCore::DrawableSurface
{
  public:
	OpaqueView(SDL_Surface *owner, SDL_Rect rect)
	{
		sdlsurface = SDL_CreateSurfaceFrom(
			static_cast<char *>(owner->pixels) + rect.y * owner->pitch + rect.x * 4, rect.w, rect.h,
			32, owner->pitch, owner->format);
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
			{
				auto *image = chunk.tiles[y * SoftwareTerrainCache::ChunkTiles + column].image;
				return image && image->hasOpaquePixels();
			};
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
bool SoftwareTerrainCache::prepare(const Map &map, GAGCore::Sprite &terrain, int left, int top,
								   int right, int bottom, int vx, int vy, Uint32 visibleTeams,
								   bool wholeMap)
{
	PERF_SCOPE_TIME(TerrainCache);
	copies.clear();
	++frame;
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	if (!enabled || map.getW() < ChunkTiles || map.getH() < ChunkTiles)
		return false;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	// Wrapped copies share one stored image. Budget the distinct canonical
	// chunks, rather than charging a small torus for every repeated screen copy.
	const auto working = std::uint64_t(std::min(x1 - x0 + 1, map.getW() / ChunkTiles)) *
						 std::min(y1 - y0 + 1, map.getH() / ChunkTiles) * ChunkPixels *
						 ChunkPixels * 4;
	if (working > Budget)
		return false;
	try
	{
		for (int cy = y0; cy <= y1; ++cy)
			for (int cx = x0; cx <= x1; ++cx)
			{
				const int worldX = (cx * ChunkTiles) & map.getMaskW(),
						  worldY = (cy * ChunkTiles) & map.getMaskH();
				std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
				for (int y = 0; y < ChunkTiles; ++y)
					for (int x = 0; x < ChunkTiles; ++x)
					{
						const int wx = worldX + x, wy = worldY + y;
						auto &tile = tiles[y * ChunkTiles + x];
						const int id = map.getTerrain(wx, wy);
						tile.terrainId = id;
						tile.discovered =
							wholeMap || map.isMapPartiallyDiscovered(wx - 1, wy - 1, wx + 1, wy + 1,
																	 visibleTeams);
						if (!tile.discovered)
							continue;
						// IDs 256..271 are water; the existing terrain pass skips them.
						if (id >= 272)
							return false;
						if (id >= 256)
							continue;
						auto *image = terrain.nativeFrame(id);
						if (!image || image->getW() != 32 || image->getH() != 32)
							return false;
						tile.image = image;
						tile.revision = image->contentRevision();
					}
				Chunk *entry = nullptr;
				for (auto &candidate : chunks)
					if (candidate->x == worldX && candidate->y == worldY)
					{
						entry = candidate.get();
						break;
					}
				if (!entry)
				{
					if (bytes() + ChunkPixels * ChunkPixels * 4 > Budget)
					{
						auto oldest = chunks.end();
						for (auto i = chunks.begin(); i != chunks.end(); ++i)
							if ((*i)->used != frame &&
								(oldest == chunks.end() || (*i)->used < (*oldest)->used))
								oldest = i;
						if (oldest == chunks.end())
							return false;
						chunks.erase(oldest);
					}
					auto fresh = std::make_unique<Chunk>();
					fresh->x = worldX;
					fresh->y = worldY;
					fresh->image =
						std::make_unique<GAGCore::DrawableSurface>(ChunkPixels, ChunkPixels);
					if (!fresh->image->getSDLSurface())
						return false;
					entry = fresh.get();
					chunks.push_back(std::move(fresh));
				}
				entry->used = frame;
				if (!entry->valid || entry->tiles != tiles)
				{
					auto *target = entry->image->getSDLSurface();
					entry->valid = false;
					if (!SDL_FillSurfaceRect(target, nullptr, 0))
						throw std::runtime_error(SDL_GetError());
					entry->opaqueRuns.clear();
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							auto *image = tiles[y * ChunkTiles + x].image;
							if (!image)
							{
								continue;
							}
							copyRGBA(image->getSDLSurface(), target,
									 SDL_Rect{x * 32, y * 32, 32, 32});
						}
					entry->image->markPixelsChanged();
					entry->tiles = tiles;
					buildOpaqueRuns(*entry);
					entry->valid = true;
					++rebuilds;
				}
				else
					++hits;
				copies.push_back({entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
			}
	}
	catch (const std::bad_alloc &)
	{
		copies.clear();
		return false;
	}
	catch (const std::runtime_error &)
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
		// Terrain cells do not overlap. Batch opaque interiors; coastlines keep
		// their original source and blending arithmetic instead of scanning the
		// transparent holes of a 512-pixel composite on every frame.
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
				auto *image = copy.chunk->tiles[y * ChunkTiles + x].image;
				if (!image || image->hasOpaquePixels())
					continue;
				SDL_Rect destination{copy.x + x * 32, copy.y + y * 32, 32, 32}, visible;
				if (SDL_GetRectIntersection(&destination, &paintBounds, &visible))
					target.drawSurface(visible.x, visible.y, image, visible.x - destination.x,
									   visible.y - destination.y, visible.w, visible.h);
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
