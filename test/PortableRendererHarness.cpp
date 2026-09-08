// SPDX-License-Identifier: GPL-3.0-or-later
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <ScreenStack.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace GAGCore;
class Context : public GraphicContext
{
public:
    Context() : GraphicContext(320, 240, PORTABLEGPU | RESIZABLE, "Glob2 portable renderer test") {}
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
        {
            struct ResourceScreen : GAGGUI::Screen {
                Context& context; DrawableSurface& sprite;
                int draws=0, red=255, green=255;
                ResourceScreen(Context& context,DrawableSurface& sprite):context(context),sprite(sprite) {}
                void onAction(GAGGUI::Widget*,GAGGUI::Action,int,int) override {}
                void updateExecution(Uint32) override {}
                void drawExecution() override {
                    context.setClipRect();
                    context.drawFilledRect(0,0,320,240,Color(0,0,0));
                    context.drawSurface(40,40,32,32,&sprite);
                    auto* pixels=context.capture();
                    expect(pixels,50*pixels->w/320,50*pixels->h/240,red,green,0);
                    SDL_FreeSurface(pixels);context.nextFrame();++draws;
                }
            };
            GAGGUI::ScreenStack stack(context);
            auto owned=std::make_unique<ResourceScreen>(context,sprite);auto* probe=owned.get();
            stack.push(std::move(owned));stack.frame(0,{});
            SDL_Event background{};background.type=SDL_APP_WILLENTERBACKGROUND;
            SDL_Event reset{};reset.type=SDL_RENDER_DEVICE_RESET;
            stack.frame(40,{background,reset});
            require(probe->draws==1,"No rendering while backgrounded with a lost device");
            // Change CPU pixels without the normal dirty notification: the
            // deferred reset must recreate the previously cached texture.
            auto* source=sprite.getSDLSurface();
            SDL_FillRect(source,nullptr,SDL_MapRGB(source->format,255,0,0));
            probe->green=0;
            SDL_Event foreground{};foreground.type=SDL_APP_DIDENTERFOREGROUND;
            stack.frame(100000,{foreground});
            require(probe->draws==2,"Resource restoration precedes the first resumed draw");
            SDL_FillRect(source,nullptr,SDL_MapRGB(source->format,0,255,0));
            probe->red=0;probe->green=255;
            reset.type=SDL_APP_LOWMEMORY;
            stack.frame(100040,{reset});
        }
        SDL_setenv("GLOB2_RESPONSIVE_UI", "1", 1);
        context.setResponsiveViewport(true);
        require(context.getW()==640 && context.getH()==480,"Responsive viewport must fill window points");
        context.resizeWindow(320,568);
        require(context.getW()==320 && context.getH()==568,"Portrait resize must update logical dimensions");
        context.drawFilledRect(0,0,320,568,Color(90,30,150));
        auto* portrait=context.capture();
        expect(portrait,portrait->w/2,portrait->h-2,90,30,150);
        SDL_FreeSurface(portrait);
        context.setResponsiveViewport(true,800,600);
        require(context.getW()==800 && context.getH()==1420,"Portrait game must extend to the full window height");
        context.drawFilledRect(0,0,800,1420,Color(90,30,150));
        auto* full=context.capture();
        expect(full,full->w/2,2,90,30,150);expect(full,full->w/2,full->h-2,90,30,150);
        SDL_FreeSurface(full);context.nextFrame();
        context.resizeWindow(568,320);
        require(context.getW()==1065 && context.getH()==600,"Landscape game must extend to the full window width");
        context.drawFilledRect(0,0,1065,600,Color(90,30,150));
        full=context.capture();
        expect(full,2,full->h/2,90,30,150);expect(full,full->w-2,full->h/2,90,30,150);
        SDL_FreeSurface(full);context.nextFrame();
        context.resizeWindow(320,568);
        context.setResponsiveViewport(false);
        require(context.getW()==320 && context.getH()==240,"Legacy logical dimensions must be restored");
        auto* letterbox=context.capture();
        expect(letterbox,letterbox->w/2,2,0,0,0);
        SDL_FreeSurface(letterbox);
        {
            struct ViewportScreen : GAGGUI::Screen {
                int changes=0;
                void onAction(GAGGUI::Widget*,GAGGUI::Action,int,int) override {}
                bool usesResponsiveViewport() const override { return true; }
                std::pair<int,int> minimumViewportSize() const override { return {800,600}; }
                void updateExecution(Uint32) override {}
                void drawExecution() override {}
                void viewportResized(int,int,int,int) override { ++changes; }
            };
            struct Child : GAGGUI::Screen {
                void onAction(GAGGUI::Widget*,GAGGUI::Action,int,int) override {}
                void updateExecution(Uint32) override { endExecute(0); }
                void drawExecution() override {}
            };
            GAGGUI::ScreenStack stack(context);
            auto parent=std::make_unique<ViewportScreen>();auto* probe=parent.get();
            stack.push(std::move(parent));stack.frame(0,{});
            require(context.getH()==1420 && probe->changes==1,"Entering gameplay must notify its expanded viewport");
            stack.push(std::make_unique<Child>());stack.frame(40,{});stack.frame(80,{});
            require(context.getH()==1420 && probe->changes==3,"A modal round trip must restore and notify the game viewport");
            SDL_SetWindowSize(SDL_GetWindowFromID(context.windowID()),568,320);
            SDL_Event resize{};resize.type=SDL_WINDOWEVENT;resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            stack.frame(120,{resize});
            require(context.getW()==1065 && context.getH()==600 && probe->changes==4,"Rotation must notify the retained game");
        }
        SDL_setenv("GLOB2_RESPONSIVE_UI", "0", 1);
        std::puts("PASS portable renderer: clipping, texture scaling, alpha, device reset, dirty textures, resized input");
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
