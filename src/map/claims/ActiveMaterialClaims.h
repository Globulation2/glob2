// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace ResourceClaims
{
enum class StepResult : std::uint8_t { Pending, Ready, Failed, Arrived };
struct ActiveRouteRuntime;
// First save format with material destination planes, the claim ledger,
// claim routes, selection cadence and recovery (FILE_FORMAT_VERSION_RESOURCE_CLAIMS).
// Earlier saves carry none of these and continue without claims.
inline constexpr int ActiveClaimsFormat = 213;
}
