#!/usr/bin/env python3
"""Source-only experimental native weighted placement, applied once to copied054 local source."""
from pathlib import Path
P=Path(__file__).resolve().parent;S=P/'weighted-source';p=S/'src/common/ComputeExecutor.h';t=p.read_text()
def replace(a,b):
 global t
 assert t.count(a)==1,(a[:100],t.count(a));t=t.replace(a,b)
replace('#include <cstring>','#include <cstring>\n#if defined(__linux__)\n#include <time.h>\n#endif')
replace('\tstruct Group\n', '''    // Stable producer identities, never callback addresses or diagnostic tags.
    enum class JobClass : unsigned { Generic, AI, PeriodicGradient, BuildingGradient, Growth, Statistics, Turret, Hiring, Count };
\tstruct Group
''')
replace('\t\tunsigned lane = NoLane;\n\t};', '\t\tunsigned lane = NoLane;\n        JobClass jobClass = JobClass::Generic;\n\t};')
replace('unsigned lane=NoLane; Dispatch* next=nullptr;', 'unsigned lane=NoLane; unsigned costClass=0; std::size_t charged=0; std::uint64_t reservedNs=0; Dispatch* next=nullptr;')
replace('bool optionalBusy=false;', 'bool optionalBusy=false;\n        std::atomic<std::uint64_t> outstandingNs{0}; // queued plus executing callback reservations')
replace('    bool stealing=false, laneAffinity=false;', '''    bool stealing=false, laneAffinity=false;
    enum class CostPolicy { Bare, Fixed, TwoChoice, Last, EWMA, ClockOnly, ObserveOnly };
    CostPolicy costPolicy=CostPolicy::Bare;
    static constexpr std::uint64_t MaxEstimateNs=1ull<<30;
    static constexpr std::size_t MaxResearchBatchJobs=1u<<24;
    // Whole-callback CPU priors, explicit research configuration, not measured fits.
    static constexpr std::array<std::uint64_t,8> fixedNs={100000,1000000,2000000,1000000,500000,500000,1000000,1000000};
    std::array<std::atomic<std::uint64_t>,16> costSamples{}; // separate continuation classes
    bool clocksCost() const { return costPolicy==CostPolicy::Last || costPolicy==CostPolicy::EWMA || costPolicy==CostPolicy::ClockOnly || costPolicy==CostPolicy::ObserveOnly; }
    bool accountsCost() const { return costPolicy!=CostPolicy::Bare && costPolicy!=CostPolicy::ClockOnly; }
    static std::uint64_t callbackCpuNs() {
#if defined(__linux__)
        timespec value{};
        if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)) std::terminate();
        return std::uint64_t(value.tv_sec)*1000000000ull+std::uint64_t(value.tv_nsec);
#else
        // Clock-based modes are rejected by configure on unsupported runtimes.
        std::terminate();
#endif
    }
    std::uint64_t estimateCost(unsigned kind) const {
        if(costPolicy==CostPolicy::Last || costPolicy==CostPolicy::EWMA || costPolicy==CostPolicy::ObserveOnly) {
            const auto sampled=costSamples[kind].load(std::memory_order_relaxed);
            return sampled ? sampled : fixedNs[kind%8];
        }
        return fixedNs[kind%8];
    }
    void observeCost(unsigned kind,std::uint64_t elapsed) {
        if(costPolicy==CostPolicy::ClockOnly) return;
        elapsed=std::clamp<std::uint64_t>(elapsed,1,MaxEstimateNs);
        if(costPolicy!=CostPolicy::EWMA) costSamples[kind].store(elapsed,std::memory_order_relaxed);
        else {
            auto old=costSamples[kind].load(std::memory_order_relaxed);
            while(!costSamples[kind].compare_exchange_weak(old,old ? (old*7+elapsed)/8 : elapsed,std::memory_order_relaxed)) {}
        }
    }
    void transferReservation(Dispatch& work,std::size_t worker) {
        if(!work.reservedNs || work.charged==worker) return;
        // Charge destination before debit; approximate placement may briefly
        // overestimate, never omit running CPU. No queue/global lock needed.
        queues[worker]->outstandingNs.fetch_add(work.reservedNs,std::memory_order_relaxed);
        const auto previous=queues[work.charged]->outstandingNs.fetch_sub(work.reservedNs,std::memory_order_relaxed);
        if(previous<work.reservedNs) std::terminate();
        work.charged=worker;
    }
    void releaseReservation(const Dispatch& work) {
        if(!work.reservedNs) return;
        const auto previous=queues[work.charged]->outstandingNs.fetch_sub(work.reservedNs,std::memory_order_relaxed);
        if(previous<work.reservedNs) std::terminate();
    }''')
replace('''        const auto index=laneAffinity && work.lane!=NoLane ? work.lane%queues.size() : roundRobin;
        auto& queue=*queues[index];''','''        auto index=laneAffinity && work.lane!=NoLane ? work.lane%queues.size() : roundRobin;
        const bool weighted=costPolicy==CostPolicy::Fixed || costPolicy==CostPolicy::TwoChoice || costPolicy==CostPolicy::Last || costPolicy==CostPolicy::EWMA;
        if(weighted) {
            if(costPolicy==CostPolicy::TwoChoice && queues.size()>1) {
                const auto second=(roundRobin+1+(dispatchCursor.load(std::memory_order_relaxed)/queues.size())%(queues.size()-1))%queues.size();
                if(queues[second]->outstandingNs.load(std::memory_order_relaxed)<queues[index]->outstandingNs.load(std::memory_order_relaxed)) index=second;
            } else for(std::size_t n=1;n<queues.size();++n) {
                const auto candidate=(roundRobin+n)%queues.size();
                if(queues[candidate]->outstandingNs.load(std::memory_order_relaxed)<queues[index]->outstandingNs.load(std::memory_order_relaxed)) index=candidate;
            }
        }
        work.charged=index;
        work.reservedNs=accountsCost() ? estimateCost(work.costClass) : 0;
        auto& queue=*queues[index];
        if(work.reservedNs) queue.outstandingNs.fetch_add(work.reservedNs,std::memory_order_relaxed);''')
# Copy dispatch then transfer while protecting unique queue node lifetime. No callback can complete this job before return.
replace('if(popDispatch(index,work)) return true;','if(popDispatch(index,work)) { transferReservation(work,index); return true; }')
assert t.count('work=*item; work.next=nullptr; return true;')==2
t=t.replace('work=*item; work.next=nullptr; return true;', 'work=*item; work.next=nullptr; transferReservation(work,index); return true;')
replace('item.offset=slot.continuations[job.index].invoke ? 0 : job.index-slot.starts[g];','item.offset=slot.continuations[job.index].invoke ? 0 : job.index-slot.starts[g];\n            item.costClass=unsigned(slot.groups[g].jobClass)+(slot.continuations[job.index].invoke ? 8u : 0u);')
replace('''\t\tstd::exception_ptr failure;
\t\ttry { work.job.invoke(work.job.context, work.offset); }
\t\tcatch (...) { failure = std::current_exception(); }''','''        const bool timedCost=clocksCost();
        const auto costStart=timedCost ? callbackCpuNs() : 0;
\t\tstd::exception_ptr failure;
\t\ttry { work.job.invoke(work.job.context, work.offset); }
\t\tcatch (...) { failure = std::current_exception(); }
        if(timedCost) observeCost(work.costClass,callbackCpuNs()-costStart);
        // End this CPU slice before any continuation/lane publication and
        // before terminal count can allow owner slot reuse. Async wait is free.
        releaseReservation(work);''')
replace('''        const auto* dispatchPolicy=std::getenv("GLOB2_DEFERRED_DISPATCH");''','''        const auto* cost=std::getenv("GLOB2_WEIGHTED_POLICY");
        costPolicy=CostPolicy::Bare;
        if(cost && *cost) {
            if(!std::strcmp(cost,"fixed") || !std::strcmp(cost,"fixed-no-steal")) costPolicy=CostPolicy::Fixed;
            else if(!std::strcmp(cost,"two-choice")) costPolicy=CostPolicy::TwoChoice;
            else if(!std::strcmp(cost,"last")) costPolicy=CostPolicy::Last;
            else if(!std::strcmp(cost,"ewma")) costPolicy=CostPolicy::EWMA;
            else if(!std::strcmp(cost,"clock-only")) costPolicy=CostPolicy::ClockOnly;
            else if(!std::strcmp(cost,"observe-only")) costPolicy=CostPolicy::ObserveOnly;
            else if(std::strcmp(cost,"bare-steal")) throw std::invalid_argument("Unknown weighted research policy");
        }
#if !defined(__linux__)
        if(clocksCost()) throw std::invalid_argument("Research callback CPU clock requires Linux");
#endif
        for(auto& sample:costSamples) sample.store(0,std::memory_order_relaxed);
        const auto* dispatchPolicy=std::getenv("GLOB2_DEFERRED_DISPATCH");''')
replace('''        laneAffinity=dispatchPolicy && !std::strcmp(dispatchPolicy,"lane-affine");''','''        laneAffinity=dispatchPolicy && !std::strcmp(dispatchPolicy,"lane-affine");
        // Weighted experimental arms always retain opportunistic stealing and
        // optional rescue; explicit incompatible static modes fail closed.
        if(cost && *cost) {
            if(dispatchPolicy && *dispatchPolicy && std::strcmp(dispatchPolicy,"steal")) throw std::invalid_argument("Weighted policies require steal dispatch");
            stealing=std::strcmp(cost,"fixed-no-steal")!=0; laneAffinity=false;
        }''')
replace('''\t\t\ttotal += group.count;''','''            if(unsigned(group.jobClass)>=unsigned(JobClass::Count)) throw std::invalid_argument("Unknown research job class");
            //84slots*2^24jobs*2^30ns <2^64; each callback has one reservation.
            if(group.count>MaxResearchBatchJobs-total) throw std::invalid_argument("Research batch job bound exceeded");
\t\t\ttotal += group.count;''')
replace('queues.clear(); dispatchCursor=0;', 'for(auto& queue:queues) if(queue->outstandingNs.load(std::memory_order_relaxed)) std::terminate();\n        queues.clear(); dispatchCursor=0;')
p.write_text(t)
# Explicit producer classes, all existing count/lane/due semantics retained.
changes={
 'src/ai/engine/AIOrderScheduler.cpp':('{1, {callback, &entry}, entry.request.player}','{1, {callback, &entry}, entry.request.player, ComputeExecutor::JobClass::AI}'),
 'src/map/gradient/GradientPipeline.h':('group{1, fn, ComputeExecutor::NoLane}','group{1, fn, ComputeExecutor::NoLane, ComputeExecutor::JobClass::PeriodicGradient}'),
 'src/map/gradient/BuildingGradientPipeline.h':('{1, {&run, job}, ComputeExecutor::NoLane}','{1, {&run, job}, ComputeExecutor::NoLane, ComputeExecutor::JobClass::BuildingGradient}'),
 'src/map/ResourceGrowth.cpp':('group{1, {callback, job}, ComputeExecutor::NoLane}','group{1, {callback, job}, ComputeExecutor::NoLane, ComputeExecutor::JobClass::Growth}'),
 'src/team/stats/TeamStatsPipeline.cpp':('1, {compute, &submitted}, ComputeExecutor::StatisticsLaneBase}','1, {compute, &submitted}, ComputeExecutor::StatisticsLaneBase, ComputeExecutor::JobClass::Statistics}'),
 'src/building/TurretTargetingPipeline.cpp':('{1,{State::Job::run,job.get()}}','{1,{State::Job::run,job.get()},ComputeExecutor::NoLane,ComputeExecutor::JobClass::Turret}'),
 'src/building/HiringPipeline.cpp':('{1, {compute, &b.work[i]}, ComputeExecutor::HiringLaneBase + b.work[i].team}','{1, {compute, &b.work[i]}, ComputeExecutor::HiringLaneBase + b.work[i].team, ComputeExecutor::JobClass::Hiring}')}
for name,(a,b) in changes.items():
 p=S/name;t=p.read_text();assert t.count(a)==1,(name,t.count(a));p.write_text(t.replace(a,b))
print('Prepared weighted source only; not compiled or qualified')
