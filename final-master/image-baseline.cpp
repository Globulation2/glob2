#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "LossyAlphaFixture.h"
#include <cstdio>
#include <initializer_list>
#include <cstring>
int main() {
 int failures=0;
 for(auto decode : {IMG_Load_IO,SDL_LoadPNG_IO}) {
  auto *surface=decode(SDL_IOFromConstMem(rgba16Fixture::png,sizeof(rgba16Fixture::png)),true);
  if(!surface)return 2;
  auto *rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32);
  if(!rgba)return 3;
  for(int y=0;y<2;++y) {bool same=std::memcmp(static_cast<char*>(rgba->pixels)+y*rgba->pitch,rgba16Fixture::rgba+y*8,8)==0;std::printf("decoder=%s row=%d match=%d\n",decode==IMG_Load_IO?"SDL_image":"SDL native",y,same);failures+=!same;}
  SDL_DestroySurface(rgba);SDL_DestroySurface(surface);
 }
 return failures?1:0;
}
