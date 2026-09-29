// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

/// Whether the building-info panel should draw its per-reason "units failing
/// requirements" rows for the selected building.
///
/// The block is suppressed when the ONLY positive failure reason is the
/// "unit not available" bucket (index `availabilityReason`): a building that
/// merely lacks spare idle units is in its normal, unremarkable state and does
/// not warrant a warning row. As soon as any *other* reason is positive
/// (too-low-level, can't-access-building, too-far-from-resource, ...) the block
/// is shown — and the caller then also prints the not-available count alongside
/// the real obstruction for context.
///
/// Pure and dependency-free (no GameGUI, no globalContainer, no SDL window) so
/// the gate can be unit-tested directly; see test/BuildingFailureDisplayTest.cpp.
/// `counts` holds `reasonCount` entries and `availabilityReason` must be a valid
/// index into it (Building::UnitNotAvailable at the call site).
inline bool shouldShowBuildingFailureReasons(const uint32_t* counts,
                                             unsigned reasonCount,
                                             unsigned availabilityReason)
{
	for (unsigned j = 0; j < reasonCount; ++j)
	{
		if (j != availabilityReason && counts[j] > 0)
			return true;
	}
	return false;
}

/// Whether the failing-unit display — the panel's per-reason rows and the map
/// view's badges alike — should be drawn for the selected building.
///
/// On top of the reason gate above, the building has to still be asking for
/// units. Its tallies are only refreshed while it is in Team's
/// "needs units" list, so a building that has stopped asking — the player
/// dragged the "Working" ratio down to what is already hired, say — keeps the
/// gids its last scan recorded. Showing badges from that scan would mark units
/// over a building that is not trying to hire them, and the panel would have
/// no row to explain the marks. Both sites ask this one question so that the
/// rows and the badges can never disagree.
inline bool shouldShowFailingUnitMarkers(const uint32_t* counts,
                                         unsigned reasonCount,
                                         unsigned availabilityReason,
                                         int unitsWorking,
                                         int desiredMaxUnitWorking)
{
	return unitsWorking < desiredMaxUnitWorking
		&& shouldShowBuildingFailureReasons(counts, reasonCount, availabilityReason);
}
