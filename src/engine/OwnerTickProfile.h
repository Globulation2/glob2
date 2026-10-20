// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Opt-in owner-thread attribution; no diagnostic values affect simulation.
#include "ai/AIProfile.h"
#include <optional>

namespace OwnerTickProfile {
enum class Phase : unsigned {
    Ending, Client, Turn, Commander, Orders, Step, Statistics, Output,
    Observation, CollectChecksums, CaptureWorld, SubmitChecksum, AdmitAI, Count
};
inline constexpr std::array<const char*,unsigned(Phase::Count)> Names{
    "ending", "client", "turn", "commander", "orders", "step", "statistics", "output",
    "observation", "collect_checksums", "capture_world", "submit_checksum", "admit_ai"};
struct Metric { std::uint64_t calls=0,wall=0,cpu=0,selfWall=0,selfCPU=0; };
class Scope;
struct Data { std::array<Metric,unsigned(Phase::Count)> phases{};Scope* top=nullptr; };
inline thread_local Data data;
inline thread_local bool enabled=false;
class Scope {
    bool measured=enabled;
    Scope* parent=nullptr;
    Phase phase;
    std::uint64_t wall=0,cpu=0,childWall=0,childCPU=0;
public:
    explicit Scope(Phase p):phase(p) {
        if(!measured)return;
        parent=data.top;data.top=this;++data.phases[unsigned(phase)].calls;
        cpu=AIProfile::cpuNow();wall=AIProfile::wallNow();
    }
    ~Scope() {
        if(!measured)return;
        const auto elapsed=AIProfile::wallNow()-wall;
        const auto used=AIProfile::cpuNow()-cpu;
        auto& value=data.phases[unsigned(phase)];
        value.wall+=elapsed;value.cpu+=used;
        value.selfWall+=elapsed-childWall;value.selfCPU+=used-childCPU;
        data.top=parent;
        if(parent){parent->childWall+=elapsed;parent->childCPU+=used;}
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
class Session;
inline std::atomic<Session*> active=nullptr;
class Session {
    std::ofstream out;
public:
    const std::uint64_t minimumNs,firstTick;
    Session(const std::filesystem::path& directory,std::uint64_t minimum,std::uint64_t first)
        :out(directory/"owner-ticks.jsonl"),minimumNs(minimum),firstTick(first) {
        if(!out)throw std::runtime_error("Cannot open owner tick profile");
        if(active.load())throw std::logic_error("Owner tick profile already active");
        const auto steady=AIProfile::wallNow();
        const auto system=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream clock(directory/"owner-clock.json");
        clock<<"{\"steady_ns\":"<<steady<<",\"system_ns\":"<<system<<"}\n";
        active.store(this);
    }
    ~Session(){active.store(nullptr);}
    void write(std::uint64_t tick,std::uint64_t started,std::uint64_t wall,std::uint64_t cpu) {
        if(wall<minimumNs)return;
        out<<"{\"schema_version\":1,\"tick_before_step\":"<<tick<<",\"started_steady_ns\":"<<started
            <<",\"wall_ns\":"<<wall<<",\"thread_cpu_ns\":";
        if(AIProfile::HasThreadCPU)out<<cpu;else out<<"null";
        out<<",\"phases\":{";
        bool comma=false;
        for(unsigned i=0;i<data.phases.size();++i) {
            const auto& p=data.phases[i];if(!p.calls)continue;
            if(comma)out<<',';comma=true;
            out<<'"'<<Names[i]<<"\":{\"calls\":"<<p.calls<<",\"wall_ns\":"<<p.wall
                <<",\"self_wall_ns\":"<<p.selfWall<<",\"thread_cpu_ns\":";
            if(AIProfile::HasThreadCPU)out<<p.cpu;else out<<"null";
            out<<",\"self_cpu_ns\":";
            if(AIProfile::HasThreadCPU)out<<p.selfCPU;else out<<"null";
            out<<'}';
        }
        out<<"}}\n";out.flush();
    }
};
class Tick {
    Session* session=nullptr;
    std::uint64_t tick,wall=0,cpu=0;
public:
    explicit Tick(std::uint64_t t):session(active.load(std::memory_order_relaxed)),tick(t) {
        if(!session || tick<session->firstTick){session=nullptr;return;}
        if(enabled)throw std::logic_error("Nested owner tick profile");
        data={};enabled=true;cpu=AIProfile::cpuNow();wall=AIProfile::wallNow();
    }
    ~Tick() {
        if(!session)return;
        const auto elapsed=AIProfile::wallNow()-wall;const auto used=AIProfile::cpuNow()-cpu;
        enabled=false;session->write(tick,wall,elapsed,used);
    }
    Tick(const Tick&)=delete;Tick& operator=(const Tick&)=delete;
};
} // namespace OwnerTickProfile
