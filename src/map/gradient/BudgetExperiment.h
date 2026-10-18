#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <stdexcept>
namespace BudgetExperiment {
inline int valueScore(int benefitMilli,int costUs) {
 const auto value=std::int64_t(benefitMilli)*1000/std::max(1,costUs);
 return int(std::clamp(value,std::int64_t(-1000000000),std::int64_t(1000000000)));
}
struct Candidate { int key, age, score; int flags=0,benefitMilli=0,costUs=0,value=0; };
// Stable keys break all ties. Worker completion and timing never enter selection.
inline int choose(const std::vector<Candidate>& c,int mode,int cursor) {
 const Candidate* b=nullptr;
 for (const auto& v:c) {
  if(!b) {b=&v;continue;}
  bool better=false;
  if(mode==0) {const bool va=v.key>=cursor,ba=b->key>=cursor;better=va!=ba?va:v.key<b->key;}
  else if(mode==1)better=v.age!=b->age?v.age>b->age:v.key<b->key;
  else if(mode==2)better=v.score!=b->score?v.score>b->score:(v.age!=b->age?v.age>b->age:v.key<b->key);
  else throw std::invalid_argument("Unknown budget policy");
  if(better)b=&v;
 }
 return b?b->key:-1;
}
inline std::uint64_t threadCpuNs() {
 timespec t{};
 if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)throw std::runtime_error("Thread CPU clock unavailable");
 return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
}
