// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Composed, cached swatch images for brush catalogue entries (BrushCatalog.h).
// One instance is owned by MapEdit (MapEdit::brushSwatches) and shared by every
// presentation so a brush looks the same in the dock, the phone tray and the
// palette, and the same as on the map:
//  - terrain swatches go through the map renderer's material compositor using
//    the type's appearance (imported types included), with any raised decor;
//  - resource swatches draw the resource's map sprite over a terrain swatch it
//    may be placed on.
// Every swatch is fully opaque. Swatches are cached by (registry digests, entry,
// pixel size) and dropped when the bound registries change or the render device
// resets. Returns nullptr when there is no display (headless runs) or for kinds
// presentations draw themselves (buildings, units, zones, tools).

#include "BrushCatalog.h"
#include "MapAssetBundle.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace GAGCore
{
class DrawableSurface;
}

class BrushSwatches
{
  public:
	BrushSwatches();
	~BrushSwatches();
	BrushSwatches(const BrushSwatches &) = delete;
	BrushSwatches &operator=(const BrushSwatches &) = delete;

	// The definitions swatches are composed from. Rebinding to registries with
	// other digests drops every cached swatch; identical digests keep them.
	void bind(std::shared_ptr<const TerrainRegistry> terrain,
			  std::shared_ptr<const ResourceRegistry> resources,
			  std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
	// Swatch for a catalogue entry, `px` pixels square. Owned by this cache: valid
	// until the next bind() that changes digests, clear() or device reset.
	GAGCore::DrawableSurface *get(const BrushEntry &entry, int px = 64);
	GAGCore::DrawableSurface *terrain(TerrainType type, int px = 64);
	GAGCore::DrawableSurface *resource(ResourceId resource, TerrainType backdrop, int px = 64);
	// Presentation-only scene and stock-stage inspection for set creation.
	GAGCore::DrawableSurface *terrainScene(TerrainType type, TerrainType neighbor, unsigned phase,
										   unsigned variation, int px = 192);
	GAGCore::DrawableSurface *resourceStage(ResourceId id, TerrainType backdrop, unsigned stock,
											unsigned phase, unsigned variation, int px = 64);
	void clear();
	std::size_t size() const { return cache.size(); }

  private:
	std::unique_ptr<GAGCore::DrawableSurface> composeTerrain(TerrainType type, int px);
	std::unique_ptr<GAGCore::DrawableSurface> composeResource(ResourceId resource,
															  TerrainType backdrop, int px);
	bool usable();
	std::shared_ptr<const TerrainRegistry> terrainRegistry;
	std::shared_ptr<const ResourceRegistry> resourceRegistry;
	std::string digest;
	std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty();
	std::uint64_t resetGeneration = 0;
	std::map<std::pair<std::string, int>, std::unique_ptr<GAGCore::DrawableSurface>> cache;
};
