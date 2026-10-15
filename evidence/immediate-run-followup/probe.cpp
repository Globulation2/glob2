#include "ComputeExecutor.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
using namespace std::chrono_literals;
static void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
struct alignas(64) Output{uint64_t value=0;};
static uint64_t payload(size_t i,unsigned iterations,bool skew){uint64_t x=0x9e3779b97f4a7c15ull+i;const unsigned n=iterations*(skew&&i%11==0?32:1);for(unsigned j=0;j<n;++j){x^=x>>12;x^=x<<25;x^=x>>27;x*=2685821657736338717ull;}return x;}
struct Gate{std::atomic<bool> entered=false,open=false;void hold(){entered=true;const auto end=std::chrono::steady_clock::now()+5s;while(!open&&std::chrono::steady_clock::now()<end)std::this_thread::yield();if(!open)throw std::runtime_error("gate timeout");}};
struct Release{Gate& gate;~Release(){gate.open=true;}};
static void qualify(){
 for(auto mode:{ComputeTelemetryMode::Off,ComputeTelemetryMode::Waits,ComputeTelemetryMode::Trace}){
  for(unsigned participants:{1u,4u}){
   std::vector<Output> out(257);ComputeExecutor e;e.configureTelemetry(mode);e.configure(participants);check(e.threadCount()==participants,"participant launch");
   for(unsigned round=0;round<96;++round){const size_t n=round%4==0?1:2+(round*37)%255;e.run(n,[&](size_t i){out[i].value=payload(i+round,8,false);});for(size_t i=0;i<n;++i)check(out[i].value==payload(i+round,8,false),"generation output");}
   e.run(17,[&](size_t i){out[i].value=0;e.run(3,[&](size_t j){out[i].value+=j;});});for(size_t i=0;i<17;++i)check(out[i].value==3,"nested result");
   for(size_t i=0;i<17;++i)out[i].value=~uint64_t(0);
   bool caught=false;try{e.run(17,[&](size_t i){out[i].value=i;if(i==3||i==8)throw std::runtime_error("expected");});}catch(const std::runtime_error&){caught=true;}check(caught,"missing error");
   // Existing serial run stops at its first throw. Parallel invoke catches
   // each callback error, drains every claimed callback, then rethrows.
   for(size_t i=0;i<17;++i){const auto expected=participants==1&&i>3?~uint64_t(0):uint64_t(i);check(out[i].value==expected,participants==1?"serial exception boundary":"parallel error did not drain");}
   e.run(17,[&](size_t i){out[i].value=i+9;});for(size_t i=0;i<17;++i)check(out[i].value==i+9,"post error");
   e.configure(participants==1?4:1);e.run(33,[&](size_t i){out[i].value=i+11;});for(size_t i=0;i<33;++i)check(out[i].value==i+11,"reconfigure");
  }
  // Sole worker is unavailable during run; owner exhausts every claim. After
  // return, releasing it exercises a stale/empty generation before the next.
  Gate optional;ComputeExecutor e;e.configureTelemetry(mode);e.configure(2);check(e.threadCount()==2,"single worker launch");Release cleanup{optional};
  auto presentation=e.submitPresentation(1,[&](size_t){optional.hold();});const auto until=std::chrono::steady_clock::now()+5s;while(!optional.entered&&std::chrono::steady_clock::now()<until)std::this_thread::yield();check(optional.entered,"presentation not entered");
  std::vector<Output> out(129);e.run(129,[&](size_t i){check(ComputeExecutor::workerSlot()==0,"sole worker should be gated");out[i].value=i+1;});for(size_t i=0;i<129;++i)check(out[i].value==i+1,"owner-only generation");presentation->cancel();optional.open=true;e.cancelPresentationAndWait();
  for(unsigned r=0;r<64;++r){const size_t n=2+r%31;e.run(n,[&](size_t i){out[i].value=payload(i+r,4,false);});for(size_t i=0;i<n;++i)check(out[i].value==payload(i+r,4,false),"late generation output");}
  // Held required worker must prevent run returning after owner work ends.
  Gate required;std::atomic<bool> returned=false,barrierFailed=false;Release release{required};
  std::jthread observer([&]{const auto end=std::chrono::steady_clock::now()+5s;while(!required.entered&&std::chrono::steady_clock::now()<end)std::this_thread::yield();if(!required.entered)barrierFailed=true;std::this_thread::sleep_for(20ms);if(returned)barrierFailed=true;required.open=true;});
  e.run(64,[&](size_t i){if(ComputeExecutor::workerSlot()!=0){bool expected=false;if(required.entered.compare_exchange_strong(expected,true))required.hold();}else if(i==0){const auto end=std::chrono::steady_clock::now()+5s;while(!required.entered&&std::chrono::steady_clock::now()<end)std::this_thread::yield();}out[i].value=i;});returned=true;observer.join();check(!barrierFailed,"required worker escaped barrier");
 }
 std::cout<<"IMMEDIATE_QUALIFICATION_PASS\n";
}
int main(int argc,char** argv){std::atomic<bool> done=false;std::jthread watchdog([&](std::stop_token stop){const auto end=std::chrono::steady_clock::now()+90s;while(!done&&!stop.stop_requested()){if(std::chrono::steady_clock::now()>end)std::_Exit(124);std::this_thread::sleep_for(10ms);}});try{
 if(argc==2&&std::string(argv[1])=="qualify"){qualify();done=true;return 0;}
 if(argc!=7)throw std::invalid_argument("bench participants repeats jobs iterations balanced|skew");unsigned participants=std::string(argv[2])=="auto"?std::max(1u,std::thread::hardware_concurrency()):std::stoul(argv[2]);unsigned repeats=std::stoul(argv[3]),iterations=std::stoul(argv[5]);size_t jobs=std::stoull(argv[4]);std::string shape=argv[6];check(std::string(argv[1])=="bench"&&participants&&participants<=256&&repeats&&jobs&&jobs<=1000000&&iterations<=100000&&(shape=="balanced"||shape=="skew"),"bounds");
 std::vector<Output> out(jobs);ComputeExecutor e;e.configure(participants);const auto begin=std::chrono::steady_clock::now();for(unsigned r=0;r<repeats;++r)e.run(jobs,[&](size_t i){out[i].value=payload(i,iterations,shape=="skew");});const auto end=std::chrono::steady_clock::now();uint64_t digest=1469598103934665603ull;for(size_t i=0;i<jobs;++i){check(out[i].value==payload(i,iterations,shape=="skew"),"payload");digest=(digest^out[i].value)*1099511628211ull;}
 std::cout<<"IMMEDIATE_RESULT {\"participants\":"<<e.threadCount()<<",\"repeats\":"<<repeats<<",\"jobs\":"<<jobs<<",\"iterations\":"<<iterations<<",\"shape\":\""<<shape<<"\",\"wall_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count()<<",\"digest\":"<<digest<<"}\n";done=true;return 0;
 }catch(const std::exception& error){done=true;std::cerr<<error.what()<<'\n';return 1;}}
