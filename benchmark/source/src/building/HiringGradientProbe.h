// Local benchmark only; not a production feature or save format.
#pragma once
#include <atomic>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <ostream>
#include <string_view>
#include <stdexcept>
namespace HiringGradientProbe {
enum Mode { Baseline, Shortlist, Exact, TopOnly };
inline Mode mode() { static const Mode m=[] { const char* s=std::getenv("GLOB2_HIRING_PREWARM"); return !s || std::string_view(s)=="baseline" ? Baseline : std::string_view(s)=="shortlist" ? Shortlist : std::string_view(s)=="exact" ? Exact : std::string_view(s)=="top" ? TopOnly : throw std::invalid_argument("Unknown hiring probe mode"); }(); return m; }
enum Counter { PrivateBuilds, PrivateBuildCpu, Applied, Discarded, ProposalMisses, ProposalCpu, OwnerMisses, OwnerCpu, HiringJobs, HiringCpu, CandidateVisits, HeuristicQueries, SharedQueries, ShortlistedGround, ShortlistedUnreachable, ShortlistedMissing, ShortlistedUnsettled, PendingReads, PendingSteps, PendingDistances, Hires, PublishedGround, PublishedMissing, PublishedUnsettled, PublishedStale, WarmedPrimary, WarmedExtra, PublishedPrimaryGround, PublishedPrimaryMissing, PublishedPrimaryUnsettled, Count };
inline const char* names[]={"private_builds","private_build_cpu_ns","applied","discarded","proposal_misses","proposal_cpu_ns","owner_misses","owner_cpu_ns","hiring_jobs","hiring_cpu_ns","candidate_visits","heuristic_queries","shared_queries","shortlisted_ground","shortlisted_unreachable","shortlisted_missing","shortlisted_unsettled","pending_reads","pending_steps","pending_distances","hires","published_ground","published_missing","published_unsettled","published_stale","warmed_primary","warmed_extra","published_primary_ground","published_primary_missing","published_primary_unsettled"};
inline std::array<std::atomic<std::uint64_t>,Count> counts{};
inline std::uint64_t cpu() { timespec t{}; if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) std::terminate(); return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec; }
inline void add(Counter c,std::uint64_t n=1) {counts[c].fetch_add(n,std::memory_order_relaxed);}
struct Timer {Counter counter;std::uint64_t start=cpu();explicit Timer(Counter c):counter(c){} ~Timer(){add(counter,cpu()-start);} };
inline void reset(){for(auto& n:counts)n.store(0,std::memory_order_relaxed);}
inline std::array<std::uint64_t,Count> captured{};
inline void capture(){for(unsigned i=0;i<Count;++i)captured[i]=counts[i].load();}
inline void write(std::ostream& s){s<<"{\"mode\":"<<int(mode());for(unsigned i=0;i<Count;++i)s<<",\""<<names[i]<<"\":"<<captured[i];s<<'}';}
}
