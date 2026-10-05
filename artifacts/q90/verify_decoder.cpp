#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    std::ifstream list(argv[1]); std::string source, target, mode; int count=0;
    while(std::getline(list,source) && std::getline(list,target) && std::getline(list,mode)) {
        auto a=IMG_Load(source.c_str()), b=IMG_Load(target.c_str());
        if(!a||!b || a->w!=b->w || a->h!=b->h) {std::cerr<<"load/dimensions: "<<source<<" "<<SDL_GetError()<<"\n";return 1;}
        auto ra=SDL_ConvertSurface(a,SDL_PIXELFORMAT_RGBA32), rb=SDL_ConvertSurface(b,SDL_PIXELFORMAT_RGBA32);
        if(!ra||!rb) return 2;
        for(int y=0;y<ra->h;y++) for(int x=0;x<ra->w;x++) {
            auto p=(unsigned char*)ra->pixels+y*ra->pitch+x*4;
            auto q=(unsigned char*)rb->pixels+y*rb->pitch+x*4;
            for(int c=(mode=="lossy"?3:0);c<4;c++) if(p[c]!=q[c]) {
                std::cerr<<"pixel mismatch: "<<source<<" at "<<x<<","<<y<<" channel "<<c<<"\n";return 3;
            }
        }
        SDL_DestroySurface(ra);SDL_DestroySurface(rb);SDL_DestroySurface(a);SDL_DestroySurface(b);count++;
    }
    std::cout<<"Verified "<<count<<" image pairs with SDL_image: exact dimensions/alpha; lossless RGBA\n";
}
