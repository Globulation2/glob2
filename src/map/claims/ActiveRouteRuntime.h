// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ActiveMaterialClaims.h"
#include "ClaimRoutePipeline.h"
#include <map>

namespace ResourceClaims
{
// Owner state only. Workers receive immutable RouteInput and never touch these
// records; request identity prevents an old task from replacing a new route.
struct ActiveRouteRuntime
{
    struct State
    {
        RouteIdentity identity;
        std::uint32_t start = 0, target = 0, cursor = 0;
        std::uint64_t due = 0;
        bool pending = true;
        Route route;
    };
    RoutePipeline pipeline;
    std::map<Owner, State> states;
    explicit ActiveRouteRuntime(ComputeExecutor& executor) : pipeline(executor) {}
};
}
