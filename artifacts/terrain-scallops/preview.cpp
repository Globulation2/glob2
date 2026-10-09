#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <fstream>
namespace
{
TerrainVisual::Catalog catalog()
{
	std::ifstream f("data/terrain/tileset.json"); nlohmann::json j; f >> j; return TerrainVisual::Catalog::parse(j);
}
template <class Field>
TerrainVisual::Recipe contextualRecipe(int x, int y, int size, Field field)
{
	TerrainVisual::Recipe r;
	r.x = (x % size + size) % size; r.y = (y % size + size) % size;
	r.width = r.height = size; r.hasNeighborhood = true;
	for (int dy = -1; dy <= 2; ++dy)
		for (int dx = -1; dx <= 2; ++dx)
			r.neighborhood[(dy + 1) * 4 + dx + 1] =
				field((r.x + dx + size) % size, (r.y + dy + size) % size);
	r.corners = {r.neighborhood[5], r.neighborhood[6], r.neighborhood[9], r.neighborhood[10]};
	return r;
}
std::array<unsigned, 64> materialWeights(const TerrainVisual::Coverage &coverage)
{
	std::array<unsigned, 64> weights{};
	for (unsigned k = 0; k < 4; ++k) weights[coverage.material[k]] += coverage.weight[k];
	return weights;
}
} // namespace

int main(int argc,char**argv) {
 auto c=catalog(); auto a=c.find("grass"), b=c.find("sand");
 std::vector<unsigned char> pixels(768*512);
 for(int y=0;y<16;++y) for(int x=0;x<24;++x) {
  auto r=contextualRecipe(x,y,32,[&](int xx,int yy){
   const int dx=xx-12,dy=yy-8;
   return (dx*dx+dy*dy<48 || xx<3) ? a:b;
  }); r.seed=73;
  TerrainVisual::PreparedCoverage p(c,r);
  for(int py=0;py<32;++py) for(int px=0;px<32;++px)
   pixels[(y*32+py)*768+x*32+px]=materialWeights(p.at(px*256+128,py*256+128))[a]*255u/65536;
 }
 std::ofstream f(argv[1],std::ios::binary); f<<"P5\n768 512\n255\n"; f.write((char*)pixels.data(),pixels.size());
}
