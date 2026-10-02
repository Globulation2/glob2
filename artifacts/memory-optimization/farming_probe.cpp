#include "AIMaximaFarming.h"
#include <ctime>
#include <iostream>
#include <vector>
int main(int argc,char**argv) {
 using namespace AIMaxima::Farming;
 int mode=argc>1?std::atoi(argv[1]):0;
 const int width=512;
 std::vector<uint8_t> water(width*width),sand(width*width);
 uint32_t random=19;
 for(int i=0;i<width*width;++i){random=random*1664525u+1013904223u;water[i]=mode==0?1:(random%100<35);random=random*1664525u+1013904223u;sand[i]=mode==2&&(random%100<4);}
 ExactFertilityCache cache;
 for(int i=0;i<8;++i)cache.rebuild(width,width,water,sand,SandCorrectionFertilityPath);
 auto start=std::clock();
 for(int i=0;i<64;++i)cache.rebuild(width,width,water,sand,SandCorrectionFertilityPath);
 double cpu=double(std::clock()-start)/CLOCKS_PER_SEC;
 uint64_t hash=1469598103934665603ull;for(auto v:cache.values()){hash^=v;hash*=1099511628211ull;}
 std::cout<<cpu<<" "<<hash<<"\n";
}
