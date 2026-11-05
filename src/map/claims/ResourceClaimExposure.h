// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Temporary diagnostic proximity proxy. Occupancy supplies neither attack
// capability nor weapon range, damage, line of fire, or future enemy movement.
#include "MapState.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <span>
#include <stdexcept>
#include <vector>

namespace ResourceClaims::Experiment
{
class ExposureProxy
{
public:
    static constexpr unsigned Radius=16, BeyondRadius=Radius+1;
    static constexpr std::array<unsigned,3> Bands{1,4,8};
    using Point=std::array<std::uint8_t,4>; // units/all, buildings/all, units/visible, buildings/visible
    struct Path
    {
        std::uint32_t endpoint=UINT32_MAX,steps=0;
        std::uint64_t cost=0;
        Point minimum{BeyondRadius,BeyondRadius,BeyondRadius,BeyondRadius};
        std::array<std::array<std::uint32_t,3>,4> nearSteps{};
        std::array<std::array<std::uint64_t,3>,4> nearCost{};
        void append(const Path& other)
        {
            if(other.endpoint!=UINT32_MAX)endpoint=other.endpoint;
            steps+=other.steps;cost+=other.cost;
            for(unsigned p=0;p<4;++p) {
                minimum[p]=std::min(minimum[p],other.minimum[p]);
                for(unsigned b=0;b<3;++b){nearSteps[p][b]+=other.nearSteps[p][b];nearCost[p][b]+=other.nearCost[p][b];}
            }
        }
    };
private:
    std::array<std::vector<std::uint8_t>,4> planes;
    std::array<std::uint32_t,4> seeds{};
public:
    ExposureProxy(unsigned width,unsigned height,std::span<const MapState::OccupancyCell> occupancy,
        std::span<const std::uint32_t> visible,std::uint32_t incomingTeams,std::uint32_t observerMask,
        unsigned unitStride,unsigned buildingStride)
    {
        const std::uint64_t cells=std::uint64_t(width)*height;
        if(!width || !height || width>INT_MAX || height>INT_MAX || cells>UINT32_MAX || occupancy.size()!=cells || visible.size()!=cells || !unitStride || !buildingStride)
            throw std::invalid_argument("invalid claim exposure snapshot shape");
        if(!incomingTeams)return;
        for(auto& plane:planes)plane.assign(cells,BeyondRadius);
        const auto hostile=[&](unsigned gid,unsigned stride) {
            if(gid==0xffff)return false;
            const unsigned team=gid/stride;
            return team<32 && (incomingTeams&(std::uint32_t(1)<<team));
        };
        for(std::size_t i=0;i<cells;++i) {
            const auto& cell=occupancy[i];
            const bool unit=hostile(cell.groundUnit,unitStride)||hostile(cell.airUnit,unitStride);
            const bool building=hostile(cell.building,buildingStride);
            const bool seen=visible[i]&observerMask;
            for(unsigned p=0;p<4;++p)if((p%2?building:unit) && (p<2 || seen)) {planes[p][i]=0;++seeds[p];}
        }
        std::vector<std::uint32_t> queue;queue.reserve(cells);
        for(unsigned p=0;p<planes.size();++p) {
            if(!seeds[p])continue;
            auto& plane=planes[p];
            queue.clear();for(std::uint32_t i=0;i<cells;++i)if(!plane[i])queue.push_back(i);
            for(std::size_t head=0;head<queue.size();++head) {
                const auto cell=queue[head];if(plane[cell]>=Radius)continue;
                const int x=cell%width,y=cell/width;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy) {
                    const auto nx=unsigned((x+dx+int(width))%int(width)),ny=unsigned((y+dy+int(height))%int(height));
                    const auto next=ny*width+nx;
                    if(plane[next]!=BeyondRadius)continue;
                    plane[next]=plane[cell]+1;queue.push_back(next);
                }
            }
        }
    }
    Point at(std::uint32_t cell) const
    {if(planes[0].empty())return {BeyondRadius,BeyondRadius,BeyondRadius,BeyondRadius};
        return {planes[0].at(cell),planes[1].at(cell),planes[2].at(cell),planes[3].at(cell)};}
    const auto& seedCells() const {return seeds;}
    void enter(Path& path,std::uint32_t cell,std::uint32_t cost) const
    {
        const auto distances=at(cell);path.endpoint=cell;++path.steps;path.cost+=cost;
        for(unsigned p=0;p<4;++p) {
            path.minimum[p]=std::min(path.minimum[p],distances[p]);
            for(unsigned b=0;b<Bands.size();++b)if(distances[p]<=Bands[b]){++path.nearSteps[p][b];path.nearCost[p][b]+=cost;}
        }
    }
};
}
