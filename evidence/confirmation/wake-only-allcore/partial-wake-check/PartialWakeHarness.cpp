#include "ComputeExecutor.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
using namespace std::chrono_literals;
static void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
struct State {
 std::mutex mutex;std::condition_variable cv;bool release=false;
 std::atomic<unsigned> entered=0,active=0,peak=0,laneActive=0,laneNext=0;
 std::atomic<bool> wrong=false;
 std::array<std::atomic<unsigned>,3> visits{};
 void reset(){std::lock_guard lock(mutex);release=false;entered=0;active=0;peak=0;laneActive=0;laneNext=0;wrong=false;for(auto& x:visits)x=0;}
 void open(){std::lock_guard lock(mutex);release=true;cv.notify_all();}
 bool await(unsigned expected){std::unique_lock lock(mutex);return cv.wait_for(lock,5s,[&]{return entered.load()>=expected;});}
};
struct Release {State& state;~Release(){state.open();}};
struct Work {State* state;unsigned base;bool lane;};
static void callback(void* pointer,std::size_t offset){
 auto& work=*static_cast<Work*>(pointer);auto& s=*work.state;const auto id=work.base+unsigned(offset);
 if(id>=s.visits.size()){s.wrong=true;return;}if(s.visits[id].fetch_add(1)!=0)s.wrong=true;
 if(work.lane){if(s.laneActive.fetch_add(1)!=0)s.wrong=true;if(s.laneNext.fetch_add(1)!=offset)s.wrong=true;}
 const auto active=s.active.fetch_add(1)+1;auto peak=s.peak.load();while(peak<active&&!s.peak.compare_exchange_weak(peak,active)){}
 {
  std::unique_lock lock(s.mutex);++s.entered;s.cv.notify_all();
  if(!s.cv.wait_for(lock,5s,[&]{return s.release;}))s.wrong=true;
 }
 --s.active;if(work.lane)--s.laneActive;
}
static unsigned qualification(){unsigned bursts=0;
 for(auto mode:{ComputeTelemetryMode::Off,ComputeTelemetryMode::Waits,ComputeTelemetryMode::Trace})
  for(unsigned participants:{5u,9u})for(unsigned shape=0;shape<3;++shape){
   State state;std::array<Work,2> work{{{&state,0,shape==2},{&state,2,false}}};
   ComputeExecutor executor;executor.configureTelemetry(mode);executor.configure(participants);check(executor.threadCount()==participants,"worker launch mismatch");Release cleanup{state};
   for(unsigned repeat=0;repeat<8;++repeat){
    state.reset();std::this_thread::sleep_for(2ms);
    const unsigned total=shape==0?2:3,heads=shape==2?2:total;
    std::array<ComputeExecutor::Group,2> groups{};
    groups[0]={shape==0?2u:shape==1?3u:2u,{callback,&work[0]},shape==2?7u:ComputeExecutor::NoLane};
    groups[1]={shape==2?1u:0u,{callback,&work[1]},ComputeExecutor::NoLane};
    auto batch=executor.submit(groups,repeat);
    const bool concurrent=state.await(heads);state.open();executor.join(batch);
    check(concurrent,"partial notify burst serialized ready heads");check(state.entered==total&&state.active==0,"missing callbacks or retained active work");
    check(state.peak>=heads,"required heads did not overlap");check(!state.wrong,"duplicate callback, gate expiry or lane order violation");
    for(unsigned id=0;id<3;++id)check(state.visits[id]==(id<total?1u:0u),"exactly once mismatch");
    check(executor.liveBatches()==0,"batch leak");if(shape==2)check(state.laneNext==2&&state.laneActive==0,"lane completion mismatch");++bursts;
   }
  }
 return bursts;
}
int main(){std::atomic<bool> done=false;std::jthread watchdog([&](std::stop_token stop){auto end=std::chrono::steady_clock::now()+60s;while(!done&&!stop.stop_requested()){if(std::chrono::steady_clock::now()>end){std::cerr<<"partial wake watchdog\n";std::_Exit(124);}std::this_thread::sleep_for(10ms);}});
 try{const auto bursts=qualification();done=true;std::cout<<"PARTIAL_WAKE_PASS bursts="<<bursts<<" modes=3 participants=5,9 shapes=2,3,mixed timing_excluded=true\n";return 0;}
 catch(const std::exception& error){done=true;std::cerr<<error.what()<<'\n';return 1;}}
