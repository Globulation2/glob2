// SPDX-License-Identifier: GPL-3.0-or-later
// Keeps query capture (and its file and lock) out of the claim pipeline header.
#include "ClaimRoutePipeline.h"
#include "field/astar/AStarCapture.h"

void ResourceClaims::captureRouteQuery(const RouteDomain& domain, const astar::Query& query, int windowSide)
{
    if (astar::capture::enabled()) [[unlikely]]
        astar::capture::point(domain, query, windowSide);
}
