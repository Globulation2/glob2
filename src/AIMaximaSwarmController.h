#ifndef AI_MAXIMA_SWARM_CONTROLLER_H
#define AI_MAXIMA_SWARM_CONTROLLER_H

#include <algorithm>

namespace AIMaxima {
namespace SwarmController {

struct Plan {
    int workers;
    int swarms;
    long long baselineMilliWorkers;
    long long fundedMilliWorkers;
};

// Integer square root keeps lockstep games identical across platforms.
inline long long squareRoot(long long value)
{
    long long low=0, high=value;
    while(low<high) {
        const long long middle=low+(high-low+1)/2;
        if(middle<=value/middle) low=middle;
        else high=middle-1;
    }
    return low;
}

// B0 = min(W, a sqrt(W))
// B  = B0 F/(F + q B0) / (1 + s H)
// N  = ceil(B/k), retaining one starting producer.
// F is deduplicated, reachable, fertility-weighted corn/wheat acreage.
// H is max(critical hunger, unserved food)/population: overlapping counts
// must not be added. No strategic phase or existing swarm count is an input.
inline Plan plan(int workers, int population, int critical, int unserved,
    int food, int scalePercent, int foodPerWorkerPercent,
    int pressureSensitivity, int workersPerSwarm)
{
    const long long workforce=std::max(0,workers);
    const long long baseline=std::min(workforce*1000,
        squareRoot(workforce*1000000)*scalePercent/100);
    const long long supply=std::max(0,food)*100000LL;
    const long long denominator=supply+foodPerWorkerPercent*baseline;
    long long funded=denominator>0 ? baseline*supply/denominator : 0;
    const long long pop=std::max(1,population);
    const long long pressure=std::min(pop,
        (long long)std::max(0,std::max(critical,unserved)));
    funded=funded*pop/(pop+pressureSensitivity*pressure);
    Plan result;
    result.baselineMilliWorkers=baseline;
    result.fundedMilliWorkers=funded;
    result.workers=int((funded+500)/1000);
    result.swarms=std::max(1,(result.workers+workersPerSwarm-1)/workersPerSwarm);
    return result;
}

}
}
#endif
