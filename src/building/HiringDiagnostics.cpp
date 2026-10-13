// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringDiagnostics.h"
#include "Building.h"
#include "Unit.h"
#include "Game.h"
#include "Team.h"
#include "HiringPlanner.h"
#include <array>
#include <chrono>
#include <map>
#include <ostream>
#include <utility>

namespace HiringDiagnostics
{
namespace
{
thread_local std::map<std::pair<std::uint16_t, std::uint32_t>, Row> rows;
thread_local std::array<Row::Timing,Team::MAX_COUNT> periodic;
std::uint64_t now()
{ return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
constexpr const char* phases[] = {"attempt", "carrying_scan", "fetch_scan", "flag_scan", "selection", "subscription", "swap", "field_initialization", "field_extension"};
constexpr const char* counters[] = {"visits", "qualifications", "building_queries", "resource_queries", "hires", "failed_attempts", "busy", "rejected", "slots"};
}
thread_local Row* active = nullptr;
std::atomic<bool> collecting{false};
thread_local Observer observer = nullptr;
thread_local void* observerContext = nullptr;
void setObserver(Observer value, void* context) { observer=value;observerContext=context; }
void beginSearch() { if(collecting.load(std::memory_order_relaxed) && observer && active) active->qualified.clear(); }
void qualifiedObserved(const Unit& unit, Hiring::Key key)
{ if(active) active->qualified.push_back({Game::refOf(&unit),key}); }
void hiredObserved(const Unit& unit, Hiring::Key key)
{
    if(active && observer) observer({active->building,active->tick,active->type,active->configuration,
        Game::refOf(&unit),key,active->qualified},observerContext);
}
bool enabled() { return collecting.load(std::memory_order_relaxed); }
void configure(bool value) { active = nullptr; rows.clear(); periodic={}; collecting.store(value,std::memory_order_relaxed); }
PeriodicSwapScope::PeriodicSwapScope(const Team& value):team(value.teamNumber)
{ if(collecting)start=now(); }
PeriodicSwapScope::~PeriodicSwapScope()
{ if(start){++periodic[team].calls;periodic[team].ns+=now()-start;} }
Scope::Scope(const Building& building, Phase value): phase(value)
{
    if (!collecting) return;
    previous = active;
    row = &rows[{building.gid, building.scriptIdentity}];
    row->building=Game::refOf(&building);row->tick=building.owner->game->stepCounter;
    row->type=building.typeNum;row->configuration=HiringPlanner::configuration(building);
    active = row;
    start = now();
}
Scope::Scope(Phase value): phase(value)
{ if(collecting.load(std::memory_order_relaxed)){row=active;previous=active;if(row)start=now();} }
Scope::~Scope()
{
    if (!row) return;
    auto& timing = row->phases[unsigned(phase)];
    ++timing.calls; timing.ns += now() - start;
    active = previous;
}
void write(std::ostream& output)
{
    output << "{\"schema_version\":1,\"clock\":\"steady_elapsed_ns\",\"inclusive\":true,\"buildings\":[";
    bool first = true;
    for (const auto& [identity, row] : rows)
    {
        if (!first) output << ',';
        first = false;
        output << "{\"team\":" << Building::GIDtoTeam(identity.first) << ",\"gid\":" << identity.first << ",\"generation\":" << identity.second << ",\"phases\":{";
        for (unsigned i=0; i<unsigned(Phase::Count); ++i)
            output << (i ? "," : "") << '"' << phases[i] << "\":{\"calls\":" << row.phases[i].calls << ",\"ns\":" << row.phases[i].ns << '}';
        output << "},\"counters\":{";
        for (unsigned i=0; i<unsigned(Counter::Count); ++i)
            output << (i ? "," : "") << '"' << counters[i] << "\":" << row.counters[i];
        output << "}}";
    }
    output << "],\"periodic_swaps\":[";
    bool firstTeam=true;
    for(unsigned team=0;team<periodic.size();++team)if(periodic[team].calls) {
        output<<(firstTeam?"":",")<<"{\"team\":"<<team<<",\"calls\":"<<periodic[team].calls<<",\"ns\":"<<periodic[team].ns<<'}';firstTeam=false;
    }
    output << "]}\n";
}
}
