// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gradient/FieldReadiness.h"
#include "sim/EntityRef.h"
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>
namespace SimulationSnapshot { struct Handle; }
struct GradientWorkspace;
namespace air_guidance {
enum class Family : std::uint8_t { Building, Material, Guard, ClearZone, ClearChoice };
struct Key {
    Family family=Family::Building;
    int team=0,material=0;
    unsigned modes=0;
    bool withMarkets=false;
    BuildingRef building,consumer;
    UnitRef unit;
    int x=0,y=0,range=0;
    std::uint32_t materialMask=0;
    int sourceX=0,sourceY=0;
    std::uint32_t epoch=0;
    auto order() const { return std::tie(family,team,material,modes,withMarkets,building.gid,building.generation,consumer.gid,consumer.generation,unit.gid,unit.generation,x,y,range,materialMask,sourceX,sourceY,epoch); }
    bool operator<(const Key& other) const { return order()<other.order(); }
    bool operator==(const Key& other) const { return order()==other.order(); }
};
struct Goal { int x=0,y=0; BuildingRef supplier; };
struct Sample { int x=0,y=0; unsigned cost=0; BuildingRef supplier; };
struct Choice { bool available=false; Sample sample; };
struct Output {
    // Computed with the private output, so routine checksums never walk goals.
    std::uint32_t goalDigest=2166136261u;
    std::vector<std::uint8_t> entryCosts;
    std::vector<std::uint32_t> labels;
    std::vector<Goal> goals;
    Choice choice;
};
struct Published {
    FieldReadiness readiness=FieldReadiness::Pending;
    std::shared_ptr<const std::uint16_t[]> values;
    std::shared_ptr<const Output> output;
    std::uint64_t captureRevision=0;
};
// Implemented by the destination selectors. This runs only as a private job;
// its complete result is serializable and requires no saved callback.
void buildSelection(const Key&,const SimulationSnapshot::Handle&,std::uint16_t*,Output&,GradientWorkspace&);
}
