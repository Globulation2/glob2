// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AdaptiveGradientState.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

// Explicit observer only. No clocks, stream objects, histories or allocations
// exist unless requested. These events never participate in world state or policy.
struct GradientWorkStats
{
    using Reason=AdaptiveGradientState::Reason;
    static constexpr unsigned Unknown=7,ReasonCount=8,FamilyCount=7;
    // Separate diagnostic identity: lazy-only buckets never enter model/state keys.
    struct Key {
        unsigned family=0,team=0,swim=0,material=GradientFieldKey::NoMaterial;
        std::uint64_t destination=0;
        Key(GradientFieldKey k):family(unsigned(k.family)),team(k.team),swim(k.swim),material(k.material){}
        Key(unsigned f,unsigned t,unsigned s,unsigned m,std::uint64_t d):family(f),team(t),swim(s),material(m),destination(d){}
        auto operator<=>(const Key&)const=default;
    };
    static Key consumerKey(unsigned team,unsigned swim,unsigned material,unsigned gid,unsigned identity,unsigned modes=0) {
        return {5,team,swim,material,(std::uint64_t(identity)<<18)|(std::uint64_t(gid&65535)<<2)|(modes&3)};
    }
    static Key buildingKey(unsigned team,unsigned swim,unsigned id,unsigned identity,unsigned route) {
        return {6,team,swim,GradientFieldKey::NoMaterial,(std::uint64_t(identity)<<32)|(std::uint64_t(id)<<8)|route};
    }
    struct Totals {std::uint64_t requests=0,duplicates=0,admissions=0,publications=0,fieldCells=0,preparedSearches=0,partialResumes=0;};
    std::array<std::array<Totals,ReasonCount>,FamilyCount> totals{};
    std::map<Key,unsigned> queuedReasons;
    std::map<Key,std::uint32_t> previousAdmission;
    std::map<std::tuple<unsigned,unsigned,std::uint32_t>,std::uint64_t> intervals;
    std::string path;
    std::ofstream events;
    explicit GradientWorkStats(const std::string& output):path(output) {
        if(!std::filesystem::path(path).parent_path().empty())std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        events.open(path,std::ios::trunc);
        if(!events)throw std::runtime_error("Cannot open gradient work statistics output");
        events<<"family,team,movement,material,destination,event,reason,tick,interval,field_cells,settled_cost_before,settled_cost_after\n";
        events.flush();
    }
    ~GradientWorkStats() {try {flush();}catch(...) {}}
    static const char* familyName(unsigned f) {static constexpr const char* names[]={"materials","markets","guard","clear","forbidden","lazy_consumer","lazy_building"};return names[f];}
    static const char* reasonName(unsigned r) {static constexpr const char* names[]={"none","cold","reactive","history_initialization","model","age_cap","fixed_interval","restored_unknown"};return names[std::min(r,Unknown)];}
    void event(Key key,const char* name,unsigned reason,std::uint32_t tick,std::uint32_t interval,std::uint64_t cells,unsigned before=0,unsigned after=0) {
        events<<familyName(key.family)<<','<<unsigned(key.team)<<','<<unsigned(key.swim)<<','<<unsigned(key.material)<<','<<key.destination<<','<<name<<','<<reasonName(reason)<<','<<tick<<','<<interval<<','<<cells<<','<<before<<','<<after<<'\n';
    }
    void request(Key key,std::uint32_t tick,unsigned reason,bool duplicate) {
        auto& total=totals[key.family][std::min(reason,Unknown)];++total.requests;
        if(duplicate)++total.duplicates;else queuedReasons[key]=reason;
        event(key,duplicate?"duplicate":"request",reason,tick,0,0);
    }
    unsigned admitted(Key key,std::uint32_t tick,std::uint64_t cells) {
        unsigned reason=Unknown;
        if(const auto found=queuedReasons.find(key);found!=queuedReasons.end()){reason=found->second;queuedReasons.erase(found);}
        auto& total=totals[key.family][std::min(reason,Unknown)];++total.admissions;total.fieldCells+=cells;
        std::uint32_t interval=0;
        if(const auto previous=previousAdmission.find(key);previous!=previousAdmission.end()) {
            interval=tick-previous->second;++intervals[{unsigned(key.family),reason,interval}];
        }
        previousAdmission[key]=tick;
        event(key,"admission",reason,tick,interval,cells);return reason;
    }
    void published(Key key,std::uint32_t tick,unsigned reason,std::uint64_t cells) {
        ++totals[key.family][std::min(reason,Unknown)].publications;
        event(key,"publication",reason,tick,0,cells);
    }
    void partial(Key key,std::uint32_t tick,unsigned before,unsigned after,bool prepared=false) {
        auto& total=totals[key.family][Unknown];
        if(prepared)++total.preparedSearches;else ++total.partialResumes;
        event(key,prepared?"prepared_search":"search_resume",Unknown,tick,0,0,before,after);
    }
    void flush() {
        events.flush();
        std::ofstream summary(path+".summary.csv",std::ios::trunc);
        summary<<"family,reason,requests,duplicates,admissions,publications,admitted_seed_field_cells,prepared_search_submissions,owner_search_resumes\n";
        for(unsigned f=0;f<FamilyCount;++f)for(unsigned r=0;r<ReasonCount;++r) {
            const auto& n=totals[f][r];summary<<familyName(f)<<','<<reasonName(r)<<','<<n.requests<<','<<n.duplicates<<','<<n.admissions<<','<<n.publications<<','<<n.fieldCells<<','<<n.preparedSearches<<','<<n.partialResumes<<'\n';
        }
        std::ofstream histogram(path+".intervals.csv",std::ios::trunc);
        histogram<<"family,reason,interval,count\n";
        for(const auto& [key,count]:intervals) {const auto [family,reason,interval]=key;histogram<<familyName(family)<<','<<reasonName(reason)<<','<<interval<<','<<count<<'\n';}
    }
};
