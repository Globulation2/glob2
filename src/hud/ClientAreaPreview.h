// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Order.h"
#include "Brush.h"
#include "render/scene/Scene.h"
#include <algorithm>
#include <map>

// Main-thread-only speculative paint. The simulation's displayed areas remain
// authoritative; a pending layer survives until a PresentationFrame includes its execution.
class ClientAreaPreview
{
    struct Pending { std::shared_ptr<Order> order; Uint64 acknowledged = 0; };
    std::vector<Pending> pending;
    std::array<std::map<size_t,bool>,4> stroke;
    Uint64 world = 0, revision = 0, areaRevision = 0, configurationRevision = 0;
    Uint32 tick = 0;
    int team = -1;
    bool dirty = true;
    static int zone(Order& order)
    {
        switch(order.getOrderType()) {
        case ORDER_ALTER_FORBIDDEN:return 0;
        case ORDER_ALTER_GUARD_AREA:return 1;
        case ORDER_ALTER_CLEAR_AREA:return 2;
        case ORDER_ALTER_FARM_AREA:return 3;
        default:return -1;
        }
    }
    static bool same(const OrderAlterArea& a,const OrderAlterArea& b)
    {
        if (a.teamNumber!=b.teamNumber || a.type!=b.type || a.centerX!=b.centerX || a.centerY!=b.centerY ||
            a.minX!=b.minX || a.maxX!=b.maxX || a.minY!=b.minY || a.maxY!=b.maxY ||
            a.mask.getBitLength()!=b.mask.getBitLength()) return false;
        for(size_t i=0;i<a.mask.getBitLength();++i) if(a.mask.get(i)!=b.mask.get(i)) return false;
        return true;
    }
public:
    std::array<Utilities::BitArray,4> shown;
    void set(unsigned zone,size_t index,bool value)
    { shown[zone].set(index,value); stroke[zone][index]=value; }
    void track(const std::shared_ptr<Order>& order,bool finishesStroke=false)
    {
        const int z=zone(*order); if(z<0) return;
        const auto& paint=static_cast<const OrderAlterArea&>(*order);
        if(paint.teamNumber!=team) return;
        if(std::none_of(pending.begin(),pending.end(),[&](const auto& p){return p.order==order;}))
            pending.push_back({order});
        if(finishesStroke) stroke[z].clear();
        dirty=true;
    }
    void acknowledge(Order& order,Uint64 serial)
    {
        const int z=zone(order); if(z<0) return;
        for(auto& p:pending)
            if(!p.acknowledged && zone(*p.order)==z && same(static_cast<const OrderAlterArea&>(*p.order),static_cast<const OrderAlterArea&>(order)))
            { p.acknowledged=serial; dirty=true; break; }
    }
    void refresh(const PresentationFrame& scene,int viewedTeam)
    {
        if(world!=scene.map.identity() || team!=viewedTeam)
        {
            pending.clear(); for(auto& layer:stroke) layer.clear();
            world=scene.map.identity(); team=viewedTeam; dirty=true;
        }
        if(!dirty && tick==scene.tick && revision==scene.executedOrderRevision
            && areaRevision==scene.world.mapGenerations[3] && configurationRevision==scene.world.configurationRevision) return;
        areaRevision=scene.world.mapGenerations[3]; configurationRevision=scene.world.configurationRevision;
        tick=scene.tick; revision=scene.executedOrderRevision; dirty=false;
        for(unsigned z=0;z<4;++z) shown[z]=scene.map.displayedArea(z);
        std::erase_if(pending,[&](const auto& p){return p.acknowledged && p.acknowledged<=revision;});
        for(const auto& p:pending)
        {
            const int z=zone(*p.order);
            const auto& a=static_cast<const OrderAlterArea&>(*p.order);
            const bool value=a.type==BrushTool::MODE_ADD;
            size_t bit=0;
            for(int y=a.minY;y<a.maxY;++y) for(int x=a.minX;x<a.maxX;++x,++bit)
                if(a.mask.get(bit))
                {
                    const int px=a.centerX+x,py=a.centerY+y;
                    if(z==3 && value && !scene.map.canPaintFarmArea(px,py)) continue;
                    shown[z].set(scene.map.coordToIndex(px,py),value);
                }
        }
        for(unsigned z=0;z<4;++z) for(const auto& [index,value]:stroke[z]) shown[z].set(index,value);
    }
};
