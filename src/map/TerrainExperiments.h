// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include "ExperimentalFeatures.h"
#include <array>
#include <optional>
// Cold availability metadata. Once a match starts, properties never depend on
// local settings; the map's required experiments travel with its header.
inline constexpr auto TERRAIN_EXPERIMENTS = [] {
    std::array<std::optional<ExperimentId>, TERRAIN_COUNT> values{};
    values[ICE] = ExperimentId::IceTerrain;
    values[TRAIL] = ExperimentId::TrailTerrain;
    return values;
}();
inline constexpr std::optional<ExperimentId> terrainExperiment(TerrainType type)
{
	return unsigned(type) < TERRAIN_COUNT ? TERRAIN_EXPERIMENTS[static_cast<unsigned>(type)]
										  : std::nullopt;
}
