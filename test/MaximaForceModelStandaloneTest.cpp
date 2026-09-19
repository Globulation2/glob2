// Golden fixed-point outputs from the frozen Python export, independent of C++ inference.
#include "AIMaximaForceModel.h"
#include <cassert>
#include <iostream>
using namespace AIMaxima::ForceModel;
int main()
{
    struct Case { int64_t x[FeatureCount]; int64_t expected[TargetCount][QuantileCount]; };
    const Case cases[]={
        {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{{0,1389,2093,3554},{0,186258,266069,299261}}},
        {{0,0,1000000,1000000,1000000,0,0,0,0,0,0,0,1000,0,0,0,0,0,0,0,0,0,0,0,0,0},{{0,5,814,1198},{0,14,16685,186258}}},
        {{8,16,2100,2000,1100,3,12,512,9,2,70,40,12000,12,25,-4,18,24,-6,800,2200,-288,10,10,-1,2000},{{0,44955,61751,67491},{0,5877219,8918441,11073861}}},
        {{4,68,3453,153,12698,14,12,4172,12,9,33,67,57783,9,14,-5,17,22,-5,4177,4182,-5,17,22,-5,0},{{4096,76014,83747,90637},{4272128,13811630,17368318,17747752}}},
        {{35,34,9089,4020,13170,0,58,1732,8,5,57,86,51152,40,45,-5,63,68,-5,1737,1742,-5,13,18,-5,2000},{{0,73205,91908,127216},{0,10175534,15478749,18848064}}},
        {{26,33,16388,18493,3957,9,33,2611,14,4,6,8,23217,31,36,-5,38,43,-5,2616,2621,-5,19,24,-5,5000},{{0,80526,114963,142129},{0,12896107,21438465,26185907}}},
        {{23,12,7133,7260,16794,9,6,2574,23,5,89,18,51015,28,33,-5,11,16,-5,2579,2584,-5,28,33,-5,0},{{23552,41350,54923,78668},{2635776,8166350,11331728,16463151}}},
        {{20,34,19739,2682,9693,14,79,3977,22,2,37,86,18886,25,30,-5,84,89,-5,3982,3987,-5,27,32,-5,2000},{{0,79292,116108,154527},{0,15161170,22091630,26332107}}},
        {{10,19,10376,13465,13456,7,56,4531,9,7,8,56,47233,15,20,-5,61,66,-5,4536,4541,-5,14,19,-5,5000},{{0,55075,72713,80323},{0,10392893,15346569,19693775}}},
        {{21,74,18145,10579,8832,1,33,3644,23,2,52,18,23843,26,31,-5,38,43,-5,3649,3654,-5,28,33,-5,0},{{21504,81428,102816,131951},{3731456,14938194,19391087,23940704}}},
        {{34,62,19659,15691,19435,9,12,850,19,4,38,91,46099,39,44,-5,17,22,-5,855,860,-5,24,29,-5,2000},{{0,74074,86087,130872},{0,10765815,14389634,18569871}}},
        {{21,10,14772,15188,18080,2,28,4851,22,3,31,51,45547,26,31,-5,33,38,-5,4856,4861,-5,27,32,-5,5000},{{0,48332,48813,88390},{0,10615302,15523712,21960474}}},
    };
    for(const auto& c:cases)
    {
        const auto actual=predict(c.x);
        for(int t=0;t<TargetCount;++t)
            for(int q=0;q<QuantileCount;++q) assert(actual.values[t][q]==c.expected[t][q]);
    }
    State state;
    int64_t first[ObservationFeatures]={10,12,0,0,0,1,20,640,5,5,80,50,1000};
    state.observe(first);state.forecast(1000);
    assert(state.features[13]==10 && state.features[14]==10 && state.features[15]==0);
    first[VisibleWarriors]=9;first[Tick]=1500;
    state.observe(first);
    assert(state.features[Tick]==1000); // Respect the trained observation cadence.
    first[Tick]=2200;state.observe(first);state.forecast(4200);
    assert(state.features[13]==10 && state.features[14]==10 && state.features[15]==-1);
    assert(state.features[25]==2000 && state.features[WarriorAge]==0);
    const auto future=predict(state.features);
    assert(state.prediction.values[Warriors][Median]==future.values[Warriors][Median]);
    Prediction rounding;rounding.values[Warriors][Median]=Scale*5+Scale/2;
    assert(rounding.rounded(Warriors)==6);
    std::cout<<"Frozen force predictions and causal history match\n";
}
