// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapRenderState.h"

#include "DynamicClouds.h"
#include "ColonySkinPreview.h"
#include "GlobalContainer.h"
#include "SoftwareTerrainCache.h"

MapRenderState::MapRenderState() = default;
MapRenderState::~MapRenderState() = default;
MapRenderState::MapRenderState(MapRenderState &&) noexcept = default;
MapRenderState &MapRenderState::operator=(MapRenderState &&) noexcept = default;

DynamicClouds &MapRenderState::clouds()
{
	if (!clouds_)
		clouds_ = std::make_unique<DynamicClouds>(&globalContainer->settings);
	return *clouds_;
}

SoftwareTerrainCache &MapRenderState::terrainCache(Uint64 mapIdentity)
{
	if (!terrainCache_ || terrainCacheMap != mapIdentity)
	{
		terrainCache_.reset();
		terrainCache_ = std::make_unique<SoftwareTerrainCache>();
		terrainCacheMap = mapIdentity;
	}
	return *terrainCache_;
}

ColonySkinPreview &MapRenderState::skinPreview()
{
	if (!skinPreview_) skinPreview_ = std::make_unique<ColonySkinPreview>();
	return *skinPreview_;
}
