// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <PerformanceTelemetry.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace benchmark_diagnostics
{
// Warm-boundary inventory only: never scan /proc in the simulation loop.
inline nlohmann::json threads()
{
    nlohmann::json out={{"available",false},{"threads",nlohmann::json::array()},
        {"note","Linux stat CPU has clock-tick resolution; exited threads may be absent at end"}};
#if defined(__linux__)
    const long hz=sysconf(_SC_CLK_TCK);
    if(hz<=0) return out;
    out["clock_ticks_per_second"]=hz; out["owner_tid"]=getpid();
    std::error_code error;
    const std::filesystem::directory_iterator entries("/proc/self/task",error);
    if(error) { out["error"]=error.message(); return out; }
    unsigned failed=0;
    for(const auto& entry:entries) {
        std::ifstream stat(entry.path()/"stat"); std::string line;
        if(!std::getline(stat,line)) { ++failed; continue; }
        const auto first=line.find('('),last=line.rfind(')');
        if(first==std::string::npos || last==std::string::npos || last+2>=line.size()) { ++failed; continue; }
        std::istringstream values(line.substr(last+2));
        std::string field; std::uint64_t user=0,system=0,started=0;
        bool valid=true;
        try {
            // Tokens begin at kernel field3; utime14,stime15,starttime22.
            for(unsigned i=0;i<=19;++i) {
                if(!(values>>field)) { valid=false; break; }
                if(i==11) user=std::stoull(field);
                if(i==12) system=std::stoull(field);
                if(i==19) started=std::stoull(field);
            }
            if(!valid) { ++failed; continue; }
            const auto ns=[&](std::uint64_t ticks) { return ticks/unsigned(hz)*1000000000ull+ticks%unsigned(hz)*1000000000ull/unsigned(hz); };
            nlohmann::json row={{"tid",std::stoul(entry.path().filename().string())},
                {"name",line.substr(first+1,std::min<std::size_t>(64,last-first-1))},
                {"start_ticks",started},{"user_cpu_ns",ns(user)},{"system_cpu_ns",ns(system)}};
            std::ifstream status(entry.path()/"status");
            while(std::getline(status,line)) if(line.starts_with("Cpus_allowed_list:")) row["allowed_cpus"]=line.substr(18,256);
            out["threads"].push_back(std::move(row));
        } catch(const std::exception&) { ++failed; }
    }
    out["available"]=true; out["failed_threads"]=failed;
#endif
    return out;
}
inline nlohmann::json ownerScopes()
{
    auto out=nlohmann::json::array();
    const auto& c=PerformanceTelemetry::collector();
    for(unsigned i=0;i<PerformanceTelemetry::ScopeCount;++i) {
        const auto& a=c.window[i]; const auto& b=c.total[i];
        out.push_back({{"scope",PerformanceTelemetry::scopeName(PerformanceTelemetry::Id(i))},
            {"inclusive_cpu_ns",a.cpu+b.cpu},{"self_cpu_ns",a.cpuSelf+b.cpuSelf},
            {"cpu_samples",a.cpuSamples+b.cpuSamples},{"self_complete",a.cpuSelfComplete&&b.cpuSelfComplete&&a.cpuSamples+b.cpuSamples==a.time.count+b.time.count}});
    }
    return out;
}
}
