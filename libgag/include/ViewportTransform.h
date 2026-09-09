// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace GAGCore
{
struct ViewPoint { double x=0, y=0; };
struct ViewRect {
    double x=0, y=0, w=0, h=0;
    bool contains(ViewPoint point) const { return point.x>=x && point.y>=y && point.x<x+w && point.y<y+h; }
};
class ViewportTransform
{
    ViewRect viewport;
    ViewPoint center, world;
    double scale=1;
    static double wrap(double value,double extent) { return value-std::floor(value/extent)*extent; }
    void normalize() { center={wrap(center.x,world.x),wrap(center.y,world.y)}; }
public:
    ViewportTransform(ViewRect viewport, ViewPoint world) : viewport(viewport), world(world)
    {
        if (!(world.x>0 && world.y>0) || !std::isfinite(world.x) || !std::isfinite(world.y))
            throw std::invalid_argument("Invalid world dimensions");
    }
    void resize(ViewRect rect) { viewport=rect; }
    ViewPoint screenToWorld(ViewPoint point) const
    {
        return {wrap(center.x+(point.x-viewport.x-viewport.w/2)/scale,world.x),
                wrap(center.y+(point.y-viewport.y-viewport.h/2)/scale,world.y)};
    }
    // Select the nearest toroidal copy. Rendering repeats tiles when a viewport
    // is wider than the world; picking always resolves to canonical coordinates.
    ViewPoint worldToScreen(ViewPoint point) const
    {
        double dx=wrap(point.x-center.x+world.x/2,world.x)-world.x/2;
        double dy=wrap(point.y-center.y+world.y/2,world.y)-world.y/2;
        return {viewport.x+viewport.w/2+dx*scale,viewport.y+viewport.h/2+dy*scale};
    }
    void pan(ViewPoint delta) { center.x-=delta.x/scale; center.y-=delta.y/scale; normalize(); }
    void zoom(double requested, ViewPoint anchor)
    {
        if (!std::isfinite(requested) || requested<=0) return;
        const auto before=screenToWorld(anchor);
        scale=std::clamp(requested,0.5,3.0);
        center={before.x-(anchor.x-viewport.x-viewport.w/2)/scale,
                before.y-(anchor.y-viewport.y-viewport.h/2)/scale};
        normalize();
    }
    void moveTo(ViewPoint point) { center=point; normalize(); }
    double zoom() const { return scale; }
    ViewPoint position() const { return center; }
    ViewRect bounds() const { return viewport; }
};
struct SafeInsets { double left=0, top=0, right=0, bottom=0; };
struct MobileLayout {
    ViewRect safe, status, world, actions, panel;
    bool persistentPanel=false;
    static MobileLayout calculate(double width,double height,SafeInsets inset={},double keyboardHeight=0,
                                  double uiScale=1,bool panelOpen=false)
    {
        MobileLayout out;
        double scale=std::clamp(uiScale,1.0,2.0), control=48*scale;
        out.safe={inset.left,inset.top,std::max(0.0,width-inset.left-inset.right),
            std::max(0.0,height-inset.top-std::max(inset.bottom,keyboardHeight))};
        auto area=out.safe;
        double statusHeight=std::min(control,area.h);
        out.status={area.x,area.y,area.w,statusHeight};
        area.y+=statusHeight;area.h-=statusHeight;
        double actionsHeight=std::min(control,area.h);
        out.actions={area.x,area.y+area.h-actionsHeight,area.w,actionsHeight};
        area.h-=actionsHeight;
        double panelWidth=216*scale;
        out.persistentPanel=area.w-panelWidth>=480;
        out.world=area;
        if(out.persistentPanel) {
            out.world.w-=panelWidth;
            out.panel={area.x+area.w-panelWidth,area.y,panelWidth,area.h};
        } else if(panelOpen && width>height) {
            double w=std::min(panelWidth,area.w);
            out.panel={area.x+area.w-w,area.y,w,area.h};
        } else if(panelOpen) {
            double h=std::min(288*scale,area.h*0.6);
            out.panel={area.x,area.y+area.h-h,area.w,h};
        }
        return out;
    }
};
}
