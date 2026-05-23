// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <unordered_map>

#include <SDL_types.h>

class Building;

/// Per-building GUI-side optimistic shadow of pending orders.
///
/// When the local player drags a flag or scrolls a building's worker count,
/// the change is queued as an Order and won't take effect on the simulation
/// until the network round-trip completes. To make the UI feel responsive,
/// the GUI stores the intended value here and renders it in preference to
/// the building's authoritative state until the order executes.
///
/// Reconciliation happens in GameGUI::executeOrder when an order matching
/// this building arrives — see GameGUI::reconcileBuildingGuiState.
///
/// Each field is nullopt when there is no pending change, in which case
/// the displayed value falls back to the authoritative Building field.
/// Keyed by Building::gid (stable for the building's lifetime).
///
/// Never read or written by simulation, AI, or scripts — strictly GUI state.
struct BuildingGuiState
{
	std::optional<Sint32> pendingPosX;
	std::optional<Sint32> pendingPosY;
	std::optional<Sint32> pendingMaxUnitWorking;
	std::optional<Sint32> pendingUnitStayRange;
};

/// Map from Building::gid to its pending GUI state.
using BuildingGuiStateMap = std::unordered_map<Uint16, BuildingGuiState>;

// Display accessors: return pending value if set, else the authoritative
// value from `b`. Defined in BuildingGuiState.cpp because Building's header
// is heavy and we want this header light enough to forward-declare through.
Sint32 displayedPosX(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedPosY(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedMaxUnitWorking(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedUnitStayRange(const BuildingGuiStateMap& m, const Building& b);

/// Count workers currently inside vs. en route to the flag's pending area.
/// `posX`, `posY`, `stayRange` should be the displayed values (caller already
/// resolved any pending state). Was Building::computeFlagStatLocal — moved off
/// the sim object because it only consumes GUI-side state.
void computeFlagStatDisplayed(const Building& b, Sint32 posX, Sint32 posY,
                              Sint32 stayRange, int* goingTo, int* onSpot);
