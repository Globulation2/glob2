// SPDX-License-Identifier: GPL-3.0-or-later
#include "SnapshotChecksum.h"
#include <bit>

namespace SimulationChecksum {
namespace {
// Mix named simulation fields so addresses and padding cannot affect results.
// This inexpensive XOR/rotate error detector retains the original mixer's
// collision limitations.
struct Checksum {
    Uint32 state=0;
    void add(Uint32 value) { state=std::rotl(state,1)^value; }
    void wide(Uint64 value) { add(Uint32(value)); add(Uint32(value>>32)); }
    template<class Range> void values(const Range& range) { for (auto value:range) add(Uint32(value)); }
};
Uint32 unitChecksum(const SimulationSnapshot::UnitView& u,const SimulationSnapshot::Entities& entities)
{
    Checksum h;
    h.add(u.serviceResourcesReserved() ? 0x73657276u : 0);
    h.add(u.constructionLevel);
    if (u.areaLastPulseTick!=Uint32(-1)) h.add(u.areaLastPulseTick);
    h.add(Uint32(u.areaServiceRemainders[0]) | (Uint32(u.areaServiceRemainders[1])<<8) | (Uint32(u.areaServiceRemainders[2])<<16));
    h.add(u.typeNum); h.add(u.isDead); h.add(u.gid);
    h.add(Uint32(u.posX)); h.add(Uint32(u.posY)); h.add(Uint32(u.delta));
    h.add(Uint32(u.dx)^Uint32(u.dy)^u.direction); h.add(Uint32(u.insideTimeout));
    h.add(Uint32(u.speed) ^ Uint32(u.terrainHealthRemainder)*0x9e3779b9u);
    h.add(Uint32(u.needToRecheckMedical()) ^ u.medical ^ u.activity ^ u.displacement ^ u.movement ^ u.action);
    h.add(Uint32(u.targetX)^Uint32(u.targetY));
    h.add(Uint32(u.hp)^Uint32(u.trigHP));
    h.add(Uint32(u.hungry)^Uint32(u.trigHungry)^Uint32(u.trigHungryCarrying));
    h.add(u.fruitMask ^ u.fruitCount);
    for (unsigned ability=0;ability<NB_ABILITY;++ability) {
        h.add(u.performance[ability]); h.add(u.level[ability]); h.add(u.canLearn(ability));
    }
    h.add(!u.attached.empty()); h.add(!u.target.empty());
    h.add(Uint32(u.destinationPurpose)^Uint32(u.carriedMaterial));
    h.add(u.carriedPacket.numerator); h.add(u.carriedPacket.denominator);
    h.add(Uint8(u.jobPurpose)|(Uint32(u.regenerationRemainder)<<8)|(Uint32(u.widePrimaryCargo())<<16));
    h.add(u.configuredFoodCapacity); h.add(u.capabilityFlags);
    // Additional cargo is sparse. Empty allocation nodes are not state.
    if ((u.capabilityFlags & UnitRuntimeTraits::ExtendedCargo) || u.widePrimaryCargo()) {
        const auto* cargo=entities.unitCargo.find(u.gid);
        h.add(cargo ? Uint32(cargo->size()) : 0);
        if (cargo) for (const auto& entry:*cargo) { h.add(entry.material); h.wide(entry.packet.numerator); h.wide(entry.packet.denominator); }
    }
    h.add(u.entityRandom.checksum());
    return h.state;
}
Uint32 buildingChecksum(const SimulationSnapshot::BuildingView& b,const SimulationSnapshot::Entities& entities)
{
    Checksum h;
    if (b.areaFunded || b.areaFundingType>=0)
        h.add(Uint32(b.areaFundingType+1)*0x9e3779b9u ^ b.areaFundingTick ^ (Uint32(b.areaFundingTeam+1)<<24) ^ (b.areaFunded ? 0x61757261u : 0));
    h.add(b.typeNum ^ b.buildingState); h.add(b.constructionResultState);
    h.add(Uint32(b.maxUnitWorking)^Uint32(b.maxUnitWorkingFuture)^Uint32(b.maxUnitWorkingPreferred)^Uint32(b.desiredMaxUnitWorking));
    h.add(b.working.count ^ Uint32(b.subscriptionWorkingTimer) ^ b.inside.count);
    h.add(Uint32(b.posX)^Uint32(b.posY)); h.add(b.unitStayRange);
    Uint32 stock=0; for (unsigned material=0;material<MaterialCount;++material) stock^=b.localMaterials[material];
    h.add(stock); h.add(b.hp);
    h.add(Uint32(b.productionTimeout)^Uint32(b.productionUnit+1)^Uint32(b.constructionOriginTypeNum+1));
    h.values(b.constructionOriginRatios);
    if (b.fundingOffset!=SimulationSnapshot::Entities::NoRecord) {
        const auto& f=entities.funding.at(b.fundingOffset);
        h.values(f.constructionBudget); h.values(f.constructionReserved);
    }
    h.add((b.siteCompletionPending ? 0x73697465u : 0) ^ (b.explorersRequireBombing ? 0x626f6d62u : 0) ^ (Uint32(b.minWorkerLevelToFlag)<<24));
    for (unsigned type=0;type<NB_UNIT_TYPE;++type) h.add(b.ratio[type]^Uint32(b.percentUsed[type]));
    for (const auto& extra:std::span(entities.extraProduction).subspan(b.extraProductionOffset,b.extraProductionCount)) {
        h.add(Uint32(extra.ratio)^Uint32(extra.used)); h.add(extra.constructionOriginRatio);
    }
    h.add(b.bullets); h.add(b.seenByMask ^ b.gid); h.add(b.entityRandom.checksum());
    return h.state;
}
Uint32 mapChecksum(const SimulationSnapshot::Handle& w)
{
    const auto cells=std::size_t(w.width)*std::size_t(w.height);
    if (w.width<=0 || w.height<=0 || w.terrain->vertices->size()!=cells || w.resources->cells.size()!=cells
        || w.occupancy->cells.size()!=cells || w.areas->cells.size()!=cells)
        throw std::invalid_argument("Snapshot checksum map component size mismatch");
    const auto& indices=w.resources->stockIndices;
    if (!indices.empty() && indices.size()!=cells) throw std::invalid_argument("Snapshot resource stock index count");
    Uint32 result=0;
    for (std::size_t index=0;index<cells;++index) {
        const auto& resource=w.resources->cells[index]; const auto& occupied=w.occupancy->cells[index];
        const auto& area=w.areas->cells[index];
        result+=Uint32((*w.terrain->vertices)[index])+occupied.building+resource.resource.getUint32()+resource.mayGrow
            +occupied.groundUnit+occupied.airUnit+area.forbidden+area.farm;
        const auto slot=indices.empty() ? 0 : indices[index];
        if (slot) for (auto stock:w.resources->stocks.at(slot-1)) result=std::rotl(result,1)^stock;
        result=std::rotl(result,1);
    }
    return result;
}
Uint32 hashState(const SimulationSnapshot::Handle& w)
{
    Checksum h;
    h.add(0x534e4103u); h.add(w.width); h.add(w.height);
    h.add(w.catalogs->units->checksum()); h.add(w.catalogs->resources->checksum());
    h.add(w.terrain->registry->checksum());
    for (const auto& team:w.teams->values) {
        Checksum t;
        t.add(team.number); t.add(team.playersMask);
        const auto units=w.entities->unitsForTeam(team.number);
        const auto buildings=w.entities->buildingsForTeam(team.number);
        t.add(Uint32(units.size())); t.add(Uint32(buildings.size()));
        for (const auto& u:units) t.add(unitChecksum(u,*w.entities));
        for (const auto& b:buildings) t.add(buildingChecksum(b,*w.entities));
        t.add(Uint32(team.swarms.size()));
        t.add(team.allies); t.add(team.enemies); t.add(team.exchangeVision); t.add(team.foodVision); t.add(team.otherVision);
        t.add(team.mask); t.add(team.noMoreBuildingSitesCountdown); t.add(team.prestige);
        h.add(t.state);
    }
    if (w.resources) h.add(mapChecksum(w));
    return h.state;
}
} // namespace
bool mapChecksumsFor(const GameHeader& players)
{
    return players.hasNetworkPlayer();
}

Uint32 snapshotChecksum(const SimulationSnapshot::Handle& input,bool mapChecksums)
{
    return hashState(input.checksumView(mapChecksums));
}
Pipeline::Pipeline(ComputeExecutor& executor)
    : executor(executor) {}
Pipeline::~Pipeline() { try { reset(); } catch (...) {} }
void Pipeline::reset()
{
    std::exception_ptr failure;
    for (std::size_t i = 0; i < count; ++i) {
        auto& work = pending[(first + i) % Capacity];
        try { executor.join(work.batch,{JoinReason::Teardown,ComputeTelemetry::NoTick}); }
        catch (...) { failure=std::current_exception(); }
    }
    for (auto& work : pending) work = {};
    first = count = 0;
    lastTick.reset(); worldIdentity=0; failed=nullptr;
    if (failure) std::rethrow_exception(failure);
}
void Pipeline::submit(const SimulationSnapshot::Handle& world, Uint32 sourceTick, bool mapChecksums)
{
    if (worldIdentity && worldIdentity!=world.worldIdentity) reset();
    worldIdentity=world.worldIdentity;
    if (failed) std::rethrow_exception(failed);
    if (lastTick && *lastTick==sourceTick) return;
    if (count>=Capacity) throw std::logic_error("Checksum results must be collected before admission");
    auto* ptr = &pending[(first + count) % Capacity];
    ptr->world=world.checksumView(mapChecksums); ptr->worldTick=world.tick; ptr->result.sourceTick=sourceTick;

    const ComputeExecutor::Group group{1,{[](void* context,std::size_t) {
        auto& work=*static_cast<Work*>(context);
        // Release leases on failure as well as success, before publishing completion.
        auto world=std::move(work.world); work.world={};
        try { work.result.value=hashState(world); }
        catch (...) { work.error=std::current_exception(); throw; }
    },ptr}};
    ++count;
    try { ptr->batch=executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(world.tick)+Delay),
        {ComputeProducer::Checksum,world.tick,Uint64(world.tick)+Delay}); }
    catch (...) { --count; *ptr = {}; throw; }
    if (executor.threadCount()==1) {
        // With no worker there is no overlap to preserve. Compute immediately
        // so the one-tick reporting delay retains only a scalar, not another
        // generation of pooled snapshot buffers. Worker configurations never
        // take this path, including when they miss their delivery deadline.
        try { executor.join(ptr->batch,{JoinReason::ImmediateRequired,world.tick}); }
        catch (...) { ptr->error=std::current_exception(); }
    }
    lastTick=sourceTick;
}
Pipeline::Results Pipeline::collect(Uint32 now, bool flush, std::optional<Uint32> worldNow)
{
    if (failed) std::rethrow_exception(failed);
    Results results;
    while (count && (flush || Uint32(now-pending[first].result.sourceTick)>=Delay)) {
        auto& work=pending[first];
        try {
            executor.join(work.batch,{flush ? JoinReason::Verification : JoinReason::TickDeadline,worldNow.value_or(now)});
            // Executor reconfiguration may have drained its slot already.
            if (work.error) std::rethrow_exception(work.error);
        }
        catch (...) { failed=std::current_exception(); throw; }
        results.samples[results.count++]={work.result,work.worldTick};
        work = {}; first = (first + 1) % Capacity; --count;
    }
    return results;
}
}
