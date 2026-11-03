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
        // Format 196 experiment only: target above remains the reservation access.
        std::uint32_t pathTarget = 0, pendingStart = 0, pendingTarget = 0;
        unsigned pathRadius = 1, pendingRadius = 1;
        std::uint64_t lastRefreshOrdinal = 0;
        bool reanchor = false, followingGradient = false, probeFinished = false;
        bool awaitingAdmission = false;

    };
    RoutePipeline pipeline;
    std::map<Owner, State> states;
    std::map<Owner, ClaimRecovery> recovery;
    explicit ActiveRouteRuntime(ComputeExecutor& executor) : pipeline(executor) {}
};
}
