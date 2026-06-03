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
	};
}
