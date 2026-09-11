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
#include <cstdint>
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

namespace {
// generateHeightField (Terrain.cpp) carves its own resource bands from the same noise field
// that carved the terrain: every tile's resource is a threshold on a value it already has,
// so wheat, wood and stone come out following the same contours the water and grass do,
// rather than looking like independent decisions. scatterResources runs after these
// generators' terrain is already fixed and irregular, so it cannot share that one field the
// way generateHeightField does, but it can still take the same threshold approach: fill the
// tightest (lowest-value) share of some smooth field that covers the requested tile count -
// the same histogrammed level search generateHeightField uses, just over an explicit candidate
// list instead of the whole grid. Compact circular clumps at independent random centers are a
// fundamentally different, and visibly clumpier, shape.
//
// scatterLevel selects that field: for corn and wood it is (the inverse of) Fertility::Field
// itself, so the band that gets painted is literally the most farmable ground on the map - it
// hugs real coastlines, bays and fjord banks because that is exactly the shape the field takes,
// and it still comes out organic rather than a uniform ring since every coastline this generates
// already is. An independent noise field would give an organic shape too, but a shape with no
// relation to where the resource could ever actually regrow (Map::growResources only replaces
// wheat or wood near water, the same kernel Fertility::Field evaluates exactly) - that was tried
// and measured: it let the noise contour claim tiles the fertility gate would have rejected on
// sight, undoing the fix that made these generators prefer farmable ground over merely legal
// ground in the first place. Stone and algae have no such regrowth rule, so they still use the
// independent noise field - any legal tile is as good as any other for them.
unsigned scatterLevel(int x, int y, const Fertility::Field *fertility, HeightMap *noise,
                     unsigned buckets) {
  if (fertility) {
    const std::uint32_t f = fertility->at(x, y);
    const std::uint32_t scaled =
        std::min<std::uint32_t>(buckets - 1, (f * buckets) / Fertility::kScale);
    return buckets - 1 - scaled;
  }
  return noise->uiLevel(x, y, buckets);
}

// A single global threshold across the whole map works for one connected landmass (Fjord,
// Maze), but Lattice's islets are separate landmasses that each carry their own, slightly
// different fertility (or noise) range - a global "take the best tiles first" pass can end up
// spending almost the entire band on whichever one or two islets happen to score highest,
// leaving the rest with none at all. That is exactly the "resources aren't balanced between
// players" complaint this whole scatter is meant to avoid, just at the scale of one islet
// instead of one player. Grouping candidates by connected landmass and running the same
// histogram threshold independently within each group, sized to that group's own share of the
// candidate pool, keeps every landmass' band proportional to how much eligible ground it has -
// on a single connected map this is one group covering everything, identical to before.
std::vector<int> computeLandComponents(const Map &map, int &numComponents) {
  const int w = map.getW(), h = map.getH();
  std::vector<int> component(size_t(w) * h, -1);
  // Bounded by the component[np] < 0 guard below to at most w*h enqueues per component, and
  // every tile belongs to exactly one component - a flat preallocated FIFO reused across
  // components, the same fix as floodReach below and computeDistances in Distances.cpp.
  std::vector<int> queue(size_t(w) * h);
  int nextId = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int start = y * w + x;
      if (component[start] != -1 || map.isWater(x, y))
        continue;
      size_t qHead = 0, qTail = 0;
      component[start] = nextId;
      queue[qTail++] = start;
      while (qHead < qTail) {
        const int p = queue[qHead++];
        const int px = p % w, py = p / w;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy)
              continue;
            const int nx = map.normalizeX(px + dx), ny = map.normalizeY(py + dy);
            const int np = ny * w + nx;
            if (component[np] == -1 && !map.isWater(nx, ny)) {
              component[np] = nextId;
              queue[qTail++] = np;
            }
          }
      }
      ++nextId;
    }
  numComponents = nextId;
  return component;
}

void scatterBand(Map &map, HeightMap *noise, int resourceType, int targetTiles,
                 const Fertility::Field *fertility, const std::vector<int> &landComponent,
                 int numComponents) {
  if (targetTiles <= 0 || numComponents <= 0)
    return;
  const int width = map.getW(), height = map.getH();
  constexpr unsigned kBuckets = 2048;
  std::vector<std::vector<MapGeneratorPoint>> candidates(numComponents);
  std::vector<std::vector<unsigned>> level(numComponents);
  std::vector<std::vector<int>> histogram(numComponents, std::vector<int>(kBuckets, 0));
  int totalCandidates = 0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      if (map.getResource(x, y).type != NO_RES_TYPE)
        continue;
      if (!map.isResourceAllowed(x, y, resourceType))
        continue;
      // Map::growResources only regrows wheat or wood near water (the same triangular kernel
      // Fertility::Field evaluates exactly) - a corn or wood tile with none nearby is a
      // one-time find that can never come back, not a farm. Stone and algae don't regrow this
      // way, so fertility is null for them and every legal tile is a candidate.
      if (fertility && fertility->at(x, y) == 0)
        continue;
      const int comp = landComponent[y * width + x];
      if (comp < 0)
        continue;
      const unsigned lvl = scatterLevel(x, y, fertility, noise, kBuckets);
      candidates[comp].emplace_back(x, y);
      level[comp].push_back(lvl);
      ++histogram[comp][lvl];
      ++totalCandidates;
    }
  if (totalCandidates == 0)
    return;
  for (int c = 0; c < numComponents; ++c) {
    const auto &compCandidates = candidates[c];
    if (compCandidates.empty())
      continue;
    const int share =
        int((std::int64_t(targetTiles) * std::int64_t(compCandidates.size())) / totalCandidates);
    const int wanted = std::min<int>(compCandidates.size(), std::max(1, share));
    unsigned threshold = kBuckets - 1;
    int accumulated = 0;
    for (unsigned b = 0; b < kBuckets; ++b) {
      accumulated += histogram[c][b];
      if (accumulated >= wanted) {
        threshold = b;
        break;
      }
    }
    for (size_t i = 0; i < compCandidates.size(); ++i)
      if (level[c][i] <= threshold)
        map.setResource(compCandidates[i].x, compCandidates[i].y, resourceType, 1);
  }
}
} // namespace

void scatterResources(Game &game, GenerationContext &context,
                      const ResourceDensities &density) {
  Map &map = game.map;
  const int width = map.getW(), height = map.getH(), area = width * height;
  const Fertility::Field fertility = Fertility::forMap(map, false);
  HeightMap noise(width, height, context.stream("scatter-noise"));
  noise.makePlain(24);
  int numComponents = 0;
  const std::vector<int> landComponent = computeLandComponents(map, numComponents);

  // Corn first, then wood from whatever the corn band didn't already claim (scatterBand skips
  // any tile that already has a resource) - wood settles for the next-best farmland the corn
  // band left behind, rather than the two competing over the same prime coastal ground.
  scatterBand(map, nullptr, CORN, density.corn * area / 1600, &fertility, landComponent,
             numComponents);
  scatterBand(map, nullptr, WOOD, density.wood * area / 1600, &fertility, landComponent,
             numComponents);
  scatterBand(map, &noise, STONE, density.stone * area / 3000, nullptr, landComponent,
             numComponents);
  scatterBand(map, &noise, ALGA, density.algae * area / 800, nullptr, landComponent,
             numComponents);

  // Fruit stays a rare, discrete find rather than a background band - "a distinct little
  // prize", the same role it already plays elsewhere in these generators - so it keeps the
  // original single-clump-at-a-random-point search instead of joining the noise bands above.
  for (int i = 0; i < density.fruit; ++i) {
    const int type = CHERRY + context.bounded("resources", 3);
    MapGeneratorPoint center(0, 0);
    bool found = false;
    for (int attempt = 0; attempt < 100 && !found; ++attempt) {
      center = {int(context.bounded("resources", width)), int(context.bounded("resources", height))};
      found = map.isResourceAllowed(center.x, center.y, type);
    }
    if (found)
      placeResourceClump(map, context, center, type, 1);
  }
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
