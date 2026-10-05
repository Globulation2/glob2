#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cassert>
#include <chrono>
namespace TerrainVisual { Coverage referenceCoverage(const Catalog &,const Recipe &,int,int); }
int main() {
 std::ifstream input("data/terrain/tileset.json");auto c=TerrainVisual::Catalog::parse(nlohmann::json::parse(input));
 std::uint64_t digest=14695981039346656037ull, count=0;
 for(int configuration=0;configuration<256;++configuration) {
  TerrainVisual::Recipe r;r.width=r.height=16;r.x=15;r.y=9;
  for(int y=0;y<4;++y)for(int x=0;x<4;++x)r.samples[y*4+x]=(configuration>>(2*((x&1)+2*(y&1))))&3;
  TerrainVisual::PreparedCoverage prepared(c,r);
  for(int scale : {1,4}) for(int y=0;y<32*scale;++y)for(int x=0;x<32*scale;++x) {
   const int px=(x*256+128)/scale,py=(y*256+128)/scale;
   auto a=TerrainVisual::referenceCoverage(c,r,px,py),b=prepared.at(px,py);
   assert(a.material==b.material && a.weight==b.weight); ++count;
   for(int k=0;k<4;++k) {digest=(digest^a.material[k])*1099511628211ull;digest=(digest^a.weight[k])*1099511628211ull;}
  }
 }
 std::cout<<count<<" native/HD coverage samples identical; digest "<<digest<<"\n";
}
