#include "ComputeExecutor.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
using namespace std::chrono_literals;
static void check(bool x,const char* text){if(!x)throw std::runtime_error(text);}
struct State {std::atomic<unsigned> entered=0,later=0,started=0,before=0,completedBefore=0;std::atomic<bool> release=false;std::atomic<long long> presentStart=0;std::vector<uint64_t> result;};
static uint64_t payload(size_t id){uint64_t x=id+1;for(unsigned k=0;k<200000;++k)x=(x^(x>>17))*0x9e3779b97f4a7c15ULL+k;return x;}
int main(int argc,char** argv){std::atomic<bool> done=false;std::jthread guard([&](std::stop_token stop){auto end=std::chrono::steady_clock::now()+30s;while(!stop.stop_requested()&&!done){if(std::chrono::steady_clock::now()>end)std::_Exit(124);std::this_thread::sleep_for(10ms);}});
 try {const bool trace=argc>1&&std::string(argv[1])=="trace";State s;s.result.resize(129);ComputeExecutor e;struct ReleaseOnExit {State& state;~ReleaseOnExit(){state.release.store(true,std::memory_order_release);}} releaseOnExit{s};e.configureTelemetry(trace?ComputeTelemetryMode::Trace:ComputeTelemetryMode::Off);e.configure(4);check(e.threadCount()==4,"requires three callback workers");
 ComputeExecutor::Group held{3,{[](void* p,size_t){auto& s=*static_cast<State*>(p);++s.entered;while(!s.release.load())std::this_thread::yield();},&s}};
 auto gates=e.submit(std::span(&held,1),0);auto limit=std::chrono::steady_clock::now()+5s;while(s.entered<3&&std::chrono::steady_clock::now()<limit)std::this_thread::yield();if(s.entered!=3){s.release=true;e.join(gates);throw std::runtime_error("workers failed to enter gate");}
 ComputeExecutor::Group later{128,{[](void* p,size_t i){auto& s=*static_cast<State*>(p);++s.started;s.result[i]=payload(i);++s.later;},&s}};
 auto backlog=e.submit(std::span(&later,1),100);
 const bool optional=argc>2&&std::string(argv[2])=="optional";
 auto present=optional?e.submitPresentation(1,[&](size_t){s.presentStart=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}):ComputeExecutor::PresentationTicket{};
 // Independent lane: earlier execution priority does not violate any predecessor.
 ComputeExecutor::Group urgent{1,{[](void* p,size_t){auto& s=*static_cast<State*>(p);s.before=s.started.load();s.completedBefore=s.later.load();s.result[128]=payload(128);},&s},2};
 auto needed=e.submit(std::span(&urgent,1),1);const auto begin=std::chrono::steady_clock::now();s.release=true;e.join(needed);const auto end=std::chrono::steady_clock::now();e.join(gates);e.join(backlog);
 if(present){auto endWait=std::chrono::steady_clock::now()+5s;while(!present->finished()&&std::chrono::steady_clock::now()<endWait)std::this_thread::yield();check(present->finished(),"presentation service timeout");e.cancelPresentationAndWait();}
 uint64_t digest=0;for(size_t i=0;i<s.result.size();++i){check(s.result[i]==payload(i),"payload mismatch");digest^=s.result[i];}
 std::cout<<"LATENCY_RESULT {\"trace\":"<<(trace?"true":"false")<<",\"urgent_join_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count()<<",\"later_started_before_urgent\":"<<s.before.load()<<",\"presentation_start_delay_ns\":"<<(optional?s.presentStart.load()-std::chrono::duration_cast<std::chrono::nanoseconds>(begin.time_since_epoch()).count():-1)<<",\"later_total\":"<<s.later.load()<<",\"digest\":"<<digest<<",\"result_bytes\":"<<s.result.size()*sizeof(uint64_t)<<"}\n";done=true;return 0;
 }catch(const std::exception& error){done=true;std::cerr<<error.what()<<'\n';return 1;}}
