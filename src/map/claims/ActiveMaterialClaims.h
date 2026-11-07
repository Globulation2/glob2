// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace ResourceClaims
{
enum class StepResult : std::uint8_t { Pending, Ready, Failed, Arrived };
struct ActiveRouteRuntime;
}
