// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GLOB2_TERRAIN_ART_BORDER_H
#define GLOB2_TERRAIN_ART_BORDER_H

#include "TerrainMaterials.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace TerrainVisual
{
// A single layout supplies an unambiguous neighbor even across the wrap.
// Multi-layout materials can meet a different layout at the block boundary.
inline bool hasArtNeighbors(const Material &material)
{
    return material.animationFrames > 1 && material.variantGrid > 1 && material.variantBlocks == 1;
}

inline unsigned artNeighbor(const Material &material, unsigned variant, int dx, int dy)
{
    const unsigned grid = material.variantGrid;
    const unsigned x = (variant % grid + dx) & (grid - 1);
    const unsigned y = (variant / grid + dy) & (grid - 1);
    return y * grid + x;
}

// Report a gutter coordinate, its neighbor's offset, and the coordinate
// inside that neighbor. Corners draw from the diagonal neighbor.
template<class Write>
void visitArtBorder(int size, Write write)
{
    for (int y : {-1, size})
        for (int x = -1; x <= size; ++x)
            write(x, y, (x == size) - (x == -1), y == -1 ? -1 : 1,
                  x == -1 ? size - 1 : x == size ? 0 : x, y == -1 ? size - 1 : 0);
    for (int y = 0; y < size; ++y)
    {
        write(-1, y, -1, 0, size - 1, y);
        write(size, y, 1, 0, 0, y);
    }
}

// Fill an atlas slot after its interior has been written.
template<class Pixel, class Row>
void extendArtRows(int size, const std::vector<Pixel> &border, Row row)
{
    std::size_t i = 0;
    visitArtBorder(size, [&](int x, int y, int, int, int, int)
    {
        row(y)[x] = border.empty()
            ? row(std::clamp(y, 0, size - 1))[std::clamp(x, 0, size - 1)]
            : border[i++];
    });
}

template<class Pixel>
void extendArtTile(Pixel *pixels, int size, const std::vector<Pixel> &border)
{
    extendArtRows(size, border, [&](int y) { return pixels + (y + 1) * (size + 2) + 1; });
}

using ArtPixel = std::array<unsigned char, 4>;

inline ArtPixel artPixel(std::uint32_t argb)
{
    return {static_cast<unsigned char>(argb >> 16), static_cast<unsigned char>(argb >> 8),
            static_cast<unsigned char>(argb), static_cast<unsigned char>(argb >> 24)};
}

// Matches the direct atlas's premultiplied box filter. Smaller sources
// are expanded by nearest-neighbor before the atlas's linear filtering.
template<class Read>
ArtPixel sampleArt(int size, int density, int x, int y, Read read)
{
    unsigned sum[4] = {};
    const int block = std::max(1, size / density);
    const int sx = x * size / density, sy = y * size / density;
    for (int dy = 0; dy < block; ++dy)
        for (int dx = 0; dx < block; ++dx)
        {
            const ArtPixel p = read(std::size_t(sy + dy) * size + sx + dx);
            for (int channel = 0; channel < 3; ++channel) sum[channel] += unsigned(p[channel]) * p[3];
            sum[3] += p[3];
        }
    ArtPixel result{};
    for (int channel = 0; channel < 3; ++channel)
        result[channel] = sum[3] ? (sum[channel] + sum[3] / 2) / sum[3] : 0;
    const unsigned count = unsigned(block * block);
    result[3] = (sum[3] + count / 2) / count;
    return result;
}
}

#endif
