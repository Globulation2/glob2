// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
#include "ResourceProperties.h"
#include "TerrainRegistry.h"
#include <SDL3/SDL_stdinc.h>
#include <memory>
#include <vector>

// Compiled terrain/resource habitat permissions. Rebuilt only when the resource
// or terrain registry changes, and shared immutably with engine snapshots.
struct ResourceHabitats
{
	std::vector<Uint16> resourceHabitatProfiles;
	std::vector<Uint8> resourceHabitatPermissions;
	std::vector<MaterialMask> terrainMaterialPermissions;
	std::vector<std::shared_ptr<const std::vector<Uint64>>> terrainResourceAllowLists;
	std::vector<MaterialMask> explicitTerrainMaterialPermissions;
	std::vector<int> terrainFarmResources;
	unsigned resourceHabitatProfileCount = 0;

	bool supportsResource(TerrainType terrain, unsigned propertyIndex, unsigned resourceType) const
	{
		if (resourceType >= resourceHabitatProfiles.size() || std::size_t(terrain) >= terrainResourceAllowLists.size()) return false;
		const auto& allowed = terrainResourceAllowLists[std::size_t(terrain)];
		if (allowed) return ((*allowed)[resourceType / 64] & (Uint64(1) << (resourceType % 64))) != 0;
		return resourceHabitatPermissions[std::size_t(propertyIndex) * resourceHabitatProfileCount + resourceHabitatProfiles[resourceType]];
	}
	MaterialMask materialPermissions(TerrainType terrain, unsigned propertyIndex) const
	{
		if (std::size_t(terrain) >= terrainResourceAllowLists.size()) return 0;
		return terrainResourceAllowLists[std::size_t(terrain)] ? explicitTerrainMaterialPermissions[std::size_t(terrain)] : terrainMaterialPermissions[propertyIndex];
	}
	int farmResource(TerrainType terrain) const
	{
		return std::size_t(terrain) < terrainFarmResources.size() ? terrainFarmResources[std::size_t(terrain)] : NO_RES_TYPE;
	}
};
