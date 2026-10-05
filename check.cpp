#include <SDL3/SDL.h>
#include "LossyAlphaFixture.h"
#include <cstdio>
#include <cstring>
int main() {
 auto surface=SDL_LoadPNG_IO(SDL_IOFromConstMem(rgba16Fixture::png,sizeof(rgba16Fixture::png)),true);
 if(!surface) { std::puts(SDL_GetError()); return 2; }
 auto rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32);
 if(!rgba) { std::puts(SDL_GetError()); return 3; }
 bool match=true;
 for(int y=0;y<2;++y) { auto p=static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch; for(int x=0;x<8;++x)std::printf("%u%s",p[x],x==7?"\n":","); match=match&&std::memcmp(p,rgba16Fixture::rgba+y*8,8)==0; }
 SDL_DestroySurface(rgba);SDL_DestroySurface(surface);
 std::puts(match?"EXACT RGBA MATCH":"RGBA MISMATCH");return match?0:1;
}
