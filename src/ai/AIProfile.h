// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Opt-in decision diagnostics. Never used by strategy, serialization or scheduling.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <pthread.h>
#endif
namespace AIProfile {
enum class Phase : unsigned {
    MaximaTick, Development, CollectWorld, SelectAction, ScoringCaches, RouteCache,
    UpgradeCandidates, FoodUpgrade, FeedingCandidates, FoodLedger, FoodEvaluate,
    FoodWalk, ResidualBound, ResidualQuality, CortexDecision, CortexObserve, Strategy, LandClearing, MaintenanceClearing, PreemptiveDefense, ReactiveDefense, Farming, Offense, ExplorerAttack, ManageBuildings, FruitFlags, ReconForces, ReconMissions, CheckPhases, Snapshot, Labour, Environment, ResourceAccess, NearbyFarm, ReconUpdate, PlanOffense, ClearingPlan, WorkerCirculation, Count
};
inline constexpr std::array<const char*, unsigned(Phase::Count)> Names{
    "Maxima::tick", "Maxima::development_cycle", "Maxima::collect_development_world",
    "Planner::selectActionIncremental", "Planner::prepareScoringCaches", "Planner::prepareRouteCache",
    "Planner::addUpgradeAndRepairCandidates", "Planner::foodUpgradePasses",
    "Planner::prepareFeedingCandidateSet", "Planner::prepareFoodLedger", "Ledger::evaluate",
    "Ledger::walk", "Ledger::residualUpperBound", "Ledger::residualQuality",
    "Cortex::decide", "Cortex::observeWorld", "Maxima::evaluate_strategy",
    "Maxima::manage_land_clearing", "Maxima::update_maintenance_clearing_areas",
    "Maxima::update_preemptive_defense", "Maxima::compute_defense_flag_positioning",
    "Maxima::update_farming", "Maxima::control_offense",
    "Maxima::compute_explorer_flag_attack_positioning", "Maxima::manage_buildings",
    "Maxima::update_fruit_flags", "Maxima::sample_reconnaissance_forces",
    "Maxima::update_reconnaissance_missions", "Maxima::check_phases",
    "Maxima::collect_snapshot", "Maxima::observe_labour", "Maxima::update_environment_model",
    "Maxima::observe_resource_access", "Maxima::nearby_farm_capacity",
    "Maxima::update_reconnaissance", "Maxima::plan_offense",
    "Maxima::build_maintenance_clearing_plan", "Maxima::worker_reachable_circulation"};
enum class Work : unsigned { UpgradeBuildings, CandidateColonies, LedgerConsumers, LedgerMapCells, WalkVisitedCells, Count };
inline constexpr std::array<const char*, unsigned(Work::Count)> WorkNames{
    "upgrade_buildings", "candidate_colonies", "ledger_consumers", "ledger_map_cells", "food_walk_visited_cells"};
inline constexpr const char* AINames[]{"none","numbi","castor","warrush","econo","nicowar","cortex","maxima","cabino","javascript"};
inline std::uint64_t wallNow() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(__EMSCRIPTEN__)
inline constexpr bool HasThreadCPU = true;
inline std::uint64_t cpuNow() noexcept {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)!=0) return 0;
    return std::uint64_t(value.tv_sec)*1000000000ULL+value.tv_nsec;
}
#else
inline constexpr bool HasThreadCPU = false;
inline std::uint64_t cpuNow() noexcept { return 0; }
#endif
struct Metric { std::uint64_t calls=0, wall=0, self=0; };
class Scope;
struct Data {
    std::array<Metric,unsigned(Phase::Count)> phases{};
    std::array<std::uint64_t,unsigned(Work::Count)> work{};
    Scope* top=nullptr;
    std::uint64_t childWall=0;
};
inline thread_local Data storage;
inline thread_local Data* current=nullptr;
class Session;
inline std::atomic<Session*> active{nullptr};
class Scope {
    Data* data=nullptr;
    Scope* parent=nullptr;
    Phase phase;
    std::uint64_t start=0, children=0;
public:
    explicit Scope(Phase p) noexcept : data(current),phase(p) {
        if (!data) return;
        parent=data->top; data->top=this; ++data->phases[unsigned(phase)].calls; start=wallNow();
    }
    ~Scope() noexcept {
        if (!data) return;
        const auto elapsed=wallNow()-start;
        auto& metric=data->phases[unsigned(phase)];metric.wall+=elapsed;metric.self+=elapsed-children;
        data->top=parent;
        if (parent) parent->children+=elapsed; else data->childWall+=elapsed;
    }
    Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
};
inline void count(Phase p) noexcept { if (current) ++current->phases[unsigned(p)].calls; }
inline void work(Work p,std::uint64_t n) noexcept { if (current) current->work[unsigned(p)]+=n; }
class Session {
    std::ofstream out;
    std::mutex mutex;
    std::atomic<bool> gated{false};
    std::filesystem::path directory;
public:
    const std::uint64_t minimumNs, firstTick, gateTick;
    const unsigned gatePlayer;
    explicit Session(const std::filesystem::path& dir,std::uint64_t minimum,std::uint64_t first,
                     unsigned player=~0u,std::uint64_t tick=~std::uint64_t(0))
        : out(dir/"ai-decisions.jsonl"),directory(dir),minimumNs(minimum),firstTick(first),gateTick(tick),gatePlayer(player) {
        if (!out) throw std::runtime_error("Cannot open AI decision profile");
        if (active.load()) throw std::logic_error("AI profile session already active");
        active.store(this);
    }
    ~Session() { active.store(nullptr); }
    // The owning session must outlive all decision jobs. Only the gated diagnostic
    // run waits here; its artificial pause is excluded from decision timing.
    std::uint64_t gate(unsigned player,std::uint64_t tick) {
        if (player!=gatePlayer || tick!=gateTick || gated.exchange(true)) return 0;
        std::filesystem::remove(directory/"ai-profile-release");
        std::filesystem::remove(directory/"ai-profile-finish");
        std::filesystem::remove(directory/"ai-profile-done.json");
        { std::ofstream ready(directory/"ai-profile-ready.json");
          ready << "{\"player\":"<<player<<",\"observed_tick\":"<<tick<<"}\n";
          if (!ready) throw std::runtime_error("Cannot write AI profile gate marker"); }
        const auto start=wallNow();
        while (!std::filesystem::exists(directory/"ai-profile-release")) {
            if (wallNow()-start>60000000000ULL) throw std::runtime_error("AI profile gate timed out after 60 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return wallNow()-start;
    }
    void endGate(unsigned player,std::uint64_t tick) noexcept {
        // Keep this worker out of subsequent decisions until sampling finishes.
        // Both diagnostic pauses are outside the exported body wall/CPU interval.
        try {
            { std::ofstream done(directory/"ai-profile-done.json");
              done<<"{\"player\":"<<player<<",\"observed_tick\":"<<tick<<"}\n"; }
            const auto start=wallNow();
            while (!std::filesystem::exists(directory/"ai-profile-finish") && wallNow()-start<120000000000ULL)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } catch (...) { /* A failed diagnostic marker must not terminate a worker. */ }
    }
    void write(unsigned ai,unsigned player,unsigned team,std::uint64_t tick,std::uint64_t due,
               std::uint64_t sequence,std::uint64_t wall,std::uint64_t cpu,std::uint64_t gateNs,const Data& data) noexcept {
        if (wall<minimumNs && !gateNs) return;
        std::lock_guard lock(mutex);
        out << "{\"schema_version\":1,\"ai\":\""<<(ai<10?AINames[ai]:"unknown")<<"\",\"player\":"<<player
            <<",\"team\":"<<team<<",\"observed_tick\":"<<tick<<",\"scheduled_tick\":"<<due
            <<",\"poll_sequence\":"<<sequence<<",\"wall_ns\":"<<wall<<",\"thread_cpu_ns\":";
        if (HasThreadCPU) out<<cpu;else out<<"null";
        out<<",\"gate_pause_ns\":"<<gateNs<<",\"unattributed_wall_ns\":"<<(wall-data.childWall)<<",\"phases\":{";
        bool comma=false;
        for (unsigned i=0;i<data.phases.size();++i) {
            const auto& m=data.phases[i];if(!m.calls)continue;
            if(comma)out<<',';comma=true;
            out<<'"'<<Names[i]<<"\":{\"calls\":"<<m.calls<<",\"wall_ns\":"<<m.wall<<",\"self_ns\":"<<m.self
               <<",\"timed\":"<<(i==unsigned(Phase::ResidualBound)?"false":"true")<<'}';
        }
        out<<"},\"work\":{";
        for(unsigned i=0;i<data.work.size();++i){if(i)out<<',';out<<'"'<<WorkNames[i]<<"\":"<<data.work[i];}
        out<<"}}\n";out.flush();
    }
};
class Decision {
    Session* session=nullptr;
    unsigned ai,player,team;
    std::uint64_t tick,due,sequence,start=0,cpuStart=0,gateNs=0;
    char previousName[64]{};
    bool named=false;
public:
    Decision(unsigned a,unsigned p,unsigned t,std::uint64_t observed,std::uint64_t scheduled,std::uint64_t poll)
        :session(active.load(std::memory_order_relaxed)),ai(a),player(p),team(t),tick(observed),due(scheduled),sequence(poll) {
        if(!session || tick<session->firstTick){session=nullptr;return;}
        if(current)throw std::logic_error("Nested profiled AI dispatch");
        if(player==session->gatePlayer && tick==session->gateTick) {
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
            if(pthread_getname_np(pthread_self(),previousName,sizeof(previousName))==0) {
                const auto name="aip"+std::to_string(player)+"t"+std::to_string(tick);
#if defined(__APPLE__)
                named=pthread_setname_np(name.c_str())==0;
#else
                named=pthread_setname_np(pthread_self(),name.substr(0,15).c_str())==0;
#endif
            }
#endif
            gateNs=session->gate(player,tick);
        }
        storage={};current=&storage;cpuStart=cpuNow();start=wallNow();
    }
    ~Decision() noexcept {
        if(!session)return;
        const auto wall=wallNow()-start;const auto cpu=cpuNow()-cpuStart;current=nullptr;
        session->write(ai,player,team,tick,due,sequence,wall,cpu,gateNs,storage);
        if(gateNs) session->endGate(player,tick);
#if defined(__APPLE__)
        if(named)pthread_setname_np(previousName);
#elif defined(__linux__) && !defined(__ANDROID__)
        if(named)pthread_setname_np(pthread_self(),previousName);
#endif
    }
    Decision(const Decision&)=delete;Decision& operator=(const Decision&)=delete;
};
} // namespace AIProfile
