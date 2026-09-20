#include "AIMaximaForceModel.h"
#include <algorithm>
#include <limits>

namespace AIMaxima { namespace ForceModel {
namespace {
struct Node { int feature, threshold, left, right; };
#include "AIMaximaForceModelData.inc"
constexpr int historySources[]={VisibleWarriors,VisibleWorkers,VisiblePower,KnownBuildings};
// The training export used Python's floor division, including negative rates.
int64_t floorDivide(int64_t value, int64_t divisor)
{ return value/divisor-(value%divisor<0 ? 1 : 0); }
}

int Prediction::rounded(Target target, Quantile quantile) const
{
    return int(std::min<int64_t>(std::numeric_limits<int>::max(),
        (values[target][quantile]+Scale/2)/Scale));
}

Prediction predict(const int64_t (&input)[FeatureCount])
{
    int64_t x[FeatureCount];
    std::copy(input,input+FeatureCount,x);
    for(int i=WarriorAge;i<=BuildingAge;++i) x[i]=std::min<int64_t>(100000,x[i]);
    Prediction result;
    for(int target=0;target<TargetCount;++target)
    {
        for(int q=0;q<QuantileCount;++q)
        {
            const int forest=target*QuantileCount+q;
            int64_t value=bases[forest];
            for(int root:roots[forest])
            {
                int n=root;
                while(nodes[n].feature>=0)
                    n=x[nodes[n].feature]<=nodes[n].threshold ? nodes[n].left : nodes[n].right;
                value+=nodes[n].threshold;
            }
            const int64_t bound=x[FeatureCount-1]==0
                ? x[target==Warriors ? VisibleWarriors : VisiblePower]*Scale : 0;
            result.values[target][q]=std::max(bound,value);
        }
        std::sort(result.values[target],result.values[target]+QuantileCount);
    }
    for(int q=0;q<QuantileCount;++q)
        result.values[Power][q]=std::max(result.values[Power][q],result.values[Warriors][q]);
    return result;
}

void State::observe(const int64_t (&observation)[ObservationFeatures])
{
    if(initialized && observation[Tick]-features[Tick]<SampleInterval) return;
    for(int i=0;i<4;++i)
    {
        const int source=historySources[i], offset=ObservationFeatures+i*3;
        const int64_t previous=initialized ? features[source] : observation[source];
        features[offset]=previous;
        features[offset+1]=initialized ? std::max(features[offset+1],observation[source]) : observation[source];
        features[offset+2]=initialized ? floorDivide((observation[source]-previous)*SampleInterval,
            std::max<int64_t>(1,observation[Tick]-features[Tick])) : 0;
    }
    std::copy(observation,observation+ObservationFeatures,features);
    initialized=true;
}

void State::forecast(int tick)
{
    if(!initialized) return;
    features[FeatureCount-1]=std::max<int64_t>(0,int64_t(tick)-features[Tick]);
    prediction=predict(features);
}
}}
