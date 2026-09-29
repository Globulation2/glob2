#ifndef AI_MAXIMA_FORCE_MODEL_H
#define AI_MAXIMA_FORCE_MODEL_H

#include <cstdint>

namespace AIMaxima { namespace ForceModel {
// Feature order and integer scales are part of the frozen model contract.
enum Feature {
    VisibleWarriors, RememberedWarriors, WarriorAge, ForceAge, BuildingAge,
    VisibleExplorers, VisibleWorkers, VisiblePower, KnownBuildings,
    VisibleBuildings, Confidence, ExploredPercent, Tick, ObservationFeatures
};
constexpr int FeatureCount=26;
constexpr int SampleInterval=1000;
constexpr int Scale=1024;
enum Target { Warriors, Power, TargetCount };
enum Quantile { Lower, Median, Upper, Extreme, QuantileCount };

struct Prediction {
    int64_t values[TargetCount][QuantileCount] = {};
    int rounded(Target target, Quantile quantile=Median) const;
};

// Only fog-visible observations and their causal history enter this state.
// Persist the cached forecast too: lightweight force samples reuse it between
// strategic reviews, including immediately after loading a saved game.
struct State {
    bool initialized=false;
    int64_t features[FeatureCount] = {};
    Prediction prediction;
    void observe(const int64_t (&observation)[ObservationFeatures]);
    void forecast(int tick);
};

Prediction predict(const int64_t (&features)[FeatureCount]);

template<class Archive> void fields(Archive& a, State& state)
{
    a("initialized",state.initialized);
    a("features",state.features);
    for(int target=0;target<TargetCount;++target)
        a("prediction",state.prediction.values[target]);
}
}}
#endif
