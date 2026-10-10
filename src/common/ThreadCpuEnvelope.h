// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <climits>
#include <mutex>
#include <utility>
#if defined(__linux__)
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace glob2
{
enum class CpuThreadRole : unsigned { Owner, Worker, Coordinator, OtherOwned };
struct CpuClockSample { std::uint64_t ns=0; bool valid=false; };
#if defined(__linux__)
inline CpuClockSample readCpuClock(clockid_t clock) noexcept {
    timespec value{};
    if(clock_gettime(clock,&value) || value.tv_sec<0 || value.tv_nsec<0 || value.tv_nsec>=1000000000 ||
       std::uint64_t(value.tv_sec)>(UINT64_MAX-std::uint64_t(value.tv_nsec))/1000000000ull)return {};
    return {std::uint64_t(value.tv_sec)*1000000000ull+std::uint64_t(value.tv_nsec),true};
}
#endif
struct CpuThreadClock {
    std::uint64_t tid=0,resolutionNs=0;
    std::int64_t handle=0;
    bool valid=false;
};
// Injectable documented clocks, without /proc scans or encoded Linux clock IDs.
// Callback state and registry must outlive every envelope/registration.
struct CpuClockReader {
    void* context=nullptr;
    CpuClockSample (*process)(void*) noexcept=nullptr;
    CpuClockSample (*thread)(void*,std::int64_t) noexcept=nullptr;
    CpuThreadClock (*current)(void*) noexcept=nullptr;
    CpuClockSample (*self)(void*) noexcept=nullptr;
    std::uint64_t processResolutionNs=0;
    static CpuClockReader native() noexcept;
};

class ProcessCpuEnvelope;
// Development diagnostic only. Register once at thread startup, before work;
// destroy all of that thread's leases before thread exit. Slots are fixed and
// overflow stays unidentified CPU, conservatively charged to the GPU. The owner
// never scans tasks or samples other threads at a tick boundary.
class CpuClockRegistry {
public:
    static constexpr std::size_t MaxThreads=256;
    class Lease {
        friend class CpuClockRegistry;
        CpuClockRegistry* registry=nullptr;
        std::size_t index=0;
        std::uint64_t generation=0;
        unsigned role=0;
        Lease(CpuClockRegistry* r,std::size_t i,std::uint64_t g,unsigned v) noexcept
            :registry(r),index(i),generation(g),role(v){}
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept { *this=std::move(other); }
        Lease& operator=(Lease&& other) noexcept {
            if(this!=&other){reset();registry=std::exchange(other.registry,nullptr);
                index=other.index;generation=other.generation;role=other.role;}return *this;
        }
        ~Lease(){reset();}
        explicit operator bool() const noexcept {return registry!=nullptr;}
        void reset() noexcept;
    };
private:
    friend class ProcessCpuEnvelope;
    struct Slot {
        std::mutex mutex;
        CpuThreadClock clock;
        std::array<unsigned,4> references{};
        std::uint64_t generation=0;
        bool active=false;
        unsigned roles() const noexcept {
            unsigned bits=0;for(unsigned i=0;i<references.size();++i)if(references[i])bits|=1u<<i;return bits;
        }
    };
    std::array<Slot,MaxThreads> slots;
    std::mutex registrations;
    CpuClockReader reader;
    void retire(std::size_t index,std::uint64_t generation,unsigned role) noexcept {
        auto& slot=slots[index];std::lock_guard lock(slot.mutex);
        if(slot.active && slot.generation==generation && slot.references[role]) {
            --slot.references[role];if(!slot.roles())slot.active=false;
        }
    }
public:
    explicit CpuClockRegistry(CpuClockReader clocks=CpuClockReader::native()) noexcept :reader(clocks){}
    CpuClockRegistry(const CpuClockRegistry&)=delete;
    CpuClockRegistry& operator=(const CpuClockRegistry&)=delete;
    // Aliases (e.g. owner/configuration owner) share one slot and one CPU delta.
    Lease registerCurrent(CpuThreadRole role) noexcept {
        if(!reader.current || unsigned(role)>=4)return {};
        const auto clock=reader.current(reader.context);
        if(!clock.valid || !clock.tid || !clock.resolutionNs)return {};
        std::lock_guard registrationsLock(registrations);
        std::size_t empty=MaxThreads;
        for(std::size_t i=0;i<slots.size();++i) {
            auto& slot=slots[i];std::lock_guard lock(slot.mutex);
            if(!slot.active){if(empty==MaxThreads && slot.generation!=UINT64_MAX)empty=i;continue;}
            if(slot.clock.tid!=clock.tid)continue;
            if(slot.clock.handle!=clock.handle || slot.references[unsigned(role)]==UINT_MAX)return {};
            ++slot.references[unsigned(role)];return Lease(this,i,slot.generation,unsigned(role));
        }
        if(empty==MaxThreads)return {};
        auto& slot=slots[empty];std::lock_guard lock(slot.mutex);
        slot.clock=clock;slot.references={};slot.references[unsigned(role)]=1;
        ++slot.generation;slot.active=true;return Lease(this,empty,slot.generation,unsigned(role));
    }
    // Caller charges fixed registry/envelope storage before allocating it.
    static constexpr std::size_t storageBytes() noexcept {return sizeof(CpuClockRegistry);}
};
inline void CpuClockRegistry::Lease::reset() noexcept {
    if(auto* value=std::exchange(registry,nullptr))value->retire(index,generation,role);
}

struct ProcessCpuEnvelopeResult {
    struct Thread {
        std::uint64_t tid=0,generation=0,cpuNs=0;
        unsigned roles=0; // Aliases overlap. Never sum CPU once per role bit.
    };
    std::array<Thread,CpuClockRegistry::MaxThreads> threads{};
    std::size_t threadCount=0,omittedThreads=0;
    std::uint64_t processCpuNs=0,knownInnerCpuNs=0,unknownUpperCpuNs=0;
    std::uint64_t samplerCpuNs=0,maxResolutionNs=0;
    bool valid=false,churn=false;
    // A completed narrow window cannot establish deferred driver-tail capture.
    // This API intentionally NEVER qualifies automatic promotion.
    bool attributionComplete=false;
};
class ProcessCpuEnvelope {
public:
    enum class Progress { Starting, Running, Finishing, Complete, Invalid };
private:
    struct Start {
        std::uint64_t cpuNs=0,generation=0,tid=0;
        unsigned roles=0;
        bool valid=false;
    };
    CpuClockRegistry& registry;
    std::array<Start,CpuClockRegistry::MaxThreads> starts{};
    CpuClockSample processStart;
    ProcessCpuEnvelopeResult result;
    Progress phase=Progress::Invalid;
    std::size_t cursor=0;
    void accountSampler(CpuClockSample start) noexcept {
        if(!registry.reader.self)return;
        const auto end=registry.reader.self(registry.reader.context);
        if(start.valid && end.valid && end.ns>=start.ns &&
           end.ns-start.ns<=UINT64_MAX-result.samplerCpuNs)result.samplerCpuNs+=end.ns-start.ns;
    }
public:
    // Background-only: process FIRST, then known start clocks in <=64-slot
    // advances. At finish known clocks FIRST, then process LAST. Every deducted
    // role interval is thus inside the outer process interval. All unidentified
    // CPU (including driver threads and boundary work) stays in unknownUpper.
    explicit ProcessCpuEnvelope(CpuClockRegistry& clocks) noexcept :registry(clocks) {
        const auto cpu=registry.reader.self ? registry.reader.self(registry.reader.context) : CpuClockSample{};
        if(registry.reader.process && registry.reader.thread && registry.reader.processResolutionNs) {
            processStart=registry.reader.process(registry.reader.context);
            result.maxResolutionNs=registry.reader.processResolutionNs;
            if(processStart.valid)phase=Progress::Starting;
        }
        accountSampler(cpu);
    }
    bool finish() noexcept {
        if(phase!=Progress::Running)return false;cursor=0;phase=Progress::Finishing;return true;
    }
    Progress advance() noexcept {
        if(phase!=Progress::Starting && phase!=Progress::Finishing)return phase;
        const auto cpu=registry.reader.self ? registry.reader.self(registry.reader.context) : CpuClockSample{};
        const auto stop=std::min(cursor+64,starts.size());
        for(;cursor<stop;++cursor) {
            auto& slot=registry.slots[cursor];auto& start=starts[cursor];
            std::lock_guard lock(slot.mutex); // Retirement completes before exit.
            if(phase==Progress::Starting) {
                if(!slot.active)continue;
                const auto value=registry.reader.thread(registry.reader.context,slot.clock.handle);
                if(!value.valid){start.generation=slot.generation;++result.omittedThreads;continue;}
                start={value.ns,slot.generation,slot.clock.tid,slot.roles(),true};
                result.maxResolutionNs=std::max(result.maxResolutionNs,slot.clock.resolutionNs);
            } else if(!start.valid) {
                if(slot.active && slot.generation!=start.generation){++result.omittedThreads;result.churn=true;}
            } else {
                if(!slot.active || slot.generation!=start.generation || slot.clock.tid!=start.tid) {
                    ++result.omittedThreads;result.churn=true;continue;
                }
                const auto value=registry.reader.thread(registry.reader.context,slot.clock.handle);
                if(!value.valid || value.ns<start.cpuNs){++result.omittedThreads;continue;}
                const auto delta=value.ns-start.cpuNs;
                if(delta>UINT64_MAX-result.knownInnerCpuNs){phase=Progress::Invalid;break;}
                result.knownInnerCpuNs+=delta;
                result.threads[result.threadCount++]={start.tid,start.generation,delta,start.roles};
                if(start.roles!=slot.roles())result.churn=true;
            }
        }
        if(cursor==starts.size()) {
            if(phase==Progress::Starting)phase=Progress::Running;
            else if(phase==Progress::Finishing) {
                const auto end=registry.reader.process(registry.reader.context);
                if(!end.valid || end.ns<processStart.ns || end.ns-processStart.ns<result.knownInnerCpuNs)
                    phase=Progress::Invalid;
                else {
                    result.processCpuNs=end.ns-processStart.ns;
                    result.unknownUpperCpuNs=result.processCpuNs-result.knownInnerCpuNs;
                    result.valid=true;phase=Progress::Complete;
                }
            }
        }
        accountSampler(cpu);return phase;
    }
    Progress progress() const noexcept {return phase;}
    const ProcessCpuEnvelopeResult& metrics() const noexcept {return result;}
    static constexpr std::size_t storageBytes() noexcept {return sizeof(ProcessCpuEnvelope);}
    // Caller measures all lifecycle CPU/storage against optional budgets,
    // including constructor/destructor/registry sampling. samplerCpuNs is only
    // component telemetry; do not add it twice to coordinator lifecycle CPU.
};

inline CpuClockReader CpuClockReader::native() noexcept {
    CpuClockReader out;
#if defined(__linux__) && defined(CLOCK_PROCESS_CPUTIME_ID) && defined(CLOCK_THREAD_CPUTIME_ID)
    // Function-pointer callbacks do not retain capturing objects or driver state.
    out.process=[](void*) noexcept -> CpuClockSample {
        return readCpuClock(CLOCK_PROCESS_CPUTIME_ID);
    };
    out.thread=[](void*,std::int64_t handle) noexcept -> CpuClockSample {
        return readCpuClock(clockid_t(handle));
    };
    out.self=[](void*) noexcept -> CpuClockSample {
        return readCpuClock(CLOCK_THREAD_CPUTIME_ID);
    };
    out.current=[](void*) noexcept -> CpuThreadClock {
        clockid_t id{};timespec resolution{};const auto tid=::syscall(SYS_gettid);
        if(tid<=0 || pthread_getcpuclockid(pthread_self(),&id) || clock_getres(id,&resolution) || resolution.tv_sec<0 ||
           resolution.tv_nsec<0 || resolution.tv_nsec>=1000000000 ||
           std::uint64_t(resolution.tv_sec)>(UINT64_MAX-std::uint64_t(resolution.tv_nsec))/1000000000ull)return {};
        return {std::uint64_t(tid),std::uint64_t(resolution.tv_sec)*1000000000ull+std::uint64_t(resolution.tv_nsec),std::int64_t(id),true};
    };
    timespec resolution{};
    if(!clock_getres(CLOCK_PROCESS_CPUTIME_ID,&resolution) && resolution.tv_sec>=0 && resolution.tv_nsec>=0 &&
       resolution.tv_nsec<1000000000 &&
       std::uint64_t(resolution.tv_sec)<=(UINT64_MAX-std::uint64_t(resolution.tv_nsec))/1000000000ull)
        out.processResolutionNs=std::uint64_t(resolution.tv_sec)*1000000000ull+std::uint64_t(resolution.tv_nsec);
#endif
    return out;
}
}
