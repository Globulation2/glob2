// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include <array>
#include <cstdint>
// Frozen saved-frame contracts. These values and hash are also used when maps
// are authored and contribute to simulation checksums. Visual packs cannot edit them.
struct TerrainCompatibility
{
	int firstFrame, variants;
	bool legacyCorners;
};
inline constexpr std::array<TerrainCompatibility, TERRAIN_COUNT> TerrainCompatibilityTable{
	{{256, 16, true},
	 {128, 16, true},
	 {0, 16, true},
	 {272, 16, false},
	 {288, 16, false},
	 {16, 112, true},
	 {144, 112, true}}};
inline constexpr const TerrainCompatibility &terrainCompatibility(TerrainType type)
{
	return TerrainCompatibilityTable[unsigned(type)];
}
inline constexpr bool terrainUsesLegacyCorners(TerrainType type)
{
	return terrainCompatibility(type).legacyCorners;
}
// Historical "visual" names below select serialized frames during map authoring.
// Renderer-only variation must use Catalog::variantIndex instead of these helpers.
inline constexpr unsigned terrainVisualHash(int x, int y)
{
	std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return h;
}
inline constexpr int terrainVisualFrame(TerrainType type, int x, int y)
{
	const auto &p = terrainCompatibility(type);
	return p.firstFrame + terrainVisualHash(x, y) % p.variants;
}
static_assert(
	[]
	{
		for (const auto &entry : TerrainCompatibilityTable)
			if (entry.firstFrame < 0 || entry.variants <= 0 ||
				entry.firstFrame + entry.variants > 65536)
				return false;
		return true;
	}(),
	"Every gameplay terrain requires an explicit saved-frame contract");
