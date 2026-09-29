// SPDX-License-Identifier: GPL-3.0-or-later
#include <MapCamera.h>
#include <cassert>
#include <iostream>
static bool close(double a,double b){return std::abs(a-b)<1e-8;}
int main()
{
    for(double zoom:{.25,.5,1.,2.,MapCamera::MAX_ZOOM})
    {
        MapCamera c;c.resize(960,720,4096,4096);c.originX=4080.25;c.originY=4000.5;
        c.setZoom(zoom,317,283);
        for(int y=16;y<720;y+=17)for(int x=0;x<960;x+=23)
        {
            auto w=c.screenToWorld(x,y);auto s=c.worldToScreen(w.first,w.second);
            assert(close(s.first,x)&&close(s.second,y));
            assert((c.tileX()+(c.localX(x)>>5))%128==int(MapCamera::wrap(w.first,4096)/32));
            assert((c.tileY()+(c.localY(y)>>5))%128==int(MapCamera::wrap(w.second,4096)/32));
        }
        auto before=c.screenToWorld(200,300);c.wheel(.3,200,300);auto after=c.screenToWorld(200,300);
        assert(close(MapCamera::wrap(before.first,4096),MapCamera::wrap(after.first,4096)));
        assert(close(MapCamera::wrap(before.second,4096),MapCamera::wrap(after.second,4096)));
        auto center=c.screenToWorld(480,360);c.resize(1200,800,4096,4096);auto resized=c.screenToWorld(600,400);
        assert(close(MapCamera::wrap(center.first,4096),MapCamera::wrap(resized.first,4096)));
        assert(close(MapCamera::wrap(center.second,4096),MapCamera::wrap(resized.second,4096)));
        c.wheel(100,200,300);assert(c.zoom==MapCamera::MAX_ZOOM);c.wheel(-100,200,300);assert(close(c.zoom,c.minimumZoom()));
    }
    MapCamera wide;wide.resize(960,720,8192,4096);wide.wheel(-100,100,100);
    assert(close(wide.zoom,720.0/4096));
    assert(close(wide.visibleH(),4096) && wide.visibleW()<8192);
    MapCamera tall;tall.resize(960,720,4096,8192);tall.wheel(-100,100,100);
    assert(close(tall.zoom,960.0/4096));
    assert(close(tall.visibleW(),4096) && tall.visibleH()<8192);
    MapCamera small;small.resize(960,720,512,512);small.setZoom(.25,100,100);
    assert(small.zoom==1 && small.visibleW()==960 && small.visibleH()==720);
    MapCamera resized;resized.resize(800,600,4096,4096);resized.setZoom(.2,400,300);
    const auto oldCenter=resized.screenToWorld(400,300);
    resized.resize(1200,900,4096,4096);
    assert(close(resized.zoom,1200.0/4096));
    const auto newCenter=resized.screenToWorld(600,450);
    assert(close(oldCenter.first,newCenter.first)&&close(oldCenter.second,newCenter.second));
    assert(small.offsetX==0&&small.offsetY==0&&small.contains(0,0));
    for(int x=0;x<960;x+=13)for(int y=0;y<720;y+=17)
    {
        auto w=small.screenToWorld(x,y);
        assert((small.tileX()+(small.localX(x)>>5))%16==int(MapCamera::wrap(w.first,512)/32));
        assert((small.tileY()+(small.localY(y)>>5))%16==int(MapCamera::wrap(w.second,512)/32));
    }
    std::cout<<"PASS camera conversions, placement/brush tiles, fractional wheel anchors, limits, resize and wrapped/small maps\n";
}
