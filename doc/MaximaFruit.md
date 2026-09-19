# Maxima fruit supply

Maxima maintains reachable fruit varieties at completed inns by default. It uses
existing inn placement, wheat staffing, construction budgets and military policy;
fruit collection does not introduce another construction or attack controller.

For each known variety, a deterministic search finds the nearest deposit within
32 steps of an inn's perimeter. Routes wrap with the map, include diagonal moves,
and respect buildings, resources, forbidden tiles and the current swimming
capability. The field is rebuilt during each existing fruit-management pass, so
new swimming routes and lost building vision take effect without a saved cache.

Each distinct reachable variety counts once. When several inns need the same
variety, the inn with more reachable varieties has priority; stable iteration
order breaks ties. Duplicate patches do not increase the count. The nearest-patch
approximation can miss a farther patch of the same variety that already has vision.

Maxima maintains at most one explorer flag per useful variety lacking completed
building vision. It reuses pending and existing missions, moves flags when their
source changes, and removes surplus assignments. Normal explorer production
includes these missions in its replacement demand. Completed buildings providing
equivalent vision release explorers; losing that vision restores the mission.
Defense and recovery do not automatically cancel supply, and new games have no
population threshold for fruit collection.

Useful visible, building-covered or stocked supply keeps enemy fV inn advertising
available. Maxima does not enable enemy mV as part of fruit collection. Disabling
`fruit.enabled` removes fruit missions and their inn advertising.

`fruit.reachable_supply=true` selects this policy. Saves predating version 110
restore the legacy policy, including its population and posture gates, so existing
games retain their original decisions. Current saves record the policy with the
ordinary strategy configuration and retain missions in the existing runtime.
The minimum save version remains 58; replay order playback is unchanged. Network
protocol 35 prevents mixed peers from running different local AI decisions.

Regression coverage lives in `MaximaImplementationIntegrationTest` and
`MaximaStrategyTest`, alongside the existing diagnostics and continuation suites.
The permanent policy has no experiment runner, scoring weights, fruit-specific
telemetry or online adaptation.
