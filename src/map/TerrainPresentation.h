// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include "TerrainPresentationLayout.h"
#include <array>
#include <cstdint>
// The built-ins' presentations, derived from the rows in TerrainCatalog.cpp and
// constant-initialized there, so editing a row recompiles only that file.
extern const std::array<TerrainPresentation, TERRAIN_COUNT> TerrainPresentations;
inline const TerrainPresentation &terrainPresentation(TerrainType type)
{
	return TerrainPresentations[unsigned(type)];
}
