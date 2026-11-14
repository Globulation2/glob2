// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace GAGCore { class DrawableSurface; }
class SceneMap;
class TerrainRegistry;
class ResourceRegistry;
class MapAssetBundle;

// Canonical CPU palette pages and the last image assembled from them. Neither
// depends on HD artwork or animation phase.
//
// Each cell is `samples` x `samples` palette samples. A visible resource adds a
// square in its minimap colour, growing from the cell's top-left corner with its
// stock, so the terrain under it stays readable.
class OverviewTerrainCache
{
  public:
	static constexpr int FewSamples = 4, ManySamples = 8;
	//! Samples per cell axis for a cell covering `pixelsPerCell` device pixels:
	//! many once a cell is large enough to show every stock size, few otherwise.
	//! `current` (0 for none) adds hysteresis so a zoom near the threshold does
	//! not recompose every page back and forth.
	static int samplesFor(double pixelsPerCell, int current);
	//! Side of the resource square, in samples: 1 at the lowest stock, rounding
	//! down to 3/4 of the cell only at full stock. Zero capacity counts as full.
	static int resourceSquare(unsigned stock, unsigned capacity, int samples);

	void update(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
				Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target);
	std::uint64_t composedCells() const { return compositions; }
	//! Samples per cell axis of the cached pages, or 0 before the first update.
	int samples() const { return pageSamples; }

  private:
	void copy(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
			  Uint32 visibleTeams, bool wholeMap, SDL_Surface *target);
	struct Output
	{
		int x, y, columns, rows, samples;
		Uint32 seed, visibleTeams;
		bool wholeMap;
		Uint64 terrainRevision, resourceRevision, discoveryRevision, surfaceIdentity, surfaceRevision;
		bool operator==(const Output &) const = default;
	};
	std::optional<Output> output;
	static constexpr int Tiles = 16;
	struct Page
	{
		Uint32 seed = 0;
		bool valid = false;
		std::uint64_t used = 0;
		std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
		// Per cell: resource type and square size last composed, or -1 for none.
		std::array<int, Tiles * Tiles> resources{};
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr,
																		   SDL_DestroySurface};
	};
	std::unordered_map<std::size_t, Page> pages;
	Uint64 mapIdentity = 0;
	std::shared_ptr<const TerrainRegistry> terrain;
	std::shared_ptr<const ResourceRegistry> resources;
	std::shared_ptr<const MapAssetBundle> assets;
	std::uint64_t use = 0, compositions = 0;
	int pageSamples = 0;
};
