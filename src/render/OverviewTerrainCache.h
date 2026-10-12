// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class SceneMap;
class TerrainRegistry;
class ResourceRegistry;
class MapAssetBundle;

// Canonical CPU palette pages. Unlike detailed terrain these do not depend on
// zoom, HD artwork or animation phase. The view's scratch image still supplies
// the ordinary renderer with exactly the same pixels and sampling as before.
class OverviewTerrainCache
{
  public:
	void copy(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
			  Uint32 visibleTeams, bool wholeMap, SDL_Surface *target);
	std::uint64_t composedCells() const { return compositions; }

  private:
	static constexpr int Tiles = 16;
	struct Page
	{
		int x, y;
		Uint32 seed = 0;
		bool valid = false;
		std::uint64_t used = 0;
		std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
		std::array<int, Tiles * Tiles> resources{};
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr,
																		   SDL_DestroySurface};
	};
	std::vector<std::unique_ptr<Page>> pages;
	Uint64 mapIdentity = 0;
	std::shared_ptr<const TerrainRegistry> terrain;
	std::shared_ptr<const ResourceRegistry> resources;
	std::shared_ptr<const MapAssetBundle> assets;
	std::uint64_t use = 0, compositions = 0;
};
