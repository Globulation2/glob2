// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include "TerrainTypeTable.h"
#include "ExperimentalFeatures.h"
#include <array>
#include <optional>
// Cold availability metadata. Once a match starts, properties never depend on
// local settings; the map's required experiments travel with its header.
// Experiments gate groups: every member of a group shares its switch. Trail keeps
// its own historical switch inside the paths group.
inline constexpr auto TERRAIN_GROUP_EXPERIMENTS = [] {
	std::array<std::optional<ExperimentId>, TERRAIN_GROUP_COUNT> values{};
	values[unsigned(TerrainGroup::Ice)] = ExperimentId::IceTerrain;
	values[unsigned(TerrainGroup::Paths)] = ExperimentId::PathTerrain;
	values[unsigned(TerrainGroup::Obstacles)] = ExperimentId::ObstacleTerrain;
	values[unsigned(TerrainGroup::Ridges)] = ExperimentId::RidgeTerrain;
	values[unsigned(TerrainGroup::Barren)] = ExperimentId::BarrenTerrain;
	values[unsigned(TerrainGroup::Rough)] = ExperimentId::RoughTerrain;
	values[unsigned(TerrainGroup::Lava)] = ExperimentId::LavaTerrain;
	values[unsigned(TerrainGroup::Fertile)] = ExperimentId::FertileTerrain;
	values[unsigned(TerrainGroup::DeepWater)] = ExperimentId::DeepWaterTerrain;
	values[unsigned(TerrainGroup::Void)] = ExperimentId::VoidTerrain;
	return values;
}();
inline constexpr std::optional<ExperimentId> terrainGroupExperiment(TerrainGroup group)
{
	return TERRAIN_GROUP_EXPERIMENTS[unsigned(group)];
}
inline constexpr auto TERRAIN_EXPERIMENTS = [] {
    std::array<std::optional<ExperimentId>, TERRAIN_COUNT> values{};
    for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
        values[i] = terrainGroupExperiment(TERRAIN_TYPES[i].group);
    values[TRAIL] = ExperimentId::TrailTerrain;
    return values;
}();
inline constexpr std::optional<ExperimentId> terrainExperiment(TerrainType type)
{
	return unsigned(type) < TERRAIN_COUNT ? TERRAIN_EXPERIMENTS[static_cast<unsigned>(type)]
										  : std::nullopt;
}
