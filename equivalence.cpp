#include <initializer_list>
#include "Glob2Math.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
int main() {
 unsigned long checked=0; uint64_t state=5489;
 auto check=[&](double n){if(!std::isfinite(n))return true; ++checked; return (std::trunc(n)==n)==(glob2_math_trunc(n)==n);};
 for(double n:{0.,-0.,0.5,-0.5,1.,-1.,2147483647.,2147483648.,4294967295.,9007199254740991.,9007199254740992.,std::numeric_limits<double>::max(),std::numeric_limits<double>::denorm_min()})
   for(double v:{n,std::nextafter(n,-INFINITY),std::nextafter(n,INFINITY)}) if(!check(v))return 1;
 for(unsigned i=0;i<1000000;++i){state^=state<<13;state^=state>>7;state^=state<<17;if(!check(std::bit_cast<double>(state)))return 2;}
 std::printf("PASS %lu finite inputs: identical integrality decisions\n",checked);
}
