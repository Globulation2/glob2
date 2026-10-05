// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include "TerrainPresentation.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class SceneMap;
class TerrainRegistry;
// Presentation-only state. It never participates in saves, orders or checksums.
class SoftwareTerrainCache
{
  public:
	static constexpr int ChunkTiles = 16, ChunkPixels = ChunkTiles * 32;
	static constexpr std::size_t Budget = 32u * 1024u * 1024u;
	struct Tile
	{
		std::array<GAGCore::DrawableSurface *, TerrainLayers::Capacity> images{};
        std::array<std::uint64_t, TerrainLayers::Capacity> revisions{};
        TerrainLayers layers;
        bool opaque = false;
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
    // Include layer descriptors and worst-case borrowed surface views in the
    // reservation, not only the RGBA payload. This remains bounded as new
    // materials add layers or create highly fragmented transparent edges.
    static constexpr std::size_t ChunkStorageBytes = ChunkPixels * ChunkPixels * 4 + sizeof(Chunk) +
        ChunkTiles * ChunkTiles * (sizeof(OpaqueRun) + sizeof(GAGCore::DrawableSurface) + sizeof(SDL_Surface));
	struct Copy
	{
		Chunk *chunk;
		int x, y;
	};

  private:
	std::vector<std::unique_ptr<Chunk>> chunks;
	std::shared_ptr<const TerrainRegistry> registry;
	std::vector<Copy> copies;
	std::uint64_t frame = 0, hits = 0, rebuilds = 0;
	SDL_Rect paintBounds{};

  public:
	bool enabled = true; // Benchmark switch, not a saved gameplay preference.
	bool prepare(const SceneMap &map, GAGCore::Sprite &terrain, int left, int top, int right, int bottom,
				 int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap, int animationTime = 0);
	void draw(GAGCore::GraphicContext &target);
	std::vector<SDL_Rect> waterRegions(SDL_Rect bounds) const;
	std::size_t bytes() const { return chunks.size() * ChunkStorageBytes; }
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
};
