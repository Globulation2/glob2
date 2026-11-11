// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace field
{
// How a terrain field moves across cells. Persisted IDs: geometric fields
// deliberately ignore movement speed and access. Code that only names a mode
// includes this rather than TerrainTravel.h, which brings the count-sized
// terrain tables and the registry.
enum class TerrainTravel : unsigned char { Geometric=0, Walk=1, Swim=2, Fly=3 };
constexpr bool validTerrainTravel(unsigned value) { return value<=unsigned(TerrainTravel::Fly); }
}
