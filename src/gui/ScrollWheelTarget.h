// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// Which selected-building field a scroll-wheel delta adjusts. Decided purely
/// from the modifier keys held and the user's "scroll wheel enabled" setting.
/// Kept as a pure, SDL-free helper so the routing decision can be unit-tested
/// without an SDL window (see test/ScrollWheelTargetTest.cpp), and so the
/// modifier state can be sampled once per scroll event instead of once at frame
/// flush — which is what lets a SHIFT release mid-frame stop misrouting the
/// deltas that were scrolled while SHIFT was still held.
enum class ScrollWheelTarget
{
	None,           ///< this modifier combination scrolls nothing
	MaxUnitWorking, ///< adjust the building's assigned-worker count
	UnitStayRange,  ///< adjust the flag's guard/stay radius
};

/// Route a scroll delta to a building field from the current modifier keys.
///
/// Mirrors the original GameGUI::flushScrollWheelOrders() branch logic exactly:
///  - scroll-wheel setting ON:  SHIFT -> stay range, otherwise -> workers.
///  - scroll-wheel setting OFF: CTRL -> workers, else SHIFT -> stay range,
///    else nothing. CTRL wins when both CTRL and SHIFT are held.
///
/// The building-type gate (whether the selected building actually exposes the
/// chosen field) is applied by the caller at flush time, not here.
inline ScrollWheelTarget scrollWheelTarget(bool shiftHeld, bool ctrlHeld,
                                           bool scrollWheelEnabled)
{
	if (scrollWheelEnabled)
		return shiftHeld ? ScrollWheelTarget::UnitStayRange
		                 : ScrollWheelTarget::MaxUnitWorking;
	if (ctrlHeld)
		return ScrollWheelTarget::MaxUnitWorking;
	if (shiftHeld)
		return ScrollWheelTarget::UnitStayRange;
	return ScrollWheelTarget::None;
}
