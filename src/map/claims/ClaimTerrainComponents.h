// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <memory>
#include <mutex>
#include <vector>
#include "MovementClass.h"

class Map;
namespace ResourceClaims
{
// Derived, never-saved connected components of terrain-passable cells for one
// movement class (8-neighbour, toroidal). Units, buildings, resources and
// forbidden areas are ignored, so every claim route stays inside one terrain
// component: a different component proves an access unreachable, while the
// same component does not prove it reachable. Labels are assigned in cell
// index order, so they depend only on the terrain, never on hash or float order.
struct TerrainComponentLabels
{
    static constexpr Uint32 None=UINT32_MAX; // Terrain-impassable cell.
    int w=0,h=0;
    std::vector<Uint32> labels;
    Uint32 at(Uint32 cell) const { return labels[cell]; }
    bool operator==(const TerrainComponentLabels&) const = default;
};
std::shared_ptr<const TerrainComponentLabels> labelTerrainComponents(const Map&,int movement);
// True unless terrain proves no cell of the access' 3x3 stance (the route
// goal: any cell within Chebyshev distance 1) shares the unit's component.
bool terrainComponentMayReach(const TerrainComponentLabels&,int unitX,int unitY,Uint32 access);

class TerrainComponentCache
{
public:
    // Rebuilt lazily when the map's terrain revisions or dimensions change.
    std::shared_ptr<const TerrainComponentLabels> labels(const Map&,int movement);
    Uint64 rebuilds() const { std::lock_guard lock(mutex); return rebuildCount; }
private:
    struct Plane {
        std::array<Uint64,2> revision{};
        std::shared_ptr<const TerrainComponentLabels> labels;
    };
    mutable std::mutex mutex;
    std::array<Plane,MOVEMENT_CLASS_COUNT> planes;
    Uint64 rebuildCount=0;
};
}
