// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ToolkitBinding.h"
#include "GeneratorControls.h"
#include <algorithm>

namespace MapGeneration::JavaScript
{
// Control factories and field setters may construct a partial record. Validate
// before using it: native control routines assume positive steps and legal shifts.
inline void prepareGeneratorControl(Binding &e, const GeneratorControl &control)
{
	const auto &c = control;
	if (c.step <= 0 || c.maximum < c.minimum ||
		(c.allowedValues.empty() && ((std::int64_t(c.maximum) - c.minimum) % c.step ||
									 (std::int64_t(c.maximum) - c.minimum) / c.step + 1 > 4096)) ||
		c.allowedValues.size() > 4096 || c.valueLabels.size() > 256)
		throw TypeMismatch("Invalid or unbounded generator control domain");
	// Both the precheck and requested method can scan legal grids and binary-search
	// each member. Reserve their bounded worst case before any native allocation.
	e.chargeNative(64 * 4096 + c.allowedValues.size() + c.valueLabels.size());
	// Two validations can each create three grids; allow geometric vector growth
	// in the cumulative allocation envelope, including grids freed during the call.
	e.allocate(12 * 4096 * sizeof(int));
	if (!c.allowedValues.empty() &&
		(c.allowedValues.front() != c.minimum || c.allowedValues.back() != c.maximum ||
		 !std::is_sorted(c.allowedValues.begin(), c.allowedValues.end()) ||
		 std::adjacent_find(c.allowedValues.begin(), c.allowedValues.end()) !=
			 c.allowedValues.end()))
		throw TypeMismatch("Invalid generator control values");
	if (c.kind != GeneratorControl::Kind::Range && c.kind != GeneratorControl::Kind::Toggle)
		throw TypeMismatch("Invalid generator control kind");
	if (c.powerOfTwo && (c.minimum < 0 || c.maximum > 30))
		throw TypeMismatch("Invalid generator control power-of-two range");
	if (c.isToggle() && (c.minimum != 0 || c.maximum != 1 || c.step != 1 || c.powerOfTwo ||
						 c.terrainWeight || !c.allowedValues.empty()))
		throw TypeMismatch("Invalid generator toggle control");
	if (c.isChoice())
	{
		if (c.isToggle() || c.powerOfTwo || c.terrainWeight || c.minimum != 0 || c.step != 1 ||
			c.allowedValues.size() != c.valueLabels.size() ||
			c.maximum != int(c.valueLabels.size()) - 1)
			throw TypeMismatch("Invalid generator choice control");
		for (std::size_t i = 0; i < c.valueLabels.size(); ++i)
			if (c.allowedValues[i] != int(i) || !c.valueLabels[i] || !*c.valueLabels[i])
				throw TypeMismatch("Invalid generator choice labels");
	}
	if (c.normalize(c.defaultValue) != c.defaultValue)
		throw TypeMismatch("Invalid generator control default");
	// A partially constructed control can omit its search declaration. When
	// supplied, both bounded alternatives must satisfy the native legal domain.
	if (c.searchRange || c.searchAllowedValues)
	{
		if ((c.searchAllowedValues && c.searchAllowedValues->size() > 4096) ||
			!c.validSearchDomain())
			throw TypeMismatch("Invalid generator control search domain");
	}
}

inline void prepareGeneratorDisplay(const GeneratorControl &control, int value)
{
	if (control.powerOfTwo && (value < 0 || value > 30))
		throw TypeMismatch("Power-of-two display values must be 0..30");
}
} // namespace MapGeneration::JavaScript
