#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
int main() {
 std::ifstream file("data/terrain/tileset.json");
 auto c=TerrainVisual::Catalog::parse(nlohmann::json::parse(file));
 int labels[32][32];
 for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
  int id=c.find(x<3?"water":x<8?"sand":"grass");
  if((x-17)*(x-17)+(y-10)*(y-10)<42)id=c.find("ice");
  if(y==20 || x==25 || (x>9&&x<24&&y==x-5))id=c.find("road");
  labels[y][x]=id;
 }
 labels[0][31]=c.find("ice");labels[0][0]=c.find("road");
 nlohmann::json result=nlohmann::json::array();
 for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
  TerrainVisual::Recipe r;r.width=r.height=32;r.x=x;r.y=y;
  for(int j=0;j<4;++j)for(int i=0;i<4;++i)
   r.samples[j*4+i]=labels[((y*2+j-1)&63)/2][((x*2+i-1)&63)/2];
  TerrainVisual::PreparedCoverage coverage(c,r);
  unsigned minimum=255,maximum=0;
  for(int py=0;py<32;++py)for(int px=0;px<32;++px) {
   auto pixel=coverage.at(px*256+128,py*256+128);unsigned water=0;
   for(int i=0;i<4;++i)if(pixel.material[i]==c.find("water"))water+=pixel.weight[i];
   unsigned alpha=((65536-water)*255+32768)/65536;
   minimum=std::min(minimum,alpha);maximum=std::max(maximum,alpha);
  }
  result.push_back({{"x",x},{"y",y},{"material",c.materials[labels[y][x]].key},{"alpha_min",minimum},{"alpha_max",maximum}});
 }
 std::cout<<result.dump(2)<<'\n';
}
