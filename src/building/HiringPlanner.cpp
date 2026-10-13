// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPlanner.h"
#include "HiringRanking.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "SharedBuildingGradient.h"
#include "ai/observation/AIWorldView.h"
#include "field/RuntimeTerrainGradient.h"
#include "field/AirPathfind.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <tuple>

namespace HiringPlanner
{
namespace
{
using Clock = std::chrono::steady_clock;
std::uint64_t elapsed(Clock::time_point start)
{ return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(); }
using World = AIEngine::AIWorldView;
using B = SimulationSnapshot::BuildingView;
using U = SimulationSnapshot::UnitView;
using FieldKey = std::tuple<std::uint64_t, std::uint64_t, int, int>;
void mix(std::uint64_t& hash, std::uint64_t value)
{ hash ^= value; hash *= 1099511628211ull; }
std::uint64_t identity(BuildingRef b) { return (std::uint64_t(b.generation)<<16)|b.gid; }
int swimClass(const U& u) { return Map::swimClass(u.performance[WALK],u.performance[SWIM]); }
int food(const U& u)
{
    if(u.hungriness<=0) return INT_MAX;
    return int(std::clamp<std::int64_t>((std::int64_t(u.hungry)-u.trigHungry)/u.hungriness,INT_MIN,INT_MAX));
}
bool hasCargo(const Inputs& input,const U& u,int material)
{
    if(u.carriedMaterial==material) return true;
    if(const auto* cargo=input.world.entities->unitCargo.find(u.gid))
        for(const auto& entry:*cargo) if(entry.material==material) return true;
    return false;
}
bool canCarry(const Inputs& input,const World& world,const U& u,int material)
{
    const auto& traits=world.unitTraits(u.typeNum);
    if(!traits.has(UnitRuntimeTraits::ExtendedCargo)) return true;
    const auto* cargo=input.world.entities->unitCargo.find(u.gid);
    const unsigned count=(u.carriedMaterial>=0)+(cargo?cargo->size():0)-(u.widePrimaryCargo?1:0);
    if(count>=unsigned(traits.cargoCapacity)) return false;
    if(u.carriedMaterial<0 || hasCargo(input,u,material)) return true;
    unsigned kinds=1; MaterialMask mask=MaterialMask(1u<<u.carriedMaterial);
    if(cargo) for(const auto& entry:*cargo) if(!(mask&(1u<<entry.material))) { mask|=1u<<entry.material; ++kinds; }
    return kinds<unsigned(traits.cargoKinds);
}
int warped(const World& world,int a,int b,int c,int d)
{
    int dx=std::abs(a-c),dy=std::abs(b-d);
    return std::max(std::min(dx,world.width-dx),std::min(dy,world.height-dy));
}
bool materialGoal(const World& world,const B& b,int material,std::size_t i,unsigned supplyModes)
{
    if(world.areasAt(i).forbidden&world.teams[b.team].mask)return false;
    const auto resource=world.resourceAt(i).resource;
    if(MapState::hasMaterialSlot(world.state(),i,material) &&
        (resource.type==NO_RES_TYPE || !world.resourceRegistry->properties(static_cast<ResourceId>(resource.type)).visibleToHarvest ||
        (world.visibilityAt(i).visible&world.teams[b.team].mask)))return true;
    const auto o=world.occupancyAt(i);
    if(o.building==NOGBID)return false;
    const auto* supplier=world.buildingAtSlot(o.building);
    if(!supplier || supplier->team!=b.team || supplier->identity==b.identity || supplier->buildingState!=B::ALIVE ||
        (supplier->usesTeamResources&&b.usesTeamResources))return false;
    const auto& st=world.catalog->at(supplier->typeNum).resolvedType;
    const auto modes=((supplyModes&1)?st.runtimeSuppliesStockMask:0)|((supplyModes&2)?st.runtimeSuppliesDirectStockMask:0);
    return modes&supplier->availableSupplyMask&(1u<<material);
}
std::uint64_t geography(const World& world,int team,std::array<std::uint64_t,5>& parts)
{
    std::uint64_t hash=1469598103934665603ull;
    mix(hash,world.width);mix(hash,world.height);mix(hash,world.terrainRevision);
    parts.fill(1469598103934665603ull);mix(parts[0],world.terrainRevision);
    for(std::size_t i=0;i<std::size_t(world.width)*world.height;++i)
    {
        const auto o=world.occupancyAt(i);
        mix(hash,world.cellRuleAt(i));mix(hash,o.building);mix(hash,o.immobileUnit);
        mix(hash,world.areasAt(i).forbidden&world.teams[team].mask);
        mix(hash,MapState::resourceBlocksGround(world.state(),i));
        mix(parts[0],world.cellRuleAt(i));mix(parts[1],o.building);mix(parts[2],o.immobileUnit);
        mix(parts[3],world.areasAt(i).forbidden&world.teams[team].mask);
        mix(parts[4],MapState::resourceBlocksGround(world.state(),i));
    }
    return hash;
}
}
struct Cache::Impl
{
    Metrics* metrics = nullptr;
    std::uint64_t previousGeo = 0;
    std::array<std::uint64_t,5> previousParts{};
    bool hasPreviousGeo = false;
    struct Field { std::vector<Uint16> values; std::uint64_t use=0; };
    std::map<FieldKey,Field> fields;
    std::size_t retained=0, budget=Cache::MaxBytes;
    static constexpr std::size_t NodeBytes=sizeof(FieldKey)+sizeof(Field)+4*sizeof(void*);
    std::size_t bytes() const
    {
        std::size_t total=sizeof(Impl)+sizeof(gradient_kernel::BackendSession)+32+retained+fields.size()*NodeBytes;
        for(const auto& bucket:scratch.buckets)total+=bucket.cells.capacity()*sizeof(std::uint32_t);
        total+=scratch.deferredSeeds.capacity()*sizeof(std::pair<int,int>);
        if(scratch.terrain){total+=sizeof(TerrainGradientWorkspace)+scratch.terrain->buckets.capacity()*sizeof(GradientBucket);
            for(const auto& bucket:scratch.terrain->buckets)total+=bucket.cells.capacity()*sizeof(std::uint32_t);}
        return total;
    }
    void trim(std::size_t needed)
    {
        while(bytes()+needed>budget)
        {
            auto victim=fields.end();
            for(auto it=fields.begin();it!=fields.end();++it) if(it->second.use<=protectedAfter && (victim==fields.end()||it->second.use<victim->second.use))victim=it;
            if(victim==fields.end())throw std::length_error("Hiring active route working set exceeds cache bound");
            ++metrics->evictions;
            retained-=victim->second.values.capacity()*sizeof(Uint16);fields.erase(victim);
        }
    }
    std::uint64_t clock=0, protectedAfter=0;
    std::array<std::uint64_t,MaterialCount> sourceStamps{};
    std::array<bool,MaterialCount> stamped{};
    GradientWorkspace scratch;
    template<class Seed>
    const std::vector<Uint16>& field(const World& world,FieldKey key,int swim,Seed seed)
    {
        if(auto it=fields.find(key);it!=fields.end()) { ++metrics->fieldHits;it->second.use=++clock;return it->second.values; }
        ++metrics->fieldMisses;
        const auto seedStart=Clock::now();
        const std::size_t bytes=std::size_t(world.width)*world.height*sizeof(Uint16);
        trim(bytes+NodeBytes);
        Field result;result.values.resize(std::size_t(world.width)*world.height);
        for(std::size_t i=0;i<result.values.size();++i) result.values[i]=seed(i);
        metrics->fieldCells+=result.values.size();
        metrics->seedNs+=elapsed(seedStart);
        const auto propagationStart=Clock::now();
        gradient_kernel::propagateTerrainField(result.values.data(),swim,gradient_kernel::COST_LIMIT,
            {world.width,world.height},scratch,[&](std::size_t i){return world.cellRuleAt(i);},
            world.terrainMovementModifiers,*world.cellRules,256);
        metrics->propagationNs+=elapsed(propagationStart);
        trim(result.values.capacity()*sizeof(Uint16)+NodeBytes);
        result.use=++clock;retained+=result.values.capacity()*sizeof(Uint16);
        return fields.emplace(key,std::move(result)).first->second.values;
    }
    int distance(const World& world,const std::vector<Uint16>& values,int x,int y)
    {
        Uint16 value=values[world.tileIndex(x,y)];
        constexpr int neighbors[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int d=0;d<8 && value<=GRADIENT_UNREACHABLE;++d) value=values[world.tileIndex(x+neighbors[d][0],y+neighbors[d][1])];
        return value>GRADIENT_UNREACHABLE ? gradientTiles(value) : INT_MAX;
    }
    const std::vector<Uint16>& resource(const World& world,const B& b,int material,int swim,std::uint64_t geo,unsigned modes,bool consumerSpecific)
    {
        const bool markets=modes!=0;
        if(!stamped[material]) {
            auto& value=sourceStamps[material];value=geo;
            for(std::size_t i=0;i<std::size_t(world.width)*world.height;++i) {
                mix(value,MapState::hasMaterialSlot(world.state(),i,material));
                mix(value,world.visibilityAt(i).visible&world.teams[b.team].mask);
            }
            stamped[material]=true;
        }
        std::uint64_t stamp=sourceStamps[material];
        mix(stamp,modes);
        if(consumerSpecific)mix(stamp,configuration(b));
        if(markets) for(const auto& supplier:world.buildings) if(supplier.team==b.team) {mix(stamp,identity(supplier.identity));mix(stamp,supplier.typeNum);mix(stamp,supplier.buildingState);mix(stamp,supplier.usesTeamResources);mix(stamp,supplier.availableSupplyMask);}
        return field(world,{stamp,consumerSpecific?identity(b.identity):std::uint64_t(b.team),100+material,swim},swim,[&](std::size_t i)->Uint16 {
            const auto o=world.occupancyAt(i);
            if((world.areasAt(i).forbidden&world.teams[b.team].mask)||o.immobileUnit!=255) return GRADIENT_FORBIDDEN;
            const bool goal=materialGoal(world,b,material,i,modes);
            if(goal) return GRADIENT_AT_GOAL;
            return !MapState::resourceBlocksGround(world.state(),i)&&o.building==NOGBID&&gradient_kernel::terrainAllowsGround(world.terrainPropertiesAt(i),swim) ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN;
        });
    }
};
Cache::Cache(std::size_t budget):impl(std::make_unique<Impl>())
{ if(!budget || budget>MaxBytes)throw std::invalid_argument("Invalid hiring cache budget");impl->budget=budget; }
Cache::~Cache()=default;
std::size_t Cache::bytes() const { return impl->bytes(); }
std::uint64_t configuration(const BuildingStateRecord& b)
{
    std::uint64_t h=1469598103934665603ull;
    for(auto value:{b.typeNum,b.posX,b.posY,b.unitStayRange,b.minLevelToFlag,b.minWorkerLevelToFlag,int(b.explorersRequireBombing)})mix(h,std::uint64_t(value));
    for(bool selected:b.clearingMaterials)mix(h,selected);
    return h;
}
bool needsCandidates(const SimulationSnapshot::BuildingView& b)
{
    // Match the owner's maintained staffing demand, without advancing clocks
    // or recomputing demand through a live building on the worker.
    return b.buildingState==B::ALIVE && b.desiredMaxUnitWorking>0 &&
        b.working.count<unsigned(b.desiredMaxUnitWorking);
}
Inputs capture(const Game& game,SimulationSnapshot::Handle world)
{
    if(world.tick!=game.stepCounter) throw std::logic_error("Hiring input boundary mismatch");
    Inputs inputs;inputs.world=std::move(world);
    std::array<unsigned,Team::MAX_COUNT> classes{};
    for(const auto& u:inputs.world.entities->units)if(!u.isDead && !u.performance[FLY])classes[u.team]|=1u<<swimClass(u);
    std::array<bool,Team::MAX_COUNT> defense{};
    for(const auto& b:inputs.world.entities->buildings) {
        if(!needsCandidates(b))continue;
        defense[b.team]|=inputs.world.catalogs->buildings->at(b.typeNum).resolvedType.attractsRole(2);
        auto* live=const_cast<Game&>(game).resolveBuilding(b.identity);
        if(!live)throw std::logic_error("Hiring capture lost building incarnation");
        Inputs::Supply supply; supply.building=b.identity;
        for(unsigned material=0;material<MaterialCount;++material) {
            const auto modes=game.map.materialSupplyModesSlot(live,material);
            supply.modes[material]=modes;
            const auto supplied=((modes&1)?live->runtime->suppliesStockMask:0)|((modes&2)?live->runtime->suppliesDirectStockMask:0);
            supply.consumerSpecific[material]=live->runtime->has(BuildingRuntimeTraits::SharedStock)||(supplied&(1u<<material));
        }
        inputs.supplies.push_back(supply);
        for(int slot=0;slot<BUILDING_GRADIENT_COUNT;++slot)if(classes[b.team]&(1u<<(slot%SWIM_CLASS_COUNT)))if(auto field=live->leaseGradient(slot)) {
            auto guard=SharedBuildingGradient::lock(field);
            // Existing engine storage is leased, not duplicated. Account its
            // current capacity; retired generations are measured separately.
            auto bytes=std::size_t(field->width)*field->height*sizeof(Uint16)+sizeof(SharedBuildingGradient);
            if(field->search)bytes+=field->search->retainedBytes();
            inputs.routeBytes+=bytes;
            inputs.routes.push_back({b.identity,slot,std::move(field),bytes});
        }
    }
    for(const auto& unit:inputs.world.entities->units) {
        if(!defense[unit.team] || unit.isDead)continue;
        const auto* live=game.resolveUnit(unit.identity);
        if(!live) throw std::logic_error("Hiring capture lost unit incarnation");
        inputs.attack.emplace_back(unit.identity,live->getRealAttackStrength());
    }
    return inputs;
}
bool better(const Candidate& a,const Candidate& b,Job job)
{
    if(a.level!=b.level) return job==Job::Delivery||job==Job::Fetch||job==Job::Defend ? a.level>b.level : a.level<b.level;
    if(a.value!=b.value) return a.value<b.value;
    return std::tie(a.unit.gid,a.unit.generation)<std::tie(b.unit.gid,b.unit.generation);
}
List shortlist(Key key,std::vector<Candidate> candidates,unsigned capacity,Pools pools)
{
    if(capacity<1 || capacity>128)throw std::invalid_argument("Hiring capacity must be 1..128");
    List result{key,{},{},{}};
    const auto compare=[&](const auto& a,const auto& b){return better(a,b,key.job);};
    const auto keep=[&](auto begin,auto end,unsigned limit,auto& destination) {
        const auto count=std::min<std::size_t>(limit,end-begin);
        if(!count)return;
        if(count<std::size_t(end-begin))std::nth_element(begin,begin+count,end,compare);
        std::sort(begin,begin+count,compare);
        destination.assign(begin,begin+count);
    };
    if(pools!=Pools::Split)keep(candidates.begin(),candidates.end(),capacity,result.combined);
    if(pools!=Pools::Combined) {
        const auto middle=std::partition(candidates.begin(),candidates.end(),[](const auto& c){return c.available;});
        keep(candidates.begin(),middle,capacity*3/4,result.available);
        keep(middle,candidates.end(),capacity/4,result.busy);
    }
    return result;
}
Output Cache::rank(const Inputs& input,unsigned team,unsigned capacity,Pools pools)
{
    if(capacity<1 || capacity>128) throw std::invalid_argument("Hiring capacity must be 1..128");
    const auto start=std::chrono::steady_clock::now();World world(input.world);
    if(team>=world.teams.size()) throw std::invalid_argument("Hiring team outside snapshot");
    Output output;output.tick=world.tick;output.team=team;
    impl->metrics=&output.metrics;
    // Never leave a pointer to stack metrics in the reusable cache, including
    // exception exits from a bounded working-set failure.
    struct Reset { Metrics*& value; ~Reset(){value=nullptr;} } reset{impl->metrics};
    ++output.metrics.refreshes;
    output.metrics.setupNs+=elapsed(start);
    impl->stamped.fill(false);
    std::uint64_t geo=0;bool hasGeo=false;
    const auto ensureGeography=[&]() {
        if(hasGeo)return geo;
        const auto geographyStart=Clock::now();
        std::array<std::uint64_t,5> parts;
        geo=geography(world,team,parts);hasGeo=true;
        output.metrics.geographyNs+=elapsed(geographyStart);
        if(impl->hasPreviousGeo) {
            output.metrics.geographyChanges+=impl->previousGeo!=geo;
            output.metrics.terrainChanges+=parts[0]!=impl->previousParts[0];
            output.metrics.buildingOccupancyChanges+=parts[1]!=impl->previousParts[1];
            output.metrics.immobileChanges+=parts[2]!=impl->previousParts[2];
            output.metrics.forbiddenChanges+=parts[3]!=impl->previousParts[3];
            output.metrics.resourceBlockingChanges+=parts[4]!=impl->previousParts[4];
        }
        impl->previousParts=parts;impl->previousGeo=geo;impl->hasPreviousGeo=true;
        return geo;
    };
    const auto attackStart=Clock::now();
    std::map<std::pair<Uint16,Uint32>,int> attack;for(const auto& [unit,strength]:input.attack)attack[{unit.gid,unit.generation}]=strength;
    output.metrics.setupNs+=elapsed(attackStart);
    for(const auto* building:world.buildingSlots(team))
    {
        if(!building)continue;
        ++output.metrics.buildingsExamined;
        if(!needsCandidates(*building)){++output.metrics.buildingsSkippedNoDemand;continue;}
        ++output.metrics.buildingsNeedingHiring;
        const auto& b=*building;const auto& type=world.catalog->at(b.typeNum).resolvedType;
        const auto supply=std::find_if(input.supplies.begin(),input.supplies.end(),[&](const auto& value){return value.building==b.identity;});
        if(supply==input.supplies.end())throw std::logic_error("Hiring input missing supplier modes");
        BuildingLists result{b.identity,b.typeNum,configuration(b),{}};
        std::vector<Key> jobs;
        for(unsigned r=0;r<MaterialCount;++r) if(type.maxMaterial[r]>0 || b.wishedMaterials[r]>0) {jobs.push_back({Job::Delivery,int(r)});jobs.push_back({Job::Fetch,int(r)});}
        for(unsigned role=0;role<3;++role) if(type.attractsRole(role))jobs.push_back({Job(unsigned(Job::Clear)+role),-1});
        for(auto key:jobs)
        {
            ++output.metrics.jobs;
            const auto precheckStart=Clock::now();
            List list{key,{},{},{}};std::vector<Candidate> candidates;
            bool any=true;
            if(key.job==Job::Delivery) {
                any=false;for(const auto* u:world.unitSlots(team))if(u&&!u->isDead&&hasCargo(input,*u,key.material)){any=true;break;}
            } else if(key.job==Job::Fetch && input.world.resources->materialSourceCounts[key.material]==0) {
                any=false;
                const auto modes=supply->modes[key.material];
                if(modes)for(const auto& supplier:world.buildings) {
                    const auto& st=world.catalog->at(supplier.typeNum).resolvedType;
                    const auto supplied=((modes&1)?st.runtimeSuppliesStockMask:0)|((modes&2)?st.runtimeSuppliesDirectStockMask:0);
                    if(supplier.team==int(team)&&supplier.identity!=b.identity&&supplier.buildingState==B::ALIVE&&
                        !(supplier.usesTeamResources&&b.usesTeamResources)&&(supplied&supplier.availableSupplyMask&(1u<<key.material))){any=true;break;}
                }
            }
            output.metrics.precheckNs+=elapsed(precheckStart);
            if(!any){++output.metrics.skippedJobs;result.jobs.push_back(std::move(list));continue;}
            std::array<std::shared_ptr<SharedBuildingGradient>,SWIM_CLASS_COUNT> buildings{};
            std::array<const std::vector<Uint16>*,SWIM_CLASS_COUNT> resources{};
            const auto route=key.job==Job::Delivery||key.job==Job::Fetch?BuildingRoute::Footprint:
                key.job==Job::Clear?BuildingRoute::Clearing:key.job==Job::Defend?BuildingRoute::Combat:BuildingRoute::Footprint;
            // Explore attraction follows the building's automatic route.
            const auto resolvedRoute=key.job==Job::Explore?BuildingRoute::Footprint:route;
            for(const auto& captured:input.routes)if(captured.building==b.identity && captured.slot/SWIM_CLASS_COUNT==int(resolvedRoute))
                buildings[captured.slot%SWIM_CLASS_COUNT]=captured.field;
            impl->protectedAfter=impl->clock;
            const auto evaluationStart=Clock::now();
            for(const auto* unit:world.unitSlots(team))
            {
                if(!unit || unit->isDead)continue;++output.candidateVisits;const auto& u=*unit;const auto& traits=world.unitTraits(u.typeNum);
                const bool transport=key.job==Job::Delivery||key.job==Job::Fetch;
                if(transport) {
                    if(!traits.has(UnitRuntimeTraits::Transport)||u.performance[BUILD]<=0||(type.isBuildingSite&&!traits.has(UnitRuntimeTraits::Construct))||u.constructionLevel<type.semantics.requiredWorkerLevel)continue;
                    if(key.job==Job::Delivery && !hasCargo(input,u,key.material))continue;
                    if(key.job==Job::Fetch && (u.performance[HARVEST]<=0 || (!hasCargo(input,u,key.material)&&!canCarry(input,world,u,key.material))))continue;
                } else {
                    const unsigned role=unsigned(key.job)-unsigned(Job::Clear);
                    constexpr unsigned types[3]={WORKER,EXPLORER,WARRIOR};
                    if(!(traits.recruitmentMask&(1u<<role)) || !type.semantics.attractionUnits[role].matches(u.typeNum,type.zonable[types[role]]?1u<<types[role]:0))continue;
                    if((role==0 && u.constructionLevel<b.minWorkerLevelToFlag)||(role==1&&b.explorersRequireBombing&&!u.level[MAGIC_ATTACK_GROUND])||(role==2&&std::min(u.level[ATTACK_SPEED],u.level[ATTACK_STRENGTH])<b.minLevelToFlag))continue;
                }
                const int swim=swimClass(u);int distance;
                if(u.performance[FLY]) {
                    ++output.metrics.airQueries;
                    field::AirDistanceField air(world.width,world.height,b.posX,b.posY,
                        [&](int x,int y){return world.terrainPropertiesAt(world.tileIndex(x,y)).flyable;},
                        [&](int x,int y){return (*world.cellRules)[world.cellRuleAt(world.tileIndex(x,y))].airCost;},world.airTerrainConstraints,field::AirDistanceDirection::ToDestination);
                    const auto cost=air.enabled()?air.costTo(u.posX,u.posY):unsigned(warped(world,u.posX,u.posY,b.posX,b.posY))*GRADIENT_STEP;
                    distance=cost==decltype(air)::unreachable?INT_MAX:int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
                    if(key.job==Job::Clear) {
                        field::AirDistanceField clearing(world.width,world.height,u.posX,u.posY,
                            [&](int x,int y){return world.terrainPropertiesAt(world.tileIndex(x,y)).flyable;},
                            [&](int x,int y){return (*world.cellRules)[world.cellRuleAt(world.tileIndex(x,y))].airCost;},world.airTerrainConstraints);
                        distance=INT_MAX;
                        MaterialMask selected=0;for(unsigned r=0;r<MaterialCount;++r)if(b.clearingMaterials[r])selected|=MaterialMask(1u<<r);
                        for(int y=0;y<world.height;++y)for(int x=0;x<world.width;++x) {
                            const auto i=world.tileIndex(x,y);const auto resource=world.resourceAt(i).resource;
                            if(world.distanceSquared(x,y,b.posX,b.posY)>b.unitStayRange*b.unitStayRange ||
                                (world.areasAt(i).forbidden&world.teams[team].mask) || resource.type==NO_RES_TYPE ||
                                !world.resourceRegistry->properties(static_cast<ResourceId>(resource.type)).clearable || !(MapState::materialMaskAt(world.state(),i)&selected))continue;
                            const auto c=clearing.enabled()?clearing.costTo(x,y):unsigned(warped(world,u.posX,u.posY,x,y))*GRADIENT_STEP;
                            if(c!=decltype(clearing)::unreachable)distance=std::min(distance,int((c+GRADIENT_STEP-1)/GRADIENT_STEP));
                        }
                    }
                } else {
                    if(!buildings[swim]){++output.metrics.missingBuildingFields;continue;}
                    ++output.metrics.sharedQueries;
                    distance=buildings[swim]->distance(u.posX,u.posY,output.metrics.sharedWaitNs,output.metrics.sharedQueryNs,output.metrics.sharedExpansions);
                }
                if(distance==INT_MAX)continue;
                Candidate candidate{u.identity,0,0,u.activity==U::ACT_RANDOM&&u.medical==U::MED_FREE};
                if(transport) {
                    candidate.level=HiringRanking::materialLevel(u.constructionLevel,u.level[WALK]);
                    if(key.job==Job::Delivery)candidate.value=std::int64_t(distance)-(food(u)>>1);
                    else {
                        int resourceDistance=INT_MAX;
                        if(!u.performance[FLY]) {
                            const auto modes=supply->modes[key.material];
                            const bool shared=modes==0 || (modes==1&&!supply->consumerSpecific[key.material]);
                            const auto published=shared?world.resourceGradient(team,key.material,swim,modes==1):std::span<const Uint16>{};
                            if(!published.empty()) {
                                ++output.metrics.publishedResourceReads;
                                const auto v=published[world.tileIndex(u.posX,u.posY)];if(v>GRADIENT_UNREACHABLE)resourceDistance=gradientTiles(v);
                            } else { ++output.metrics.privateResourceReads; if(!resources[swim])resources[swim]=&impl->resource(world,b,key.material,swim,ensureGeography(),modes,supply->consumerSpecific[key.material]);
                                resourceDistance=impl->distance(world,*resources[swim],u.posX,u.posY); }
                        } else {
                            ++output.metrics.airQueries;
                    field::AirDistanceField air(world.width,world.height,u.posX,u.posY,
                                [&](int x,int y){return world.terrainPropertiesAt(world.tileIndex(x,y)).flyable;},
                                [&](int x,int y){return (*world.cellRules)[world.cellRuleAt(world.tileIndex(x,y))].airCost;},world.airTerrainConstraints);
                            for(int y=0;y<world.height;++y)for(int x=0;x<world.width;++x) if(materialGoal(world,b,key.material,world.tileIndex(x,y),supply->modes[key.material])) {
                                const auto cost=air.enabled()?air.costTo(x,y):unsigned(warped(world,u.posX,u.posY,x,y))*GRADIENT_STEP;
                                if(cost!=decltype(air)::unreachable)resourceDistance=std::min(resourceDistance,int((cost+GRADIENT_STEP-1)/GRADIENT_STEP));
                            }
                        }
                        if(resourceDistance==INT_MAX)continue;
                        candidate.value=(std::int64_t(resourceDistance)+std::max(distance,resourceDistance))*256+(u.carriedMaterial>=0?5*256:0);
                    }
                } else {
                    const unsigned role=unsigned(key.job)-unsigned(Job::Clear);
                    std::int64_t time=u.hungriness>0?(std::int64_t(u.hungry)-(role==0?u.trigHungry:0))/u.hungriness:INT_MAX;
                    std::int64_t hp=std::int64_t(u.hp)*16/std::max(1,traits.flagRankingHealth);
                    if(role==1){time=std::clamp<std::int64_t>(time,-1000000000,1000000000);time*=time;hp=std::clamp<std::int64_t>(hp,-1000000000,1000000000);hp*=hp;distance=u.performance[FLY]&&!world.airTerrainConstraints?world.distanceSquared(u.posX,u.posY,b.posX,b.posY):int(std::min<std::int64_t>(INT_MAX,std::int64_t(distance)*distance));}
                    candidate.level=role==0?u.constructionLevel:role==1?u.level[MAGIC_ATTACK_GROUND]:std::int64_t(u.performance[ATTACK_SPEED])*attack[{u.identity.gid,u.identity.generation}];
                    candidate.value=distance-(role==0?1:2)*(time+hp);
                }
                candidates.push_back(candidate);
            }
            output.metrics.evaluationNs+=elapsed(evaluationStart);
            output.metrics.rankedCandidates+=candidates.size();
            const auto sortStart=Clock::now();
            list=shortlist(key,std::move(candidates),capacity,pools);
            output.metrics.sortOutputNs+=elapsed(sortStart);
            output.retainedBytes+=(list.combined.capacity()+list.available.capacity()+list.busy.capacity())*sizeof(Candidate);
            result.jobs.push_back(std::move(list));
        }
        output.buildings.push_back(std::move(result));
    }
    output.retainedBytes+=sizeof(Output)+output.buildings.capacity()*sizeof(BuildingLists);
    for(const auto& building:output.buildings)output.retainedBytes+=building.jobs.capacity()*sizeof(List);
    output.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    return output;
}
}
