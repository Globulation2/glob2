// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "MapInternal.h"
#include "Unit.h"
#include "gradient/GradientRuntime.h"

namespace
{
using SafetyField = TerrainSafetyCache::Field;

struct EscapeProfile
{
    bool air;
    int swim;
    Uint32 team;
    bool escapeForbidden;

    bool safe(const TerrainProperties& terrain) const
    {
        return air ? terrain.flyable && terrain.airHealthQ8 >= 0
            : (terrain.walkable || (swim > 0 && terrain.swimmable)) && terrain.groundHealthQ8 >= 0;
    }

    unsigned entryCost(const Map& map, size_t index, bool diagonal) const
    {
        const auto terrain = map.terrainTypeAt(index);
        if (air)
        {
            const auto cardinal = map.terrainRegistry().airRouteCost(terrain);
            return diagonal ? cardinal * 14 / 10 : cardinal;
        }
        const auto cost = map.terrainRegistry().movement(swim).entries[terrain];
        return diagonal ? cost.diagonal : cost.cardinal;
    }

    Uint32 initialCost(const Map& map, int x, int y) const
    {
        const auto index = map.coordToIndex(x, y);
        const auto& terrain = map.terrainPropertiesAt(index);
        const bool forbidden = !air && (map.getForbidden(x, y) & team);
        const bool passable = air ? terrain.flyable && !map.resourceBlocksAir(index)
            : (terrain.walkable || (swim > 0 && terrain.swimmable)) &&
              map.getBuilding(x, y) == NOGBID && !map.resourceBlocksGround(index) &&
              (!forbidden || escapeForbidden);
        if (!passable) return SafetyField::Blocked;
        return safe(terrain) && !forbidden ? 0 : SafetyField::Unreachable;
    }

    bool freeNextStep(const Map& map, int x, int y) const
    {
        if (air) return map.isFreeForAirUnit(x, y);
        return escapeForbidden ? map.isFreeForGroundUnitNoForbidden(x, y, swim > 0)
            : map.isFreeForGroundUnit(x, y, swim > 0, team);
    }
};

void buildEscapeField(const Map& map, const EscapeProfile& profile, SafetyField& field)
{
    const int width = map.getW(), height = map.getH();
    auto& costs = field.costs;
    costs.resize(size_t(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            costs[map.coordToIndex(x, y)] = profile.initialCost(map, x, y);

    // Reverse multi-source Dijkstra: a cell stores the remaining cost of entering
    // successive destinations. Therefore relaxing predecessor n from i charges
    // the cost of entering i, not n. All safe, unblocked cells are zero-cost goals.
    // Queue only their hazardous boundary, rather than every safe map cell.
    constexpr unsigned BucketCount = 256;
    std::array<std::vector<Uint32>, BucketCount> buckets;
    size_t pending = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto index = map.coordToIndex(x, y);
            if (costs[index] != SafetyField::Unreachable) continue;
            Uint32 best = SafetyField::Unreachable;
            for (const auto& offset : tabClose)
            {
                const auto next = map.coordToIndex(x + offset[0], y + offset[1]);
                if (costs[next] == 0)
                    best = std::min(best, profile.entryCost(map, next, offset[0] && offset[1]));
            }
            if (best == SafetyField::Unreachable) continue;
            costs[index] = best;
            buckets[best % BucketCount].push_back(index);
            ++pending;
        }

    // Compiled edges lie in [1,253], so no relaxation appends to the bucket
    // currently being visited, nor can a future layer alias an active layer.
    for (Uint32 current = 0; pending; ++current)
    {
        auto& bucket = buckets[current % BucketCount];
        for (const auto index : bucket)
        {
            --pending;
            if (costs[index] != current) continue; // Superseded queue entry.
            const int x = index % width, y = index / width;
            const auto cardinal = profile.entryCost(map, index, false);
            const auto diagonal = profile.entryCost(map, index, true);
            for (const auto& offset : tabClose)
            {
                const auto next = map.coordToIndex(x + offset[0], y + offset[1]);
                if (costs[next] == SafetyField::Blocked) continue;
                const Uint64 candidate = Uint64(current) + (offset[0] && offset[1] ? diagonal : cardinal);
                if (candidate >= SafetyField::Blocked || candidate >= costs[next]) continue;
                costs[next] = Uint32(candidate);
                buckets[candidate % BucketCount].push_back(next);
                ++pending;
            }
        }
        bucket.clear();
    }
}

bool chooseEscapeStep(const Map& map, const EscapeProfile& profile, const SafetyField& field, Unit& unit)
{
    const auto here = field.costs[map.coordToIndex(unit.posX, unit.posY)];
    if (here == SafetyField::Unreachable || here == 0) return false;
    Uint64 best = std::numeric_limits<Uint64>::max();
    for (const auto& offset : tabClose)
    {
        // Map cell queries wrap coordinates on the torus.
        const int x = unit.posX + offset[0];
        const int y = unit.posY + offset[1];
        const auto next = map.coordToIndex(x, y);
        // Strict descent prevents oscillation if traffic blocks the ideal step.
        // Blocked origins may still escape (e.g. a deposit appeared under a unit).
        if (field.costs[next] >= here || !profile.freeNextStep(map, x, y)) continue;
        const auto score = Uint64(field.costs[next]) + profile.entryCost(map, next, offset[0] && offset[1]);
        if (score < best)
        {
            best = score;
            unit.dx = offset[0];
            unit.dy = offset[1];
        }
    }
    unit.directionFromDxDy();
    return best != std::numeric_limits<Uint64>::max();
}
}

bool Map::pathfindTerrainSafety(Unit* unit)
{
    unit->dx = unit->dy = 0;
    unit->direction = 8;
    const bool air = unit->performance[FLY] > 0;
    const EscapeProfile profile{air, unit->swimClass(), unit->owner->me,
        !air && bool(getForbidden(unit->posX, unit->posY) & unit->owner->me)};

    // Avoid allocating a field on wholly hazardous maps. Counts are maintained
    // by map edits, so this scans terrain definitions rather than map cells.
    bool hasSafeTerrain = false;
    for (size_t t = 0; t < terrainCounts.size(); ++t)
        if (terrainCounts[t] && profile.safe(terrainRegistry().properties(TerrainType(t))))
        {
            hasSafeTerrain = true;
            break;
        }
    if (!hasSafeTerrain) return false;

    // Ground units share by permissions, not unit type or HP. Flyers ignore team
    // paint and share one field. Occupancy is deliberately checked only at descent.
    const unsigned key = air ? 0 : 1 + (unit->owner->teamNumber * SWIM_CLASS_COUNT + profile.swim) * 2 + profile.escapeForbidden;
    auto& cache = gradientRuntime->safety;
    auto& field = cache.acquire(key, size);
    const Uint64 generation = air ? cache.airGeneration : cache.groundGeneration;
    if (field.costs.size() != size || field.generation != generation)
    {
        buildEscapeField(*this, profile, field);
        field.generation = generation;
        ++cache.builds;
    }
    return chooseEscapeStep(*this, profile, field, *unit);
}
