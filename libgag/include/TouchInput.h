// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <cstdint>
#include <map>
#include <vector>

namespace GAGCore
{
// ZoomDrag is the second contact of a double-tap: vertical travel zooms about
// the contact, and a release without travel requests the 1:1 reset. Travel
// that starts mostly sideways pans instead. The caller owns the tap timing
// window and chooses this mode on finger-down.
enum class TouchMode { Navigate, Placement, Paint, ZoomDrag };
// Pan carries a delta; PanEnd follows the last Pan of a gesture (finger lifted or a
// second finger changed the gesture) so consumers can release momentum.
enum class TouchActionKind { Select, Pan, PanEnd, Zoom, Preview, BeginStroke, Stroke, EndStroke, Cancel, ZoomReset };
// `time` is the SDL event timestamp in milliseconds when the caller supplies one.
struct TouchAction { TouchActionKind kind; ViewPoint point; double factor=1; std::uint64_t time=0; };
class TouchInput
{
    using Key=std::pair<std::int64_t,std::int64_t>;
    struct Finger { ViewPoint start, point; };
    std::map<Key,Finger> fingers;
    TouchMode mode=TouchMode::Navigate;
    bool dragging=false, suppress=false, painting=false, dragUpZoomsIn=true;
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
    //! Points of vertical travel that double (or halve) the zoom.
    static constexpr double zoomDoublingPoints=180;
    //! Travel below this is a tap rather than a drag.
    static constexpr double slop=8;
    bool hasPointers() const { return !fingers.empty(); }
    void setZoomDragDirection(bool upZoomsIn) { dragUpZoomsIn=upZoomsIn; }
    //! True while a single contact is actively zooming (for feedback).
    bool zoomDragging() const { return mode==TouchMode::ZoomDrag && dragging && !suppress && fingers.size()==1; }
    std::vector<TouchAction> cancel()
    {
        fingers.clear();dragging=false;suppress=false;painting=false;
        return {{TouchActionKind::Cancel,{}}};
    }
    std::vector<TouchAction> setMode(TouchMode selected) { auto actions=cancel();mode=selected;return actions; }
    std::vector<TouchAction> down(std::int64_t device,std::int64_t finger,ViewPoint point,std::uint64_t time=0)
    {
        if(fingers.contains({device,finger})) return {};
        fingers[{device,finger}]={point,point};
        if(fingers.size()>2) { suppress=true;painting=false;return {{TouchActionKind::Cancel,{},1,time}}; }
        if(suppress) return {};
        if(fingers.size()==2) {
            dragging=true;
            if(painting) {painting=false;return {{TouchActionKind::Cancel,point,1,time}};}
            return {};
        }
        dragging=false;
        if(mode==TouchMode::Placement) return {{TouchActionKind::Preview,point,1,time}};
        if(mode==TouchMode::Paint) {painting=true;return {{TouchActionKind::BeginStroke,point,1,time}};}
        return {};
    }
    std::vector<TouchAction> move(std::int64_t device,std::int64_t finger,ViewPoint point,std::uint64_t time=0)
    {
        auto it=fingers.find({device,finger});
        if(it==fingers.end() || suppress) return {};
        auto previous=it->second.point;
        if(fingers.size()==2) {
            auto middle=midpoint();double span=separation();it->second.point=point;
            auto next=midpoint();double nextSpan=separation();
            std::vector<TouchAction> out{{TouchActionKind::Pan,{next.x-middle.x,next.y-middle.y},1,time}};
            if(span>=1 && nextSpan>=1) out.push_back({TouchActionKind::Zoom,next,nextSpan/span,time});
            return out;
        }
        it->second.point=point;
        if(mode==TouchMode::ZoomDrag) {
            if(!dragging) {
                if(distance(point,it->second.start)<slop) return {};
                dragging=true;previous=it->second.start;
                // Zoom travels vertically. A contact that sets off sideways is a
                // pan that merely began soon after a tap, so it stays a pan.
                if(std::abs(point.x-previous.x)>std::abs(point.y-previous.y)) {
                    mode=TouchMode::Navigate;
                    return {{TouchActionKind::Pan,{point.x-previous.x,point.y-previous.y},1,time}};
                }
            }
            const double rise=previous.y-point.y;
            return {{TouchActionKind::Zoom,it->second.start,std::pow(2.0,(dragUpZoomsIn?rise:-rise)/zoomDoublingPoints),time}};
        }
        if(mode==TouchMode::Placement) return {{TouchActionKind::Preview,point,1,time}};
        if(mode==TouchMode::Paint) return {{TouchActionKind::Stroke,point,1,time}};
        if(!dragging) {
            if(distance(point,it->second.start)<slop) return {};
            dragging=true;previous=it->second.start;
        }
        return {{TouchActionKind::Pan,{point.x-previous.x,point.y-previous.y},1,time}};
    }
    std::vector<TouchAction> up(std::int64_t device,std::int64_t finger,ViewPoint point,std::uint64_t time=0)
    {
        auto it=fingers.find({device,finger});
        if(it==fingers.end()) return {};
        std::vector<TouchAction> out;
        if(!suppress && fingers.size()==1) {
            if(painting) out.push_back({TouchActionKind::EndStroke,point,1,time});
            else if(mode==TouchMode::ZoomDrag && !dragging && distance(point,it->second.start)<slop)
                out.push_back({TouchActionKind::ZoomReset,it->second.start,1,time});
            else if(mode==TouchMode::Navigate && !dragging && distance(point,it->second.start)<slop)
                out.push_back({TouchActionKind::Select,point,1,time});
        }
        if(!suppress && dragging && !painting && mode!=TouchMode::ZoomDrag) out.push_back({TouchActionKind::PanEnd,point,1,time});
        fingers.erase(it);painting=false;
        if(fingers.empty()) {suppress=false;dragging=false;} else suppress=true;
        return out;
    }
};
}
