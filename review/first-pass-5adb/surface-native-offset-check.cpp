#include <SurfaceRaster.h>
#include <initializer_list>
#include <climits>
#include <cstdio>
#include <cstring>
int main(){
 auto*source=SDL_CreateSurface(5,4,SDL_PIXELFORMAT_RGBA32);auto*target=SDL_CreateSurface(4,4,SDL_PIXELFORMAT_RGBA32);if(!source||!target)return 2;
 const SDL_Rect clip{2,2,1,1};SDL_SetSurfaceClipRect(target,&clip);
 for(const auto rectangle:{SDL_Rect{INT_MAX,0,8,7},SDL_Rect{0,INT_MAX,8,7}}){
  std::memset(target->pixels,0x42,target->pitch*target->h);
  GAGCore::SurfaceRaster::blit(target,source,rectangle,{2,2,8,7},173,false,GAGCore::SurfaceRaster::BlitBlend::Triangle);
  auto*bytes=static_cast<unsigned char*>(target->pixels);for(int i=0;i<target->pitch*target->h;i++)if(bytes[i]!=0x42)return 3;
 }
 SDL_DestroySurface(source);SDL_DestroySurface(target);std::puts("two far-source axes: unchanged clipped pixels, UBSan clean");
}
