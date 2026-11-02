// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceClaimOracle.h"
#include <algorithm>
#include <bit>
#include <span>
#include <stdexcept>

namespace ResourceClaims::Experiment
{
enum class PatternSelection { FirstAvailable, ClosestGeometric };
struct PatternView
{
    // SoA layout emitted by tools/resource_claim_patterns.py.
    std::span<const Sint16> x, y;
    std::span<const Uint8> origin; // 0 unit, 1 closest resource, 2 wrapped midpoint.
};
struct PatternOutcome
{
    Uint32 source = Oracle::NoTile, access = Oracle::NoTile, probe = Oracle::NoTile;
    Uint64 distance = Oracle::Unreachable;
    Uint64 accessDistance = Oracle::Unreachable;
    Uint32 probes = 0, destinationReads = 0;
    bool fallback = true;
};

// One evaluator per sampled decision. It caches metadata reads and claim-source
// resolution across all patterns. Oracle distances LABEL decisions; they never
// choose a policy's candidate or remove an unreachable candidate from its search.
class PatternEvaluator
{
    const Oracle::Result& oracle_;
    int width_, height_;
    Uint32 unit_, closest_;
    std::vector<Uint32> sourceChoices_, sourceRows_;
    std::vector<Uint32> probeSources_;
    std::vector<bool> probeRead_;

    static int delta(int a, int b, int dimension)
    { return ((b-a+dimension/2)&(dimension-1))-dimension/2; }
    Uint32 offset(Uint32 origin, int dx, int dy) const
    {
        return Uint32(((int(origin/width_)+dy)&(height_-1))*width_ +
                      ((int(origin%width_)+dx)&(width_-1)));
    }
    unsigned geometric(Uint32 access) const
    {
        const auto dx=std::max(0,std::abs(delta(int(unit_%width_),int(access%width_),width_))-1);
        const auto dy=std::max(0,std::abs(delta(int(unit_/width_),int(access/width_),height_))-1);
        return unsigned(10*std::max(dx,dy)+4*std::min(dx,dy));
    }
public:
    // Integer octants: cardinal when one component is at least twice the
    // other, diagonal otherwise. Arrays are generated in all eight headings;
    // no runtime coordinate rotation, rounding, or duplicate probes are added.
    unsigned closestOctant() const
    {
        if (closest_==Oracle::NoTile) return 0;
        const int dx=delta(int(unit_%width_),int(closest_%width_),width_);
        const int dy=delta(int(unit_/width_),int(closest_/width_),height_);
        if (std::abs(dx)>=2*std::abs(dy)) return dx>=0?0:4;
        if (std::abs(dy)>=2*std::abs(dx)) return dy>=0?2:6;
        return dx>=0?(dy>=0?1:7):(dy>=0?3:5);
    }
    PatternEvaluator(const Oracle::Result& oracle, int width, int height, Uint32 unit, Uint32 closest)
        : oracle_(oracle), width_(width), height_(height), unit_(unit), closest_(closest)
    {
        if (width<=0 || height<=0 || !std::has_single_bit(unsigned(width)) || !std::has_single_bit(unsigned(height)))
            throw std::invalid_argument("invalid pattern evaluator dimensions");
        const auto size=std::size_t(width)*height;
        if (unit>=size || (closest!=Oracle::NoTile && closest>=size) || size>Oracle::NoTile)
            throw std::invalid_argument("invalid pattern evaluator origin");
        sourceChoices_.assign(size,Oracle::NoTile);
        sourceRows_.assign(size,Oracle::NoTile);
        probeSources_.assign(size,Oracle::NoTile); probeRead_.assign(size,false);
        std::vector<Uint32> components;
        for (std::size_t row=0;row<oracle_.candidates.size();++row) {
            const auto& candidate=oracle_.candidates[row];
            if (candidate.source>=size || (candidate.component!=Oracle::NoTile && candidate.component>=size))
                throw std::invalid_argument("invalid pattern candidate index");
            sourceRows_[candidate.source]=Uint32(row);
            if (!candidate.available || candidate.component==Oracle::NoTile) continue;
            if (components.size()<=candidate.component) components.resize(candidate.component+1,Oracle::NoTile);
            auto& selected=components[candidate.component];
            if (selected==Oracle::NoTile || candidate.source<oracle_.candidates[selected].source) selected=Uint32(row);
        }
        for (std::size_t row=0;row<oracle_.candidates.size();++row) {
            const auto& candidate=oracle_.candidates[row];
            if (!candidate.accessEligible) continue;
            sourceChoices_[candidate.source]=candidate.component!=Oracle::NoTile ?
                (candidate.component<components.size()?components[candidate.component]:Oracle::NoTile) :
                candidate.available?Uint32(row):Oracle::NoTile;
        }
    }

    template<class DestinationAt>
    PatternOutcome evaluate(PatternView pattern, PatternSelection selection, DestinationAt&& destinationAt)
    {
        if (pattern.x.size()!=pattern.y.size() || pattern.x.size()!=pattern.origin.size() ||
            pattern.x.size()>std::numeric_limits<Uint32>::max())
            throw std::invalid_argument("invalid pattern planes");
        PatternOutcome outcome;
        unsigned best=std::numeric_limits<unsigned>::max();
        const auto midpoint=closest_==Oracle::NoTile?Oracle::NoTile:offset(unit_,
            delta(int(unit_%width_),int(closest_%width_),width_)/2,
            delta(int(unit_/width_),int(closest_/width_),height_)/2);
        for (std::size_t i=0;i<pattern.x.size();++i) {
            if (pattern.origin[i]>2) throw std::invalid_argument("invalid pattern origin tag");
            const auto origin=pattern.origin[i]==0?unit_:pattern.origin[i]==1?closest_:midpoint;
            if (origin==Oracle::NoTile) continue;
            const auto probe=offset(origin,pattern.x[i],pattern.y[i]);
            ++outcome.probes;
            if (!probeRead_[probe]) {
                probeSources_[probe]=destinationAt(probe);
                probeRead_[probe]=true;
                ++outcome.destinationReads;
            }
            const auto access=probeSources_[probe];
            if (access==Oracle::NoTile) continue;
            if (access>=sourceChoices_.size()) throw std::invalid_argument("invalid destination metadata index");
            const auto row=sourceChoices_[access];
            if (row==Oracle::NoTile) continue;
            const auto score=geometric(access);
            if (!outcome.fallback && score>=best) continue;
            best=score; outcome.fallback=false;
            outcome.source=oracle_.candidates[row].source;
            outcome.distance=oracle_.candidates[row].distance;
            outcome.accessDistance=oracle_.candidates[sourceRows_[access]].accessDistance;
            outcome.access=access; outcome.probe=probe;
            if (selection==PatternSelection::FirstAvailable) break;
        }
        return outcome;
    }
};
}
