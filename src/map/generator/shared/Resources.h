#pragma once
#include "Regions.h"
#include <cstdint>
namespace MapGeneration {
// A resource-amount percentage (GeneratorControl::percentage) applied to a count or a share,
// rounding to nearest. 100 returns the input unchanged, so a default map is exactly what it was.
inline std::int64_t scaledCount(std::int64_t count, int percent) {
  return percent == 100 ? count : (count * percent + 50) / 100;
}
inline double scaledShare(double share, int percent) {
  return percent == 100 ? share : share * percent / 100.0;
}
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
// Map::setResource(x, y, type, size) scaled to `percent` of that square's tiles: the tiles
// nearest its centre, placed in setResource's own order, so 100 is exactly that call.
void setScaledResource(Map &, int x, int y, int resourceType, int size, int percent);
void scatterResources(Game &, GenerationContext &, const ResourceDensities &);
// Ensures every team has wheat within wheatRange and wood within woodRange of its boot tile,
// walking through the same reachable-space flood used to measure that distance (so anything
// placed is actually reachable, not just straight-line nearby). Teams already served within
// range are left untouched. clearRadius skips a ring around the boot tile that a caller still
// has to carve out for the swarm/workers after this runs (StartingPositions.cpp's setNoResource
// calls) — pass 0 when this runs after that carving has already happened.
// protectedWalls, when given, is a row-major width*height mask of resource tiles that belong to
// the map's design (a generator's stone ridgelines, say). They are treated like terrain: never
// cleared, and never looked past when deciding whether a colony is walled into a pocket.
void guaranteeStartingResources(Game &game, GenerationContext &context, int wheatRange,
                                int woodRange, int clearRadius = 0,
                                const std::vector<unsigned char> *protectedWalls = nullptr);
} // namespace MapGeneration
