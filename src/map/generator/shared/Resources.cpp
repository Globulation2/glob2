// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Resources.h"
#include "Distances.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>
#include <utility>
using namespace MapGeneration;

namespace MapGeneration {
int placeResourceClump(Map &map, GenerationContext &context,
                       MapGeneratorPoint center, int resourceType, int radius) {
  int placed = 0;
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > radius * radius || (d2 > (radius - 1) * (radius - 1) &&
                                   context.bounded("resources", 4) == 0))
        continue;
      const int x = map.normalizeX(center.x + dx),
                y = map.normalizeY(center.y + dy);
      const int existingType = map.getResource(x, y).type;
      if (map.isResourceAllowed(x, y, resourceType) &&
          (existingType == NO_RES_TYPE || existingType == resourceType)) {
        map.setResource(x, y, resourceType, 1);
        ++placed;
      }
    }
  return placed;
}

int placeResourceClumpInArea(Map &map, GenerationContext &context,
                             const std::vector<MapGeneratorPoint> &points,
                             int resourceType, int radius) {
  if (points.empty())
    return 0;
  const size_t first = context.bounded("resources", points.size());
  for (size_t offset = 0; offset < points.size(); ++offset) {
    const MapGeneratorPoint &center = points[(first + offset) % points.size()];
    if (!map.isResourceAllowed(center.x, center.y, resourceType))
      continue;
    const int placed =
        placeResourceClump(map, context, center, resourceType, radius);
    if (placed)
      return placed;
  }
  return 0;
}

void scatterResources(Game &game, GenerationContext &context,
                      const ResourceDensities &density) {
  const int width = game.map.getW(), height = game.map.getH(),
            area = width * height;
  // Map::growResources only regrows a wheat or wood tile near water (the same triangular
  // kernel Fertility::Field evaluates exactly) - a corn or wood clump dropped somewhere with
  // none nearby is a one-time find that can never come back, not a farm. Computed once here,
  // ungated: nothing has been harvested yet at this point in generation, so "reachable from an
  // existing deposit" doesn't apply - this is the more basic question of whether the tile can
  // regrow at all. Stone, algae and fruit don't regrow this way, so they scatter freely.
  const Fertility::Field fertility = Fertility::forMap(game.map, false);
  auto scatter = [&](int type, int count, bool preferFertile) {
    const int radius = count < 8 ? 1 : 2;
    const int expectedClumpSize = radius == 1 ? 5 : 12;
    const int clumps =
        std::max(1, (count + expectedClumpSize - 1) / expectedClumpSize);
    for (int i = 0; i < clumps; ++i) {
      MapGeneratorPoint center(0, 0);
      bool found = false;
      std::uint32_t bestFertility = 0;
      // Best of up to 100 random legal tiles, scored by fertility - not a fixed bar, so it
      // never comes down to a coin flip between "no better candidate turned up in the budget"
      // and "sacrifice this clump's density entirely". A non-fertile-sensitive type (stone,
      // algae, fruit) scores every candidate 0, so this keeps its original behaviour of simply
      // taking the first legal tile found.
      for (int attempt = 0; attempt < 100; ++attempt) {
        const MapGeneratorPoint candidate = {int(context.bounded("resources", width)),
                                             int(context.bounded("resources", height))};
        if (!game.map.isResourceAllowed(candidate.x, candidate.y, type))
          continue;
        const std::uint32_t score = preferFertile ? fertility.at(candidate.x, candidate.y) : 0;
        if (!found || score > bestFertility) {
          center = candidate;
          bestFertility = score;
          found = true;
        }
      }
      if (found)
        placeResourceClump(game.map, context, center, type, radius);
    }
  };
  scatter(CORN, density.corn * area / 1600, true);
  scatter(WOOD, density.wood * area / 1600, true);
  scatter(STONE, density.stone * area / 3000, false);
  scatter(ALGA, density.algae * area / 800, false);
  for (int i = 0; i < density.fruit; ++i)
    scatter(CHERRY + context.bounded("resources", 3), 1, false);
}

void fillInResource(Map &map, GenerationContext &context,
                    std::vector<MapGeneratorPoint> &points, int resourceType,
                    int maxFillSize) {
  if (maxFillSize < 1 || maxFillSize >= std::min(map.getW(), map.getH()))
    throw GenerationFailure("Invalid resource patch size");
  for (unsigned int n = 0; n < points.size(); ++n) {
    map.setResource(points[n].x, points[n].y, resourceType,
                    1 + context.stream("regions")() % maxFillSize);
  }
}

namespace {
// Ground units can't walk onto a tile carrying a resource (Map::isHardSpaceForGroundUnit
// excludes them), so a solid, unbroken band from the noise-band resource painting can wall a
// team's boot tile off from the rest of an otherwise perfectly connected landmass. floodReach
// walks that real, resource-respecting space; dist is kept (not just aggregated) so a caller
// can compare it against a wall-blind flood and find exactly where such a wall runs.
struct ReachResult {
  int wheatDist = -1, woodDist = -1;
  std::vector<MapGeneratorPoint> closeGrass, farGrass;
  std::vector<int> dist;
};
ReachResult floodReach(Map &map, int bootX, int bootY, int exploreLimit, int closeRange,
                       int clearRadius) {
  const int w = map.getW(), h = map.getH();
  ReachResult r;
  r.dist.assign(size_t(w) * h, -1);
  // Bounded by the r.dist[np] < 0 guard below to at most w*h enqueues - a flat preallocated
  // FIFO instead of std::queue<int>'s std::deque, which grows by separately heap-allocated
  // blocks (same fix as computeDistances in Distances.cpp).
  std::vector<int> q(size_t(w) * h);
  size_t qHead = 0, qTail = 0;
  int start = bootY * w + bootX;
  r.dist[start] = 0;
  q[qTail++] = start;
  while (qHead < qTail) {
    int p = q[qHead++];
    int x = p % w, y = p / w;
    if (map.getUMTerrain(x, y) == GRASS && r.dist[p] >= clearRadius && r.dist[p] <= exploreLimit)
      (r.dist[p] <= closeRange ? r.closeGrass : r.farGrass).push_back(MapGeneratorPoint(x, y));
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0)
          continue;
        int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
        int np = ny * w + nx;
        int resType = map.getResource(nx, ny).type;
        if (resType == CORN && r.wheatDist < 0)
          r.wheatDist = r.dist[p] + 1;
        if (resType == WOOD && r.woodDist < 0)
          r.woodDist = r.dist[p] + 1;
        if (r.dist[np] < 0 && r.dist[p] < exploreLimit &&
            map.isHardSpaceForGroundUnit(nx, ny, false, 0)) {
          r.dist[np] = r.dist[p] + 1;
          q[qTail++] = np;
        }
      }
  }
  return r;
}

// The same flood, but blocked only by water — as if resources didn't exist. Diffing this
// against floodReach's result isolates exactly which tiles a resource wall blocks: reachable
// here, not reachable there, adjacent to what is. Clearing only those tiles (rather than an
// entire neighborhood) opens the way while disturbing nothing else nearby.
std::vector<int> terrainOnlyReach(Map &map, int bootX, int bootY, int limit) {
  const int w = map.getW(), h = map.getH();
  std::vector<int> dist(size_t(w) * h, -1);
  std::vector<int> q(size_t(w) * h);
  size_t qHead = 0, qTail = 0;
  int start = bootY * w + bootX;
  dist[start] = 0;
  q[qTail++] = start;
  while (qHead < qTail) {
    int p = q[qHead++];
    if (dist[p] >= limit)
      continue;
    int x = p % w, y = p / w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0)
          continue;
        int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
        int np = ny * w + nx;
        if (dist[np] < 0 && !map.isWater(nx, ny)) {
          dist[np] = dist[p] + 1;
          q[qTail++] = np;
        }
      }
  }
  return dist;
}

// Clears the resource tiles directly responsible for a team's cramped pocket: exactly the
// tiles reachable without resources blocking the way, not reachable with them, and touching a
// tile that is — the wall's inner face — plus, on a wall thick enough to have an outer face
// too, that face as well. A real dead end (no meaningfully larger landmass once resources are
// ignored) clears nothing, since there is no better tile on the other side to find. Iterated a
// few times by the caller in case a wall is thicker still.
bool clearResourceWall(Map &map, const std::vector<int> &boxedDist,
                       const std::vector<int> &openDist, int minGain) {
  const int w = map.getW(), h = map.getH();
  int cleared = 0;
  std::vector<std::pair<int, int>> toClear;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int p = y * w + x;
      if (boxedDist[p] >= 0 || openDist[p] < 0 || !map.isResource(x, y))
        continue;
      bool touchesBoxed = false;
      for (int dy = -1; dy <= 1 && !touchesBoxed; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0)
            continue;
          int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
          if (boxedDist[ny * w + nx] >= 0) {
            touchesBoxed = true;
            break;
          }
        }
      if (touchesBoxed)
        toClear.push_back({x, y});
    }
  if ((int)toClear.size() < minGain)
    return false;
  for (auto [x, y] : toClear) {
    map.setNoResource(x, y, 1);
    ++cleared;
  }
  return cleared > 0;
}
} // namespace

void guaranteeStartingResources(Game &game, GenerationContext &context, int wheatRange,
                                int woodRange, int clearRadius) {
  Map &map = game.map;
  const int exploreLimit = std::max(wheatRange, woodRange) * 5 / 2;
  const int closeRange = wheatRange / 2;
  // Below this many reached tiles a team is badly boxed in, but that has two very different
  // causes: a resource wall sealing off an otherwise fine landmass (swamp and river both paint
  // resources with no regard for what they might enclose), or the boot search simply landing on
  // a genuinely small spot — a real little island on a water-heavy map, say, where there's
  // nothing bigger on the other side to reach at all. Only the first is fixable, and only its
  // exact wall tiles are cleared, so a genuinely small spot is correctly left untouched.
  const int minPocketTiles = 60;
  for (int team = 0; team < context.request.nbTeams; ++team) {
    int bootX = context.bootX[team], bootY = context.bootY[team];
    ReachResult reach = floodReach(map, bootX, bootY, exploreLimit, closeRange, clearRadius);
    auto pocketSize = [&] {
      int n = 0;
      for (int v : reach.dist)
        if (v >= 0)
          ++n;
      return n;
    };
    bool underServed = reach.wheatDist < 0 || reach.wheatDist > wheatRange ||
                       reach.woodDist < 0 || reach.woodDist > woodRange;
    for (int attempt = 0; underServed && pocketSize() < minPocketTiles && attempt < 6; ++attempt) {
      std::vector<int> open = terrainOnlyReach(map, bootX, bootY, exploreLimit);
      if (!clearResourceWall(map, reach.dist, open, /*minGain=*/1))
        break;
      reach = floodReach(map, bootX, bootY, exploreLimit, closeRange, clearRadius);
      underServed = reach.wheatDist < 0 || reach.wheatDist > wheatRange || reach.woodDist < 0 ||
                    reach.woodDist > woodRange;
    }
    auto placeReachable = [&](int resourceType) {
      if (!reach.closeGrass.empty() &&
          placeResourceClumpInArea(map, context, reach.closeGrass, resourceType, 2))
        return;
      if (!reach.farGrass.empty())
        placeResourceClumpInArea(map, context, reach.farGrass, resourceType, 2);
    };
    if (reach.wheatDist < 0 || reach.wheatDist > wheatRange)
      placeReachable(CORN);
    if (reach.woodDist < 0 || reach.woodDist > woodRange)
      placeReachable(WOOD);
  }
}
} // namespace MapGeneration
