// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"

// AICortex policy layer. Maps an Observation to an Action intent and NOTHING
// else. It must not include Game.h / Team.h / Order.h or touch any engine
// pointer — its entire input is the CortexObservation, its entire output is a
// CortexAction. That isolation is what lets v0 (hand rules, here) be swapped for
// a behavior tree or a neural net later without rewriting observation or action
// code (see docs/AI/cortex/README.md).

namespace Cortex
{
	class CortexPolicy
	{
	public:
		CortexPolicy();

		/// Decide the next action intent from the current observation.
		/// Scaffold: always returns NoOp. Both engine bindings share this.
		CortexAction decide(const CortexObservation& obs);

		/// Wheat-forbidden upkeep decision, evaluated EVERY decision cycle in
		/// PARALLEL with decide()'s single primary action — not as a competing
		/// ACTION_* the build/upgrade ladder could starve. Painting the checkerboard
		/// is area-paint (OrderAlterateForbidden), not an OrderCreate, so it need not
		/// contend for the cycle's one action slot. The policy still owns the gate:
		/// true only when the colony is not starving (never wall off wheat while the
		/// colony is dying) and the reconcile has real work (newly-revealed wheat to
		/// forbid, or wheat gone/out of view to un-forbid). The open-margin N feeds
		/// the executor from obs.wheatOpenMargin (the ML seam — a learned policy later
		/// outputs it). The action layer (AICortex::enqueueWheatForbidden) rebuilds
		/// the full ADD/DEL tile masks and emits the orders.
		bool wantWheatProtection(const CortexObservation& obs) const;
	};
}
