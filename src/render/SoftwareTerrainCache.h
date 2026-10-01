// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class Map;
// Presentation-only state. It never participates in saves, orders or checksums.
class SoftwareTerrainCache
{
  public:
	static constexpr int ChunkTiles = 16, ChunkPixels = ChunkTiles * 32;
	static constexpr std::size_t Budget = 32u * 1024u * 1024u;
	struct Tile
	{
		GAGCore::DrawableSurface *image = nullptr;
		std::uint64_t revision = 0;
		int terrainId = -1;
		bool discovered = false;
		bool operator==(const Tile &) const = default;
	};
	struct OpaqueRun
	{
		SDL_Rect rect;
		std::unique_ptr<GAGCore::DrawableSurface> image;
	};
	struct Chunk
	{
		int x = 0, y = 0;
		std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
		std::unique_ptr<GAGCore::DrawableSurface> image;
		// Views borrow image pixels, and must be destroyed before image.
		std::vector<OpaqueRun> opaqueRuns;
		bool valid = false;
		std::uint64_t used = 0;
	};
	struct Copy
	{
		Chunk *chunk;
		int x, y;
	};

  private:
	std::vector<std::unique_ptr<Chunk>> chunks;
	std::vector<Copy> copies;
	std::uint64_t frame = 0, hits = 0, rebuilds = 0;
	SDL_Rect paintBounds{};

  public:
	bool enabled = true; // Benchmark switch, not a saved gameplay preference.
	bool prepare(const Map &map, GAGCore::Sprite &terrain, int left, int top, int right, int bottom,
				 int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap);
	void draw(GAGCore::GraphicContext &target);
	std::vector<SDL_Rect> waterRegions(SDL_Rect bounds) const;
	std::size_t bytes() const { return chunks.size() * ChunkPixels * ChunkPixels * 4; }
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
};
