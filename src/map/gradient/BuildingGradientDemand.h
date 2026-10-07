// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientConstants.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

// Simulation-owner demand observation. No collector is visible to private jobs.
// Equal requests within a tick are counted together; extension work remains separate.
class BuildingGradientDemand
{
public:
    struct Colony { unsigned units=0, buildings=0; };
    struct Request {
        int team=0, gid=0, route=0, swim=0, resource=-1;
        std::uint32_t identity=0, captured=0, generation=0;
        std::uint64_t epoch=0, popped=0;
        std::string type, caller;
        int cost=-1, before=-1, after=-1;
        bool full=false, reachable=false, forbidden=false, extended=false, complete=false;
    };
private:
    using Key=std::tuple<std::uint64_t,std::string,int,int,int,bool,bool,bool,bool,bool>;
    struct Row { Request request; std::uint64_t count=0, popped=0; };
    using Field=std::tuple<int,std::uint32_t,int,int,int,std::uint32_t,std::uint32_t>;
    std::map<Field,std::uint64_t> fields;
    std::map<Field,int> finishedCosts;
    std::map<Key,Row> rows;
    std::array<Colony,32> colonies{};
    std::ofstream requestsFile,ticksFile;
    std::uint64_t tick=0,nextEpoch=0,requests=0,dropped=0;
    bool active=false;
    static void quoted(std::ostream& out,const std::string& value) {
        out << '"'; for(char c:value) { if(c=='"') out << '"'; out << c; } out << '"';
    }
public:
    explicit BuildingGradientDemand(const std::string& prefix)
      : requestsFile(prefix+"-requests.csv"),ticksFile(prefix+"-ticks.csv") {
        if(!requestsFile || !ticksFile) throw std::runtime_error("cannot open building gradient demand telemetry");
        requestsFile << "tick,epoch,team,gid,identity,type,route,swim,resource,captured_tick,generation,colony_units,colony_buildings,caller,cost,before_cost,after_cost,full,reachable,forbidden,extended,complete,requests,popped\n";
        ticksFile << "tick,requests,rows,dropped_requests\n";
    }
    void begin(std::uint64_t value,const std::array<Colony,32>& counts) {
        tick=value;colonies=counts;rows.clear();requests=dropped=0;active=true;
    }
    void record(Request request,std::uint64_t* searchEpoch) {
        if(!active)return;
        ++requests;
        if(searchEpoch) { if(!*searchEpoch)*searchEpoch=++nextEpoch;request.epoch=*searchEpoch; }
        else {
            auto [it,inserted]=fields.try_emplace(Field{request.gid,request.identity,request.route,request.swim,request.resource,request.captured,request.generation},0);
            if(inserted)it->second=++nextEpoch;request.epoch=it->second;
        }
        Key key{request.epoch,request.caller,request.cost,request.before,request.after,request.full,request.reachable,request.forbidden,request.extended,request.complete};
        auto it=rows.find(key);
        if(it==rows.end()) {
            if(rows.size()==65536) {++dropped;return;}
            it=rows.emplace(std::move(key),Row{request,0,0}).first;
        }
        ++it->second.count;it->second.popped+=request.popped;
    }
    void end() {
        if(!active)return;active=false;
        ticksFile << tick << ',' << requests << ',' << rows.size() << ',' << dropped << '\n';
        for(const auto& [key,row]:rows) {
            const auto& r=row.request;const auto& colony=colonies.at(r.team);
            requestsFile << tick << ',' << r.epoch << ',' << r.team << ',' << r.gid << ',' << r.identity << ',';
            quoted(requestsFile,r.type);
            requestsFile << ',' << r.route << ',' << r.swim << ',' << r.resource << ',' << r.captured << ',' << r.generation
              << ',' << colony.units << ',' << colony.buildings << ',';quoted(requestsFile,r.caller);
            requestsFile << ',' << r.cost << ',' << r.before << ',' << r.after << ',' << r.full << ',' << r.reachable
              << ',' << r.forbidden << ',' << r.extended << ',' << r.complete << ',' << row.count << ',' << row.popped << '\n';
        }
    }
    int finishedCost(const Request& r,const std::uint16_t* field,std::size_t cells) {
        if(!field)return -1;
        const Field key{r.gid,r.identity,r.route,r.swim,r.resource,r.captured,r.generation};
        auto found=finishedCosts.find(key);
        if(found!=finishedCosts.end())return found->second;
        // Only complete fields lack a continuation. Scanning is diagnostic-only,
        // once per identity/version, and never extends a live search.
        int maximum=0;
        for(std::size_t i=0;i<cells;++i)if(field[i]>GRADIENT_UNREACHABLE)maximum=std::max(maximum,GRADIENT_AT_GOAL-int(field[i]));
        return finishedCosts.emplace(key,maximum+1).first->second;
    }
    void flush() {requestsFile.flush();ticksFile.flush();}
};
