// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include "terrain/TerrainMaterials.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class SceneMap;
class TerrainRegistry;
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
		Uint32 seed = 0; // Map terrain seed the page was composed with.
		std::array<Uint32, (ChunkTiles + 2) * (ChunkTiles + 2)> sources{};
		std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
		// Empty cells expose only the separately drawn ocean and submit no software blit.
		std::array<bool, ChunkTiles * ChunkTiles> opaque{}, empty{};
		std::unique_ptr<GAGCore::DrawableSurface> image;
		std::vector<OpaqueRun> opaqueRuns;
		bool valid = false;
		// Only materials present in discovered recipes invalidate this page.
		std::vector<std::pair<TerrainVisual::MaterialId, std::uint64_t>> materialRevisions;
		std::uint64_t used = 0;
	};
	static constexpr std::size_t ChunkStorageBytes =
		ChunkPixels * ChunkPixels * 4 + sizeof(Chunk) +
		ChunkTiles * ChunkTiles *
			(sizeof(OpaqueRun) + sizeof(GAGCore::DrawableSurface) + sizeof(SDL_Surface) +
			 16 * sizeof(std::pair<TerrainVisual::MaterialId, std::uint64_t>));
	struct Copy
	{
		Chunk *chunk;
		int x, y;
	};

  private:
	bool prepareAtResolution(const SceneMap &, GAGCore::Sprite &, int left, int top, int right,
							 int bottom, int vx, int vy, Uint32 visibleTeams, bool wholeMap,
							 int animationTime, int preferredResolution, bool tiledCapture = false,
							 int preferredDownsample = 0);
	std::vector<std::unique_ptr<Chunk>> chunks;
	std::shared_ptr<const TerrainRegistry> registry;
	std::vector<Copy> copies;
	std::uint64_t frame = 0, hits = 0, rebuilds = 0;
	SDL_Rect paintBounds{};
	// Composition scale and subsequent page reduction are separate: reduction
	// filters the native material result, never its individual source textures.
	// Only GPU pages reduce; at most one of these factors can exceed one.
	int resolution = 1;
	int downsample = 1;
	bool gpu = false;

  public:
	enum class FallbackMode
	{
		StreamPages,
		// Same coverage geometry, but fractional sampling and HD mip neighborhoods
		// can differ from pages. Requires only one reusable native/HD tile.
		EmergencyTiles
	};
	// Stream the same canonical pages as a full-view cache, with at most one
	// page alive. EmergencyTiles is explicit for diagnostics and severe limits.
	static void drawUncached(const SceneMap &, GAGCore::Sprite &, int left, int top, int right,
							 int bottom, int vx, int vy, Uint32 visibleTeams, bool wholeMap,
							 int animationTime = 0, FallbackMode mode = FallbackMode::StreamPages,
							 bool tiledCapture = false);
	bool enabled = true;
	bool prepare(const SceneMap &, GAGCore::Sprite &, int left, int top, int right, int bottom,
				 int vx, int vy, Uint32 visibleTeams, bool wholeMap, int animationTime = 0,
				 bool tiledCapture = false);
	void draw(GAGCore::GraphicContext &);
	std::vector<SDL_Rect> waterRegions(SDL_Rect bounds) const;
	std::size_t bytes() const;
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
};
