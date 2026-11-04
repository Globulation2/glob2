// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <array>
#include <vector>

class SceneMap;
namespace TerrainVisual { class Compositor; }

// Map-space source data shared by direct terrain draws. Texel layout is part
// of the shader contract. Camera transforms and animation do not rebuild it.
struct DirectTerrainData
{
    using Pixel = std::array<Uint8, 4>;
    using Record = std::array<Uint16, 4>;
    static constexpr int TilePixels = 32;
    static constexpr int AtlasWidth = 2048;
    static constexpr int VariantWidth = 256;

    // One palette and coverage index per wrapped cell. Index zero is uniform.
    std::vector<Record> cells;
    std::vector<Uint16> masks;
    std::vector<Pixel> art, coverage;
    // Coverage and edges share tile coordinates. Edge texels pack a Q8
    // margin in the low 12 bits and two palette selectors in the high bits.
    std::vector<Uint16> edges;
    std::vector<Record> variants;
    std::vector<Pixel> calm;
    int artHeight, coverageHeight, variantHeight;

    static DirectTerrainData build(const SceneMap &, TerrainVisual::Compositor &, int maximumTextureSize);
};
