// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Types.h"
#include <vector>
namespace SimulationSnapshot {
// Numeric mixing is independent of byte order, object padding and local stamps.
inline Uint64 digestWord(Uint64 digest, Uint64 word) { return (digest ^ word)*1099511628211ull; }
constexpr Uint64 DigestSeed=1469598103934665603ull;
struct LogicalChunks {
    static constexpr std::size_t MaxBytes=16*1024;
    std::vector<Uint64> values; Uint64 digest=0;
};
}
