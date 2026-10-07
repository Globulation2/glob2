#pragma once
// Benchmark-only instrumentation, identical in both revisions; never shipped.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <time.h>
namespace RenderBench {
inline uint64_t ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline uint64_t cpu(clockid_t id) { timespec t{}; clock_gettime(id,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec; }
inline unsigned option(const char* key,unsigned fallback) {const char* v=std::getenv(key);return v?std::strtoul(v,nullptr,10):fallback;}
struct Row { unsigned tick; uint64_t simulation,owner,interval; };
struct Probe {
 bool enabled=false,verify=false; unsigned first=0,warm=0,last=0,before=0;
 uint64_t beginNs=0,simulationEnd=0,previousEnd=0,wallStart=0,wallEnd=0,processStart=0,processEnd=0,ownerStart=0,ownerEnd=0;
 std::atomic<unsigned> phase{0}; std::vector<Row> rows; std::vector<uint32_t> checksums;
 void setup(unsigned initial) {enabled=true;verify=option("GLOB2_BENCH_VERIFY",0);first=initial;warm=initial+option("GLOB2_BENCH_WARMUP",500);last=warm+option("GLOB2_BENCH_TICKS",2000);rows.reserve(last-initial);checksums.reserve(last-initial);}
 void begin(unsigned tick) {
  if(!enabled)return;before=tick;beginNs=ns();
  if(tick==warm && phase.load(std::memory_order_relaxed)==0) {wallStart=beginNs;previousEnd=beginNs;processStart=cpu(CLOCK_PROCESS_CPUTIME_ID);ownerStart=cpu(CLOCK_THREAD_CPUTIME_ID);phase.store(1);}
 }
 void simulationDone(){if(enabled)simulationEnd=ns();}
 template<class Game> bool end(Game& game) {
  if(!enabled || game.stepCounter==before)return false;
  const auto now=ns();
  if(before>=warm)rows.push_back({unsigned(game.stepCounter),simulationEnd-beginNs,now-beginNs,now-previousEnd});
  previousEnd=now;
  if(verify)checksums.push_back(game.checkSum());
  if(game.stepCounter>=last) {wallEnd=now;processEnd=cpu(CLOCK_PROCESS_CPUTIME_ID);ownerEnd=cpu(CLOCK_THREAD_CPUTIME_ID);phase.store(2);return true;}
  return false;
 }
};
inline Probe probe;
}
