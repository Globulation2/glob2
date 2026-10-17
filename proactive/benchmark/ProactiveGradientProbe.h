#pragma once
#include <cstdint>
// Private benchmark instrumentation. requestBuildingField is owner-thread only.
namespace proactive_probe {
struct Counters { std::uint64_t queries=0, pending=0, pendingTicks=0, lastTick=UINT64_MAX; };
inline Counters counters;
}
