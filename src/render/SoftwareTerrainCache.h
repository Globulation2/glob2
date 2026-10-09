// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include "terrain/TerrainCompositor.h"
#include <array>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

class SceneMap;
class TerrainRegistry;
class MapAssetBundle;
// Shared presentation page cache. The historical name remains for existing view
// controls/benchmarks; software and GPU consume the same composed coverage.
class SoftwareTerrainCache
{
  public:
	static constexpr int ChunkTiles = 16, ChunkPixels = ChunkTiles * 32;
	static constexpr std::size_t Budget = 32u * 1024u * 1024u, GPUBudget = 128u * 1024u * 1024u;
	// Preserve the existing density/GPU allowance; keep inactive zoom pixels
	// in an additional bounded CPU reserve rather than recomposing them.
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	static constexpr std::size_t GPUCacheBudget = GPUBudget;
#else
	static constexpr std::size_t GPUCacheBudget = GPUBudget + 128u * 1024u * 1024u;
#endif
	// Coverage kept for mixed cells next to animated materials, so a phase change
	// re-blends their textures instead of re-sampling coverage. A quarter of the
	// original density budget; cells beyond it compose without a kept mask.
	static constexpr std::size_t MaskBudget = Budget / 4, GPUMaskBudget = GPUBudget / 4;
	struct HeldMask
	{
		TerrainVisual::Compositor::CellMask mask;
		std::shared_ptr<std::size_t> total;
		HeldMask(TerrainVisual::Compositor::CellMask m, std::shared_ptr<std::size_t> t)
			: mask(std::move(m)), total(std::move(t))
		{
			*total += mask.bytes();
		}
		~HeldMask() { *total -= mask.bytes(); }
		HeldMask(const HeldMask &) = delete;
		HeldMask &operator=(const HeldMask &) = delete;
	};
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
		int x = 0, y = 0, scale = 1, downsample = 1;
		Uint32 seed = 0; // Map terrain seed the page was composed with.
		// Corner vertices and one-vertex halo used by contextual borders.
		std::array<Uint32, (ChunkTiles + 3) * (ChunkTiles + 3)> sources{};
		std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
		// Empty cells expose only the separately drawn ocean and submit no software blit.
		std::array<bool, ChunkTiles * ChunkTiles> opaque{}, empty{};
		// An uploaded drawable owns the pixels; a retired page keeps them in parked.
	// Exactly one owner exists, so texture retirement never discards composition.
	std::unique_ptr<TerrainVisual::Surface> image;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> parked{nullptr, SDL_DestroySurface};
		std::vector<OpaqueRun> opaqueRuns;
		std::array<std::unique_ptr<HeldMask>, ChunkTiles * ChunkTiles> masks;
		bool valid = false;
		// Only materials present in discovered recipes invalidate this page.
		std::vector<std::pair<TerrainVisual::MaterialId, std::uint64_t>> materialRevisions;
		std::uint64_t used = 0;
		std::uint64_t textureUsed = 0;
	};
	static constexpr std::size_t ChunkStorageBytes =
		ChunkPixels * ChunkPixels * 4 + sizeof(Chunk) +
		ChunkTiles * ChunkTiles *
			(sizeof(OpaqueRun) + sizeof(GAGCore::DrawableSurface) + sizeof(SDL_Surface) +
			 std::tuple_size_v<decltype(TerrainVisual::Recipe::corners)> *
				 sizeof(std::pair<TerrainVisual::MaterialId, std::uint64_t>));
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
	// Frees kept masks of pages not drawn this frame, oldest first, until
	// `needed` more bytes fit the mask budget. Returns whether they now fit.
	bool releaseMasks(std::size_t needed);
	std::vector<std::unique_ptr<Chunk>> chunks;
	std::shared_ptr<const TerrainRegistry> registry;
    std::shared_ptr<const MapAssetBundle> assets;
	std::vector<Copy> copies;
	std::uint64_t frame = 0, hits = 0, rebuilds = 0;
	std::uint64_t textureUse = 0, reductions = 0;
	// Bytes of every chunk's kept masks; shared so a mask outliving the cache is safe.
	std::shared_ptr<std::size_t> maskTotal = std::make_shared<std::size_t>(0);
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
	std::size_t bytes() const;
	std::size_t residentTextureBytes() const;
	int samplingResolution() const { return resolution; }
	int samplingReduction() const { return downsample; }
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
	std::uint64_t cacheReductions() const { return reductions; }
	std::size_t maskBytes() const { return *maskTotal; }
};
