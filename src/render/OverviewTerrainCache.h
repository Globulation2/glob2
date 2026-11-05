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
class OverviewTerrainCache
{
  public:
	void update(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
				Uint32 visibleTeams, bool wholeMap, GAGCore::DrawableSurface &target);
	std::uint64_t composedCells() const { return compositions; }

  private:
	void copy(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
			  Uint32 visibleTeams, bool wholeMap, SDL_Surface *target);
	struct Output
	{
		int x, y, columns, rows;
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
};
