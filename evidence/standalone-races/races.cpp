#include "ComputeExecutor.h"
#include <iostream>
#include <future>
using namespace std::chrono_literals;
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Gate {std::mutex m;std::condition_variable cv;bool entered=false,open=false;
 void hold(){std::unique_lock l(m);entered=true;cv.notify_all();require(cv.wait_for(l,5s,[&]{return open;}),"gate timeout");}
 bool wait(){std::unique_lock l(m);return cv.wait_for(l,5s,[&]{return entered;});}
 void release(){std::lock_guard l(m);open=true;cv.notify_all();}};
static void presentation(){ComputeExecutor e;e.configure(4);Gate g;
 auto p=e.submitPresentation(1,[&](size_t){g.hold();});require(g.wait(),"presentation not entered");
 std::atomic<unsigned> n=0;ComputeExecutor::Group work{64,{[](void* p,size_t){++*static_cast<std::atomic<unsigned>*>(p);},&n}};
 auto b=e.submit(std::span(&work,1));e.join(b);require(n==64,"required jobs missing");
 require(!p->finished(),"optional work delayed barrier until timeout");p->cancel();g.release();e.cancelPresentationAndWait();}
static void early(){ComputeExecutor e;e.configure(4);struct C{ComputeExecutor& e;Gate gate;std::atomic<unsigned> resumed=0,tail=0;} c{e};
 std::array<ComputeExecutor::Group,2> groups{{{1,{[](void* p,size_t){auto& c=*static_cast<C*>(p);auto t=c.e.defer();require(t->resume({[](void* p,size_t){++static_cast<C*>(p)->resumed;},&c}),"resume rejected");c.gate.hold();},&c},3},{1,{[](void* p,size_t){auto& c=*static_cast<C*>(p);require(c.resumed==1,"tail before continuation");++c.tail;},&c},3}}};
 auto b=e.submit(groups);require(c.gate.wait(),"early callback not entered");require(c.resumed==0&&c.tail==0,"early continuation overlaps callback");c.gate.release();e.join(b);require(c.resumed==1&&c.tail==1,"continuation count");}
static void reuse(){ComputeExecutor e;e.configure(6);for(unsigned k=0;k<512;++k){std::array<unsigned,17> values{};
 ComputeExecutor::Group g{values.size(),{[](void* p,size_t i){static_cast<unsigned*>(p)[i]=unsigned(i)+1;},values.data()}};
 auto b=e.submit(std::span(&g,1));e.join(b);for(size_t i=0;i<values.size();++i)require(values[i]==i+1,"result publication/reuse");}}
static void lane(){ComputeExecutor e;e.configure(6);struct C{std::atomic<unsigned> next=0,active=0;std::atomic<bool> overlap=false;} c;
 auto fn=[](void* p,size_t){auto& c=*static_cast<C*>(p);if(c.active.fetch_add(1))c.overlap=true;++c.next;c.active.fetch_sub(1);};
 for(unsigned k=0;k<256;++k){ComputeExecutor::Group g{7,{fn,&c},4};auto a=e.submit(std::span(&g,1),k);auto b=e.submit(std::span(&g,1),k);e.join(a);e.join(b);}require(c.next==3584&&!c.overlap,"lane overlap/count");}
static void waking(){ComputeExecutor e;e.configure(4);std::atomic<unsigned> n=0;
 for(unsigned k=0;k<512;++k){ComputeExecutor::Group g{1,{[](void* p,size_t){++*static_cast<std::atomic<unsigned>*>(p);},&n}};auto b=e.submit(std::span(&g,1));e.join(b);if(k%7==0)std::this_thread::sleep_for(50us);}require(n==512,"wake count");}
int main(int argc,char** argv){std::atomic<bool> done=false;std::jthread watchdog([&](std::stop_token s){auto limit=std::chrono::steady_clock::now()+40s;while(!s.stop_requested()&&!done){if(std::chrono::steady_clock::now()>limit){std::cerr<<"watchdog expired\n";std::_Exit(124);}std::this_thread::sleep_for(10ms);}});
 try{std::string test=argc>1?argv[1]:"all";if(test=="all"||test=="presentation")presentation();if(test=="all"||test=="early")early();if(test=="all"||test=="reuse")reuse();if(test=="all"||test=="lane")lane();if(test=="all"||test=="waking")waking();done=true;std::cout<<"PASS "<<test<<'\n';return 0;}catch(const std::exception& x){done=true;std::cerr<<x.what()<<'\n';return 1;}}
