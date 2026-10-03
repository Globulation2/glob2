// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene/Scene.h"

#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <valarray>

class DynamicClouds;
class ColonySkinPreview;
class SoftwareTerrainCache;

//! Presentation state one map view keeps between frames: animation phases, the
//! cloud field, the software terrain cache and scratch buffers. Owned by the
//! viewer (Game::ViewState), never by Game or Map, so the simulation neither
//! reads nor writes it and each view animates independently.
struct MapRenderState
{
	MapRenderState();
	~MapRenderState();
	MapRenderState(MapRenderState &&) noexcept;
	MapRenderState &operator=(MapRenderState &&) noexcept;

	//! Water and cloud phase, advanced once per drawn frame; frozen while paused.
	int animationTime = 0;
	//! Phase of the animated area (forbidden/guard/clear) markers.
	int areaAnimationTick = 0;
	//! Reused alpha buffer for overlay maps, kept to avoid per-frame allocation.
	std::valarray<unsigned char> overlayAlphas;
	//! Scene this view extracts for itself when drawn without a published one.
	Scene ownScene;

	//! The cloud field for this view, created on first use.
	DynamicClouds &clouds();
	ColonySkinPreview &skinPreview();
	// Keep match appearance while rebuilding the rest of a reconnect view.
	void swapSkinPreview(MapRenderState &other);
	//! The software terrain cache for map, rebuilt when the map was replaced.
	//! May throw std::bad_alloc; callers fall back to uncached terrain.
	SoftwareTerrainCache &terrainCache(Uint64 mapIdentity);
	//! The current software terrain cache, or null if none was created yet.
	SoftwareTerrainCache *existingTerrainCache() const { return terrainCache_.get(); }

private:
	std::unique_ptr<DynamicClouds> clouds_;
	std::unique_ptr<ColonySkinPreview> skinPreview_;
	std::unique_ptr<SoftwareTerrainCache> terrainCache_;
	Uint64 terrainCacheMap = 0; //!< Map::identity() the cache was built for.
};
