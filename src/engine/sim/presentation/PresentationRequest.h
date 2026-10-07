// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/ClientRequests.h"
#include <optional>
#include <utility>

//! What the client wants drawn this frame: whose view, and what it has selected.
struct SceneRequest
{
	std::optional<std::pair<Uint32, Uint32>> highlights;
	bool includeScriptAreas = false;
	bool includePanels = true;
    bool includeHistory = false;
    bool includeTelemetry = false;
	int localTeam = 0;
	bool spectating = false;
	ClientRequests::ClientView view;
	BuildingRef selectedBuilding;
	UnitRef selectedUnit;
	//! When the latest tick finished and the interval to the next (see PresentationFrame).
	Uint64 tickTime = 0;
	Uint32 tickInterval = 0;
    bool operator==(const SceneRequest&) const = default;
};
