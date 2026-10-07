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
// Explicit nullopt construction avoids GCC 11's constant-evaluation failure
// when copying value-initialized empty optionals from a constexpr array.
inline constexpr std::array<std::optional<ExperimentId>, TERRAIN_GROUP_COUNT>
TERRAIN_GROUP_EXPERIMENTS{{
	std::nullopt, // Water
	std::nullopt, // Sand
	std::nullopt, // Grass
	ExperimentId::IceTerrain,
	std::nullopt, // Shore
	ExperimentId::PathTerrain,
	ExperimentId::ObstacleTerrain,
	ExperimentId::RidgeTerrain,
	ExperimentId::BarrenTerrain,
	ExperimentId::RoughTerrain,
	ExperimentId::LavaTerrain,
	ExperimentId::FertileTerrain,
	ExperimentId::DeepWaterTerrain,
	ExperimentId::VoidTerrain,
}};
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
