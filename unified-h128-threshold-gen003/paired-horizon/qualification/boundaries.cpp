#include <cstdint>
#include <cassert>
struct PairedShortCounters {
    unsigned queries=0;
    std::int64_t deferred=0,refreshed=0;
    static constexpr bool eligible(unsigned age,unsigned delay) {return age>=delay && age<32;}
    static constexpr bool complete(unsigned age) {return age>=32;}
    void record(unsigned age,unsigned delay,std::int64_t a,std::int64_t b) {
        if(eligible(age,delay)){++queries;deferred+=a;refreshed+=b;}
    }
};
static_assert(!PairedShortCounters::eligible(7,8));
static_assert(PairedShortCounters::eligible(8,8));
static_assert(PairedShortCounters::eligible(31,8));
static_assert(!PairedShortCounters::eligible(32,8));
static_assert(!PairedShortCounters::complete(31) && PairedShortCounters::complete(32));

int main(){
 PairedShortCounters shortWindow;unsigned full=0;
 for(unsigned age=0;age<128;++age){shortWindow.record(age,8,2,1);if(age>=8)++full;}
 assert(shortWindow.queries==24&&shortWindow.deferred==48&&shortWindow.refreshed==24&&full==120);
 PairedShortCounters early;for(unsigned age=0;age<31;++age)early.record(age,8,2,1);
 assert(early.queries==23&&!PairedShortCounters::complete(31));
 early.record(31,8,2,1);assert(early.queries==24&&PairedShortCounters::complete(32));
 early.record(32,8,2,1);assert(early.queries==24);
 PairedShortCounters delay32;for(unsigned age=0;age<128;++age)delay32.record(age,32,2,1);assert(delay32.queries==0);
}
