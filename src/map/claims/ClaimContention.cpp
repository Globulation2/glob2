// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClaimContention.h"
#include "Map.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include <algorithm>
#include <cstdlib>

namespace ResourceClaims
{
namespace
{
Uint64 key(int material,Uint32 cell) { return (Uint64(Uint32(material))<<32)|cell; }
}
std::optional<Uint32> ContentionIndex::targetCell(const Map& map,const Unit& u)
{
    if(u.isDead || u.activity!=Unit::ACT_FILLING || u.medical!=Unit::MED_FREE || !u.attachedBuilding
        || !validMaterial(u.destinationPurpose))return std::nullopt;
    if(u.displacement!=Unit::DIS_GOING_TO_RESOURCE && u.displacement!=Unit::DIS_HARVESTING)return std::nullopt;
    if(u.activeMaterialClaim)return u.activeMaterialClaim->access<map.cellCount()?std::optional<Uint32>(u.activeMaterialClaim->access):std::nullopt;
    if(u.displacement==Unit::DIS_HARVESTING || u.validTarget())return Uint32(map.coordToIndex(u.targetX,u.targetY));
    return std::nullopt;
}
void ContentionIndex::rebuild(const Map& map,int team,Uint64 tick)
{
    auto& snapshot=teams[team];
    snapshot.tick=tick;snapshot.keys.clear();snapshot.selves.clear();
    if(!map.game || team>=map.game->teamsCount() || !map.game->teams[team])return;
    for(const Unit* unit:map.game->teams[team]->liveUnits)
        if(const auto cell=targetCell(map,*unit)) {
            const auto k=key(unit->destinationPurpose,*cell);
            snapshot.keys.push_back(k);snapshot.selves.emplace_back(unit->gid,k);
        }
    std::sort(snapshot.keys.begin(),snapshot.keys.end());
    std::sort(snapshot.selves.begin(),snapshot.selves.end());
}
unsigned ContentionIndex::others(const Map& map,const Unit& self,int material,int x,int y,unsigned radius) const
{
    const auto& snapshot=teams[self.owner->teamNumber];
    const auto& keys=snapshot.keys;
    if(keys.empty())return 0;
    const int w=map.getW(),h=map.getH(),r=int(radius);
    std::size_t count=0;
    const auto range=[&](int row,int x0,int x1) { // Inclusive, x0<=x1, one row.
        const auto lo=std::lower_bound(keys.begin(),keys.end(),key(material,Uint32(map.coordToIndex(x0,row))));
        const auto hi=std::upper_bound(lo,keys.end(),key(material,Uint32(map.coordToIndex(x1,row))));
        count+=std::size_t(hi-lo);
    };
    const auto row=[&](int yy) {
        if(2*r+1>=w){range(yy,0,w-1);return;}
        const int x0=map.normalizeX(x-r),x1=map.normalizeX(x+r);
        if(x0<=x1)range(yy,x0,x1);else{range(yy,0,x1);range(yy,x0,w-1);}
    };
    if(2*r+1>=h)for(int yy=0;yy<h;++yy)row(yy);
    else for(int dy=-r;dy<=r;++dy)row(map.normalizeY(y+dy));
    // The querying worker is excluded only where the snapshot counted it.
    const auto found=std::lower_bound(snapshot.selves.begin(),snapshot.selves.end(),std::pair<Uint16,Uint64>{self.gid,0});
    if(found!=snapshot.selves.end() && found->first==self.gid && Uint32(found->second>>32)==Uint32(material)) {
        const auto cell=Uint32(found->second);
        const auto axis=[](int a,int b,int extent){const int d=std::abs(a-b)%extent;return std::min(d,extent-d);};
        const int cx=int(cell%Uint32(w)),cy=int(cell/Uint32(w));
        if(std::max(axis(map.normalizeX(x),cx,w),axis(map.normalizeY(y),cy,h))<=r && count)--count;
    }
    return unsigned(count);
}
}
