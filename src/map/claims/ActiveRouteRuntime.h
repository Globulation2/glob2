// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ActiveMaterialClaims.h"
#include "ClaimRoutePipeline.h"
#include "ClaimRecovery.h"
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
        // start: first cell of the published route; target: the claimed access.
        // cursor: index of the next route cell to enter.
        std::uint32_t start = 0, target = 0, cursor = 0;
        std::uint64_t due = 0;
        bool pending = true;
        Route route;
        std::uint32_t routeDigest = 0;
        void cacheRouteDigest() {
            routeDigest=2166136261u;
            const auto mix=[&](std::uint32_t v){routeDigest=(routeDigest^v)*16777619u;};
            mix(unsigned(route.status));mix(route.cost);mix(route.expansions);mix(route.cells.size());
            for(auto cell:route.cells)mix(cell);
        }
        // A newly published route was planned from where the worker stood when
        // it was requested; the next step first rejoins it from the worker's
        // current cell (the worker kept moving on the gradient meanwhile).
        bool reanchor = false;
        // No route admission was available (pipeline full or recovery
        // backoff); the request is retried on the next decision.
        bool awaitingAdmission = false;
        // Consecutive decisions spent waiting behind traffic on this route.
        std::uint32_t trafficWaits = 0;
    };
    RoutePipeline pipeline;
    std::map<Owner, State> states;
    std::map<Owner, ClaimRecovery> recovery;
    ActiveRouteRuntime(ComputeExecutor& executor, unsigned delay) : pipeline(executor, 4096, delay) {}
};
}
