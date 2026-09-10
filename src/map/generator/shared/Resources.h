#pragma once
#include "Regions.h"
namespace MapGeneration {
struct ResourceDensities {
  int corn, wood, stone, algae, fruit;
};
void fillInResource(Map &map, GenerationContext &context,
                    std::vector<MapGeneratorPoint> &points, int resourceType,
                    int maxFillSize);
int placeResourceClump(Map &, GenerationContext &, MapGeneratorPoint center,
                       int resourceType, int radius);
int placeResourceClumpInArea(Map &, GenerationContext &,
                             const std::vector<MapGeneratorPoint> &,
                             int resourceType, int radius);
void scatterResources(Game &, GenerationContext &, const ResourceDensities &);
// Ensures every team has wheat within wheatRange and wood within woodRange of its boot tile,
// walking through the same reachable-space flood used to measure that distance (so anything
// placed is actually reachable, not just straight-line nearby). Teams already served within
// range are left untouched. clearRadius skips a ring around the boot tile that a caller still
// has to carve out for the swarm/workers after this runs (StartingPositions.cpp's setNoResource
// calls) — pass 0 when this runs after that carving has already happened.
void guaranteeStartingResources(Game &game, GenerationContext &context, int wheatRange,
                                int woodRange, int clearRadius = 0);
} // namespace MapGeneration
