// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise runtime loading, all manifest frames, recoloring, zoom and cache release.
#include <GraphicContext.h>
#include <Toolkit.h>
#include <SDL.h>
#include <SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <chrono>
#include <vector>
#include <cmath>
#include <filesystem>
using namespace GAGCore;
struct Frame{std::string prefix,id;int index,w,h;};
static void capture(const std::string &name)
{
    glFinish();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);int w=v[2],h=v[3];
    std::vector<unsigned char>a(w*h*4),b(a.size());glReadPixels(v[0],v[1],w,h,GL_RGBA,GL_UNSIGNED_BYTE,a.data());
    for(int y=0;y<h;++y)std::copy_n(a.data()+y*w*4,w*4,b.data()+(h-1-y)*w*4);
    auto s=SDL_CreateRGBSurfaceWithFormatFrom(b.data(),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
    assert(s&&IMG_SavePNG(s,name.c_str())==0);SDL_FreeSurface(s);
}
int main(int argc,char**argv)
{
    std::filesystem::create_directories(".cache/highres-runtime-check");
    const bool software=argc>1&&std::string(argv[1])=="software";
    const bool fallback=argc>1&&std::string(argv[1])=="fallback";
    const bool original=argc>1&&std::string(argv[1])=="original";
    Toolkit::init("glob2-hd-pack-check");
    auto gfx=Toolkit::initGraphic(1280,960,software?0:GraphicContext::USEGPU,"HD runtime pack validation");
    {
        std::ifstream input("data/highres/v1/frames.txt");std::string magic,id,base,team;int version,w,h,scale;
        input>>magic>>version;assert(version==1);
        std::vector<Frame> frames;
        std::map<std::string,std::unique_ptr<Sprite>> sprites;
        while(input>>id>>w>>h>>scale>>base>>team)
        {
            auto split=id.find_last_not_of("0123456789")+1;auto prefix=id.substr(0,split);int index=std::stoi(id.substr(split));
            if(!sprites.count(prefix)){auto s=std::make_unique<Sprite>();assert(s->load("data/gfx/"+prefix));sprites[prefix]=std::move(s);}
            frames.push_back({prefix,id,index,w,h});
        }
        Sprite::setHighResolution(!original);
        for(auto f:frames){assert(sprites[f.prefix]->getW(f.index)==f.w);assert(sprites[f.prefix]->getH(f.index)==f.h);}
        if(software||fallback){assert(Sprite::highResolutionStats().cpuBytes==0);std::cout<<"PASS software: original resources, all logical sizes\n";}
        else
        {
            const size_t initial=Sprite::highResolutionStats().cpuBytes;assert(original?initial==0:initial>0);
            // Hue wheel covers all allowed team hues, including red wraparound.
            for(int color=0;color<16;++color)
            {
                Color teamColor;teamColor.setHSV(color*360.f/16,1,1);
                for(auto &entry:sprites)entry.second->setBaseColor(teamColor);
                for(size_t page=0;page*16<frames.size();++page)
                {
                    gfx->drawFilledRect(0,0,1280,960,36,45,38);
                    for(size_t k=page*16;k<std::min(frames.size(),(page+1)*16);++k)
                    {
                        auto f=frames[k];auto sprite=sprites[f.prefix].get();int col=(k%16)%4,row=(k%16)/4;
                        gfx->drawSprite(col*320+20,row*240+20,f.w*2,f.h*2,sprite,f.index);gfx->finishDrawingSprite(sprite,255);
                    }
                    if(!original&&(page==0||color==0||color==5||color==10))
                        capture(".cache/highres-runtime-check/team"+std::to_string(color)+"-page"+std::to_string(page)+".png");
                }
                assert(glGetError()==GL_NO_ERROR);
            }
            auto stats=Sprite::highResolutionStats();
            std::cout<<"All "<<frames.size()<<" frames, 16 team hues: CPU bytes="<<stats.cpuBytes<<", GPU bytes="<<DrawableSurface::allocatedTextureBytes()<<", colored frames="<<stats.coloredFrames<<"\n";
            for(double zoom:{.5,3.})
            {
                gfx->resetDrawCallCount();auto start=std::chrono::steady_clock::now();
                for(int repeat=0;repeat<30;++repeat)
                {
                    gfx->beginMapTransform(zoom,0,0,0,0,1280,960);
                    for(int y=0;y<std::ceil(960/zoom);y+=32)for(int x=0;x<std::ceil(1280/zoom);x+=32)
                        gfx->drawSprite(x,y,sprites["terrain"].get(),((x/32)+(y/32)*3)%272);
                    gfx->finishDrawingSprite(sprites["terrain"].get(),255);
                    for(int y=0;y<960/zoom;y+=64)for(int x=0;x<1280/zoom;x+=64)
                        gfx->drawSprite(x,y,sprites["ressource"].get(),((x/64)+(y/64)*3)%65);
                    gfx->finishDrawingSprite(sprites["ressource"].get(),255);

                    for(int y=0;y<960/zoom;y+=96)for(int x=0;x<1280/zoom;x+=96)
                    {auto f=frames[((x/96)+(y/96)*7)%73];gfx->drawSprite(x,y,sprites[f.prefix].get(),f.index);gfx->finishDrawingSprite(sprites[f.prefix].get(),255);}
                    gfx->endMapTransform();glFinish();
                }
                double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;
                std::cout<<zoom*100<<"% synthetic dense scene: "<<ms<<" ms/frame, "<<gfx->getDrawCallCount()/30<<" draw calls/frame\n";
            }
            assert(Sprite::highResolutionStats().coloredFrames==stats.coloredFrames);
            Sprite::setHighResolution(false);assert(Sprite::highResolutionStats().cpuBytes==0);
            std::cout<<"PASS logical sizes, all frames/team hues, GL errors, bounded cache, session release\n";
        }
    }
    Toolkit::close();
}
