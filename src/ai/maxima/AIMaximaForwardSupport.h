// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIMaximaPlacement.h"
#include "field/Grid.h"
#include <algorithm>
#include <cstdlib>
#include <climits>

namespace AIMaxima::ForwardSupport
{
// Additional constraints for the dedicated forward-support purpose. Ordinary
// feeding/healing intents never call this policy. Engine legality, protected
// food capacity, builder access and construction reservations still apply.
struct Front
{
    int x=-1,y=-1;
    int minimumDistance=8,maximumDistance=32,preferredDistance=20;
    int maximumThreat=0;
};
inline Front front(const AIMaximaPlacement::DevelopmentIntent& intent)
{
    return {intent.supportX,intent.supportY,std::max(8,intent.supportDistance-8),
        intent.supportDistance+8,intent.supportDistance,0};
}
inline int distance(const AIMaximaPlacement::WorldState& world,int ax,int ay,int bx,int by)
{
    const int dx=std::abs(world.normalizeX(ax)-world.normalizeX(bx));
    const int dy=std::abs(world.normalizeY(ay)-world.normalizeY(by));
    return std::max(std::min(dx,world.width-dx),std::min(dy,world.height-dy));
}
inline bool accepts(const AIMaximaPlacement::WorldState& world,const Front& front,int x,int y)
{
    if(front.x<0 || front.y<0 || front.minimumDistance<0
        || front.maximumDistance<front.minimumDistance) return false;
    const int d=distance(world,x,y,front.x,front.y);
    return d>=front.minimumDistance && d<=front.maximumDistance
        && world.tile(x,y).threat<=front.maximumThreat;
}
inline int locationQuality(const AIMaximaPlacement::WorldState& world,const Front& front,int x,int y)
{
    const int d=distance(world,x,y,front.x,front.y);
    return std::max(0,100-std::abs(d-front.preferredDistance)*100
        /std::max(1,front.maximumDistance-front.minimumDistance));
}

// Diagnostic route estimates only; these never authorize or rank a site.
// All inputs are the AI's discovered grid, owned
// buildings and common movement capability of its living warriors.
struct Routes
{
    struct Site
    {
        int objective=INT_MAX,support=INT_MAX,detour=INT_MAX,quality=0;
        int approach=-1;
        bool accepted=false;
    };
    int supportBuilding=-1,direct=INT_MAX,width=0;
    std::vector<int> objectiveDistance,supportDistance;

    Routes(const AIMaximaPlacement::WorldState& world,
        const AIMaximaPlacement::DevelopmentIntent& intent,bool canSwim=false)
        : width(world.width)
    {
        using namespace AIMaximaPlacement;
        if(world.width<=0 || world.height<=0 || intent.supportX<0 || intent.supportY<0)return;
        const auto passable=[&](int index,bool safe) {
            const auto& tile=world.tiles[index];
            return tile.discovered && tile.foodTraversable && !tile.occupied
                && !tile.resourceBlocksGround && tile.canTravel(canSwim)
                && (!safe || tile.threat==0);
        };
        const auto flood=[&](const std::vector<int>& sources,bool safe) {
            std::vector<int> result(world.tiles.size(),INT_MAX),queue;
            queue.reserve(world.tiles.size());
            for(int index:sources)if(passable(index,safe) && result[index]==INT_MAX)
            {result[index]=0;queue.push_back(index);}
            for(size_t cursor=0;cursor<queue.size();++cursor)
            {
                const int index=queue[cursor],x=index%world.width,y=index/world.width;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                {
                    if(dx==0 && dy==0)continue;
                    const int next=world.index(x+dx,y+dy);
                    if(result[next]!=INT_MAX || !passable(next,safe))continue;
					// Distances lead toward the source apron: inspect the
					// predecessor's forward edge, not the settled cell's edge.
					if (!(world.tiles[next].travelConnections(canSwim) &
						  (1u << field::directionIndex(-dx, -dy))))
						continue;
					if (dx && dy &&
						(!passable(world.index(x + dx, y), false) ||
						 !passable(world.index(x, y + dy), false)))
						continue;
					result[next] = result[index] + 1;
					queue.push_back(next);
				}
            }
            return result;
        };
        // Goal apron, rather than the occupied building center. This is a
        // route-to-contact proxy; it does not assert that combat will occur.
        std::vector<int> goals;
        for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)
            goals.push_back(world.index(intent.supportX+dx,intent.supportY+dy));
        objectiveDistance=flood(goals,false);
        std::vector<int> supportSources;
        int sourcePriority=2;
        for(const auto& building:world.buildings)
        {
            if(building.site || building.upgrading)continue;
            const auto* profile=world.profile(building.buildingType);
            const auto* variant=profile?profile->atLevel(building.level):nullptr;
            const unsigned roles=variant?variant->roles:0;
            if(!(roles & (AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding)
                | AIMaximaBuildings::roleBit(AIMaximaBuildings::Production))))continue;
            // A closer swarm is not necessarily a food source. Prefer a
            // reachable feeding base; production is the no-feeder fallback.
            const int priority=(roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding))?0:1;
            const auto& shape=variant->footprint;
            std::vector<int> apron;
            int nearest=INT_MAX;
            for(int dy=shape.top-1;dy<=shape.top+shape.height;++dy)
                for(int dx=shape.left-1;dx<=shape.left+shape.width;++dx)
                {
                    if(dx>=shape.left && dx<shape.left+shape.width
                        && dy>=shape.top && dy<shape.top+shape.height)continue;
                    const int index=world.index(building.centerX+dx,building.centerY+dy);
                    if(!passable(index,true))continue;
                    apron.push_back(index);nearest=std::min(nearest,objectiveDistance[index]);
                }
            if(nearest!=INT_MAX && (priority<sourcePriority || (priority==sourcePriority
                && (nearest<direct || (nearest==direct && building.id<supportBuilding)))))
            {direct=nearest;supportBuilding=building.id;supportSources=std::move(apron);sourcePriority=priority;}
        }
        supportDistance=flood(supportSources,true);
    }
    Site site(const Front& front,const std::vector<int>& apron) const
    {
        Site best;
        if(direct==INT_MAX || supportBuilding<0)return best;
        for(int index:apron)
        {
            if(index<0 || size_t(index)>=objectiveDistance.size())continue;
            const int objective=objectiveDistance[index],support=supportDistance[index];
            if(objective==INT_MAX || support==INT_MAX)continue;
            const int64_t detour=int64_t(objective)+support-direct;
            const bool accepted=support<direct && objective<direct && objective>=front.minimumDistance
                && objective<=front.maximumDistance && detour<=std::max(8,direct/4);
            const int64_t penalty=int64_t(std::abs(objective-front.preferredDistance))*100
                /std::max(1,front.maximumDistance-front.minimumDistance)+std::max<int64_t>(0,detour)*2;
            const int quality=int(std::max<int64_t>(0,100-penalty));
            if(best.approach<0 || (accepted && !best.accepted)
                || (accepted==best.accepted && (quality>best.quality
                    || (quality==best.quality && index<best.approach))))
                best={objective,support,int(detour),quality,index,accepted};
        }
        return best;
    }
};
}
