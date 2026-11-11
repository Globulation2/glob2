// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "FogFade.h"
#include "MapOverlayQueue.h"
#include "ZoomDetail.h"
#include "scene/Scene.h"

#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <cstdint>
#include <unordered_map>
#include <vector>

class DynamicClouds;
class ColonySkinPreview;
class UnitEffectArt;
namespace GAGCore { class DrawableSurface; }
class SoftwareTerrainCache;
class DirectTerrain;
class OverviewTerrainCache;
class ResourceRowCache;

//! Presentation state one map view keeps between frames: animation phases, the
//! cloud field, the terrain page cache and scratch buffers. Owned by the
//! viewer (ViewState), never by Game or Map, so the simulation neither
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
	//! How far each tile has faded into the fog of war, with the smooth fog
	//! setting. MapDrawer::drawMap updates it each frame, and resets it while the fade is
	//! not drawn, so it is active() exactly when this frame draws the fog faded.
	FogFade fogFade;
	//! Reused alpha buffer for overlay maps, kept to avoid per-frame allocation.
	std::vector<unsigned char> overlayAlphas;
	//! How this frame's zoom draws each map element; set by MapDrawer::drawMap.
	ZoomDetail detail;
	//! The terrain overview's sampled palette image, kept between frames.
	std::unique_ptr<GAGCore::DrawableSurface> overview;
	std::unique_ptr<OverviewTerrainCache> overviewCache;
	//! The furthest this view's camera can zoom out, set by its owner; 0 when
	//! unknown. It anchors the far end of the detail curves (ZoomDetail::rampTile).
	double minimumZoom = 0;
	//! The player is painting zones, so they keep their full strength zoomed out.
	bool zonesEmphasised = false;
	//! Constant-size overlays queued by this frame's map passes.
	MapOverlayQueue overlays;
	//! PresentationFrame explicitly prepared by a standalone owner before drawing.
	PresentationFrame ownScene;
	//! Unit effects this frame draws (UnitEffects.h): the record of each unit a
	//! building has just produced or serviced by its gid, and each producing
	//! swarm's glow opacity by its gid. Set by MapDrawer::drawMap; empty when the
	//! renderer or the detail setting does not draw them.
	std::unordered_map<Uint16, SceneUnitEffect> unitEffects;
	std::unordered_map<Uint16, float> swarmGlows;
	//! What is drawn over a unit playing an effect (a spawn's twinkles, a trained
	//! unit's level number), queued as the unit is drawn and drawn by the effects
	//! pass, after the buildings that would otherwise cover it. (x, y) is the
	//! unit's drawn top-left corner, which a spawn's dust and a service's icons
	//! also follow; a unit drawn on both sides of a map seam queues one per copy.
	struct UnitOverlay { float x, y, age, hue, opacity; std::uint32_t seed; Uint16 unit; Uint8 kind, level; };
	std::vector<UnitOverlay> unitOverlays;

	//! The cloud field for this view, created on first use.
	DynamicClouds &clouds();
	ColonySkinPreview &skinPreview();
	//! The spawn effect's procedural images, created on first use.
	UnitEffectArt &effectArt();
	// Keep match appearance while rebuilding the rest of a reconnect view.
	void swapSkinPreview(MapRenderState &other);
	//! The terrain page cache for map, rebuilt when the map was replaced.
	//! May throw std::bad_alloc; callers fall back to uncached terrain.
	SoftwareTerrainCache &terrainCache(Uint64 mapIdentity);
    struct ResourceRowMetrics { std::uint64_t eligibilityCells = 0, frameCells = 0, animatedCells = 0; };
    ResourceRowMetrics resourceRowMetrics;
    std::size_t peakTerrainTextureBytes = 0;
    std::unique_ptr<ResourceRowCache> resourceRows;
	DirectTerrain &directTerrain();
	const DirectTerrain *existingDirectTerrain() const { return directTerrain_.get(); }
	//! The current terrain page cache, or null if none was created yet.
	SoftwareTerrainCache *existingTerrainCache() const { return terrainCache_.get(); }

private:
	std::unique_ptr<DirectTerrain> directTerrain_;
	std::unique_ptr<DynamicClouds> clouds_;
	std::unique_ptr<ColonySkinPreview> skinPreview_;
	std::unique_ptr<UnitEffectArt> effectArt_;
	std::unique_ptr<SoftwareTerrainCache> terrainCache_;
	Uint64 terrainCacheMap = 0; //!< Map::identity() the cache was built for.
};
