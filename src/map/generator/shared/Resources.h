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
} // namespace MapGeneration
