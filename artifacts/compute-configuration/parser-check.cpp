#include "ComputeThreads.h"
#include <cassert>
#include <limits>
#include <string>
int main() {
 assert(resolveComputeThreadCount(0,0)==1);
 assert(resolveComputeThreadCount(0,128)==128);
 assert(resolveComputeThreadCount(65,8)==65);
 assert(parseComputeThreadCount("auto")==0);
 assert(parseComputeThreadCount("128")==128);
 assert(parseComputeThreadCount(std::to_string(std::numeric_limits<unsigned>::max()))==std::numeric_limits<unsigned>::max());
 for(auto s:{"0","-1","+1","1x","1.5","999999999999999999999"}) {
  bool rejected=false;try{parseComputeThreadCount(s);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 }
}
