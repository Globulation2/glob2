// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include <array>
#include <cstdint>

// Cold presentation metadata is deliberately separate from simulation properties.
struct TerrainColor { std::uint8_t r, g, b; };
struct TerrainPresentation
{
    const char *name;
    const char *label;
    const char *sprite;
    int firstFrame, variants, editorFrame;
    TerrainColor minimap, overview, image;
    bool animatedBackdrop;
    bool legacyCorners;
    int edgeFirstFrame; // Four-side masks 1..15, one frame each; -1 means no overlay.
    int layerPriority;
    // Each animation phase contains variants consecutive 32x32 frames.
    int animationFrames = 1, animationTicks = 1;
    // Optional 32x32 background layer, drawn beneath the primary tile.
    const char *backdropSprite = nullptr;
    int backdropFirstFrame = 0, backdropFrames = 1, backdropTicks = 1;
    TerrainColor preview = minimap;
    bool editorSelectable = true;
};
struct TerrainSharedBackdrop
{
    const char *sprite;
    int firstFrame, frames, ticksPerFrame, scrollDivisorX, scrollDivisorY;
};
// The classic ocean is a shared scrolling backdrop, including the transparent
// holes in old shore artwork. Other materials may have their own cell backdrop.
inline constexpr TerrainSharedBackdrop TerrainOceanBackdrop{"data/gfx/water",0,1,1,2,0};
inline constexpr auto TerrainPresentations = [] {
    std::array<TerrainPresentation, TERRAIN_COUNT> definitions{{
    {"water", "[water]", "data/gfx/terrain", 256, 16, 259, {0,40,120}, {70,50,191}, {0,64,255}, true, true, -1, 0},
    {"sand", "[sand]", "data/gfx/terrain", 128, 16, 128, {170,170,0}, {182,168,48}, {240,220,140}, false, true, -1, 0},
    {"grass", "[grass]", "data/gfx/terrain", 0, 16, 0, {0,90,0}, {30,113,30}, {0,128,0}, false, true, -1, 0},
    {"ice", "[ice]", "data/gfx/terrain", 272, 16, 272, {190,225,240}, {190,225,240}, {190,225,240}, false, false, 304, 2},
    // Legacy external name/key retained for scripts, reports and editor actions.
    {"road", "[road]", "data/gfx/terrain", 288, 16, 288, {176,138,98}, {176,138,98}, {176,138,98}, false, false, 319, 1},
    {"grass_sand_border", "[sand]", "data/gfx/terrain", 16, 112, 16, {85,130,0}, {106,140,39}, {240,220,140}, false, true, -1, 0},
    {"sand_water_border", "[sand]", "data/gfx/terrain", 144, 112, 144, {85,105,60}, {126,109,119}, {240,220,140}, false, true, -1, 0},
}};
    definitions[GRASS_SAND_SHORE].editorSelectable = false;
    definitions[SAND_WATER_SHORE].editorSelectable = false;
    return definitions;
}();
inline constexpr const TerrainPresentation &terrainPresentation(TerrainType type)
{
    return TerrainPresentations[static_cast<std::size_t>(type)];
}
inline constexpr bool terrainUsesLegacyCorners(TerrainType type)
{
    return terrainPresentation(type).legacyCorners;
}
inline constexpr unsigned terrainVisualHash(int x, int y)
{
    std::uint32_t h = std::uint32_t(x)*73856093u ^ std::uint32_t(y)*19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h;
}
inline constexpr int terrainVisualFrame(TerrainType type, int x, int y)
{
    const auto &p = terrainPresentation(type);
    return p.firstFrame + terrainVisualHash(x,y)%p.variants;
}
inline constexpr int terrainScrollOffset(int time, int divisor)
{
    return divisor ? time/divisor : 0;
}
inline constexpr int terrainAnimatedFrame(int first, int phases, int ticks, int time)
{
    return first + (static_cast<unsigned>(time) / static_cast<unsigned>(ticks)) % phases;
}
inline constexpr int terrainAnimationOffset(const TerrainPresentation &p, int time)
{
    return terrainAnimatedFrame(0,p.animationFrames,p.animationTicks,time)*p.variants;
}
// A cell can touch at most four other materials. Layers are clipped to its tile.
struct TerrainLayers
{
    static constexpr int Capacity = 6;
    std::array<int, Capacity> frames{{-1,-1,-1,-1,-1,-1}};
    // Base occupies slot zero, or slot one when a backdrop is prepended.
    // Edge slots follow; material ownership chooses each layer's registered atlas.
    std::array<TerrainType, Capacity> materials{};
    std::array<bool, Capacity> backdrop{};
    bool operator==(const TerrainLayers &) const = default;
};

inline constexpr TerrainLayers terrainBaseLayers(TerrainType type, int baseFrame,
    const TerrainPresentation &p, int animationTime)
{
    TerrainLayers result;
    const int base = p.backdropSprite ? 1 : 0;
    result.materials[base] = type;
    if (!p.animatedBackdrop) result.frames[base] = baseFrame + terrainAnimationOffset(p,animationTime);
    if (p.backdropSprite) {
        result.frames[0] = terrainAnimatedFrame(p.backdropFirstFrame,p.backdropFrames,p.backdropTicks,animationTime);
        result.materials[0] = type;
        result.backdrop[0] = true;
    }
    return result;
}
// Cache descriptors reserve sixteen bits for a sprite frame and fifteen for
// material/backdrop ownership; frame values remain identical in saved maps.
static_assert(TERRAIN_COUNT <= 16384);
static_assert([] {
    for (const auto &p : TerrainPresentations)
        if (!p.name || !p.label || !p.sprite || p.variants <= 0 || p.firstFrame < 0 || p.editorFrame < 0 ||
            p.animationFrames < 1 || p.animationTicks < 1 || p.backdropFrames < 1 || p.backdropTicks < 1 ||
            p.firstFrame+p.variants*p.animationFrames > 65536 || p.edgeFirstFrame+15 > 65536 ||
            p.backdropFirstFrame < 0 || p.backdropFirstFrame+p.backdropFrames > 65536)
            return false;
    return true;
}(), "Every terrain requires complete presentation metadata");
