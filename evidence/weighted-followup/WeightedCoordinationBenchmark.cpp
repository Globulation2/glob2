// Experimental standalone weighted placement micro. Never linked into the game.
// Compile/run only at the root gate; this does not validate engine contracts.
#include <array>
#include <barrier>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <stdexcept>
struct alignas(64) Output { std::uint64_t value=0; unsigned calls=0; };
struct alignas(64) ClassSample { std::atomic<std::uint64_t> durationNs{0}; };
struct Dispatch { std::size_t index=0, charged=0; unsigned family=0; std::uint64_t reserved=1, hintUnits=1; };
struct Queue { std::mutex mutex; std::condition_variable ready; std::deque<Dispatch> jobs; std::atomic<std::uint64_t> outstanding{0}; std::atomic<unsigned> event{0}; bool stop=false; };
class Experiment {
 std::vector<std::unique_ptr<Queue>> queues;
 std::array<ClassSample,4> samples{};
 std::vector<Output> outputs;
 std::vector<std::thread> workers;
 std::atomic<std::size_t> left;
 std::atomic<unsigned> cursor{0};
 std::size_t jobs,lanes; unsigned iterations; std::string policy,shape;
 static constexpr std::uint64_t MaxReservation=1ull<<40; // with jobs<=1e6, total cannot overflow64bits
 std::uint64_t hint(unsigned family) const {
  const std::uint64_t weights[]={1,2,8,32};
  // Configured prior only. The estimator never sees job.index or actual drift/within-class work.
  return std::max<std::uint64_t>(1,(shape=="misleading" ? weights[3-family] : weights[family])*iterations+1);
 }
 std::uint64_t estimate(unsigned family,std::uint64_t units) {
  if(policy=="bare-steal" || policy=="steal")return 1;
  if(policy=="last" || policy=="ewma" || policy=="observe-only") {
   // Class hint is constant in this micro. Store whole callback duration, not
   // integer ns/hint-unit (which would prevent learning cheap classes).
   // Cold prior is explicit1ns for every class; warmed history is separate.
   return std::clamp<std::uint64_t>(samples[family].durationNs.load(std::memory_order_relaxed),1,MaxReservation);
  }
  return std::min(MaxReservation,units); // static relative units, separate arm
 }
 std::size_t place() {
  auto first=cursor.fetch_add(1,std::memory_order_relaxed)%queues.size();
  if(policy=="bare-steal" || policy=="steal" || policy=="observe-only")return first;
  auto best=first;
  if(policy=="two-choice") {
   auto second=(first+1+(cursor.load(std::memory_order_relaxed)/queues.size())%(queues.size()-1 ? queues.size()-1 : 1))%queues.size();
   if(queues[second]->outstanding.load(std::memory_order_relaxed)<queues[best]->outstanding.load(std::memory_order_relaxed))best=second;
  } else for(std::size_t i=1;i<queues.size();++i) {auto q=(first+i)%queues.size();if(queues[q]->outstanding.load(std::memory_order_relaxed)<queues[best]->outstanding.load(std::memory_order_relaxed))best=q;}
  return best;
 }
 void submit(std::size_t index) {
  Dispatch d;d.index=index;d.family=index%4;d.hintUnits=policy=="bare-steal" ? 1 : hint(d.family);d.charged=place();d.reserved=policy=="bare-steal" ? 0 : estimate(d.family,d.hintUnits);
  auto& q=*queues[d.charged];
  if(policy!="bare-steal")q.outstanding.fetch_add(d.reserved,std::memory_order_relaxed); // reservation includes queue-lock wait
  {std::lock_guard lock(q.mutex);q.jobs.push_back(d);}
  q.ready.notify_one();
  if(queues.size()>1) {auto& peer=*queues[(d.charged+1)%queues.size()];{std::lock_guard lock(peer.mutex);peer.event.fetch_add(1,std::memory_order_relaxed);}peer.ready.notify_one();}
 }
 bool take(std::size_t worker,Dispatch& d) {
  for(std::size_t step=0;step<(policy=="static" ? 1 : queues.size());++step) {
   auto victim=(worker+step)%queues.size();auto& q=*queues[victim];std::unique_lock lock(q.mutex,std::try_to_lock);
   if(!lock || q.jobs.empty())continue;
   d=q.jobs.front();q.jobs.pop_front();
   if(victim!=worker && policy!="bare-steal") {auto old=q.outstanding.fetch_sub(d.reserved,std::memory_order_relaxed);if(old<d.reserved)std::terminate();queues[worker]->outstanding.fetch_add(d.reserved,std::memory_order_relaxed);d.charged=worker;}
   return true;
  }
  return false;
 }
 std::uint64_t payload(std::size_t index) const {
  const unsigned weight[]={1,2,8,32};unsigned factor=shape=="balanced"?1:weight[index%4];
  if(shape=="drift" && index>=jobs/2)factor=weight[3-index%4];
  if(shape=="within-class")factor=(index/4)%7==0 ? 32 : 1;
  auto value=std::uint64_t(0x9e3779b97f4a7c15ull+index);
  for(unsigned i=0;i<iterations*factor;++i){value^=value>>12;value^=value<<25;value^=value>>27;value*=2685821657736338717ull;}
  return value;
 }
 void run(std::size_t worker) {
  auto& q=*queues[worker];unsigned seen=0;
  for(;;) {
   Dispatch d;
   if(!take(worker,d)) {std::unique_lock lock(q.mutex);q.ready.wait(lock,[&]{return q.stop || !q.jobs.empty() || q.event.load(std::memory_order_relaxed)!=seen;});if(q.stop)return;seen=q.event.load(std::memory_order_relaxed);continue;}
   const bool timed=policy=="last" || policy=="ewma" || policy=="clock-only" || policy=="observe-only";
   const auto start=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
   const auto value=payload(d.index);
   outputs[d.index].value=value;++outputs[d.index].calls;
   if(timed) {
    auto elapsed=std::max<std::uint64_t>(1,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    const auto unit=std::clamp<std::uint64_t>(elapsed,1,MaxReservation);
    if(policy=="last" || policy=="observe-only")samples[d.family].durationNs.store(unit,std::memory_order_relaxed);
    if(policy=="ewma") {auto old=samples[d.family].durationNs.load(std::memory_order_relaxed);while(!samples[d.family].durationNs.compare_exchange_weak(old,old ? std::max<std::uint64_t>(1,(old*7+unit)/8) : unit,std::memory_order_relaxed)) {}}
   }
   // Debit's immutable local value must be used before publishing continuation.
   if(policy!="bare-steal") {auto previous=queues[d.charged]->outstanding.fetch_sub(d.reserved,std::memory_order_relaxed);if(previous<d.reserved)std::terminate();}
   if(lanes && d.index+lanes<jobs)submit(d.index+lanes); // only next lane head becomes runnable
   if(left.fetch_sub(1,std::memory_order_acq_rel)==1) {for(auto& ptr:queues) {{std::lock_guard lock(ptr->mutex);ptr->stop=true;}ptr->ready.notify_all();}return;}
  }
 }
public:
 Experiment(unsigned count,std::size_t n,std::size_t laneCount,unsigned work,std::string mode,std::string costs):outputs(n),left(n),jobs(n),lanes(laneCount),iterations(work),policy(std::move(mode)),shape(std::move(costs)) {
  if(!count || !n || count>256 || n>1000000 || laneCount>n || work>100000)throw std::invalid_argument("bounds");
  for(unsigned i=0;i<count;++i)queues.push_back(std::make_unique<Queue>());
 }
 void execute(unsigned warmupPerClass=0) {
  std::uint64_t warmDigest=0;
  for(unsigned round=0;round<warmupPerClass;++round)for(unsigned family=0;family<4;++family) {
   const bool timed=policy=="last" || policy=="ewma" || policy=="observe-only" || policy=="clock-only";
   auto before=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
   warmDigest^=payload(family+4*round);
   if(timed && policy!="clock-only") {auto elapsed=std::max<std::uint64_t>(1,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-before).count());samples[family].durationNs.store(std::clamp<std::uint64_t>(elapsed,1,MaxReservation),std::memory_order_relaxed);}
  }
  std::barrier readyGate(static_cast<std::ptrdiff_t>(queues.size()+1)),startGate(static_cast<std::ptrdiff_t>(queues.size()+1));
  for(std::size_t i=0;i<queues.size();++i)workers.emplace_back([&,i]{readyGate.arrive_and_wait();startGate.arrive_and_wait();run(i);});
  readyGate.arrive_and_wait(); // exclude worker creation from inner operation span
  const auto start=std::chrono::steady_clock::now();startGate.arrive_and_wait();
  for(std::size_t i=0;i<(lanes?lanes:jobs);++i)submit(i);
  for(auto& w:workers)w.join();
  auto end=std::chrono::steady_clock::now();std::uint64_t digest=1469598103934665603ull;
  for(auto& r:outputs){if(r.calls!=1)throw std::runtime_error("job count mismatch");digest=(digest^r.value)*1099511628211ull;}
  for(auto& q:queues)if(q->outstanding.load()!=0)throw std::runtime_error("leaked reservation");
  std::cout<<"WEIGHTED_RESULT {\"workers\":"<<queues.size()<<",\"threads_including_submitter\":"<<queues.size()+1<<",\"submitter_executes_callbacks\":false,\"jobs\":"<<jobs<<",\"iterations\":"<<iterations<<",\"lanes\":"<<lanes<<",\"policy\":\""<<policy<<"\",\"shape\":\""<<shape<<"\",\"wall_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<<",\"digest\":"<<digest<<",\"warmup_per_class\":"<<warmupPerClass<<",\"warm_digest\":"<<warmDigest<<",\"samples\":[";
  for(unsigned i=0;i<4;++i)std::cout<<(i?",":"")<<samples[i].durationNs.load();std::cout<<"]}\n";
 }
};
int main(int argc,char** argv) {
 if(argc!=7 && argc!=8){std::cerr<<"workers jobs lanes iterations policy[bare-steal|steal|static|static-steal|two-choice|clock-only|observe-only|last|ewma] shape[balanced|skew|misleading|drift|within-class] [excluded_warmup_per_class]\n";return 2;}
 const std::string policy=argv[5],shape=argv[6];
 if(policy!="bare-steal" && policy!="steal" && policy!="static" && policy!="static-steal" && policy!="observe-only" && policy!="two-choice" && policy!="clock-only" && policy!="last" && policy!="ewma")return 2;
 if(shape!="balanced" && shape!="skew" && shape!="misleading" && shape!="drift" && shape!="within-class")return 2;
 Experiment e(std::stoul(argv[1]),std::stoul(argv[2]),std::stoul(argv[3]),std::stoul(argv[4]),policy,shape);e.execute(argc==8?std::stoul(argv[7]):0);
}
