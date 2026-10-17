// Private diagnostic instrumentation, never part of the production PR.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string_view>
#include <vector>
namespace attribution {
enum Stage { Owner, Ranking, Primary, Speculative, MapSeed, PrimarySeed, ProactiveSeed, StageCount };
inline constexpr const char* stageNames[]={"owner","ranking","primary","speculative","map_seed","primary_seed","proactive_seed"};
enum Metric { HiringCpu, RankingCpu, PrimaryCpu, SpeculativeCpu, ObserveCpu, MapCpu, MapSeeds, PrivateSeeds, ProactiveSeeds, SpeculativeSeeds, SharedQueries, MissingQueries, CandidateVisits, RankedCandidates, UnitTicks, BuildingTicks, UnitCpu, BuildingCpu, UnitSteps, AiCpu, HiringJobs, MetricCount };
inline constexpr const char* metricNames[]={"hiring_cpu_ns","ranking_cpu_ns","primary_cpu_ns","speculative_cpu_ns","observe_cpu_ns","map_cpu_ns","map_seeds","private_seeds","proactive_seeds","speculative_seeds","ranking_shared_queries","ranking_missing_queries","candidate_visits","ranked_candidates","unit_ticks","building_ticks","unit_cpu_ns","building_cpu_ns","unit_steps","ai_cpu_ns","hiring_jobs"};
inline std::array<std::atomic<std::uint64_t>,MetricCount> metrics{};
inline std::array<std::atomic<std::uint64_t>,StageCount> advanceCpu{},entries{},calls{};
inline thread_local Stage stage=Owner;
inline thread_local unsigned domain=0;
inline std::array<std::atomic<std::uint64_t>,3> ownerDomainCpu{};
struct DomainScope{unsigned old=domain;explicit DomainScope(unsigned d){domain=d;}~DomainScope(){domain=old;}};
inline void add(Metric m,std::uint64_t n=1){metrics[m].fetch_add(n,std::memory_order_relaxed);}
inline std::uint64_t cpu(){timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))std::terminate();return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Context { Stage old=stage;explicit Context(Stage s){stage=s;}~Context(){stage=old;} };
struct Timer { Metric m;std::uint64_t start=cpu();explicit Timer(Metric v):m(v){}~Timer(){add(m,cpu()-start);} };
inline std::string_view mode(){static const std::string_view m=[] {const char* p=std::getenv("GLOB2_GRADIENT_ATTRIBUTION");return std::string_view(p?p:"proactive");}();return m;}
struct Field {
 unsigned gid=0,identity=0,slot=0,tick=0;bool proactive=false,speculative=false;
 std::uint64_t seedCpu=0;
 std::atomic<unsigned> flags{0},usedAtRetire{0},retired{0},published{0},retireTick{0},discardReason{0};
 std::array<std::atomic<std::uint64_t>,StageCount> cpu{},entries{};
 void touch(Stage s){const unsigned bit=s==Ranking?1:s==Primary?2:4;if(!(flags.load(std::memory_order_relaxed)&bit))flags.fetch_or(bit,std::memory_order_relaxed);}
 void retire(unsigned when,unsigned reason=1){unsigned expected=0;if(retired.compare_exchange_strong(expected,reason)){usedAtRetire=flags.load();retireTick=when;}}
};
struct MapSeedRecord {unsigned gid,identity,slot,tick,cold,previousTick,depth,fromProactive;};
inline std::vector<MapSeedRecord> mapSeeds;
inline std::mutex registryMutex;
inline std::vector<std::shared_ptr<Field>> fields;
inline std::shared_ptr<Field> makeField(unsigned gid,unsigned identity,unsigned slot,unsigned tick,bool proactive){auto f=std::make_shared<Field>();f->gid=gid;f->identity=identity;f->slot=slot;f->tick=tick;f->proactive=proactive;f->speculative=stage==Speculative;std::lock_guard lock(registryMutex);fields.push_back(f);return f;}
inline void reset(){for(auto& n:metrics)n=0;for(auto& n:advanceCpu)n=0;for(auto& n:entries)n=0;for(auto& n:calls)n=0;for(auto& n:ownerDomainCpu)n=0;std::lock_guard lock(registryMutex);fields.clear();mapSeeds.clear();}
inline void write(std::ostream& s){s<<"{\"mode\":\""<<mode()<<"\"";for(unsigned i=0;i<MetricCount;++i)s<<",\""<<metricNames[i]<<"\":"<<metrics[i].load();s<<",\"owner_other_cpu_ns\":"<<ownerDomainCpu[0]<<",\"owner_unit_cpu_ns\":"<<ownerDomainCpu[1]<<",\"owner_building_cpu_ns\":"<<ownerDomainCpu[2];s<<",\"search\":{";for(unsigned i=0;i<StageCount;++i){if(i)s<<',';s<<'\"'<<stageNames[i]<<"\":{\"cpu_ns\":"<<advanceCpu[i].load()<<",\"entries\":"<<entries[i].load()<<",\"calls\":"<<calls[i].load()<<'}';}s<<"}}";}
inline void writeMapSeeds(){const char* path=std::getenv("GLOB2_ATTR_FIELDS");if(!path)return;std::ofstream s(std::string(path)+".map.csv");s<<"gid,identity,slot,captured,cold,previous_tick,depth,from_proactive\n";for(const auto& r:mapSeeds)s<<r.gid<<','<<r.identity<<','<<r.slot<<','<<r.tick<<','<<r.cold<<','<<r.previousTick<<','<<r.depth<<','<<r.fromProactive<<'\n';}
inline void writeFields(){writeMapSeeds();const char* path=std::getenv("GLOB2_ATTR_FIELDS");if(!path)return;std::ofstream s(path);s<<"gid,identity,slot,captured,proactive,speculative,seed_cpu_ns,published,flags,retired,used_at_retire,retire_tick,discard_reason,rank_cpu_ns,primary_cpu_ns,owner_cpu_ns,rank_entries,primary_entries,owner_entries\n";std::lock_guard lock(registryMutex);for(const auto& f:fields)s<<f->gid<<','<<f->identity<<','<<f->slot<<','<<f->tick<<','<<f->proactive<<','<<f->speculative<<','<<f->seedCpu<<','<<f->published<<','<<f->flags<<','<<f->retired<<','<<f->usedAtRetire<<','<<f->retireTick<<','<<f->discardReason<<','<<f->cpu[Ranking]<<','<<f->cpu[Primary]<<','<<f->cpu[Owner]<<','<<f->entries[Ranking]<<','<<f->entries[Primary]<<','<<f->entries[Owner]<<'\n';}
}
