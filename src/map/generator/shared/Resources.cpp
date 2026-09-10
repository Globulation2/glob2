// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Resources.h"
#include "Distances.h"
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
  auto scatter = [&](int type, int count) {
    const int radius = count < 8 ? 1 : 2;
    const int expectedClumpSize = radius == 1 ? 5 : 12;
    const int clumps =
        std::max(1, (count + expectedClumpSize - 1) / expectedClumpSize);
    for (int i = 0; i < clumps; ++i) {
      MapGeneratorPoint center(0, 0);
      bool found = false;
      for (int attempt = 0; attempt < 100 && !found; ++attempt) {
        center = {int(context.bounded("resources", width)),
                  int(context.bounded("resources", height))};
        found = game.map.isResourceAllowed(center.x, center.y, type);
      }
      if (found)
        placeResourceClump(game.map, context, center, type, radius);
    }
  };
  scatter(CORN, density.corn * area / 1600);
  scatter(WOOD, density.wood * area / 1600);
  scatter(STONE, density.stone * area / 3000);
  scatter(ALGA, density.algae * area / 800);
  for (int i = 0; i < density.fruit; ++i)
    scatter(CHERRY + context.bounded("resources", 3), 1);
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
} // namespace MapGeneration
