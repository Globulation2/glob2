// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace ResourceClaims
{
// Explicit research policies, never the default or a claimed tournament winner.
enum class ActiveClaimPolicy : std::uint8_t { Disabled, PrototypeStationaryV1, Experimental };
enum class StepResult : std::uint8_t { Pending, Ready, Failed, Arrived };
struct ActiveRouteRuntime;
// Claims, policy cadence and recovery were consolidated into one shipped format
// (FILE_FORMAT_VERSION_RESOURCE_CLAIMS); unshipped intermediate saves are not read.
inline constexpr int ActiveClaimsFormat = 213;
inline constexpr int PolicyConfigFormat = ActiveClaimsFormat;
inline constexpr int ClaimRecoveryFormat = ActiveClaimsFormat;
}
