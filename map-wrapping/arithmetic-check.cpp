#include "src/common/PowerOfTwo.h"
#include "src/map/generator/shared/Grid.h"
#include <cassert>
#include <limits>
#include <cstdint>
#include <random>
template<class A,class B> void check(A a,B b) {
 assert(dimensionRemainder(a,b)==a%b);
 if(b>0 && (b&(b-1))==0) assert(powerOfTwoRemainder(a,b)==a%b);
}
int main() {
 std::mt19937_64 rng(7349);
 for(int i=0;i<1000000;++i) {
  auto v=rng();int s=1u<<(rng()%31);
  check(int(v),s);check(unsigned(v),s);check(int(v),unsigned(s));
  check(std::int64_t(v),unsigned(s));check(v,s);
  check(v,std::uint64_t(1)<<(rng()%64));
  check(std::int64_t(v),int(rng()%4000+1));
 }
 MapGeneration::Torus t(128,256);
 assert(t.x(-129)==127 && t.remainderX(-129)==-1);
 t.w=7;assert(t.x(-129)==4 && t.remainderX(-129)==-3);
}
