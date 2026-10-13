// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Pure comparisons over already-qualified candidates. Equal keys retain the
// caller's traversal order; snapshot planners supply identity-ordered ties.
namespace HiringRanking
{
constexpr int materialLevel(int construction, int walking) { return construction * 10 + walking; }
constexpr bool materialBetter(int level, int value, int bestLevel, int bestValue)
{ return level > bestLevel || (level == bestLevel && value < bestValue); }
constexpr bool roleBetter(int role, std::int64_t level, std::int64_t value,
                         std::int64_t bestLevel, std::int64_t bestValue)
{ return (role == 2 ? level > bestLevel : level < bestLevel) || (level == bestLevel && value < bestValue); }
}
