// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace ResourceClaims
{
// Explicit research policy, never the default or a claimed tournament winner.
enum class ActiveClaimPolicy : std::uint8_t { Disabled, PrototypeStationaryV1 };
enum class StepResult : std::uint8_t { Pending, Ready, Failed, Arrived };
struct ActiveRouteRuntime;
inline constexpr int ActiveClaimsFormat = 195;
}
