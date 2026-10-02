// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <unordered_map>

#include <SDL3/SDL_stdinc.h>

#include "Ressource.h" // BASIC_COUNT
#include "UnitConsts.h" // NB_UNIT_TYPE

class Building;

/// Per-building GUI-side optimistic shadow of pending orders.
///
/// When the local player moves a flag or changes a building's worker count,
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

	/// Pending priority radio (-1, 0, +1). Cleared by reconcileBuildingGuiState
	/// when ORDER_CHANGE_PRIORITY for this gid arrives from a non-local sender
	/// or during replay.
	std::optional<Sint32> pendingPriority;

	/// Pending clearing-flag resource mask. Whole array replaces wholesale (the
	/// OrderModifyClearingFlag payload also carries the whole array). Cleared
	/// by reconcileBuildingGuiState on ORDER_MODIFY_CLEARING_FLAG (non-local /
	/// replay).
	std::optional<std::array<bool, BASIC_COUNT>> pendingClearingResources;

	/// Pending min-level-to-flag (warflag / explorationflag). Cleared by
	/// reconcileBuildingGuiState on ORDER_MODIFY_MIN_LEVEL_TO_FLAG (non-local /
	/// replay).
	std::optional<Sint32> pendingMinLevelToFlag;

	/// Pending per-unit-type swarm ratios. Whole array replaces wholesale (the
	/// OrderModifySwarm payload also carries the whole array). Cleared by
	/// reconcileBuildingGuiState on ORDER_MODIFY_SWARM (non-local / replay).
	std::optional<std::array<Sint32, NB_UNIT_TYPE>> pendingRatio;
};

/// Map from Building::gid to its pending GUI state.
using BuildingGuiStateMap = std::unordered_map<Uint16, BuildingGuiState>;

// Display accessors: return pending value if set, else the authoritative
// value from `b`. Defined in BuildingGuiState.cpp because Building's header
// is heavy and we want this header light enough to forward-declare through.
Sint32 displayedPosX(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedPosY(const BuildingGuiStateMap& m, const Building& b);
//! Same, for drawing from a Scene: the building's gid and authoritative position.
Sint32 displayedPosX(const BuildingGuiStateMap& m, Uint16 gid, Sint32 posX);
Sint32 displayedPosY(const BuildingGuiStateMap& m, Uint16 gid, Sint32 posY);
Sint32 displayedMaxUnitWorking(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedUnitStayRange(const BuildingGuiStateMap& m, const Building& b);
Sint32 displayedPriority(const BuildingGuiStateMap& m, const Building& b);
bool displayedClearingResource(const BuildingGuiStateMap& m, const Building& b, int i);
Sint32 displayedMinLevelToFlag(const BuildingGuiStateMap& m, const Building& b);
std::array<Sint32, NB_UNIT_TYPE> displayedRatio(const BuildingGuiStateMap& m, const Building& b);

/// The same accessors for presentation copies of a building (e.g. SceneBuildingPanel):
/// any type with the Building field names gid, posX, posY, maxUnitWorking,
/// unitStayRange, priority, clearingResources, minLevelToFlag and ratio.
template <class B> const BuildingGuiState* pendingStateOf(const BuildingGuiStateMap& m, const B& b)
{
	auto it = m.find(b.gid);
	return it == m.end() ? nullptr : &it->second;
}
template <class B> Sint32 displayedPosX(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingPosX) ? *s->pendingPosX : b.posX; }
template <class B> Sint32 displayedPosY(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingPosY) ? *s->pendingPosY : b.posY; }
template <class B> Sint32 displayedMaxUnitWorking(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingMaxUnitWorking) ? *s->pendingMaxUnitWorking : b.maxUnitWorking; }
template <class B> Sint32 displayedUnitStayRange(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingUnitStayRange) ? *s->pendingUnitStayRange : b.unitStayRange; }
template <class B> Sint32 displayedPriority(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingPriority) ? *s->pendingPriority : b.priority; }
template <class B> bool displayedClearingResource(const BuildingGuiStateMap& m, const B& b, int i)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingClearingResources) ? (*s->pendingClearingResources)[i] : b.clearingResources[i]; }
template <class B> Sint32 displayedMinLevelToFlag(const BuildingGuiStateMap& m, const B& b)
{ auto* s = pendingStateOf(m, b); return (s && s->pendingMinLevelToFlag) ? *s->pendingMinLevelToFlag : b.minLevelToFlag; }
template <class B> std::array<Sint32, NB_UNIT_TYPE> displayedRatio(const BuildingGuiStateMap& m, const B& b)
{
	auto* s = pendingStateOf(m, b);
	if (s && s->pendingRatio)
		return *s->pendingRatio;
	std::array<Sint32, NB_UNIT_TYPE> r;
	for (int i = 0; i < NB_UNIT_TYPE; i++)
		r[i] = b.ratio[i];
	return r;
}

/// Count workers currently inside vs. en route to the flag's pending area.
/// `posX`, `posY`, `stayRange` should be the displayed values (caller already
/// resolved any pending state). Was Building::computeFlagStatLocal — moved off
/// the sim object because it only consumes GUI-side state.
void computeFlagStatDisplayed(const Building& b, Sint32 posX, Sint32 posY,
                              Sint32 stayRange, int* goingTo, int* onSpot);
