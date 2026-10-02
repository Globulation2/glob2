// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapRenderState.h"

#include "DynamicClouds.h"
#include "GlobalContainer.h"
#include "Map.h"
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

SoftwareTerrainCache &MapRenderState::terrainCache(const Map &map)
{
	if (!terrainCache_ || terrainCacheMap != map.identity())
	{
		terrainCache_.reset();
		terrainCache_ = std::make_unique<SoftwareTerrainCache>();
		terrainCacheMap = map.identity();
	}
	return *terrainCache_;
}
