// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainType.h"

// The number of built-in terrain identities. A new built-in terrain takes the
// next identity here, as `inline constexpr TerrainType NAME = TerrainType(N);`,
// and bumps the count, rather than extending the TerrainType enum, which about
// half of the code includes. Only the built-in tables, the catalogue, the
// gradient kernels and code sized by the count include this header.
inline constexpr TerrainType TERRAIN_COUNT = TerrainType(64);
