// SPDX-License-Identifier: GPL-3.0-or-later
#include <GraphicContext.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace GAGCore;
class Context : public GraphicContext
{
public:
    Context() : GraphicContext(320, 240, PORTABLEGPU, "Glob2 portable renderer test") {}
    SDL_Surface* capture() { return renderer->capture(); }
    void resetTextures() { renderer->reset(); }
    void resizeWindow(int width,int height) { SDL_SetWindowSize(window,width,height); updateWindowSize(); }
    Uint32 windowID() const { return SDL_GetWindowID(window); }
};
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void expect(SDL_Surface* pixels, int x, int y, int r, int g, int b)
{
    Uint32 value;
    std::memcpy(&value, static_cast<char*>(pixels->pixels)+y*pixels->pitch+x*4, 4);
    Uint8 red, green, blue;
    SDL_GetRGB(value,pixels->format,&red,&green,&blue);
    if (std::abs(int(red)-r)>3 || std::abs(int(green)-g)>3 || std::abs(int(blue)-b)>3) {
        std::fprintf(stderr,"pixel %d,%d: %d,%d,%d expected %d,%d,%d\n",x,y,red,green,blue,r,g,b);
        throw std::runtime_error("Unexpected renderer pixel");
    }
}
int main()
{
    try {
        Context context;
        DrawableSurface sprite(16,16);
        sprite.drawFilledRect(0,0,16,16,Color(0,255,0));
        for(int pass=0;pass<3;++pass) {
            context.setClipRect();
            context.drawFilledRect(0,0,320,240,Color(0,0,0));
            context.drawFilledRect(10,10,20,20,Color(255,0,0));
            context.setClipRect(15,15,5,5);
            context.drawFilledRect(0,0,320,240,Color(0,0,255));
            context.setClipRect();
            context.drawSurface(40,40,32,32,&sprite);
            context.drawFilledRect(80,80,20,20,Color(255,255,255,128));
            auto* pixels=context.capture();
            require(pixels->w>=320 && pixels->h>=240,"Invalid drawable size");
            auto check=[&](int x,int y,int r,int g,int b) { expect(pixels,x*pixels->w/320,y*pixels->h/240,r,g,b); };
            check(0,0,0,0,0); check(12,12,255,0,0); check(16,16,0,0,255);
            check(50,50,pass==2?255:0,255,0); check(85,85,128,128,128);
            SDL_FreeSurface(pixels);
            context.nextFrame();
            if(pass==0) context.resetTextures();
            if(pass==1) sprite.drawFilledRect(0,0,16,16,Color(255,255,0));
        }
        context.resizeWindow(640,480);
        SDL_Event event{};
        while(SDL_PollEvent(&event)) GraphicContext::translateMouseEvent(&event);
        for(auto type:{SDL_MOUSEMOTION,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONUP}) {
            event={};event.type=type;
            if(type==SDL_MOUSEMOTION) {
                event.motion.windowID=context.windowID();event.motion.x=240;event.motion.y=160;
            } else {
                event.button.windowID=context.windowID();event.button.x=240;event.button.y=160;
            }
            require(SDL_PushEvent(&event)==1,"Could not enqueue resized-window input");
            bool observed=false;
            while(SDL_PollEvent(&event)) {
                GraphicContext::translateMouseEvent(&event);
                if(event.type==type) {
                    int x=type==SDL_MOUSEMOTION?event.motion.x:event.button.x;
                    int y=type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
                    require(x==120 && y==80,"Renderer mouse events must be mapped exactly once");
                    observed=true;
                }
            }
            require(observed,"Resized-window event was not delivered");
        }
        int x=240,y=160;
        GraphicContext::translateMouseCoordinates(x,y);
        require(x==120 && y==80,"Raw mouse coordinates must use the same logical mapping");
        std::puts("PASS portable renderer: clipping, texture scaling, alpha, device reset, dirty textures, resized input");
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
