// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include "terrain/TerrainMaterials.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class SceneMap;
// Shared presentation page cache. The historical name remains for existing view
// controls/benchmarks; software and GPU consume the same composed coverage.
class SoftwareTerrainCache
{
  public:
	static constexpr int ChunkTiles = 16, ChunkPixels = ChunkTiles * 32;
	static constexpr std::size_t Budget = 32u * 1024u * 1024u, GPUBudget = 128u * 1024u * 1024u;
	struct Tile
	{
		TerrainVisual::Recipe recipe;
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
		int x = 0, y = 0, scale = 1;
		std::array<Uint32, (ChunkTiles + 2) * (ChunkTiles + 2)> sources{};
		std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
		std::array<bool, ChunkTiles * ChunkTiles> opaque{};
		std::unique_ptr<GAGCore::DrawableSurface> image;
		std::vector<OpaqueRun> opaqueRuns;
		bool valid = false;
		std::uint64_t used = 0, revision = 0;
	};
	static constexpr std::size_t ChunkStorageBytes =
		ChunkPixels * ChunkPixels * 4 + sizeof(Chunk) +
		ChunkTiles * ChunkTiles *
			(sizeof(OpaqueRun) + sizeof(GAGCore::DrawableSurface) + sizeof(SDL_Surface));
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
	int resolution = 1;
	bool gpu = false;

  public:
	bool enabled = true;
	bool prepare(const SceneMap &, GAGCore::Sprite &, int left, int top, int right, int bottom,
				 int vx, int vy, Uint32 visibleTeams, bool wholeMap, int animationTime = 0);
	void draw(GAGCore::GraphicContext &);
	std::vector<SDL_Rect> waterRegions(SDL_Rect bounds) const;
	std::size_t bytes() const
	{
		return chunks.size() * (ChunkStorageBytes + (resolution * resolution * (gpu ? 3 : 1) - 1) *
														ChunkPixels * ChunkPixels * 4);
	}
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
};
