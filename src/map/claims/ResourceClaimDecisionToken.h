// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace ResourceClaims {
// Diagnostic identity only; zero ordinal means no frozen event was submitted.
struct DecisionToken { std::uint64_t ordinal=0; std::uint32_t nearestAccess=UINT32_MAX; };
}
