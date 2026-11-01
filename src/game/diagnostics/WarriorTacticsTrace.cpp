// SPDX-License-Identifier: GPL-3.0-or-later
#include "WarriorTacticsTrace.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

namespace GameDiagnostics
{
namespace
{
enum State { Fighting, ToFlag, AtFlag, ToFood, ToHeal, Training, InsideService, Idle, StateCount };
constexpr const char* names[]{"fighting","to_flag","at_flag","to_food","to_heal","training","inside_service","idle"};
Uint64 key(UnitRef ref) { return (Uint64(ref.generation)<<16)|ref.gid; }
bool warrior(const SimulationSnapshot::UnitView& u)
{
    return !u.isDead && (u.capabilityFlags & UnitRuntimeTraits::Melee)
        && u.performance[ATTACK_SPEED] && u.realAttackStrength>0;
}
int distance(int a,int b,int size)
{
    const int d=std::abs(a-b); return std::min(d,size-d);
}
}
struct WarriorTacticsTrace::Impl
{
    struct Journey
    {
        BuildingRef flag;
        Uint32 start=0;
        Sint64 arrival=-1,contact=-1;
        int x=-1,y=-1,range=0,originX=0,originY=0;
        bool leftCensored=false;
    };
    struct Sample
    {
        Journey journey;
        UnitRef unit;
        int team=0,x=0,y=0,startX=0,startY=0,flagX=-1,flagY=-1,range=0,purpose=-1,medical=0;
        State state=Idle;
        BuildingRef flag,previousFlag,service;
        Uint32 start=0,last=0;
        Uint64 ticks=0,movedTicks=0,steps=0;
        bool leftCensored=false;
    };
    struct Total
    {
        std::array<Uint64,StateCount> ticks{};
        Uint64 movedTicks=0,steps=0,experiencedTicks=0;
    };
    std::ofstream episodes,windows,flags,journeys;
    std::map<Uint64,Sample> units;
    std::map<Uint64,SimulationSnapshot::BuildingView> lastFlags;
    std::array<Total,32> totals{};
    struct Endpoint
    {
        int population=0,warriors=0,buildings=0;
        Uint64 hp=0,meleeBuildings=0,meleeUnits=0;
        Uint64 combatDeaths=0,starvationDeaths=0,conversionsIn=0,conversionsOut=0;
        double power=0;
        bool alive=false,present=false,attritionAvailable=false;
    };
    std::array<Endpoint,32> endpoints{};
    Uint32 tick=0,windowStart=0;
    Uint64 worldIdentity=0;
    bool started=false,finished=false;

    explicit Impl(const std::string& directory)
    {
        episodes.open(std::filesystem::path(directory)/"warrior-episodes.csv");
        windows.open(std::filesystem::path(directory)/"warrior-windows.csv");
        flags.open(std::filesystem::path(directory)/"warrior-flags.csv");
        journeys.open(std::filesystem::path(directory)/"warrior-journeys.csv");
        if(!episodes || !windows || !flags || !journeys) throw std::runtime_error("Cannot open warrior tactics telemetry");
        episodes<<"version,world,team,gid,generation,start,end,state,flag_gid,flag_generation,flag_x,flag_y,flag_range,previous_flag_gid,previous_flag_generation,service_gid,service_generation,x,y,ticks,moved_ticks,steps,left_censored,end_reason,start_x,start_y,service_purpose,medical\n";
        windows<<"version,world,start,end,team,alive,population,warriors,combat_power,buildings,building_hp,fighting,to_flag,at_flag,to_food,to_heal,training,inside_service,idle,moved_ticks,steps,experienced_ticks,melee_building_damage,melee_unit_damage,melee_class_combat_deaths,melee_class_starvation_deaths,melee_class_conversions_in,melee_class_conversions_out\n";
        flags<<"version,world,tick,event,team,gid,generation,x,y,range,requested\n";
        journeys<<"version,world,team,gid,generation,start,end,flag_gid,flag_generation,flag_x,flag_y,range,origin_x,origin_y,arrival,first_contact,left_censored,end_reason\n";
    }
    void closeJourney(const Sample& s,const char* reason,Uint32 end)
    {
        const auto& j=s.journey;if(j.flag.empty()) return;
        journeys<<1<<','<<worldIdentity<<','<<s.team<<','<<s.unit.gid<<','<<s.unit.generation
            <<','<<j.start<<','<<end<<','<<j.flag.gid<<','<<j.flag.generation<<','<<j.x<<','<<j.y<<','<<j.range
            <<','<<j.originX<<','<<j.originY<<','<<j.arrival<<','<<j.contact<<','<<j.leftCensored<<','<<reason<<'\n';
    }
    void close(const Sample& s,const char* reason)
    {
        if(!s.ticks) return;
        episodes<<1<<','<<worldIdentity<<','<<s.team<<','<<s.unit.gid<<','<<s.unit.generation<<','<<s.start<<','<<s.last<<','<<names[s.state]
            <<','<<s.flag.gid<<','<<s.flag.generation<<','<<s.flagX<<','<<s.flagY<<','<<s.range
            <<','<<s.previousFlag.gid<<','<<s.previousFlag.generation<<','<<s.service.gid<<','<<s.service.generation
            <<','<<s.x<<','<<s.y<<','<<s.ticks<<','<<s.movedTicks<<','<<s.steps<<','<<s.leftCensored<<','<<reason
            <<','<<s.startX<<','<<s.startY<<','<<s.purpose<<','<<s.medical<<'\n';
    }
    void flagEvent(const SimulationSnapshot::BuildingView& b,const char* event)
    {
        flags<<1<<','<<worldIdentity<<','<<tick<<','<<event<<','<<b.team<<','<<b.gid<<','<<b.identity.generation
            <<','<<b.posX<<','<<b.posY<<','<<b.unitStayRange<<','<<b.maxUnitWorking<<'\n';
    }
    void window()
    {
        for(int n=0;n<32;++n) if(endpoints[n].present)
        {
            const auto& e=endpoints[n];
            windows<<2<<','<<worldIdentity<<','<<windowStart<<','<<tick<<','<<n<<','<<e.alive<<','<<e.population<<','<<e.warriors<<','<<e.power<<','<<e.buildings<<','<<e.hp;
            for(auto value:totals[n].ticks) windows<<','<<value;
            windows<<','<<totals[n].movedTicks<<','<<totals[n].steps<<','<<totals[n].experiencedTicks<<','<<e.meleeBuildings<<','<<e.meleeUnits;
            if(e.attritionAvailable)
                windows<<','<<e.combatDeaths<<','<<e.starvationDeaths<<','<<e.conversionsIn<<','<<e.conversionsOut;
            else windows<<",,,,";
            windows<<'\n';
        }
        totals={}; windowStart=tick;
    }
};
WarriorTacticsTrace::WarriorTacticsTrace(const std::string& directory):impl(std::make_unique<Impl>(directory)) {}
WarriorTacticsTrace::~WarriorTacticsTrace()
{
    try { finish(); }
    catch(const std::exception& e) { std::cerr<<"Warrior telemetry finalization failed: "<<e.what()<<'\n'; }
}
void WarriorTacticsTrace::capture(const SimulationSnapshot::Handle& world)
{
    auto& p=*impl;
    if(p.finished || !world.entities || !world.teams || !world.catalogs || !world.catalogs->buildings) return;
    if(p.started && (world.worldIdentity!=p.worldIdentity || world.tick<p.tick))
        throw std::runtime_error("Warrior trace cannot join different simulation timelines");
    if(p.started && world.tick==p.tick) return;
    const bool first=!p.started;
    const auto elapsed=first ? 0u : world.tick-p.tick;
    if(elapsed>1) throw std::runtime_error("Warrior trace requires consecutive completed ticks");
    p.worldIdentity=world.worldIdentity; p.tick=world.tick;
    if(first) {p.started=true;p.windowStart=p.tick;}

    // Snapshot entity slots already index the compact records; no path queries,
    // mutable game access, or additional spatial capture is necessary.
    const auto building=[&](BuildingRef ref)->const SimulationSnapshot::BuildingView* {
        const auto& slots=world.entities->buildingSlotIndices;
        if(ref.empty() || ref.gid>=slots.size()) return nullptr;
        const auto i=slots[ref.gid];
        if(i>=world.entities->buildings.size()) return nullptr;
        const auto& b=world.entities->buildings[i]; return b.identity==ref ? &b : nullptr;
    };
    p.endpoints={};
    for(const auto& t:world.teams->values) if(t.number>=0 && t.number<32)
    {
        auto& endpoint=p.endpoints[t.number];endpoint.alive=t.alive;endpoint.present=true;
        if(world.statisticsInputs && size_t(t.number)<world.statisticsInputs->events.size())
        {
            const auto& events=world.statisticsInputs->events[t.number];
            endpoint.meleeBuildings=events.damageDealt[GameplayMeasurements::MELEE][GameplayMeasurements::BUILDING];
            endpoint.meleeUnits=events.damageDealt[GameplayMeasurements::MELEE][GameplayMeasurements::UNIT];
            if(world.catalogs->units && events.deaths.size()>=world.catalogs->units->size()
                && events.conversionsIn.size()>=world.catalogs->units->size()
                && events.conversionsOut.size()>=world.catalogs->units->size())
            {
                endpoint.attritionAvailable=true;
                // Historical counters belong to catalog classes, rather than
                // the surviving units whose effective clocks we observe now.
                for(unsigned id=0;id<world.catalogs->units->size();++id)
                    if(world.catalogs->units->runtime(id).has(UnitRuntimeTraits::Melee))
                    {
                        endpoint.combatDeaths+=events.deaths[id][GameplayMeasurements::COMBAT];
                        endpoint.starvationDeaths+=events.deaths[id][GameplayMeasurements::STARVATION];
                        endpoint.conversionsIn+=events.conversionsIn[id];
                        endpoint.conversionsOut+=events.conversionsOut[id];
                    }
            }
        }
    }
    std::map<Uint64,SimulationSnapshot::BuildingView> currentFlags;
    for(const auto& b:world.entities->buildings)
    {
        if(b.typeNum>=world.catalogs->buildings->size()) continue;
        const auto& kind=world.catalogs->buildings->at(b.typeNum);
        if(!kind.isVirtual && b.team>=0 && b.team<32)
        {++p.endpoints[b.team].buildings;p.endpoints[b.team].hp+=std::max(0,b.hp);}
        if(!kind.isVirtual) continue;
        const auto k=(Uint64(b.identity.generation)<<16)|b.identity.gid;
        currentFlags.emplace(k,b);
        const auto old=p.lastFlags.find(k);
        if(old==p.lastFlags.end()) p.flagEvent(b,first ? "initial" : "created");
        else if(std::tie(old->second.posX,old->second.posY,old->second.unitStayRange,old->second.maxUnitWorking)
            !=std::tie(b.posX,b.posY,b.unitStayRange,b.maxUnitWorking)) p.flagEvent(b,"changed");
    }
    for(const auto& [k,b]:p.lastFlags) if(!currentFlags.count(k)) p.flagEvent(b,"removed");
    p.lastFlags=std::move(currentFlags);

    for(const auto& u:world.entities->units) if(!u.isDead && u.team>=0 && u.team<32)
    {
        auto& endpoint=p.endpoints[u.team];++endpoint.population;
        if(!warrior(u)) continue;
        ++endpoint.warriors;
        endpoint.power+=double(u.realAttackStrength)*u.performance[ATTACK_SPEED]
            *std::max(0,u.hp)/std::max<int>(1,u.performance[HP]);
        Impl::Sample next;
        next.unit=u.identity;next.team=u.team;next.x=next.startX=u.posX;next.y=next.startY=u.posY;
        next.purpose=u.destinationPurpose;next.medical=u.medical;
        next.start=next.last=p.tick;next.leftCensored=first;
        const auto* b=building(u.attached);
        const auto* kind=b && b->typeNum<world.catalogs->buildings->size() ? &world.catalogs->buildings->at(b->typeNum) : nullptr;
        if(kind && kind->isVirtual)
        {
            next.flag=b->identity;next.flagX=b->posX;next.flagY=b->posY;next.range=b->unitStayRange;
            next.previousFlag=next.flag;
        }
        else if(b) next.service=b->identity;
        if(u.movement==UnitState::MOV_ATTACKING_TARGET) next.state=Fighting;
        else if(!next.flag.empty()) next.state=u.displacement==UnitState::DIS_GOING_TO_FLAG ? ToFlag : AtFlag;
        else if(u.displacement==UnitState::DIS_INSIDE || u.displacement==UnitState::DIS_ENTERING_BUILDING || u.displacement==UnitState::DIS_EXITING_BUILDING) next.state=InsideService;
        else if(u.medical==UnitState::MED_HUNGRY
            || (u.activity==UnitState::ACT_UPGRADING && u.destinationPurpose==FEED)) next.state=ToFood;
        else if(u.medical==UnitState::MED_DAMAGED
            || (u.activity==UnitState::ACT_UPGRADING && u.destinationPurpose==HEAL)) next.state=ToHeal;
        else if(u.activity==UnitState::ACT_UPGRADING) next.state=Training;
        auto previous=p.units.find(key(u.identity));
        if(previous!=p.units.end())
        {
            const auto& old=previous->second;
            const bool same=old.team==next.team && old.flag==next.flag && old.flagX==next.flagX
                && old.flagY==next.flagY && old.range==next.range;
            if(same) next.journey=old.journey;
            else p.closeJourney(old,old.team!=next.team ? "converted" : next.flag.empty() ? "detached" : "redirected",p.tick);
        }
        if(!next.flag.empty())
        {
            auto& j=next.journey;
            if(j.flag.empty())
            {
                j.flag=next.flag;j.start=p.tick;j.x=next.flagX;j.y=next.flagY;j.range=next.range;
                j.originX=next.x;j.originY=next.y;j.leftCensored=first;
            }
            const int dx=distance(u.posX,j.x,world.width),dy=distance(u.posY,j.y,world.height);
            if(j.arrival<0 && dx*dx+dy*dy<=j.range*j.range) j.arrival=p.tick;
            if(j.contact<0 && next.state==Fighting) j.contact=p.tick;
        }
        auto [it,inserted]=p.units.try_emplace(key(u.identity),next);
        if(inserted) continue;
        auto& prev=it->second;
        auto& total=p.totals[prev.team];
        total.ticks[prev.state]+=elapsed;
        if(!prev.previousFlag.empty()) total.experiencedTicks+=elapsed;
        const auto steps=std::max(distance(prev.x,next.x,world.width),distance(prev.y,next.y,world.height));
        const bool moved=steps>0 && prev.state!=InsideService && next.state!=InsideService;
        prev.ticks+=elapsed;prev.last=p.tick;
        if(moved) {prev.movedTicks+=elapsed;prev.steps+=steps;total.movedTicks+=elapsed;total.steps+=steps;}
        if(next.previousFlag.empty()) next.previousFlag=prev.previousFlag;
        const bool changed=prev.team!=next.team || prev.state!=next.state || prev.flag!=next.flag || prev.service!=next.service || prev.purpose!=next.purpose || prev.medical!=next.medical
            || prev.flagX!=next.flagX || prev.flagY!=next.flagY || prev.range!=next.range;
        if(changed) {p.close(prev,prev.team!=next.team ? "converted" : "changed");prev=next;}
        else {prev.x=next.x;prev.y=next.y;prev.journey=next.journey;}
    }
    for(auto it=p.units.begin();it!=p.units.end();)
    {
        if(it->second.last!=p.tick)
        {
            auto& s=it->second;
            // Last observed state owns the final interval up to disappearance.
            p.totals[s.team].ticks[s.state]+=elapsed;
            if(!s.previousFlag.empty()) p.totals[s.team].experiencedTicks+=elapsed;
            s.ticks+=elapsed;s.last=p.tick;p.close(s,"disappeared");p.closeJourney(s,"disappeared",p.tick);
            it=p.units.erase(it);
        }
        else ++it;
    }
    if(first || p.tick-p.windowStart>=512) p.window();
}
void WarriorTacticsTrace::finish()
{
    auto& p=*impl;
    if(p.finished) return;
    for(const auto& [k,s]:p.units) {p.close(s,"right_censored");p.closeJourney(s,"right_censored",p.tick);}
    if(p.tick>p.windowStart) p.window();
    p.episodes.flush();p.windows.flush();p.flags.flush();p.journeys.flush();p.finished=true;
    if(!p.episodes || !p.windows || !p.flags || !p.journeys)
        throw std::runtime_error("Failed writing warrior tactics telemetry");
}
}
