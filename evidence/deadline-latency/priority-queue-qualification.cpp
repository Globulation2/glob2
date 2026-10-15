#include "ComputeExecutor.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
using namespace std::chrono_literals;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Gate {std::mutex m;std::condition_variable cv;bool entered=false,open=false;
 void hold(){std::unique_lock lock(m);entered=true;cv.notify_all();if(!cv.wait_for(lock,5s,[&]{return open;}))throw std::runtime_error("gate expired");}
 bool wait(){std::unique_lock lock(m);return cv.wait_for(lock,5s,[&]{return entered;});}
 void release(){std::lock_guard lock(m);open=true;cv.notify_all();}};
struct Release {Gate& gate;~Release(){gate.release();}};
struct Context {std::vector<int> order;Gate gate;ComputeExecutor::CompletionTicket ticket;std::atomic<bool> suspended=false;ComputeExecutor* executor=nullptr;std::mutex ticketMutex;bool closing=false;};
struct Item{Context* context;int value;};
struct Cleanup{Context& c;~Cleanup(){c.gate.release();std::lock_guard lock(c.ticketMutex);c.closing=true;if(c.ticket)c.ticket->complete();}};
static void sorted(){Context c;std::array<Item,4> items{{{&c,30},{&c,10},{&c,21},{&c,22}}};ComputeExecutor e;e.configure(2);check(e.threadCount()==2,"needs one worker");Release release{c.gate};
 for(unsigned round=0;round<16;++round){c.order.clear();{std::lock_guard lock(c.gate.m);c.gate.entered=false;c.gate.open=false;}
 ComputeExecutor::Group held{1,{[](void* p,size_t){static_cast<Context*>(p)->gate.hold();},&c}};auto h=e.submit(std::span(&held,1));check(c.gate.wait(),"worker not gated");
 std::array<ComputeExecutor::Batch,4> batches;const unsigned dues[]={30,10,20,20};for(size_t i=0;i<4;++i){ComputeExecutor::Group g{1,{[](void* p,size_t){auto& i=*static_cast<Item*>(p);i.context->order.push_back(i.value);},&items[i]}};batches[i]=e.submit(std::span(&g,1),dues[i]);}
 c.gate.release();e.join(h);for(auto& b:batches)e.join(b);check(c.order==std::vector<int>({10,21,22,30}),"head/middle/tail/serial order");}}
static void continuation(){Context c;ComputeExecutor e;c.executor=&e;e.configure(2);check(e.threadCount()==2,"needs one worker");Cleanup cleanup{c};
 ComputeExecutor::Group jobs{3,{[](void* p,size_t index){auto& c=*static_cast<Context*>(p);if(index==0){std::lock_guard lock(c.ticketMutex);if(c.closing)return;c.ticket=c.executor->defer();c.suspended.store(true,std::memory_order_release);}else if(index==1){c.order.push_back(1);c.gate.hold();}else c.order.push_back(2);},&c}};
 auto batch=e.submit(std::span(&jobs,1),20);check(c.gate.wait(),"index1 not gated");check(c.suspended.load(std::memory_order_acquire),"index0 did not suspend");
 check(c.ticket->resume({[](void* p,size_t){static_cast<Context*>(p)->order.push_back(0);},&c}),"continuation rejected");c.gate.release();e.join(batch);check(c.order==std::vector<int>({1,0,2}),"continuation/index tie order");}
int main(){std::atomic<bool> done=false;std::jthread watchdog([&](std::stop_token stop){const auto end=std::chrono::steady_clock::now()+30s;while(!done&&!stop.stop_requested()){if(std::chrono::steady_clock::now()>end)std::_Exit(124);std::this_thread::sleep_for(10ms);}});try{sorted();continuation();done=true;std::cout<<"PRIORITY_QUEUE_PASS\n";return 0;}catch(const std::exception& error){done=true;std::cerr<<error.what()<<'\n';return 1;}}
