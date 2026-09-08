// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <cstdint>
#include <map>
#include <vector>

namespace GAGCore
{
enum class TouchMode { Navigate, Placement, Paint };
enum class TouchActionKind { Select, Pan, Zoom, Preview, BeginStroke, Stroke, EndStroke, Cancel };
struct TouchAction { TouchActionKind kind; ViewPoint point; double factor=1; };
class TouchInput
{
    using Key=std::pair<std::int64_t,std::int64_t>;
    struct Finger { ViewPoint start, point; };
    std::map<Key,Finger> fingers;
    TouchMode mode=TouchMode::Navigate;
    bool dragging=false, suppress=false, painting=false;
    static double distance(ViewPoint a,ViewPoint b) { return std::hypot(a.x-b.x,a.y-b.y); }
    ViewPoint midpoint() const
    {
        auto it=fingers.begin();const auto a=it++->second.point,b=it->second.point;
        return {(a.x+b.x)/2,(a.y+b.y)/2};
    }
    double separation() const
    {
        auto it=fingers.begin();const auto a=it++->second.point,b=it->second.point;
        return distance(a,b);
    }
public:
    std::vector<TouchAction> cancel()
    {
        fingers.clear();dragging=false;suppress=false;painting=false;
        return {{TouchActionKind::Cancel,{}}};
    }
    std::vector<TouchAction> setMode(TouchMode selected) { auto actions=cancel();mode=selected;return actions; }
    std::vector<TouchAction> down(std::int64_t device,std::int64_t finger,ViewPoint point)
    {
        if(fingers.contains({device,finger})) return {};
        fingers[{device,finger}]={point,point};
        if(fingers.size()>2) { suppress=true;painting=false;return {{TouchActionKind::Cancel,{}}}; }
        if(suppress) return {};
        if(fingers.size()==2) {
            dragging=true;
            if(painting) {painting=false;return {{TouchActionKind::EndStroke,point}};}
            return {};
        }
        dragging=false;
        if(mode==TouchMode::Placement) return {{TouchActionKind::Preview,point}};
        if(mode==TouchMode::Paint) {painting=true;return {{TouchActionKind::BeginStroke,point}};}
        return {};
    }
    std::vector<TouchAction> move(std::int64_t device,std::int64_t finger,ViewPoint point)
    {
        auto it=fingers.find({device,finger});
        if(it==fingers.end() || suppress) return {};
        auto previous=it->second.point;
        if(fingers.size()==2) {
            auto middle=midpoint();double span=separation();it->second.point=point;
            auto next=midpoint();double nextSpan=separation();
            std::vector<TouchAction> out{{TouchActionKind::Pan,{next.x-middle.x,next.y-middle.y}}};
            if(span>=1 && nextSpan>=1) out.push_back({TouchActionKind::Zoom,next,nextSpan/span});
            return out;
        }
        it->second.point=point;
        if(mode==TouchMode::Placement) return {{TouchActionKind::Preview,point}};
        if(mode==TouchMode::Paint) return {{TouchActionKind::Stroke,point}};
        if(!dragging) {
            if(distance(point,it->second.start)<8) return {};
            dragging=true;previous=it->second.start;
        }
        return {{TouchActionKind::Pan,{point.x-previous.x,point.y-previous.y}}};
    }
    std::vector<TouchAction> up(std::int64_t device,std::int64_t finger,ViewPoint point)
    {
        auto it=fingers.find({device,finger});
        if(it==fingers.end()) return {};
        std::vector<TouchAction> out;
        if(!suppress && fingers.size()==1) {
            if(painting) out.push_back({TouchActionKind::EndStroke,point});
            else if(mode==TouchMode::Navigate && !dragging && distance(point,it->second.start)<8)
                out.push_back({TouchActionKind::Select,point});
        }
        fingers.erase(it);painting=false;
        if(fingers.empty()) {suppress=false;dragging=false;} else suppress=true;
        return out;
    }
};
}
