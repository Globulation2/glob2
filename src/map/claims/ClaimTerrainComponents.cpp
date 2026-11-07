// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClaimTerrainComponents.h"
#include "Map.h"
#include "field/TerrainMovementCosts.h"
#include <stdexcept>

namespace ResourceClaims
{
std::shared_ptr<const TerrainComponentLabels> labelTerrainComponents(const Map& map,int movement)
{
    if(movement<0 || movement>=MOVEMENT_CLASS_COUNT)throw std::invalid_argument("unknown movement class");
    auto result=std::make_shared<TerrainComponentLabels>();
    const int w=map.getW(),h=map.getH();result->w=w;result->h=h;
    const auto cells=std::size_t(w)*std::size_t(h);
    auto& labels=result->labels;labels.assign(cells,TerrainComponentLabels::None);
    std::vector<Uint8> passable(cells);
    for(std::size_t i=0;i<cells;++i)
        passable[i]=gradient_kernel::terrainAllowsMovement(map.terrainPropertiesAt(i),movement);
    std::vector<Uint32> queue;queue.reserve(cells);
    Uint32 next=0;
    for(std::size_t seed=0;seed<cells;++seed) {
        if(!passable[seed] || labels[seed]!=TerrainComponentLabels::None)continue;
        queue.clear();queue.push_back(Uint32(seed));labels[seed]=next;
        for(std::size_t head=0;head<queue.size();++head) {
            const auto cell=queue[head];const int x=int(cell%Uint32(w)),y=int(cell/Uint32(w));
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy)continue;
                const auto n=Uint32(((y+dy+h)%h)*w+(x+dx+w)%w);
                if(passable[n] && labels[n]==TerrainComponentLabels::None){labels[n]=next;queue.push_back(n);}
            }
        }
        ++next;
    }
    return result;
}

bool terrainComponentMayReach(const TerrainComponentLabels& c,int unitX,int unitY,Uint32 access)
{
    const int w=c.w,h=c.h;
    if(!w || !h || access>=c.labels.size())return true;
    const int ux=((unitX%w)+w)%w,uy=((unitY%h)+h)%h;
    const auto own=c.at(Uint32(uy*w+ux));
    // A unit off its own passable terrain gives no evidence; never filter it.
    if(own==TerrainComponentLabels::None)return true;
    const int ax=int(access%Uint32(w)),ay=int(access/Uint32(w));
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if(c.at(Uint32(((ay+dy+h)%h)*w+(ax+dx+w)%w))==own)return true;
    return false;
}

std::shared_ptr<const TerrainComponentLabels> TerrainComponentCache::labels(const Map& map,int movement)
{
    if(movement<0 || movement>=MOVEMENT_CLASS_COUNT)throw std::invalid_argument("unknown movement class");
    // Both revisions: the published terrain generation and the per-cell change
    // tracker, which moves immediately inside an unfinished terrain batch.
    const std::array<Uint64,2> revision{map.terrainGeneration(),map.changes(MapState::TrackedArray::Terrain).generation};
    std::lock_guard lock(mutex);
    auto& plane=planes[unsigned(movement)];
    if(!plane.labels || plane.revision!=revision || plane.labels->w!=map.getW() || plane.labels->h!=map.getH()) {
        plane.labels=labelTerrainComponents(map,movement);plane.revision=revision;++rebuildCount;
    }
    return plane.labels;
}
}
