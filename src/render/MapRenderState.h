// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MapOverlayQueue.h"
#include "ZoomDetail.h"
#include "scene/Scene.h"

#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <valarray>

class DynamicClouds;
namespace GAGCore { class DrawableSurface; }
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
	//! Phase of the animated area (forbidden/guard/clear/farm) markers.
	int areaAnimationTick = 0;
	//! How far this frame draws units from their ticked state towards the next tick,
	//! 0..1 (see UnitMotion.h); 0 draws exactly the simulated positions.
	float unitMotion = 0;
	//! Reused alpha buffer for overlay maps, kept to avoid per-frame allocation.
	std::valarray<unsigned char> overlayAlphas;
	//! How this frame's zoom draws each map element; set by Game::drawMap.
	ZoomDetail detail;
	//! The terrain overview's image, one pixel per visible tile, kept between frames.
	std::unique_ptr<GAGCore::DrawableSurface> overview;
	//! The furthest this view's camera can zoom out, set by its owner; 0 when
	//! unknown. It anchors the far end of the detail curves (ZoomDetail::rampTile).
	double minimumZoom = 0;
	//! The player is painting zones, so they keep their full strength zoomed out.
	bool zonesEmphasised = false;
	//! Constant-size overlays queued by this frame's map passes.
	MapOverlayQueue overlays;
	//! Scene this view extracts for itself when drawn without a published one.
	Scene ownScene;

	//! The cloud field for this view, created on first use.
	DynamicClouds &clouds();
	//! The software terrain cache for map, rebuilt when the map was replaced.
	//! May throw std::bad_alloc; callers fall back to uncached terrain.
	SoftwareTerrainCache &terrainCache(Uint64 mapIdentity);
	//! The current software terrain cache, or null if none was created yet.
	SoftwareTerrainCache *existingTerrainCache() const { return terrainCache_.get(); }

private:
	std::unique_ptr<DynamicClouds> clouds_;
	std::unique_ptr<SoftwareTerrainCache> terrainCache_;
	Uint64 terrainCacheMap = 0; //!< Map::identity() the cache was built for.
};
