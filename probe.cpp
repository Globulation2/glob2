#include "TerrainExperiments.h"
#include <cstdio>
constexpr bool valid() {
 for(unsigned i=0;i<TERRAIN_COUNT;++i) {
  const auto actual=terrainExperiment(static_cast<TerrainType>(i));
  const auto expected=i==TRAIL?std::optional<ExperimentId>(ExperimentId::TrailTerrain):terrainGroupExperiment(TERRAIN_TYPES[i].group);
  if(actual!=expected)return false;
 }
 return !terrainExperiment(static_cast<TerrainType>(TERRAIN_COUNT));
}
static_assert(valid());
int main() {
 for(unsigned i=0;i<TERRAIN_GROUP_COUNT;++i) {auto e=terrainGroupExperiment(static_cast<TerrainGroup>(i));std::printf("group %u %d\n",i,e?int(*e):-1);}
 for(unsigned i=0;i<=TERRAIN_COUNT;++i) {auto e=terrainExperiment(static_cast<TerrainType>(i));std::printf("terrain %u %d\n",i,e?int(*e):-1);}
}
